/* SPDX-License-Identifier: MIT
 * proc_cpu_test [SECONDS]: which processes use the processor, every 3 s for SECONDS (60). Each window's top five as a
 * line "CPU t=N: name pct; ...", and every window a "CPU HISTORY" of the windows so far (the log ring keeps only its
 * last seconds). For the stalls the user sees on the home screen's far-left/far-right panes and in Wallpaper (y99).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <pthread.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <net/if.h>

/* (y173) what came in and went out over the network interfaces (not loopback) so far: bytes and packets */
static void
net_totals(uint64_t *ib, uint64_t *ip, uint64_t *ob)
{
	struct ifaddrs *list = NULL, *i;

	*ib = *ip = *ob = 0;
	if (getifaddrs(&list) != 0) {
		return;
	}
	for (i = list; i != NULL; i = i->ifa_next) {
		if (i->ifa_addr != NULL && i->ifa_addr->sa_family == AF_LINK && i->ifa_data != NULL &&
		    strncmp(i->ifa_name, "lo", 2) != 0) {
			const struct if_data *d = i->ifa_data;
			*ib += d->ifi_ibytes;
			*ip += d->ifi_ipackets;
			*ob += d->ifi_obytes;
		}
	}
	freeifaddrs(list);
}
kern_return_t task_read_for_pid(mach_port_name_t target_tport, int pid, mach_port_name_t *t);
kern_return_t mach_vm_read_overwrite(vm_map_t, mach_vm_address_t, mach_vm_size_t, mach_vm_address_t, mach_vm_size_t *);

/*
 * A sampling profiler (y103): every 10 ms, the running threads of a few processes are stopped for a moment
 * (thread_get_state) and their place read -- the program counter and up to 12 callers by the frame pointers. Code
 * of the shared cache is named by dladdr() here (the cache is at the same place in every process of a boot); other
 * code is "image+page". Each 3 s window: the five functions most often running ("LEAF") and the five most often on
 * the stack ("IN"), per process. As root, task_for_pid() is allowed (bsd/vm/vm_unix.c).
 */
enum { NS = 6 };
static const char *const sampled[NS] = { "SpringBoard", "backboardd", "com.apple.WebKit.GPU", "com.apple.WebKit.Networking", "chronod", "com.apple.WebKit.WebContent" };
static const char *const stag[NS] = { "SB", "BB", "WKGPU", "WKNET", "CHRONO", "WKWEB" };
#define SYMS 512
struct sym { const void *key; char name[48]; unsigned leaf, in; };
static struct sym syms[NS][SYMS];
static unsigned samples[NS];
/* what the sampler got as far as (y103: nothing at all): task_threads calls, threads, infos, running, states */
static volatile unsigned dbg[NS][5];
static volatile int dbg_kr[NS][3], dbg_tfp[NS] = { 99, 99, 99, 99, 99, 99 }, dbg_pid[NS];
static pthread_mutex_t sym_lock = PTHREAD_MUTEX_INITIALIZER;

int proc_listallpids(void *buffer, int buffersize);
int proc_name(int pid, void *buffer, uint32_t buffersize);

/*
 * The target's own images (y108: code outside the shared cache was only "pc 104e12000"; that is libOSMesa, metal2vulkan,
 * panvk -- loaded at their own places in each process): dyld's list, read from the target (TASK_DYLD_INFO), kept
 * sorted by load address; refreshed with the process list.
 */
#include <mach-o/dyld_images.h>
#define IMAGES 600
struct image { uint64_t base; char name[40]; };
static struct image images[NS][IMAGES];
static int nimages[NS];

static int
image_cmp(const void *a, const void *b)
{
	uint64_t x = ((const struct image *)a)->base, y = ((const struct image *)b)->base;
	return x < y ? -1 : x > y;
}

static void
images_of(int p, task_t task)
{
	struct task_dyld_info di;
	mach_msg_type_number_t n = TASK_DYLD_INFO_COUNT;
	uint32_t head[4];
	mach_vm_size_t got;

	nimages[p] = 0;
	if (task_info(task, TASK_DYLD_INFO, (task_info_t)&di, &n) != KERN_SUCCESS ||
	    mach_vm_read_overwrite(task, di.all_image_info_addr, sizeof(head), (mach_vm_address_t)head, &got) != KERN_SUCCESS) {
		return;
	}
	uint32_t count = head[1];
	uint64_t array = (uint64_t)head[2] | (uint64_t)head[3] << 32;
	for (uint32_t i = 0; i < count && nimages[p] < IMAGES; i++) {
		uint64_t e[3];
		char path[256];
		if (mach_vm_read_overwrite(task, array + i * sizeof(e), sizeof(e), (mach_vm_address_t)e, &got) != KERN_SUCCESS) {
			break;
		}
		Dl_info here;
		if (dladdr((const void *)e[0], &here) && here.dli_fbase == (void *)e[0]) {
			continue;               /* in the shared cache: named here by dladdr */
		}
		memset(path, 0, sizeof(path));
		if (mach_vm_read_overwrite(task, e[1], sizeof(path) - 1, (mach_vm_address_t)path, &got) != KERN_SUCCESS) {
			continue;
		}
		const char *slash = strrchr(path, '/');
		images[p][nimages[p]].base = e[0];
		snprintf(images[p][nimages[p]].name, sizeof(images[p][0].name), "%s", slash ? slash + 1 : path);
		nimages[p]++;
	}
	qsort(images[p], (size_t)nimages[p], sizeof(images[p][0]), image_cmp);
}

static struct sym *
sym_of(int p, uint64_t pc)
{
	Dl_info info;
	const void *key;
	char name[48];

	if (dladdr((const void *)pc, &info) && info.dli_sname != NULL && strcmp(info.dli_sname, "<redacted>") != 0) {
		key = info.dli_saddr;
		snprintf(name, sizeof(name), "%s", info.dli_sname);
	} else if (info.dli_fname != NULL && info.dli_fbase != NULL && dladdr((const void *)pc, &info)) {
		/* the shared cache's private functions: image!offset of the function's start */
		const char *slash = strrchr(info.dli_fname, '/');
		uint64_t start = info.dli_saddr ? (uint64_t)info.dli_saddr : pc;
		key = (const void *)start;
		snprintf(name, sizeof(name), "%.24s!%llx", slash ? slash + 1 : info.dli_fname,
		    (unsigned long long)(start - (uint64_t)info.dli_fbase));
	} else {
		/* the target's own images: image+offset (in 64-byte steps), for the host's symbols */
		int lo = 0, hi = nimages[p] - 1, at = -1;
		while (lo <= hi) {
			int mid = (lo + hi) / 2;
			if (images[p][mid].base <= pc) {
				at = mid;
				lo = mid + 1;
			} else {
				hi = mid - 1;
			}
		}
		if (at >= 0 && pc - images[p][at].base < (64u << 20)) {
			uint64_t off = (pc - images[p][at].base) & ~0x3fULL;
			key = (const void *)(images[p][at].base + off);
			snprintf(name, sizeof(name), "%.24s+%llx", images[p][at].name, (unsigned long long)off);
		} else {
			key = (const void *)(pc & ~0xfffULL);
			snprintf(name, sizeof(name), "pc %llx", (unsigned long long)(pc & ~0xfffULL));
		}
	}
	unsigned h = (unsigned)(((uintptr_t)key >> 2) * 2654435761u) % SYMS;
	for (unsigned n = 0; n < SYMS; n++, h = (h + 1) % SYMS) {
		struct sym *s = &syms[p][h];
		if (s->key == key) {
			return s;
		}
		if (s->key == NULL) {
			s->key = key;
			memcpy(s->name, name, sizeof(name));
			return s;
		}
	}
	return NULL;
}

static void
sample_task(int p, task_t task)
{
	thread_act_array_t threads;
	mach_msg_type_number_t count;

	kern_return_t kr = task_threads(task, &threads, &count);
	dbg[p][0]++;
	if (kr != KERN_SUCCESS) {
		dbg_kr[p][0] = kr;
		return;
	}
	for (unsigned t = 0; t < count; t++) {
		thread_basic_info_data_t bi;
		mach_msg_type_number_t bn = THREAD_BASIC_INFO_COUNT;
		arm_thread_state64_t st;
		mach_msg_type_number_t sn = ARM_THREAD_STATE64_COUNT;

		dbg[p][1]++;
		kr = thread_info(threads[t], THREAD_BASIC_INFO, (thread_info_t)&bi, &bn);
		if (kr != KERN_SUCCESS) {
			dbg_kr[p][1] = kr;
			mach_port_deallocate(mach_task_self(), threads[t]);
			continue;
		}
		dbg[p][2]++;
		if (bi.run_state != TH_STATE_RUNNING) {
			mach_port_deallocate(mach_task_self(), threads[t]);
			continue;
		}
		dbg[p][3]++;
		kr = thread_get_state(threads[t], ARM_THREAD_STATE64, (thread_state_t)&st, &sn);
		if (kr != KERN_SUCCESS) {
			/* y106: ARM_THREAD_STATE64 refused (4) on a read port; the unified flavor, the kernel choosing 64-bit */
			arm_unified_thread_state_t us;
			mach_msg_type_number_t un = ARM_UNIFIED_THREAD_STATE_COUNT;
			kern_return_t kr2 = thread_get_state(threads[t], ARM_THREAD_STATE, (thread_state_t)&us, &un);
			if (kr2 != KERN_SUCCESS || us.ash.flavor != ARM_THREAD_STATE64) {
				dbg_kr[p][2] = kr2 != KERN_SUCCESS ? kr2 : 0x64;
				mach_port_deallocate(mach_task_self(), threads[t]);
				continue;
			}
			st = us.ts_64;
		}
		dbg[p][4]++;
		uint64_t pcs[13];
		int n = 0;
		pcs[n++] = __darwin_arm_thread_state64_get_pc(st);
		pcs[n++] = __darwin_arm_thread_state64_get_lr(st);
		uint64_t fp = __darwin_arm_thread_state64_get_fp(st);
		while (n < 13 && fp != 0 && (fp & 7) == 0) {
			uint64_t frame[2];
			mach_vm_size_t got = 0;
			if (mach_vm_read_overwrite(task, fp, sizeof(frame), (mach_vm_address_t)frame, &got) != KERN_SUCCESS ||
			    got != sizeof(frame) || frame[0] <= fp) {
				break;
			}
			pcs[n++] = frame[1];
			fp = frame[0];
		}
		pthread_mutex_lock(&sym_lock);
		samples[p]++;
		const void *seen[13];
		int ns = 0;
		for (int i = 0; i < n; i++) {
			struct sym *s = sym_of(p, pcs[i]);
			if (s == NULL) {
				continue;
			}
			if (i == 0) {
				s->leaf++;
			}
			int dup = 0;
			for (int j = 0; j < ns; j++) {
				dup |= seen[j] == s->key;
			}
			if (!dup) {
				s->in++;
				seen[ns++] = s->key;
			}
		}
		pthread_mutex_unlock(&sym_lock);
		mach_port_deallocate(mach_task_self(), threads[t]);
	}
	vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(threads[0]));
}

static void *
sampler(void *unused)
{
	(void)unused;
	int pids[NS] = { 0 };
	task_t tasks[NS] = { 0 };
	unsigned tick = 0;
	static int all[4096];

	for (;;) {
		if (tick++ % 100 == 0) {                /* the processes again every second (they come and go) */
			int n = proc_listallpids(all, sizeof(all));
			for (int p = 0; p < NS; p++) {
				int found = 0;
				for (int i = 0; i < n; i++) {
					char name[64] = "";
					if (proc_name(all[i], name, sizeof(name)) > 0 && strcmp(name, sampled[p]) == 0) {
						found = all[i];
					}
				}
				if (found != pids[p]) {
					if (tasks[p]) {
						mach_port_deallocate(mach_task_self(), tasks[p]);
					}
					tasks[p] = 0;
					pids[p] = found;
					dbg_pid[p] = found;
					dbg_tfp[p] = found ? task_read_for_pid(mach_task_self(), found, &tasks[p]) : 98;   /* y105: task_for_pid gave a port task_threads refused (0x10000003: a platform binary's control port); a read port does */
					if (found && dbg_tfp[p] != KERN_SUCCESS) {
						static int said;
						if (said++ < 4) {
							printf("SAMPLE: task_for_pid(%s) refused\n", sampled[p]);
						}
						tasks[p] = 0;
					}
				}
				if (tasks[p] && tick % 300 == 1) {     /* the images, every 3 s (plug-ins are loaded late) */
					pthread_mutex_lock(&sym_lock);
					images_of(p, tasks[p]);
					pthread_mutex_unlock(&sym_lock);
				}
			}
		}
		for (int p = 0; p < NS; p++) {
			if (tasks[p]) {
				sample_task(p, tasks[p]);
			}
		}
		usleep(10000);
	}
	return NULL;
}

/* a window's top five, by leaf or by on-the-stack, into line; the counts cleared */
static void
sample_window(int w, char hist[NS][4000])
{
	pthread_mutex_lock(&sym_lock);
	for (int p = 0; p < NS; p++) {
		printf("SAMPLEDBG %s pid %d tfp %d: calls %u threads %u infos %u running %u states %u; kr %x %x %x; samples %u\n",
		    stag[p], dbg_pid[p], dbg_tfp[p], dbg[p][0], dbg[p][1], dbg[p][2], dbg[p][3], dbg[p][4], dbg_kr[p][0],
		    dbg_kr[p][1], dbg_kr[p][2], samples[p]);
		if (samples[p] < 5) {
			memset(syms[p], 0, sizeof(syms[p]));
			samples[p] = 0;
			continue;
		}
		/* y300: backboardd's profile over a whole phase of the session, not a window's top two: the script's swipes (phase A)
		 * and its video with seeks (phase B), each the 22 places most often
		 * running and the 14 most often on the stack, in samples, said again with every window (the log keeps its end). */
		if (p == 1) {
			enum { TOT = 400 };
			static struct { char name[48]; unsigned leaf, in; } tot[2][TOT];
			static unsigned tot_n[2];
			int t = w * 3, ph = t >= 85 ? 0 : t >= 58 && t < 72 ? 1 : -1;   /* y300's script: swipes from 85 s, the video and its seeks 58 to 72 */
			if (ph >= 0) {
				tot_n[ph] += samples[p];
				for (int i = 0; i < SYMS; i++) {
					struct sym *s = &syms[p][i];
					if (!s->key || !(s->leaf | s->in)) {
						continue;
					}
					int at = -1, free_at = -1;
					for (int k = 0; k < TOT; k++) {
						if (tot[ph][k].name[0] == 0) {
							free_at = free_at < 0 ? k : free_at;
							break;
						}
						if (strncmp(tot[ph][k].name, s->name, sizeof(s->name)) == 0) {
							at = k;
							break;
						}
					}
					if (at < 0 && free_at >= 0) {
						at = free_at;
						memcpy(tot[ph][at].name, s->name, sizeof(s->name));
						tot[ph][at].name[47] = 0;
					}
					if (at >= 0) {
						tot[ph][at].leaf += s->leaf;
						tot[ph][at].in += s->in;
					}
				}
				for (int pass = 0; pass < 2; pass++) {
					char out[1500];
					unsigned char used[TOT];
					int oo = snprintf(out, sizeof(out), "PROFILE BB %c %s n=%u:", "AB"[ph], pass ? "IN" : "LEAF", tot_n[ph]);
					memset(used, 0, sizeof(used));
					for (int k = 0; k < (pass ? 14 : 22); k++) {
						int best = -1;
						for (int i = 0; i < TOT && tot[ph][i].name[0]; i++) {
							unsigned v = pass ? tot[ph][i].in : tot[ph][i].leaf;
							if (!used[i] && v && (best < 0 || v > (pass ? tot[ph][best].in : tot[ph][best].leaf))) {
								best = i;
							}
						}
						if (best < 0 || oo > (int)sizeof(out) - 64) {
							break;
						}
						used[best] = 1;
						oo += snprintf(out + oo, sizeof(out) - (size_t)oo, " %.44s %u;", tot[ph][best].name,
						    pass ? tot[ph][best].in : tot[ph][best].leaf);
					}
					printf("%s\n", out);
				}
			}
		}
		char line[700], brief[300];
		int o = snprintf(line, sizeof(line), "t=%d n=%u LEAF", w * 3, samples[p]);
		int bo = snprintf(brief, sizeof(brief), "t=%d:", w * 3);
		for (int pass = 0; pass < 2; pass++) {
			if (pass == 1) {
				o += snprintf(line + o, sizeof(line) - (size_t)o, " IN");
				bo += snprintf(brief + bo, sizeof(brief) - (size_t)bo, " /");
			}
			for (int k = 0; k < 5; k++) {
				struct sym *best = NULL;
				for (int i = 0; i < SYMS; i++) {
					struct sym *s = &syms[p][i];
					unsigned v = pass ? s->in : s->leaf;
					if (s->key && v && (best == NULL || v > (pass ? best->in : best->leaf))) {
						best = s;
					}
				}
				if (best == NULL) {
					break;
				}
				unsigned v = pass ? best->in : best->leaf;
				o += snprintf(line + o, sizeof(line) - (size_t)o, " %.40s %u%%;", best->name, v * 100 / samples[p]);
				if (k < (pass ? 4 : 2) && bo < (int)sizeof(brief)) {
					bo += snprintf(brief + bo, sizeof(brief) - (size_t)bo, " %.26s %u", best->name, v * 100 / samples[p]);
				}
				if (pass) {
					best->in = 0;
				} else {
					best->leaf = 0;
				}
			}
		}
		(void)line;
		if (strlen(hist[p]) + strlen(brief) + 4 < 4000) {
			strcat(hist[p], " | ");
			strcat(hist[p], brief);
		}
		printf("SAMPLES %s%s\n", stag[p], hist[p]);
		memset(syms[p], 0, sizeof(syms[p]));
		samples[p] = 0;
	}
	pthread_mutex_unlock(&sym_lock);
}

int proc_listallpids(void *buffer, int buffersize);
int proc_name(int pid, void *buffer, uint32_t buffersize);
int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
#define PROC_PIDTASKINFO        4
struct proc_taskinfo {
	uint64_t pti_virtual_size, pti_resident_size, pti_total_user, pti_total_system, pti_threads_user, pti_threads_system;
	int32_t pti_policy, pti_faults, pti_pageins, pti_cow_faults, pti_messages_sent, pti_messages_received;
	int32_t pti_syscalls_mach, pti_syscalls_unix, pti_csw, pti_threadnum, pti_numrunning, pti_priority;
};

/* and backboardd's threads by name (y99: backboardd half a core to a core with a video; which of its threads) */
#define PROC_PIDTHREADINFO      5
#define PROC_PIDLISTTHREADS     6
struct proc_threadinfo {
	uint64_t pth_user_time, pth_system_time;
	int32_t pth_cpu_usage, pth_policy, pth_run_state, pth_flags, pth_sleep_time, pth_curpri, pth_priority, pth_maxpriority;
	char pth_name[64];
};
#define MAXT 256
struct thr { uint64_t id, ns; char name[40]; };

static int
threads_of(int pid, struct thr *s)
{
	static uint64_t ids[MAXT];
	int bytes = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, ids, sizeof(ids)), n = 0;
	for (int i = 0; i < bytes / 8 && n < MAXT; i++) {
		struct proc_threadinfo ti;
		if (proc_pidinfo(pid, PROC_PIDTHREADINFO, ids[i], &ti, sizeof(ti)) != (int)sizeof(ti)) {
			continue;
		}
		s[n].id = ids[i];
		s[n].ns = ti.pth_user_time + ti.pth_system_time;
		snprintf(s[n].name, sizeof(s[n].name), "%s", ti.pth_name[0] ? ti.pth_name : "?");
		n++;
	}
	return n;
}

#define MAXP 1024
struct proc { int pid; uint64_t t, s; char name[32]; };   /* s: of t, in the kernel (y172) */

static int
take(struct proc *p)
{
	static int pids[MAXP];
	int n = proc_listallpids(pids, sizeof(pids)), k = 0;
	for (int i = 0; i < n && k < MAXP; i++) {
		struct proc_taskinfo ti;
		if (pids[i] <= 0 || proc_pidinfo(pids[i], PROC_PIDTASKINFO, 0, &ti, sizeof(ti)) != (int)sizeof(ti)) {
			continue;
		}
		p[k].pid = pids[i];
		p[k].t = ti.pti_total_user + ti.pti_total_system;   /* mach absolute time units */
		p[k].s = ti.pti_total_system;
		p[k].name[0] = 0;
		proc_name(pids[i], p[k].name, sizeof(p[k].name));
		k++;
	}
	return k;
}

int
main(int argc, char **argv)
{
	int seconds = argc > 1 ? atoi(argv[1]) : 60;
	static struct proc a[MAXP], b[MAXP];
	static char history[3000];
	mach_timebase_info_data_t tbase;
	mach_timebase_info(&tbase);
	setvbuf(stdout, NULL, _IOLBF, 0);
	/* (y111: the kernel now tells programs about the crypto extensions; what they see, at the head of the history) */
	{
		int aes = -1, sha = -1, pmull = -1;
		size_t z = sizeof(int);
		sysctlbyname("hw.optional.arm.FEAT_AES", &aes, &z, NULL, 0);
		z = sizeof(int);
		sysctlbyname("hw.optional.arm.FEAT_SHA256", &sha, &z, NULL, 0);
		z = sizeof(int);
		sysctlbyname("hw.optional.arm.FEAT_PMULL", &pmull, &z, NULL, 0);
		snprintf(history, sizeof(history), " [FEAT_AES %d SHA256 %d PMULL %d]", aes, sha, pmull);
	}

	int na = take(a);
	uint64_t nib0, nip0, nob0;
	net_totals(&nib0, &nip0, &nob0);
	static char shist[NS][4000];
	pthread_t sampler_thread;
	pthread_create(&sampler_thread, NULL, sampler, NULL);
	/*
	 * The threads of the processes the stalls point at (y99-y101): backboardd (the compositor), SpringBoard (the
	 * far-left/far-right panes), chronod (the widgets), the WebKit GPU process (video). A window's threads over 3%.
	 */
	enum { NW = 4 };
	static const char *const watch[NW] = { "backboardd", "SpringBoard", "chronod", "com.apple.WebKit.GPU" };
	static const char *const tag[NW] = { "BB", "SB", "CHRONO", "WKGPU" };
	static struct thr ta[NW][MAXT], tb[MAXT];
	static int nta[NW], wpid[NW];
	static char thistory[NW][2500];
	for (int w = 1; w * 3 <= seconds; w++) {
		sleep(3);
		int nb = take(b);
		for (int k = 0; k < NW; k++) {
			int pid = -1;
			for (int j = 0; j < nb; j++) {
				if (strcmp(b[j].name, watch[k]) == 0) {
					pid = b[j].pid;
				}
			}
			if (pid != wpid[k]) {           /* new (or new again): counted from now */
				wpid[k] = pid;
				nta[k] = pid > 0 ? threads_of(pid, ta[k]) : 0;
				continue;
			}
			if (pid <= 0) {
				continue;
			}
			int ntb = threads_of(pid, tb);
			char tl[300];
			int to = snprintf(tl, sizeof(tl), "t=%d:", w * 3);
			for (int pick = 0; pick < 4; pick++) {
				int best = -1;
				double bp = 3.0;
				for (int j = 0; j < ntb; j++) {
					uint64_t before = tb[j].ns;
					for (int i = 0; i < nta[k]; i++) {
						if (ta[k][i].id == tb[j].id) {
							before = ta[k][i].ns;
						}
					}
					double p = 100.0 * (double)(tb[j].ns - before) / 3e9;
					if (p > bp && tb[j].name[0] != 0) {
						bp = p;
						best = j;
					}
				}
				if (best < 0) {
					break;
				}
				to += snprintf(tl + to, sizeof(tl) - (size_t)to, " %.28s %.0f%%;", tb[best].name, bp);
				tb[best].name[0] = 0;            /* (picked) */
			}
			nta[k] = threads_of(pid, ta[k]);  /* the times for the next window */
			if (to > 12 && strlen(thistory[k]) + strlen(tl) + 4 < sizeof(thistory[k])) {
				strcat(thistory[k], " | ");
				strcat(thistory[k], tl);
			}
			(void)tag;           /* (y103: the sampler says more; the log ring is small) */
		}
		/* the top five of this window */
		int top[5] = { -1, -1, -1, -1, -1 };
		double pct[MAXP], kpct[MAXP];
		double all = 0;
		for (int j = 0; j < nb; j++) {
			uint64_t before = b[j].t, sbefore = b[j].s;     /* new process: counted from now */
			for (int i = 0; i < na; i++) {
				if (a[i].pid == b[j].pid) {
					before = a[i].t;
					sbefore = a[i].s;
				}
			}
			double ns = (double)(b[j].t - before) * tbase.numer / tbase.denom;
			pct[j] = 100.0 * ns / 3e9;
			kpct[j] = 100.0 * (double)(b[j].s - sbefore) * tbase.numer / tbase.denom / 3e9;
			all += pct[j];
			for (int s = 0; s < 5; s++) {
				if (top[s] < 0 || pct[j] > pct[top[s]]) {
					for (int m = 4; m > s; m--) {
						top[m] = top[m - 1];
					}
					top[s] = j;
					break;
				}
			}
		}
		char line[520];
		/* (y146: the VM compressor's mode and what it holds, MB) */
		int vmmode = -1;
		uint64_t vmbytes = 0;
		size_t vz = sizeof(vmmode);
		sysctlbyname("vm.compressor_mode", &vmmode, &vz, NULL, 0);
		vz = sizeof(vmbytes);
		sysctlbyname("vm.compressor_bytes_used", &vmbytes, &vz, NULL, 0);
		uint64_t nib, nip, nob;
		net_totals(&nib, &nip, &nob);
		int o = snprintf(line, sizeof(line), "t=%d all %.0f%% vmc %d/%lluMB net %lluKB/s %llupk/s out %lluKB/s:", w * 3, all,
		    vmmode, (unsigned long long)(vmbytes >> 20), (unsigned long long)((nib - nib0) / 3072),
		    (unsigned long long)((nip - nip0) / 3), (unsigned long long)((nob - nob0) / 3072));
		nib0 = nib, nip0 = nip, nob0 = nob;
		for (int s = 0; s < 5 && top[s] >= 0; s++) {
			if (pct[top[s]] >= 2.0) {
				/* (y172: and how much of it in the kernel, "kN": WebKit.Networking at 86-111% in y171) */
				o += snprintf(line + o, sizeof(line) - (size_t)o, " %s %.0f%% k%.0f;", b[top[s]].name, pct[top[s]],
				    kpct[top[s]]);
			}
		}
		(void)0;
		if (strlen(history) + strlen(line) + 4 < sizeof(history)) {
			strcat(history, " | ");
			strcat(history, line);
		}
		printf("CPU HISTORY%s\n", history);
		sample_window(w, shist);
		memcpy(a, b, sizeof(b[0]) * (size_t)nb);
		na = nb;
	}
	for (int k = 0; k < 20; k++) {
		printf("CPU HISTORY%s\n", history);
		sleep(3);
	}
	return 0;
}
