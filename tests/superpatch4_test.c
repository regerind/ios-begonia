/* SPDX-License-Identifier: MIT
 * superpatch4_test, round 4 after y83: the buses whose pins were not in I2C mode (the device tree's scl/sda pins,
 * mode 1, bus 5's setup copied), a scan of all, and the device beside the speaker amp on I2C 6 (0x0e: a compass?).
 * Lines "SUPER4 <WHAT>: ...", a summary at the end.
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static char summary[2048];

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
	printf("SUPER4 %s: %s\n", what, buf);
	snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " | %s", buf);
}

int
main(void)
{
	static const char *const pins[] = { "pins 1 61 62", "pins 2 112 113", "pins 3 55 56", "pins 4 110 111", "pins 7 198 199" };

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
		step("PINS", pins[i]);
	}
	step("I2C", "scan");
	step("COMPASS", "who 6 0e");
	step("COMPASS", "r 6 0e 00 4");
	printf("SUPER4 SUMMARY:%s\n", summary);
	return 0;
}
