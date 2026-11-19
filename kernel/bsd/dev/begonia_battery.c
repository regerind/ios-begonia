/*
 * begonia: the battery's level, from the MT6359 PMIC. There is no fuel gauge driver here; the status bar showed a full
 * battery always (userland's fallback, inject_sb.c) because nothing published kern.hakios.battery_*. This publishes them:
 *
 *   battery_mv              the battery's voltage: the PMIC's AUXADC, channel BATADC (request AUXADC_RQST0 0x1108 bit 0,
 *                           result AUXADC_ADC0 0x1088, ready bit 15, 15 bits; mV = raw * 7/2 * 1800 >> 15: the divider of
 *                           MediaTek's mt635x-auxadc for this channel), four samples averaged, at most every 20 s
 *   battery_soc             0..100 from that voltage by a Li-ion open-circuit curve: an estimate, not a gauge -- it reads
 *                           high while charging and low under load
 *   battery_present         1 while the voltage is plausible
 *   battery_charger_present CHR_CON0 (0xa88) bit 0, RGS_CHRDET
 *   battery_charging        a charger is there and the level is under 100
 *
 * The only PMIC register written is AUXADC_RQST0 (a conversion's request); nothing else is (no rail, no clock).
 */
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <mach/mach_time.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void begonia_pwrap_take(void);
void begonia_pwrap_give(void);
void begonia_perf_note(const char *format, ...) __printflike(1, 2);

#define PWRAP           0x1000d000
#define AUXADC_RQST0    0x1108
#define AUXADC_ADC0     0x1088
#define CHR_CON0        0x0a88

static vm_offset_t pwrap;
static int mv, soc = -1, charger;
static uint64_t sampled;                /* mach_absolute_time of the last reading */

static uint32_t rd(unsigned o) {return *(volatile uint32_t *)(pwrap + o);}
static void wr(unsigned o, uint32_t v) {*(volatile uint32_t *)(pwrap + o) = v; __asm__ volatile("dsb sy" ::: "memory");}

static int
idle(void)
{
	for (unsigned i = 0; i < 2000; i++) {
		uint32_t state = rd(0xc24);
		if ((state & (1u << 22)) && ((state >> 16) & 7) == 0) {
			return 0;
		}
		delay(10);
	}
	return -1;
}

static int
pmic_read(unsigned address, uint32_t *value)
{
	int result = -1;

	begonia_pwrap_take();
	if (!idle()) {
		wr(0xc20, (address >> 1) << 16);
		for (unsigned i = 0; i < 1000; i++) {
			uint32_t state = rd(0xc24);
			if (((state >> 16) & 7) == 6) {
				*value = state & 0xffff;
				wr(0xc28, 1);
				(void) idle();
				result = 0;
				break;
			}
			delay(10);
		}
	}
	begonia_pwrap_give();
	return result;
}

static int
pmic_request_conversion(void)
{
	int result = -1;

	begonia_pwrap_take();
	if (!idle()) {
		wr(0xc20, (1u << 31) | ((AUXADC_RQST0 >> 1) << 16) | 1u);       /* BATADC: bit 0; this register alone is written */
		delay(2);
		result = idle();
	}
	begonia_pwrap_give();
	return result;
}

static int
battery_millivolts(void)
{
	uint32_t raw = 0;

	if (pmic_request_conversion() != 0) {
		return -1;
	}
	for (unsigned i = 0; i < 100; i++) {
		delay(100);
		if (pmic_read(AUXADC_ADC0, &raw) != 0) {
			return -1;
		}
		if (raw & (1u << 15)) {
			return (int) (((uint64_t) (raw & 0x7fff) * 7 * 1800 / 2) >> 15);
		}
	}
	return -1;
}

/* resting voltage to percent, a typical 4.4 V Li-ion cell */
static int
level_of(int millivolts)
{
	static const int curve[][2] = {{3450, 0}, {3650, 5}, {3720, 10}, {3760, 20}, {3800, 30}, {3850, 40}, {3900, 50},
		                       {3970, 60}, {4050, 70}, {4150, 80}, {4250, 90}, {4370, 100}};
	const unsigned n = sizeof(curve) / sizeof(curve[0]);

	if (millivolts <= curve[0][0]) {
		return 0;
	}
	for (unsigned i = 1; i < n; i++) {
		if (millivolts <= curve[i][0]) {
			return curve[i - 1][1] + (millivolts - curve[i - 1][0]) * (curve[i][1] - curve[i - 1][1]) / (curve[i][0] - curve[i - 1][0]);
		}
	}
	return 100;
}

static void
sample(void)
{
	uint64_t now = mach_absolute_time(), interval;
	uint32_t chr = 0;
	int sum = 0, got = 0;

	nanoseconds_to_absolutetime(20ull * NSEC_PER_SEC, &interval);
	if (pwrap == 0 || (sampled != 0 && now - sampled < interval)) {
		return;
	}
	sampled = now;
	for (int i = 0; i < 4; i++) {
		int v = battery_millivolts();
		if (v > 0) {
			sum += v;
			got++;
		}
	}
	if (got) {
		int level = level_of(sum / got);

		mv = sum / got;
		soc = soc < 0 ? level : (soc * 3 + level + 2) / 4;      /* smoothed: one reading moves it a quarter */
	}
	if (pmic_read(CHR_CON0, &chr) == 0) {
		charger = chr & 1;
	}
}

static int
sysctl_battery SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg2)
	int value;

	sample();
	switch ((int) (uintptr_t) arg1) {
	case 0: value = soc < 0 ? 100 : soc; break;
	case 1: value = mv; break;
	case 2: value = mv > 3000 && mv < 4700; break;
	case 3: value = charger; break;
	default: value = charger && soc >= 0 && soc < 100; break;
	}
	return sysctl_io_number(req, value, sizeof(int), NULL, NULL);
}
SYSCTL_DECL(_kern_hakios);
SYSCTL_PROC(_kern_hakios, OID_AUTO, battery_soc, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    (void *) 0, 0, sysctl_battery, "I", "battery level, percent (from its voltage)");
SYSCTL_PROC(_kern_hakios, OID_AUTO, battery_mv, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    (void *) 1, 0, sysctl_battery, "I", "battery voltage, mV");
SYSCTL_PROC(_kern_hakios, OID_AUTO, battery_present, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    (void *) 2, 0, sysctl_battery, "I", "a battery is there");
SYSCTL_PROC(_kern_hakios, OID_AUTO, battery_charger_present, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    (void *) 3, 0, sysctl_battery, "I", "a charger is plugged in");
SYSCTL_PROC(_kern_hakios, OID_AUTO, battery_charging, CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    (void *) 4, 0, sysctl_battery, "I", "charging");

void
begonia_battery_init(void)
{
	pwrap = ml_io_map(PWRAP, 0x1000);
	sample();
	begonia_perf_note("BEGONIA BATTERY: %d mV, about %d%%, charger %s", mv, soc, charger ? "plugged in" : "not plugged in");
}
