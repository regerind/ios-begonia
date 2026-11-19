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
#include <mach/mach_time.h>

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
struct proc { int pid; uint64_t t; char name[32]; };

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

	int na = take(a);
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
			printf("THR %s%s\n", tag[k], thistory[k]);
		}
		/* the top five of this window */
		int top[5] = { -1, -1, -1, -1, -1 };
		double pct[MAXP];
		double all = 0;
		for (int j = 0; j < nb; j++) {
			uint64_t before = b[j].t;               /* new process: counted from now */
			for (int i = 0; i < na; i++) {
				if (a[i].pid == b[j].pid) {
					before = a[i].t;
				}
			}
			double ns = (double)(b[j].t - before) * tbase.numer / tbase.denom;
			pct[j] = 100.0 * ns / 3e9;
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
		char line[400];
		int o = snprintf(line, sizeof(line), "t=%d all %.0f%%:", w * 3, all);
		for (int s = 0; s < 5 && top[s] >= 0; s++) {
			if (pct[top[s]] >= 2.0) {
				o += snprintf(line + o, sizeof(line) - (size_t)o, " %s %.0f%%;", b[top[s]].name, pct[top[s]]);
			}
		}
		printf("CPU %s\n", line);
		if (strlen(history) + strlen(line) + 4 < sizeof(history)) {
			strcat(history, " | ");
			strcat(history, line);
		}
		printf("CPU HISTORY%s\n", history);
		memcpy(a, b, sizeof(b[0]) * (size_t)nb);
		na = nb;
	}
	for (int k = 0; k < 20; k++) {
		printf("CPU HISTORY%s\n", history);
		sleep(3);
	}
	return 0;
}
