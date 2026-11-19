/*
 * begonia: the LCD backlight. The board's device tree (dtbo, led@6 "mediatek,lcd-backlight") says led_mode 5,
 * MT65XX_LED_MODE_CUST_BLS_PWM, led_bits 11: the panel's backlight is the display PWM, DISP_PWM0 at 0x1100e000, a level
 * of 0..2047. LK leaves it on at its own level; nothing here changed it, so the screen could not be dimmed, and stayed
 * lit when the phone locked (the framebuffer's power change was answered and ignored).
 *
 * Registers (MediaTek's ddp_pwm.c, this generation): EN 0x00 (bit 0), COMMIT 0x0c (1 then 0 makes CON_* take effect),
 * CON_0 0x18 (clock source and divider), CON_1 0x1c (period in bits 0..12, the high width -- the level -- in 16..29).
 * They are read and said once at start, so the layout can be checked against what LK left.
 *
 *   kern.begonia_backlight      the level, 0..2047 (0: off); writable by anybody, for a test session's script
 *   begonia_backlight_set()     the level, from the framebuffer's brightness (MiniIOMobileFramebuffer.cpp, patch 0019)
 *   begonia_backlight_power()   off and on again at the level last set (the framebuffer's power change)
 */
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void begonia_perf_note(const char *format, ...) __printflike(1, 2);

#define DISP_PWM0       0x1100e000
#define PWM_EN          0x00
#define PWM_COMMIT      0x0c
#define PWM_CON_0       0x18
#define PWM_CON_1       0x1c
#define PWM_MAX         2047

static volatile uint32_t *pwm;
static uint32_t level_set;              /* the level asked for (kept while the display is off) */
static bool powered = true;

static uint32_t rd(uint32_t offset) {return pwm[offset / 4];}
static void wr(uint32_t offset, uint32_t value) {pwm[offset / 4] = value;}

static void
apply(uint32_t level)
{
	if (pwm == NULL) {
		return;
	}
	if (level > PWM_MAX) {
		level = PWM_MAX;
	}
	if (level > 0) {
		wr(PWM_CON_1, (rd(PWM_CON_1) & ~(0x3fffu << 16)) | (level << 16));
		wr(PWM_EN, rd(PWM_EN) | 1);
	} else {
		wr(PWM_CON_1, (rd(PWM_CON_1) & ~(0x3fffu << 16)) | (1u << 16));      /* (MediaTek's: never 0) */
		wr(PWM_EN, rd(PWM_EN) & ~1u);
	}
	wr(PWM_COMMIT, 1);
	wr(PWM_COMMIT, 0);
}

uint32_t
begonia_backlight_get(void)
{
	return level_set;
}

void
begonia_backlight_set(uint32_t level)
{
	static unsigned said;

	level_set = level > PWM_MAX ? PWM_MAX : level;
	if (said++ < 12) {
		printf("BEGONIA BACKLIGHT: level %u%s\n", level_set, powered ? "" : " (kept: the display is off)");
	}
	if (powered) {
		apply(level_set);
	}
}

void
begonia_backlight_power(bool on)
{
	static unsigned said;

	if (said++ < 16) {
		printf("BEGONIA BACKLIGHT: display %s (level %u)\n", on ? "on" : "off", level_set);
	}
	powered = on;
	apply(on ? level_set : 0);
}

static int
sysctl_begonia_backlight SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int value = (int) level_set, changed = 0;
	int error = sysctl_io_number(req, value, sizeof(int), &value, &changed);

	if (error == 0 && changed) {
		if (value < 0 || value > PWM_MAX) {
			return EINVAL;
		}
		begonia_backlight_set((uint32_t) value);
	}
	return error;
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_backlight, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    0, 0, sysctl_begonia_backlight, "I", "the LCD backlight's level, 0..2047");

void
begonia_backlight_init(void)
{
	pwm = (volatile uint32_t *) ml_io_map(DISP_PWM0, 0x1000);
	if (pwm == NULL) {
		begonia_perf_note("BEGONIA BACKLIGHT: DISP_PWM0 not mapped");
		return;
	}
	uint32_t con1 = rd(PWM_CON_1);

	level_set = (con1 >> 16) & 0x3fff;
	begonia_perf_note("BEGONIA BACKLIGHT: as LK left it: EN %#x COMMIT %#x CON_0 %#x CON_1 %#x (period %u, level %u), debug %#x",
	    rd(PWM_EN), rd(PWM_COMMIT), rd(PWM_CON_0), con1, con1 & 0x1fff, level_set, rd(0x80));
	if (level_set == 0 || level_set > PWM_MAX) {
		level_set = 1024;
	}
}
