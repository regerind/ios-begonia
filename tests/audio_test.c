/* SPDX-License-Identifier: MIT
 * audio_test: kern.begonia_audio status, tone (3 s on the earpiece), status -- every answer printed as "AUDIO: ...",
 * then the whole again every 4 s until the session ends (the log ring keeps only its last seconds).
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static char all[12288];

static void
run(const char *cmd)
{
	static char buf[4096];
	size_t got = sizeof(buf);

	buf[0] = 0;
	if (sysctlbyname("kern.begonia_audio", NULL, NULL, (void *)cmd, strlen(cmd)) != 0 ||
	    sysctlbyname("kern.begonia_audio", buf, &got, NULL, 0) != 0) {
		snprintf(buf, sizeof(buf), "\"%s\" failed\n", cmd);
	}
	snprintf(all + strlen(all), sizeof(all) - strlen(all), "%s", buf);
}

static void
say_all(void)
{
	for (char *line = all, *end; *line; line = end + 1) {
		end = strchr(line, '\n');
		if (end == NULL) {
			printf("AUDIO: %s\n", line);
			break;
		}
		printf("AUDIO: %.*s\n", (int)(end - line), line);
	}
}

int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	run("status");
	run("tone");
	run("status");
	for (int k = 0; k < 30; k++) {
		say_all();
		sleep(4);
	}
	return 0;
}
