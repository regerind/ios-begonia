/*
 * begonia: the LCD's brightness, which is the LM36273's (TI backlight + display bias chip) on I2C bus 0 at 0x11 (y81's
 * scan: the one device there; LK set the bus up). Xiaomi's leds-lm36273.c configures it with BC1 = 0x60, "disable pwm":
 * the brightness is its registers alone -- BB_LSB (0x04, bits 0..2) and BB_MSB (0x05, bits 3..10), 11 bits -- and
 * BL_ENABLE (0x08) switches the LED strings (Xiaomi: 0x17 on, 0 off). The panel's DCS 0x51 (y80, y81: acknowledged by
 * DSI, no visible change) and the display PWM (off as LK leaves it) are not this phone's brightness.
 * Only 0x04, 0x05 and 0x08 are written; the bias registers (0x09..0x0e: the panel's +-5.5 V) are never touched.
 *
 *   kern.begonia_backlight      the level, 0..255 (0: dark); writable by anybody, for a test session's script
 *   begonia_backlight_set()     the level, from iOS's brightness (the framebuffer, patch 0019)
 *   begonia_backlight_power()   dark (BL_ENABLE 0) and back (the framebuffer's power change: the screen locked)
 */
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/clock.h>
#include <mach/mach_time.h>

void begonia_perf_note(const char *format, ...) __printflike(1, 2);
int begonia_i2c_xfer(int bus, uint8_t addr, const uint8_t *out, unsigned n, uint8_t *in, unsigned m);

#define LM_BUS          0
#define LM_ADDR         0x11
#define LM_BB_LSB       0x04
#define LM_BB_MSB       0x05
#define LM_BL_ENABLE    0x08
#define LEVEL_MAX       255

static uint32_t level_set = 255;        /* the level asked for (kept while the display is off) */
static bool powered = true, known;
static uint8_t enable_on = 0x17;        /* BL_ENABLE as LK left it (Xiaomi's 0x17 if it read 0) */

static int
lm_write(uint8_t reg, uint8_t v)
{
	uint8_t b[2] = { reg, v };
	return begonia_i2c_xfer(LM_BUS, LM_ADDR, b, 2, NULL, 0);
}

static int
lm_read(uint8_t reg, uint8_t *v)
{
	return begonia_i2c_xfer(LM_BUS, LM_ADDR, &reg, 1, v, 1);
}

static void
learn(void)
{
	uint8_t en = 0, lsb = 0, msb = 0, bc1 = 0;
	if (known) {
		return;
	}
	if (lm_read(LM_BL_ENABLE, &en) == 0 && lm_read(LM_BB_LSB, &lsb) == 0 && lm_read(LM_BB_MSB, &msb) == 0 &&
	    lm_read(0x02, &bc1) == 0) {
		known = true;
		if (en != 0) {
			enable_on = en;
		}
		printf("BEGONIA BACKLIGHT: LM36273 as LK left it: BL_ENABLE %#x, brightness %u of 2047, BC1 %#x\n", en,
		    ((unsigned)msb << 3) | (lsb & 7), bc1);
	}
}

/* the 11-bit brightness for a level 0..255 (squared: the eye's steps are finer at the dark end) */
static int
apply(uint32_t level)
{
	learn();
	if (level == 0) {
		return lm_write(LM_BL_ENABLE, 0);
	}
	uint32_t v = (level * level * 2047 + 65024) / 65025;
	if (v < 8) {
		v = 8;
	}
	int e = lm_write(LM_BB_LSB, (uint8_t)(v & 7));
	e |= lm_write(LM_BB_MSB, (uint8_t)(v >> 3));
	e |= lm_write(LM_BL_ENABLE, enable_on);
	return e;
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

	level_set = level > LEVEL_MAX ? LEVEL_MAX : level;
	int e = powered ? apply(level_set) : 0;
	if (said++ < 24) {
		printf("BEGONIA BACKLIGHT: level %u%s\n", level_set, !powered ? " (kept: the display is off)" : e ? " NOT set (I2C)" : " set");
		if (said <= 8) {
			begonia_perf_note("BEGONIA BACKLIGHT: level %u%s", level_set, !powered ? " (display off)" : e ? " NOT set" : "");
		}
	}
}

void
begonia_backlight_power(bool on)
{
	static unsigned said;

	powered = on;
	int e = apply(on ? level_set : 0);
	if (said++ < 16) {
		printf("BEGONIA BACKLIGHT: display %s (level %u)%s\n", on ? "on" : "off", level_set, e ? " NOT set (I2C)" : "");
		/* kept in the notes, which are said again and again (a session's lock is minutes before its log is read) */
		uint64_t ns;
		absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
		begonia_perf_note("BEGONIA BACKLIGHT: display %s at %llu s (level %u)%s", on ? "on" : "off", ns / NSEC_PER_SEC,
		    level_set, e ? " NOT set" : "");
	}
}

static int
sysctl_begonia_backlight SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int value = (int) level_set, changed = 0;
	int error = sysctl_io_number(req, value, sizeof(int), &value, &changed);

	if (error == 0 && changed) {
		if (value < 0 || value > LEVEL_MAX) {
			return EINVAL;
		}
		begonia_backlight_set((uint32_t) value);
	}
	return error;
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_backlight, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    0, 0, sysctl_begonia_backlight, "I", "the LCD's brightness (LM36273 on I2C 0), 0..255");

void
begonia_backlight_init(void)
{
	begonia_perf_note("BEGONIA BACKLIGHT: the LM36273 on I2C bus 0, 0x11 (read at first use)");
}
