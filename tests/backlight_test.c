/* SPDX-License-Identifier: MIT
 * backlight_test: the panel's brightness through kern.begonia_backlight (DCS 0x51, kernel/bsd/dev/begonia_backlight.c),
 * in steps somebody at the phone can see: 255, 64, 16, 128, 255, three seconds each.
 */
#include <stdio.h>
#include <sys/sysctl.h>
#include <unistd.h>

int
main(void)
{
	static const int steps[] = { 255, 64, 16, 128, 255 };

	setvbuf(stdout, NULL, _IOLBF, 0);
	for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
		int level = steps[i], back = -1;
		size_t size = sizeof(back);

		if (sysctlbyname("kern.begonia_backlight", NULL, NULL, &level, sizeof(level)) != 0) {
			perror("BACKLIGHT TEST: kern.begonia_backlight");
			return 1;
		}
		sysctlbyname("kern.begonia_backlight", &back, &size, NULL, 0);
		printf("BACKLIGHT TEST: level %d (reads %d)\n", level, back);
		sleep(3);
	}
	printf("BACKLIGHT TEST: end\n");
	return 0;
}
