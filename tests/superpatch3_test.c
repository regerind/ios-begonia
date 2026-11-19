/* SPDX-License-Identifier: MIT
 * superpatch3_test, round 3 after y82: lines "SUPER3 <WHAT>: ..." and a summary at the end.
 *   I2C      the configuration of buses LK set up (0, 5) and of one it did not (6); bus 5's copied onto 6, 3 and 9;
 *            a scan again (y81, y82: those three "acknowledged" every address)
 *   SOUND    the amp (TFA98xx, I2C6 0x34) on the cloned bus
 *   SENSORS  the device on I2C 8 (0x1d): registers 0x00..0x0f
 *   TORCH    kern.begonia_torch (Control Centre's path): lowest current for two seconds, then off
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static char summary[2048];

static void
note(const char *what, const char *text)
{
	snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " | %s %s", what, text);
}

static const char *
i2c(const char *cmd, int all_lines)
{
	static char buf[4096];
	size_t got = sizeof(buf);

	buf[0] = 0;
	if (sysctlbyname("kern.begonia_i2c", NULL, NULL, (void *)cmd, strlen(cmd)) != 0 ||
	    sysctlbyname("kern.begonia_i2c", buf, &got, NULL, 0) != 0) {
		snprintf(buf, sizeof(buf), "\"%s\" failed", cmd);
	}
	if (!all_lines) {
		buf[strcspn(buf, "\n")] = 0;
	} else {
		for (char *p = buf; (p = strchr(p, '\n')) != NULL;) {
			*p = ';';
		}
	}
	return buf;
}

static void
step(const char *what, const char *cmd, int all_lines)
{
	const char *a = i2c(cmd, all_lines);
	printf("SUPER3 %s: %s\n", what, a);
	note(what, a);
}

int
main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	step("I2C", "regs 0", 0);
	step("I2C", "regs 5", 0);
	step("I2C", "regs 6", 0);
	step("I2C", "clone 5 6", 1);
	step("I2C", "clone 5 3", 1);
	step("I2C", "clone 5 9", 1);
	step("I2C", "scan", 1);
	step("SOUND", "tfa", 0);
	step("SENSORS", "r 8 1d 00 8", 0);
	step("SENSORS", "r 8 1d 08 8", 0);

	int on = 0, off = -1, e1, e2;
	e1 = sysctlbyname("kern.begonia_torch", NULL, NULL, &on, sizeof(on));
	sleep(2);
	e2 = sysctlbyname("kern.begonia_torch", NULL, NULL, &off, sizeof(off));
	char t[80];
	snprintf(t, sizeof(t), "on %s, off %s", e1 == 0 ? "ok" : "FAILED", e2 == 0 ? "ok" : "FAILED");
	printf("SUPER3 TORCH: %s\n", t);
	note("torch", t);

	printf("SUPER3 SUMMARY:%s\n", summary);
	return 0;
}
