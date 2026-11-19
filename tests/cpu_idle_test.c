/* SPDX-License-Identifier: MIT
 * cpu_idle_test: who uses the CPU while nobody touches the phone (the heat). Every process's user + system time
 * (proc_pid_rusage, pid 0 = the kernel: its threads, the polling drivers among them) at the start and SECONDS later;
 * the busiest, in % of one core over that time, on one line each and in one summary line at the end.
 *   cpu_idle_test [SECONDS]      (30)
 */
#include <libproc.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>

#define MAX 2048

struct sample {
	int pid;
	uint64_t ns;
	char name[64];
};

static double ticks_to_ns;

static int
take(struct sample *s)
{
	static int pids[MAX];
	int n = proc_listallpids(pids, sizeof(pids)) , got = 0;

	if (n > MAX) {
		n = MAX;
	}
	/* pid 0 is not always listed */
	int have0 = 0;
	for (int i = 0; i < n; i++) {
		have0 |= pids[i] == 0;
	}
	if (!have0 && n < MAX) {
		pids[n++] = 0;
	}
	for (int i = 0; i < n; i++) {
		struct rusage_info_v2 ri;

		if (proc_pid_rusage(pids[i], RUSAGE_INFO_V2, (rusage_info_t *)&ri) != 0) {
			continue;
		}
		s[got].pid = pids[i];
		s[got].ns = (uint64_t)((double)(ri.ri_user_time + ri.ri_system_time) * ticks_to_ns);
		if (pids[i] == 0) {
			strcpy(s[got].name, "kernel");
		} else if (proc_name(pids[i], s[got].name, sizeof(s[got].name)) <= 0) {
			snprintf(s[got].name, sizeof(s[got].name), "pid%d", pids[i]);
		}
		got++;
	}
	return got;
}

int
main(int argc, char **argv)
{
	static struct sample a[MAX], b[MAX];
	struct { int i; uint64_t d; } top[16] = { 0 };
	mach_timebase_info_data_t tb;
	int seconds = argc > 1 ? atoi(argv[1]) : 30;

	setvbuf(stdout, NULL, _IOLBF, 0);
	mach_timebase_info(&tb);
	ticks_to_ns = (double)tb.numer / tb.denom;
	int na = take(a);
	uint64_t t0 = mach_absolute_time();
	sleep(seconds);
	int nb = take(b);
	double wall = (double)(mach_absolute_time() - t0) * ticks_to_ns;
	uint64_t total = 0;

	for (int j = 0; j < nb; j++) {
		uint64_t before = 0;
		for (int i = 0; i < na; i++) {
			if (a[i].pid == b[j].pid) {
				before = a[i].ns;
				break;
			}
		}
		uint64_t d = b[j].ns > before ? b[j].ns - before : 0;
		total += d;
		for (int k = 0; k < 16; k++) {
			if (d > top[k].d) {
				memmove(&top[k + 1], &top[k], sizeof(top[0]) * (15 - k));
				top[k].i = j;
				top[k].d = d;
				break;
			}
		}
	}
	char summary[1024] = "";
	printf("CPU IDLE: %d processes, %.1f s, all together %.1f%% of one core\n", nb, wall / 1e9, 100.0 * total / wall);
	for (int k = 0; k < 16 && top[k].d; k++) {
		printf("CPU IDLE: %-28s pid %5d %6.2f%%\n", b[top[k].i].name, b[top[k].i].pid, 100.0 * top[k].d / wall);
		snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " %s %.1f", b[top[k].i].name,
		    100.0 * top[k].d / wall);
	}
	printf("CPU IDLE SUMMARY (%% of one core over %d s, total %.1f):%s\n", seconds, 100.0 * total / wall, summary);
	return 0;
}
