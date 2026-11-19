/*
 * begonia: the SoC's I2C controllers (MT6785: ten, I2C0..9, register layout v2 -- i2c_common "ver = 2" in the device
 * tree), polled, FIFO only (up to 8 bytes out and 8 in a transfer). From MediaTek's i2c-mtk.c / i2c-mtk.h for this
 * phone (Xiaomi's begonia-q-oss): mt_i2c_do_transfer's register sequence, without DMA and without interrupts.
 *
 * All ten share one clock gate, INFRACFG_AO_I2C0_CG (infracfg 0x84 clear / 0x90 status, bit 11), ungated first: a
 * gated controller's registers are not to be touched. A controller LK set up (it uses I2C for the charger PMIC and the
 * display's bias) keeps LK's timing; one LK left alone (TIMING 0) gets a conservative one (clock-div 5, ~100 kHz at the
 * expected source clock, slower if the clock is slower). A bus whose transfer times out is not used again this boot.
 *
 *   begonia_i2c_xfer(bus, addr, out, n, in, m)    write n bytes, then (repeated start) read m; either may be 0
 *   kern.begonia_i2c = "scan" | "r BUS ADDR REG N" | "w BUS ADDR REG VALUE" | "torch" | "tfa"; reading it: the answer
 */
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/locks.h>
#include <kern/clock.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

#define BUSES                   10
static const uint32_t bases[BUSES] = { 0x11007000, 0x11008000, 0x11009000, 0x1100f000, 0x11011000, 0x11016000,
	                               0x11005000, 0x1101a000, 0x1101b000, 0x11015000 };
#define INFRA_CG0_CLR           0x10001084
#define INFRA_CG0_STA           0x10001090
#define I2C_CG                  (1u << 11)

/* v2 offsets (16-bit registers) */
#define DATA_PORT       0x00
#define SLAVE_ADDR      0x04
#define INTR_MASK       0x08
#define INTR_STAT       0x0c
#define CONTROL         0x10
#define TRANSFER_LEN    0x14
#define TRANSAC_LEN     0x18
#define DELAY_LEN       0x1c
#define TIMING          0x20
#define START           0x24
#define EXT_CONF        0x28
#define FIFO_ADDR_CLR   0x38
#define IO_CONFIG       0x34
#define MCU_INTR        0x40
#define TRANSFER_LEN_AUX 0x44
#define CLOCK_DIV       0x48
#define SOFTRESET       0x50

#define ST_COMP         (1u << 0)
#define ST_ACKERR       (1u << 1)
#define ST_HS_NACKERR   (1u << 2)
#define ST_ARB_LOST     (1u << 3)
#define ST_TIMEOUT      (1u << 5)
#define ST_BUS_ERR      (1u << 8)
#define ST_ALL          0x1ff
#define CTL_RS          (1u << 1)
#define CTL_CLK_EXT_EN  (1u << 3)
#define CTL_DIR_CHANGE  (1u << 4)
#define CTL_ACKERR_DET  (1u << 5)

static volatile uint8_t *regs[BUSES];
static bool dead[BUSES], configured[BUSES];
static LCK_GRP_DECLARE(i2c_grp, "begonia_i2c");
static LCK_MTX_DECLARE(i2c_mtx, &i2c_grp);

static uint16_t rd(int bus, uint32_t o) {return *(volatile uint16_t *)(regs[bus] + o);}
static void wr(int bus, uint32_t o, uint16_t v) {__builtin_arm_dsb(0xf); *(volatile uint16_t *)(regs[bus] + o) = v;}

static bool
bus_ready(int bus)
{
	if (bus < 0 || bus >= BUSES || dead[bus]) {
		return false;
	}
	if (regs[bus] == NULL) {
		volatile uint32_t *cg = (volatile uint32_t *)ml_io_map(INFRA_CG0_CLR & ~0xfffu, 0x1000);
		if (cg == NULL) {
			return false;
		}
		if (cg[(INFRA_CG0_STA & 0xfff) / 4] & I2C_CG) {
			cg[(INFRA_CG0_CLR & 0xfff) / 4] = I2C_CG;        /* ungate (gate set = clock off) */
			delay(10);
		}
		if (cg[(INFRA_CG0_STA & 0xfff) / 4] & I2C_CG) {
			printf("BEGONIA I2C: the I2C clock stays gated\n");
			return false;
		}
		regs[bus] = (volatile uint8_t *)ml_io_map(bases[bus], 0x1000);
		if (regs[bus] == NULL) {
			return false;
		}
	}
	if (!configured[bus]) {
		configured[bus] = true;
		if (rd(bus, TIMING) == 0) {
			wr(bus, CLOCK_DIV, (4u << 8) | 4u);             /* clock-div 5 */
			wr(bus, TIMING, (3u << 8) | 33u);               /* sample 4 x step 34: ~100 kHz at 27.3 MHz */
			wr(bus, EXT_CONF, 0x8001);                      /* standard-mode start condition */
			wr(bus, IO_CONFIG, 0x0003);                     /* open drain */
			printf("BEGONIA I2C: bus %d had no timing (LK did not use it): set up for ~100 kHz\n", bus);
		}
	}
	return true;
}

/* 0, or -1 (no such bus), -2 (no acknowledgement: nobody there), -3 (timed out: the bus is given up), -4 (other) */
int
begonia_i2c_xfer(int bus, uint8_t addr, const uint8_t *out, unsigned n, uint8_t *in, unsigned m)
{
	if (n > 8 || m > 8 || (n == 0 && m == 0)) {
		return -4;
	}
	lck_mtx_lock(&i2c_mtx);
	if (!bus_ready(bus)) {
		lck_mtx_unlock(&i2c_mtx);
		return -1;
	}
	bool wrrd = n && m;
	wr(bus, CONTROL, CTL_ACKERR_DET | CTL_CLK_EXT_EN | (wrrd ? (CTL_DIR_CHANGE | CTL_RS) : 0));
	wr(bus, DELAY_LEN, 0x000a);
	wr(bus, SLAVE_ADDR, (uint16_t)((addr << 1) | (n == 0 ? 1 : 0)));
	wr(bus, INTR_STAT, ST_ALL);
	wr(bus, FIFO_ADDR_CLR, 1);
	wr(bus, INTR_MASK, ST_COMP | ST_ACKERR | ST_HS_NACKERR | ST_ARB_LOST | ST_TIMEOUT | ST_BUS_ERR);
	if (wrrd) {
		wr(bus, TRANSFER_LEN, (uint16_t)n);
		wr(bus, TRANSFER_LEN_AUX, (uint16_t)m);
		wr(bus, TRANSAC_LEN, 2);
	} else {
		wr(bus, TRANSFER_LEN, (uint16_t)(n ? n : m));
		wr(bus, TRANSAC_LEN, 1);
	}
	for (unsigned i = 0; i < n; i++) {
		wr(bus, DATA_PORT, out[i]);
	}
	wr(bus, MCU_INTR, 1);
	wr(bus, START, 1);
	uint16_t st = 0;
	for (unsigned t = 0; t < 2000 && !((st = rd(bus, INTR_STAT)) & (ST_COMP | ST_ACKERR | ST_HS_NACKERR | ST_ARB_LOST |
	    ST_TIMEOUT | ST_BUS_ERR)); t++) {
		delay(10);
	}
	int ret;
	if (st & (ST_ACKERR | ST_HS_NACKERR)) {
		ret = -2;
	} else if (st & ST_COMP) {
		for (unsigned i = 0; i < m; i++) {
			in[i] = (uint8_t)rd(bus, DATA_PORT);
		}
		ret = 0;
	} else if (st == 0 || (st & (ST_TIMEOUT | ST_BUS_ERR))) {
		ret = -3;
		dead[bus] = true;
		printf("BEGONIA I2C: bus %d timed out (status %#x, START %#x): not used again this boot\n", bus, st, rd(bus, START));
		wr(bus, SOFTRESET, 1);
	} else {
		ret = -4;
	}
	wr(bus, INTR_STAT, ST_ALL);
	wr(bus, FIFO_ADDR_CLR, 1);
	lck_mtx_unlock(&i2c_mtx);
	return ret;
}

static int
reg_read(int bus, uint8_t addr, uint8_t reg, uint8_t *v)
{
	return begonia_i2c_xfer(bus, addr, &reg, 1, v, 1);
}

static int
reg_write(int bus, uint8_t addr, uint8_t reg, uint8_t v)
{
	uint8_t b[2] = { reg, v };
	return begonia_i2c_xfer(bus, addr, b, 2, NULL, 0);
}

#define say(...) do { if (o < len) o += (size_t)snprintf(out + o, len - o, __VA_ARGS__); } while (0)

/* every bus, every 7-bit address: who answers a 1-byte read */
static size_t
scan(char *out, size_t len, size_t o)
{
	for (int bus = 0; bus < BUSES; bus++) {
		unsigned found = 0;
		say("bus %d:", bus);
		for (uint8_t a = 0x08; a < 0x78; a++) {
			uint8_t v;
			int r = begonia_i2c_xfer(bus, a, NULL, 0, &v, 1);
			if (r == 0) {
				say(" %02x", a);
				found++;
			} else if (r == -1 || r == -3) {
				say(" %s", r == -1 ? "(not usable)" : "(timed out)");
				break;
			}
		}
		say("%s\n", found ? "" : " nobody");
	}
	return o;
}

/* the MT6360's flash LED 1 as a torch at its lowest current for one second, then everything as it was */
static size_t
torch(char *out, size_t len, size_t o)
{
	for (int bus = 0; bus < BUSES; bus++) {
		uint8_t id, tor, en;
		if (bus == 6 || reg_read(bus, 0x34, 0x00, &id) != 0) {         /* (I2C6 0x34 is the speaker amp) */
			continue;
		}
		if ((id & 0xf0) != 0x50) {
			say("torch: bus %d 0x34 answers, id %#x: not an MT6360\n", bus, id);
			continue;
		}
		if (reg_read(bus, 0x34, 0x75, &tor) != 0 || reg_read(bus, 0x34, 0x7e, &en) != 0) {
			say("torch: MT6360 on bus %d, its flash registers not read\n", bus);
			return o;
		}
		int e = reg_write(bus, 0x34, 0x75, (uint8_t)(tor & ~0x1fu));                        /* lowest torch current */
		e |= reg_write(bus, 0x34, 0x7e, (uint8_t)((en & ~0x04u) | 0x02u | 0x08u));         /* CS1 + torch, no strobe */
		uint8_t on = 0;
		(void)reg_read(bus, 0x34, 0x7e, &on);
		delay(1000000);
		e |= reg_write(bus, 0x34, 0x7e, en);
		e |= reg_write(bus, 0x34, 0x75, tor);
		say("torch: MT6360 (id %#x) on bus %d: FLED1_TOR %#x FLED_EN %#x -> %#x for 1 s, restored%s\n", id, bus, tor, en,
		    on, e ? " (A WRITE FAILED)" : "");
		return o;
	}
	say("torch: no MT6360 found at 0x34 on any bus\n");
	return o;
}

/* the speaker amp: TFA98xx on I2C6 0x34, its revision register 0x03 (16 bits) */
static size_t
tfa(char *out, size_t len, size_t o)
{
	uint8_t reg = 0x03, v[2] = { 0, 0 };
	int r = begonia_i2c_xfer(6, 0x34, &reg, 1, v, 2);
	say("tfa: I2C6 0x34 rev %s %02x%02x\n", r == 0 ? "read" : r == -2 ? "NO ACK (in reset? pins not muxed?)" : "FAILED", v[0], v[1]);
	return o;
}

static char answer[4096];

static int
sysctl_begonia_i2c SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char cmd[96];
	int error;

	if (req->newptr != USER_ADDR_NULL) {
		size_t l = req->newlen < sizeof(cmd) - 1 ? req->newlen : sizeof(cmd) - 1;
		if ((error = SYSCTL_IN(req, cmd, l)) != 0) {
			return error;
		}
		cmd[l] = 0;
		size_t o = 0, len = sizeof(answer);
		char *out = answer;
		answer[0] = 0;
		unsigned bus, addr, reg, val;
		if (strncmp(cmd, "scan", 4) == 0) {
			o = scan(out, len, o);
		} else if (strncmp(cmd, "torch", 5) == 0) {
			o = torch(out, len, o);
		} else if (strncmp(cmd, "tfa", 3) == 0) {
			o = tfa(out, len, o);
		} else if (sscanf(cmd, "r %u %x %x %u", &bus, &addr, &reg, &val) == 4 && val <= 8) {
			uint8_t r8 = (uint8_t)reg, v[8] = { 0 };
			int r = begonia_i2c_xfer((int)bus, (uint8_t)addr, &r8, 1, v, val);
			say("r %u %02x %02x: %d:", bus, addr, reg, r);
			for (unsigned i = 0; i < val; i++) {
				say(" %02x", v[i]);
			}
			say("\n");
		} else if (sscanf(cmd, "w %u %x %x %x", &bus, &addr, &reg, &val) == 4) {
			say("w %u %02x %02x %02x: %d\n", bus, addr, reg, val, reg_write((int)bus, (uint8_t)addr, (uint8_t)reg, (uint8_t)val));
		} else {
			say("commands: scan | torch | tfa | r BUS ADDR REG N | w BUS ADDR REG VALUE\n");
		}
		(void)o;
		return 0;
	}
	return SYSCTL_OUT(req, answer, strlen(answer) + 1);
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_i2c, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_LOCKED,
    0, 0, sysctl_begonia_i2c, "A", "the I2C buses: scan, torch, tfa, r, w");
