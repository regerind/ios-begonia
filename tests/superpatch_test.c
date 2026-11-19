/* SPDX-License-Identifier: MIT
 * superpatch_test: one session's first look at everything still missing, each line "SUPER <WHAT>: ...", and one
 * summary line at the end (the log ring keeps the end):
 *   BRIGHTNESS  kern.begonia_backlight 255, 64, 16, 128, 255 (the panel's DCS 0x51), three seconds each
 *   I2C         kern.begonia_i2c "scan": who answers on each of the ten buses (sensors, camera, amp, PMIC are among them)
 *   SOUND       the speaker amp (TFA98xx, I2C6 0x34): its revision
 *   TORCH       the MT6360's flash LED, lowest current, one second
 *   BT          kern.begonia_wifi "bt": the Bluetooth controller on and answering HCI
 */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <unistd.h>

static char summary[1024];

static void
note(const char *what, const char *text)
{
	snprintf(summary + strlen(summary), sizeof(summary) - strlen(summary), " | %s %s", what, text);
}

/* a command to a string sysctl, its answer printed line by line under WHAT; the answer kept in BUF */
static int
command(const char *name, const char *what, const char *cmd, char *buf, size_t size)
{
	size_t got = size;

	buf[0] = 0;
	if (sysctlbyname(name, NULL, NULL, (void *)cmd, strlen(cmd)) != 0 || sysctlbyname(name, buf, &got, NULL, 0) != 0) {
		printf("SUPER %s: %s \"%s\" failed\n", what, name, cmd);
		return -1;
	}
	for (char *line = buf, *end; *line; line = end + 1) {
		end = strchr(line, '\n');
		if (end == NULL) {
			printf("SUPER %s: %s\n", what, line);
			break;
		}
		printf("SUPER %s: %.*s\n", what, (int)(end - line), line);
	}
	return 0;
}

int
main(void)
{
	static char buf[8192];
	static const int steps[] = { 255, 64, 16, 128, 255 };
	char tmp[256];

	setvbuf(stdout, NULL, _IOLBF, 0);

	int sent = 0;
	for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		int level = steps[i];
		sent += sysctlbyname("kern.begonia_backlight", NULL, NULL, &level, sizeof(level)) == 0;
		printf("SUPER BRIGHTNESS: level %d\n", level);
		sleep(3);
	}
	snprintf(tmp, sizeof(tmp), "%d/5 set (see the kernel's BEGONIA BACKLIGHT lines: sent or NOT sent)", sent);
	note("brightness", tmp);

	if (command("kern.begonia_i2c", "I2C", "scan", buf, sizeof(buf)) == 0) {
		char *p = buf;
		tmp[0] = 0;
		for (char *line = strtok(p, "\n"); line != NULL; line = strtok(NULL, "\n")) {
			snprintf(tmp + strlen(tmp), sizeof(tmp) - strlen(tmp), "[%s]", line);
		}
		note("i2c", tmp);
	}
	if (command("kern.begonia_i2c", "SOUND", "tfa", buf, sizeof(buf)) == 0) {
		buf[strcspn(buf, "\n")] = 0;
		note("sound", buf);
	}
	if (command("kern.begonia_i2c", "TORCH", "torch", buf, sizeof(buf)) == 0) {
		buf[strcspn(buf, "\n")] = 0;
		note("torch", buf);
	}
	if (command("kern.begonia_wifi", "BT", "bt", buf, sizeof(buf)) == 0) {
		char *last = strstr(buf, "hci reset");
		if (last != NULL) {
			last[strcspn(last, "\n")] = 0;
		}
		note("bt", last ? last : "no hci answer line");
	}
	printf("SUPER SENSORS: on the SCP sensor hub (SCP_nanoHub); see the I2C scan for any on the AP's own buses\n");
	printf("SUPER CAMERA: sensors unpowered (rails need the user's OK); see the I2C scan\n");
	printf("SUPER SUMMARY:%s\n", summary);
	return 0;
}
