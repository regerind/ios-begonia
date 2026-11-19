/* SPDX-License-Identifier: MIT
 * superpatch5_test, round 5 after y84: the sensors are on I2C 1 once its pins (GPIO61/62) leave the SCP's I2C (mode 2)
 * for the AP's (mode 1): 0x0c (an AKM compass?), 0x38, 0x39, 0x47 (light / proximity?). Their id registers, and
 * bus 6 set up again (the speaker amp). Lines "SUPER5 <WHAT>: ...", a summary at the end.
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static char summary[3072];

static void
step(const char *what, const char *cmd)
{
	static char buf[4096];
	size_t got = sizeof(buf);

	buf[0] = 0;
	if (sysctlbyname("kern.begonia_i2c", NULL, NULL, (void *)cmd, strlen(cmd)) != 0 ||
	    sysctlbyname("kern.begonia_i2c", buf, &got, NULL, 0) != 0) {
		snprintf(buf, sizeof(buf), "\"%s\" failed", cmd);
	}
	for (char *p = buf; (p = strchr(p, '\n')) != NULL;) {
		*p = ';';
	}
	printf("SUPER5 %s: %s\n", what, buf);
	snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " | %s", buf);
}

int
main(void)
{
	static const char *const cmds[] = {
		"pins 1 61 62",
		"r 1 0c 00 4",          /* AKM: WIA1 0x48, WIA2 (device) */
		"r 1 38 00 8", "r 1 38 3e 2", "r 1 38 86 2", "r 1 38 92 1",
		"r 1 39 00 8", "r 1 39 3e 2", "r 1 39 86 2", "r 1 39 92 1", "r 1 39 12 1",
		"r 1 47 00 8", "r 1 47 3e 2", "r 1 47 86 2", "r 1 47 0f 1",
		"clone 5 6", "r 6 34 03 2",
	};

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
		step("SENSORS", cmds[i]);
	}
	printf("SUPER5 SUMMARY:%s\n", summary);
	return 0;
}
