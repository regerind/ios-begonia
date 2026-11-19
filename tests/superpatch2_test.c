/* SPDX-License-Identifier: MIT
 * superpatch2_test, round 2 after y81's logs, each line "SUPER2 <WHAT>: ..." and a summary at the end:
 *   LM36273     its registers 0x00..0x0f (I2C 0, 0x11: the backlight, y81's scan)
 *   BRIGHTNESS  kern.begonia_backlight 255, 64, 16, 128, 255 (now the LM36273's own brightness), registers read back
 *   SOUND       I2C6's pins to I2C mode, the amp's reset pulsed, its revision (kern.begonia_i2c "tfa")
 *   SENSORS     the one device on I2C 8 (0x1d): its chip-id registers
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static char summary[1536];

static void
note(const char *what, const char *text)
{
	snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " | %s %s", what, text);
}

static const char *
i2c(const char *cmd)
{
	static char buf[4096];
	size_t got = sizeof(buf);

	buf[0] = 0;
	if (sysctlbyname("kern.begonia_i2c", NULL, NULL, (void *)cmd, strlen(cmd)) != 0 ||
	    sysctlbyname("kern.begonia_i2c", buf, &got, NULL, 0) != 0) {
		snprintf(buf, sizeof(buf), "\"%s\" failed", cmd);
	}
	buf[strcspn(buf, "\n")] = 0;
	return buf;
}

int
main(void)
{
	static const int steps[] = { 255, 64, 16, 128, 255 };
	const char *a;

	setvbuf(stdout, NULL, _IOLBF, 0);
	a = i2c("r 0 11 00 8");
	printf("SUPER2 LM36273: %s\n", a);
	note("lm 00-07", a);
	a = i2c("r 0 11 08 8");
	printf("SUPER2 LM36273: %s\n", a);
	note("lm 08-0f", a);

	for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		int level = steps[i];
		char line[160];

		sysctlbyname("kern.begonia_backlight", NULL, NULL, &level, sizeof(level));
		a = i2c("r 0 11 04 5");
		snprintf(line, sizeof(line), "%d: %s", level, a);
		printf("SUPER2 BRIGHTNESS: level %s\n", line);
		note("bl", line);
		sleep(4);
	}

	a = i2c("tfa");
	printf("SUPER2 SOUND: %s\n", a);
	note("sound", a);
	a = i2c("who 8 1d");
	printf("SUPER2 SENSORS: %s\n", a);
	note("sensors", a);
	printf("SUPER2 SUMMARY:%s\n", summary);
	return 0;
}
