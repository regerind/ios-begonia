/*
 * begonia: the LCD's brightness. On this phone it is the panel's: Xiaomi's own driver for both of begonia's panels
 * (tianma_fhd_nt36672a_dsi_vdo.c, csot_fhd_nt36672a_dsi_vdo.c: lcm_setbacklight_cmdq) sends the NT36672A the DCS
 * command 0x51 (write display brightness) with one byte, 0..255, and the panel drives the LM36273 backlight from it.
 * The display PWM (DISP_PWM0, which this driver drove first, from the device tree's led_mode 5) is off as LK leaves it
 * (EN 0, level 0, the screen lit all the same): it is not this phone's backlight.
 *
 * The command goes in video mode, between frames: DSI0 (0x14014000) VM_CMD_CON (0x130) = VM_CMD_EN, a short packet,
 * sent in the vertical front porch, data id 0x15 (DCS short write, one parameter), 0x51, the level; then VM_CMD_START
 * (DSI_START bit 16) from 0 to 1, VM_CMD_DONE awaited in DSI_INTSTA (0x00c, bit 5) and cleared (registers: MediaTek's
 * ddp_reg_dsi.h for MT6785). Nothing is sent unless DSI_MODE_CTRL says video mode.
 *
 *   kern.begonia_backlight      the level, 0..255 (0: dark); writable by anybody, for a test session's script
 *   begonia_backlight_set()     the level, from iOS's brightness (the framebuffer, patch 0019)
 *   begonia_backlight_power()   dark and back at the level last set (the framebuffer's power change: the screen locked)
 */
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/locks.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void begonia_perf_note(const char *format, ...) __printflike(1, 2);

#define DSI0            0x14014000
#define DSI_START       0x000
#define DSI_INTSTA      0x00c
#define DSI_MODE_CTRL   0x014
#define DSI_VM_CMD_CON  0x130
#define VM_CMD_START    (1u << 16)
#define VM_CMD_DONE     (1u << 5)
#define VM_CMD_EN       (1u << 0)
#define TS_VFP_EN       (1u << 5)
#define DCS_SHORT_1     0x15
#define DCS_BRIGHTNESS  0x51
#define LEVEL_MAX       255

static volatile uint32_t *dsi;
static uint32_t level_set = 255;        /* the level asked for (kept while the display is off) */
static bool powered = true;
static unsigned sent, failed;
static LCK_GRP_DECLARE(bl_grp, "begonia_backlight");
static LCK_MTX_DECLARE(bl_mtx, &bl_grp);

static uint32_t rd(uint32_t offset) {return dsi[offset / 4];}
static void wr(uint32_t offset, uint32_t value) {__builtin_arm_dsb(0xf); dsi[offset / 4] = value;}

/* One DCS 0x51 to the panel between frames; false when not sent (not video mode, or no VM_CMD_DONE in 50 ms). */
static bool
send_brightness(uint32_t level)
{
	if (dsi == NULL || (rd(DSI_MODE_CTRL) & 3) == 0) {
		return false;
	}
	lck_mtx_lock(&bl_mtx);
	wr(DSI_INTSTA, rd(DSI_INTSTA) & ~VM_CMD_DONE);
	wr(DSI_VM_CMD_CON, VM_CMD_EN | TS_VFP_EN | (DCS_SHORT_1 << 8) | (DCS_BRIGHTNESS << 16) | ((level & 0xff) << 24));
	wr(DSI_START, rd(DSI_START) & ~VM_CMD_START);
	wr(DSI_START, rd(DSI_START) | VM_CMD_START);
	bool done = false;
	for (unsigned i = 0; i < 500 && !(done = (rd(DSI_INTSTA) & VM_CMD_DONE) != 0); i++) {
		delay(100);
	}
	wr(DSI_INTSTA, rd(DSI_INTSTA) & ~VM_CMD_DONE);
	wr(DSI_START, rd(DSI_START) & ~VM_CMD_START);
	lck_mtx_unlock(&bl_mtx);
	if (done) {
		sent++;
	} else if (failed++ < 4) {
		printf("BEGONIA BACKLIGHT: 0x51 %u not acknowledged (INTSTA %#x START %#x VM_CMD_CON %#x MODE %#x)\n", level,
		    rd(DSI_INTSTA), rd(DSI_START), rd(DSI_VM_CMD_CON), rd(DSI_MODE_CTRL));
	}
	return done;
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
	bool ok = powered && send_brightness(level_set);
	if (said++ < 24) {
		printf("BEGONIA BACKLIGHT: level %u%s\n", level_set, !powered ? " (kept: the display is off)" : ok ? " sent" : " NOT sent");
	}
}

void
begonia_backlight_power(bool on)
{
	static unsigned said;

	powered = on;
	bool ok = send_brightness(on ? level_set : 0);
	if (said++ < 16) {
		printf("BEGONIA BACKLIGHT: display %s (level %u)%s\n", on ? "on" : "off", level_set, ok ? "" : " NOT sent");
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
    0, 0, sysctl_begonia_backlight, "I", "the panel's brightness (DCS 0x51), 0..255");

void
begonia_backlight_init(void)
{
	dsi = (volatile uint32_t *) ml_io_map(DSI0, 0x1000);
	if (dsi == NULL) {
		begonia_perf_note("BEGONIA BACKLIGHT: DSI0 not mapped");
		return;
	}
	begonia_perf_note("BEGONIA BACKLIGHT: DSI0 as LK left it: START %#x INTSTA %#x MODE %#x VM_CMD_CON %#x", rd(DSI_START),
	    rd(DSI_INTSTA), rd(DSI_MODE_CTRL), rd(DSI_VM_CMD_CON));
}
