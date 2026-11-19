/* SPDX-License-Identifier: MIT
 * thread_cpu_test [NAME [SECONDS]]: which threads of a process (default mediaserverd: 2 cores while a video played,
 * y86) use the processor, by name, over SECONDS (15). Lines "THREADS: ...", a summary at the end.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* libSystem's (libproc.h is not in the iOS SDK) */
int proc_listallpids(void *buffer, int buffersize);
int proc_name(int pid, void *buffer, uint32_t buffersize);
int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);
#define PROC_PIDTHREADINFO      5
#define PROC_PIDLISTTHREADS     6
struct proc_threadinfo {
	uint64_t pth_user_time, pth_system_time;
	int32_t pth_cpu_usage, pth_policy, pth_run_state, pth_flags, pth_sleep_time, pth_curpri, pth_priority, pth_maxpriority;
	char pth_name[64];
};

#define MAXT 512
struct sample { uint64_t id, ns; char name[64]; };

static int
take(int pid, struct sample *s)
{
	static uint64_t ids[MAXT];
	int bytes = proc_pidinfo(pid, PROC_PIDLISTTHREADS, 0, ids, sizeof(ids)), n = 0;

	for (int i = 0; i < bytes / 8 && n < MAXT; i++) {
		struct proc_threadinfo ti;
		if (proc_pidinfo(pid, PROC_PIDTHREADINFO, ids[i], &ti, sizeof(ti)) != (int)sizeof(ti)) {
			continue;
		}
		s[n].id = ids[i];
		s[n].ns = ti.pth_user_time + ti.pth_system_time;            /* nanoseconds */
		snprintf(s[n].name, sizeof(s[n].name), "%s", ti.pth_name[0] ? ti.pth_name : "(no name)");
		n++;
	}
	return n;
}

int
main(int argc, char **argv)
{
	const char *want = argc > 1 ? argv[1] : "mediaserverd";
	int seconds = argc > 2 ? atoi(argv[2]) : 15, pid = -1;
	static int pids[4096];
	static struct sample a[MAXT], b[MAXT];
	char summary[1024] = "";

	setvbuf(stdout, NULL, _IOLBF, 0);
	int n = proc_listallpids(pids, sizeof(pids));
	for (int i = 0; i < n; i++) {
		char name[64] = "";
		if (proc_name(pids[i], name, sizeof(name)) > 0 && strcmp(name, want) == 0) {
			pid = pids[i];
		}
	}
	if (pid < 0) {
		printf("THREADS: no %s\n", want);
		return 1;
	}
	int na = take(pid, a);
	sleep(seconds);
	int nb = take(pid, b);
	printf("THREADS: %s[%d], %d threads, %d s\n", want, pid, nb, seconds);
	for (int j = 0; j < nb; j++) {
		uint64_t before = 0;
		for (int i = 0; i < na; i++) {
			if (a[i].id == b[j].id) {
				before = a[i].ns;
			}
		}
		double pct = 100.0 * (double)(b[j].ns - before) / ((double)seconds * 1e9);
		if (pct >= 1.0) {
			printf("THREADS: %6.1f%%  %s\n", pct, b[j].name);
			snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " %s %.0f%%;", b[j].name, pct);
		}
	}
	printf("THREADS SUMMARY %s:%s\n", want, summary);
	return 0;
}
