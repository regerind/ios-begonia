/*
 * begonia: sound, first step -- a test tone on the earpiece, with no DMA: the SoC's audio front end (AFE, 0x11210000)
 * generates a sine (AFE_SINEGEN, into its outputs O3/O4 = the DAC link, "ADDA" DL) that goes over the AUD_*_MOSI pins
 * to the MT6359's codec, whose receiver (handset, "HS") drives the earpiece. Everything from MediaTek's own drivers for
 * this phone (Xiaomi begonia-q-oss): mt6785-afe-clk.c, mt6785-dai-adda.c, mt6785-misc-control.c, mt6359.c (its DAPM
 * widgets and events for "DL Power Supply" .. "RCV Mux"), clk-mt6785.c, clk-mt6785-pg.c (spm_mtcmos_ctrl_audio).
 *
 * The user's approval of 2026-11-19 22:05 covers what this writes: the audio power domain (SPM 0x1000631c), the audio
 * clock muxes (audio_sel to 26 MHz, aud_intbus_sel to mainpll_d2_d4; audio_h and the APLLs are left as LK left them),
 * the MT6359's VAUD18 LDO enable bit. Nothing is written without the boot-arg begonia_audio=1.
 *
 *   kern.begonia_audio = "status"   every stage's registers, read only
 *   kern.begonia_audio = "tone"     the earpiece plays a quiet 48 kHz-domain sine for 3 s (sine 1/8, HS gain -10 dB)
 */
#include <libkern/libkern.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <kern/locks.h>

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void begonia_pwrap_take(void);
void begonia_pwrap_give(void);

/* ---- the PMIC (MT6359) through the PMIC wrapper, under the shared lock ---- */
#define PWRAP_BASE      0x1000d000u
static volatile uint32_t *pwrap;

static bool
pw_idle(void)
{
	for (int i = 0; i < 1000; i++) {
		uint32_t st = pwrap[0xc24 / 4];
		if ((st & (1u << 22)) && ((st >> 16) & 7) == 0) {
			return true;
		}
		delay(10);
	}
	return false;
}

static int
pm_read(uint32_t addr, uint32_t *v)
{
	int e = -1;
	begonia_pwrap_take();
	if (pw_idle()) {
		pwrap[0xc20 / 4] = (addr >> 1) << 16;
		for (int i = 0; i < 1000; i++) {
			uint32_t st = pwrap[0xc24 / 4];
			if (((st >> 16) & 7) == 6) {
				*v = st & 0xffff;
				pwrap[0xc28 / 4] = 1;
				e = 0;
				break;
			}
			delay(10);
		}
	}
	begonia_pwrap_give();
	return e;
}

static int
pm_write(uint32_t addr, uint32_t v)
{
	int e = -1;
	begonia_pwrap_take();
	if (pw_idle()) {
		pwrap[0xc20 / 4] = (1u << 31) | ((addr >> 1) << 16) | (v & 0xffff);
		e = pw_idle() ? 0 : -1;
	}
	begonia_pwrap_give();
	return e;
}

static int
pm_update(uint32_t addr, uint32_t mask, uint32_t v)
{
	uint32_t cur = 0;
	if (pm_read(addr, &cur) != 0) {
		return -1;
	}
	return pm_write(addr, (cur & ~mask) | (v & mask));
}

static uint32_t
pm(uint32_t addr)
{
	uint32_t v = 0xffff;
	(void)pm_read(addr, &v);
	return v;
}

/* MT6359 audio registers (registers.h / mt6359.h of the begonia kernel) */
#define DCXO_CW12               0x07a8          /* bit 13 RG_XO_AUDIO_EN_M */
#define LDO_VAUD18_CON0         0x1c98          /* bit 0 enable */
#define LDO_VAUD18_MON          0x1c9a
#define GPIO_MODE2              0x00d8
#define GPIO_MODE2_SET          0x00da
#define GPIO_MODE2_CLR          0x00dc
#define GPIO_MODE3              0x00de
#define GPIO_MODE3_SET          0x00e0
#define GPIO_MODE3_CLR          0x00e2
#define AUD_TOP_ID              0x2300
#define AUD_TOP_CKPDN_CON0      0x230c          /* 1 = off: 6 AUDNCP, 5 ZCD13M, 2 AUDIF, 1 AUD_CK */
#define AFE_UL_DL_CON0          0x2388          /* bit 0 AFE_ON */
#define AFE_DL_SRC2_CON0_L      0x238a          /* bit 0 DL_2_SRC_ON */
#define AUDIO_TOP_CON0_PM       0x2396          /* 1 = off: 7 AFE_CTL, 6 DAC_CTL, 2 PWR_CLK_DIS, 0 RESERVED */
#define AFUNC_AUD_CON0          0x239a
#define AFUNC_AUD_CON2          0x239e          /* bit 1 CCI_AFIFO_CLK_PWDB */
#define AFE_NCP_CFG0            0x24de          /* bit 0 RG_NCP_ON */
#define AUDENC_ANA_CON23        0x2536          /* bit 0 CLKSQ_EN, bit 1 CLKSQ_IN_SEL_TEST */
#define AUDDEC_ANA_CON0         0x2588
#define AUDDEC_ANA_CON2         0x258c          /* bit 8 AUDREFN_DERES_EN */
#define AUDDEC_ANA_CON6         0x2594
#define AUDDEC_ANA_CON9         0x259a
#define AUDDEC_ANA_CON10        0x259c
#define AUDDEC_ANA_CON11        0x259e          /* bits 10..12 DRBIAS_HS */
#define AUDDEC_ANA_CON12        0x25a0          /* bit 8 AUDIBIASPWRDN (1 = off); 6..7 IBIAS_ZCD, 2..3 IBIAS_HS */
#define AUDDEC_ANA_CON13        0x25a2          /* bit 4 AUDGLB_PWRDN (1 = off), bit 0 RSTB_DECODER */
#define AUDDEC_ANA_CON14        0x25a4          /* 0 LCLDO_DEC_EN, 2 LCLDO_REMOTE_SENSE, 4 NVREG_EN */
#define ZCD_CON3                0x260e          /* HS gain index: 18 = -10 dB, 0x1f = -40 dB */

/* ---- the SoC side ---- */
#define TOPCK                   0x10000000u
#define CLK_CFG_UPDATE          0x004
#define CLK_CFG_6               0x080           /* audio_sel 8..9 (gate 15), aud_intbus_sel 16..17 (gate 23) */
#define CLK_CFG_6_SET           0x084
#define CLK_CFG_6_CLR           0x088
#define CLK_CFG_13              0x0f0           /* audio_h_sel 8..9 (read only here) */
#define INFRA                   0x10001000u
#define INFRA1_CG_CLR           0x08c
#define INFRA1_CG_STA           0x094
#define INFRA_AUDIO_CG          (1u << 25)
#define INFRA_PROT_SET          0x2a0
#define INFRA_PROT_CLR          0x2a4
#define INFRA_PROT_STA1         0x228
#define AUDIO_PROT              (1u << 31)
#define SPM                     0x10006000u
#define SPM_POWERON_CONFIG_EN   0x000
#define AUDIO_PWR_CON           0x31c
#define PWR_STATUS              0x160
#define PWR_STATUS_2ND          0x164
#define AUDIO_PWR_STA           (1u << 24)
#define PWR_RST_B               (1u << 0)
#define PWR_ISO                 (1u << 1)
#define PWR_ON                  (1u << 2)
#define PWR_ON_2ND              (1u << 3)
#define PWR_CLK_DIS             (1u << 4)
#define SRAM_PDN                (1u << 8)
#define SRAM_PDN_ACK            (1u << 12)
#define GPIO_BASE               0x10005000u
#define AFE                     0x11210000u
#define AUDIO_TOP_CON0          0x0000          /* 1 = off: 2 AFE, 25 DAC, 26 DAC_PREDIS, 27 TML */
#define AFE_DAC_CON0            0x0010          /* bit 0 AFE_ON */
#define AFE_ADDA_DL_SRC2_CON0   0x0108
#define AFE_ADDA_DL_SRC2_CON1   0x010c
#define AFE_ADDA_UL_DL_CON0     0x0124          /* bit 0 ADDA_AFE_ON, bit 29 FIFO_AUTO_RST (1 = held) */
#define AFE_SINEGEN_CON2        0x01dc          /* 0..5 inner loop-back mode: 34 = O3O4 */
#define AFE_SINEGEN_CON0        0x01f0
#define AFE_ADDA_PREDIS_CON0    0x0260
#define AFE_ADDA_PREDIS_CON1    0x0264
#define AFE_ADDA_DL_SDM_DCCOMP_CON 0x0c50
#define AFE_ADDA_DL_SDM_AUTO_RESET_CON 0x0c74

static volatile uint8_t *topck, *infra, *spm, *gpio, *afe;

static uint32_t r32(volatile uint8_t *b, uint32_t o) {return *(volatile uint32_t *)(b + o);}
static void w32(volatile uint8_t *b, uint32_t o, uint32_t v) {__builtin_arm_dsb(0xf); *(volatile uint32_t *)(b + o) = v;}
static void m32(volatile uint8_t *b, uint32_t o, uint32_t mask, uint32_t v) {w32(b, o, (r32(b, o) & ~mask) | (v & mask));}

static bool
mapped(void)
{
	if (pwrap == NULL) {
		pwrap = (volatile uint32_t *)ml_io_map(PWRAP_BASE, 0x1000);
		topck = (volatile uint8_t *)ml_io_map(TOPCK, 0x1000);
		infra = (volatile uint8_t *)ml_io_map(INFRA, 0x1000);
		spm = (volatile uint8_t *)ml_io_map(SPM, 0x1000);
		gpio = (volatile uint8_t *)ml_io_map(GPIO_BASE, 0x1000);
	}
	return pwrap && topck && infra && spm && gpio;
}

static bool
audio_domain_on(void)
{
	return (r32(spm, PWR_STATUS) & AUDIO_PWR_STA) && (r32(spm, PWR_STATUS_2ND) & AUDIO_PWR_STA);
}

static bool
permitted(void)
{
	uint32_t v = 0;
	return PE_parse_boot_argn("begonia_audio", &v, sizeof(v)) && v == 1;
}

#define say(...) do { if (o < len) o += (size_t)snprintf(out + o, len - o, __VA_ARGS__); } while (0)

static size_t
status(char *out, size_t len, size_t o)
{
	uint32_t cfg6 = r32(topck, CLK_CFG_6), cfg13 = r32(topck, CLK_CFG_13);
	say("audio: domain PWR_CON %#x %s; audio_sel %u%s, aud_intbus_sel %u%s, audio_h_sel %u%s; infra audio %s; writes %s\n",
	    r32(spm, AUDIO_PWR_CON), audio_domain_on() ? "ON" : "off", (cfg6 >> 8) & 3, (cfg6 & (1u << 15)) ? " (gated)" : "",
	    (cfg6 >> 16) & 3, (cfg6 & (1u << 23)) ? " (gated)" : "", (cfg13 >> 8) & 3, (cfg13 & (1u << 15)) ? " (gated)" : "",
	    (r32(infra, INFRA1_CG_STA) & INFRA_AUDIO_CG) ? "gated" : "on", permitted() ? "PERMITTED (begonia_audio=1)" : "not permitted");
	if (audio_domain_on() && afe != NULL) {
		say("audio: AFE TOP_CON0 %#x DAC_CON0 %#x UL_DL_CON0 %#x DL_SRC2 %#x/%#x SINEGEN %#x/%#x\n", r32(afe, AUDIO_TOP_CON0),
		    r32(afe, AFE_DAC_CON0), r32(afe, AFE_ADDA_UL_DL_CON0), r32(afe, AFE_ADDA_DL_SRC2_CON0), r32(afe, AFE_ADDA_DL_SRC2_CON1),
		    r32(afe, AFE_SINEGEN_CON0), r32(afe, AFE_SINEGEN_CON2));
	}
	say("audio: codec id %#x; VAUD18 %#x (mon %#x); XO_AUDIO %u; CKPDN %#x TOP_CON0 %#x UL_DL %#x DL_SRC2 %#x; ANA13 %#x ANA14 %#x "
	    "ANA6 %#x ANA0 %#x ZCD3 %#x; pins 189..192 mode %#x\n", pm(AUD_TOP_ID), pm(LDO_VAUD18_CON0), pm(LDO_VAUD18_MON),
	    (pm(DCXO_CW12) >> 13) & 1, pm(AUD_TOP_CKPDN_CON0), pm(AUDIO_TOP_CON0_PM), pm(AFE_UL_DL_CON0), pm(AFE_DL_SRC2_CON0_L),
	    pm(AUDDEC_ANA_CON13), pm(AUDDEC_ANA_CON14), pm(AUDDEC_ANA_CON6), pm(AUDDEC_ANA_CON0), pm(ZCD_CON3),
	    (r32(gpio, 0x300 + 0x10 * 23) >> 20) & 0xfff);
	return o;
}

static bool
poll32(volatile uint8_t *b, uint32_t off, uint32_t mask, uint32_t want, unsigned us)
{
	for (unsigned t = 0; t < us; t += 10) {
		if ((r32(b, off) & mask) == want) {
			return true;
		}
		delay(10);
	}
	return (r32(b, off) & mask) == want;
}

static size_t
tone(char *out, size_t len, size_t o)
{
	if (!permitted()) {
		say("tone: not permitted (boot-arg begonia_audio=1)\n");
		return o;
	}
	/* 1. clocks: audio_sel = clk26m and on, aud_intbus_sel = mainpll_d2_d4 and on; infra's audio gate open */
	w32(topck, CLK_CFG_6_CLR, (3u << 8) | (1u << 15) | (3u << 16) | (1u << 23));
	w32(topck, CLK_CFG_6_SET, 1u << 16);
	w32(topck, CLK_CFG_UPDATE, (1u << 25) | (1u << 26));
	w32(infra, INFRA1_CG_CLR, INFRA_AUDIO_CG);
	delay(50);
	/* 2. the audio power domain (spm_mtcmos_ctrl_audio), bus protection released */
	if (!audio_domain_on()) {
		w32(spm, SPM_POWERON_CONFIG_EN, (0xb16u << 16) | 1u);
		m32(spm, AUDIO_PWR_CON, PWR_ON, PWR_ON);
		m32(spm, AUDIO_PWR_CON, PWR_ON_2ND, PWR_ON_2ND);
		if (!poll32(spm, PWR_STATUS, AUDIO_PWR_STA, AUDIO_PWR_STA, 5000) ||
		    !poll32(spm, PWR_STATUS_2ND, AUDIO_PWR_STA, AUDIO_PWR_STA, 5000)) {
			say("tone: the audio domain did not come up (PWR_CON %#x)\n", r32(spm, AUDIO_PWR_CON));
			return o;
		}
		m32(spm, AUDIO_PWR_CON, PWR_CLK_DIS, 0);
		m32(spm, AUDIO_PWR_CON, PWR_ISO, 0);
		m32(spm, AUDIO_PWR_CON, PWR_RST_B, PWR_RST_B);
		m32(spm, AUDIO_PWR_CON, SRAM_PDN, 0);
		if (!poll32(spm, AUDIO_PWR_CON, SRAM_PDN_ACK, 0, 5000)) {
			say("tone: the audio SRAM did not power up (PWR_CON %#x)\n", r32(spm, AUDIO_PWR_CON));
			return o;
		}
		w32(infra, INFRA_PROT_CLR, AUDIO_PROT);
		if (!poll32(infra, INFRA_PROT_STA1, AUDIO_PROT, 0, 5000)) {
			say("tone: the audio bus protection did not release\n");
			return o;
		}
	}
	if (afe == NULL) {
		afe = (volatile uint8_t *)ml_io_map(AFE, 0x1000);
		if (afe == NULL) {
			say("tone: AFE not mapped\n");
			return o;
		}
	}
	say("tone: audio domain on (PWR_CON %#x)\n", r32(spm, AUDIO_PWR_CON));
	/* 3. the AFE's own gates (AFE, DAC, DAC_PREDIS, TML), the pins to the codec (GPIO189..192 mode 1) */
	m32(afe, AUDIO_TOP_CON0, (1u << 2) | (1u << 25) | (1u << 26) | (1u << 27), 0);
	m32(gpio, 0x300 + 0x10 * 23, 0xfu << 20 | 0xfu << 24 | 0xfu << 28, 1u << 20 | 1u << 24 | 1u << 28);    /* 189, 190, 191 */
	m32(gpio, 0x300 + 0x10 * 24, 0xfu, 1u);                                                              /* 192 */
	/* 4. the DAC link (mtk_dai_adda_hw_params at 48 kHz, then its DAPM supplies) */
	w32(afe, AFE_ADDA_PREDIS_CON0, 0);
	w32(afe, AFE_ADDA_PREDIS_CON1, 0);
	w32(afe, AFE_ADDA_DL_SRC2_CON0, (8u << 28) | (3u << 24) | (1u << 12) | (1u << 11) | (1u << 1));
	w32(afe, AFE_ADDA_DL_SRC2_CON1, 0xf74fu << 16);
	m32(afe, AFE_ADDA_DL_SDM_DCCOMP_CON, 0x3fu | (1u << 28), 0x1d);
	w32(afe, AFE_ADDA_DL_SDM_AUTO_RESET_CON, 0x400u | (1u << 31));
	m32(afe, AFE_ADDA_UL_DL_CON0, (1u << 29) | 1u, 1u);
	m32(afe, AFE_ADDA_DL_SRC2_CON0, 1u, 1u);
	/* 5. the sine into O3/O4 (the DAC link): 48 kHz both channels, amplitude 1/8 */
	m32(afe, AFE_SINEGEN_CON2, 0x3f, 34);
	w32(afe, AFE_SINEGEN_CON0, 0x04a82a81);
	m32(afe, AFE_DAC_CON0, 1u, 1u);
	say("tone: AFE on: TOP_CON0 %#x DAC_CON0 %#x UL_DL %#x DL_SRC2 %#x SINEGEN %#x/%#x\n", r32(afe, AUDIO_TOP_CON0),
	    r32(afe, AFE_DAC_CON0), r32(afe, AFE_ADDA_UL_DL_CON0), r32(afe, AFE_ADDA_DL_SRC2_CON0), r32(afe, AFE_SINEGEN_CON0),
	    r32(afe, AFE_SINEGEN_CON2));
	/* 6. the codec: DL Power Supply, DL Digital Clock, DL_GPIO (mt6359.c's widgets in their order) */
	int e = 0;
	e |= pm_update(DCXO_CW12, 1u << 13, 1u << 13);                         /* CLK_BUF */
	e |= pm_update(LDO_VAUD18_CON0, 1u, 1u);                                /* LDO_VAUD18 */
	delay(100);
	e |= pm_update(AUDDEC_ANA_CON13, 1u << 4, 0);                           /* AUDGLB */
	e |= pm_update(AUDENC_ANA_CON23, 1u << 1, 0);                           /* CLKSQ: from the internal DCXO */
	e |= pm_update(AUDENC_ANA_CON23, 1u, 1u);
	e |= pm_update(AUD_TOP_CKPDN_CON0, (1u << 6) | (1u << 5) | (1u << 2) | (1u << 1), 0);    /* NCP, ZCD13M, AUDIF, AUD_CK */
	delay(250);
	e |= pm_update(AUDIO_TOP_CON0_PM, (1u << 7) | (1u << 6) | (1u << 2) | 1u, 0);              /* AFE_CTL DAC_CTL PWR_CLK RES */
	delay(250);
	e |= pm_update(AUDDEC_ANA_CON2, 1u << 8, 1u << 8);                      /* ESD_RESIST */
	delay(250);
	e |= pm_update(AUDDEC_ANA_CON14, 1u, 1u);                               /* LDO */
	e |= pm_update(AUDDEC_ANA_CON14, 1u << 2, 1u << 2);                     /* LDO_REMOTE */
	e |= pm_update(AUDDEC_ANA_CON14, 1u << 4, 1u << 4);                     /* NV_REGULATOR */
	delay(100);
	e |= pm_update(AUDDEC_ANA_CON12, 1u << 8, 0);                           /* IBIST */
	e |= pm_update(AFUNC_AUD_CON2, 1u << 1, 1u << 1);                       /* SDM_FIFO_CLK */
	e |= pm_update(AFUNC_AUD_CON2, 0xfffd, 0x0006);                         /* SDM */
	e |= pm_write(AFUNC_AUD_CON0, 0xcba1);
	e |= pm_update(AFUNC_AUD_CON2, 0xfffd, 0x0003);
	e |= pm_update(AFUNC_AUD_CON2, 0xfffd, 0x000b);
	e |= pm_write(AFE_NCP_CFG0, 0xc800);                                    /* NCP */
	e |= pm_update(AFE_NCP_CFG0, 1u, 1u);
	e |= pm_update(AFE_UL_DL_CON0, 1u, 1u);                                 /* AFE_ON */
	e |= pm_update(AFE_DL_SRC2_CON0_L, 1u, 1u);                             /* AFE_DL_SRC */
	e |= pm_write(GPIO_MODE2_CLR, 0x0ffe);                                  /* DL_GPIO: the MOSI pins of the codec */
	e |= pm_write(GPIO_MODE2_SET, 0x0249);
	e |= pm_write(GPIO_MODE2, 0x0249);
	e |= pm_write(GPIO_MODE3_CLR, 0x6);
	e |= pm_write(GPIO_MODE3_SET, 0x1);
	e |= pm_update(GPIO_MODE3, 0x7, 0x1);
	/* 7. the receiver (mt_rcv_event PRE_PMU), at -10 dB */
	e |= pm_write(AUDDEC_ANA_CON6, 0x0010);
	e |= pm_update(AUDDEC_ANA_CON11, 0x7u << 10, 2u << 10);
	e |= pm_update(AUDDEC_ANA_CON12, 0x3u << 6, 1u << 6);
	e |= pm_update(AUDDEC_ANA_CON12, 0x3u << 2, 1u << 2);
	e |= pm_write(AUDDEC_ANA_CON6, 0x0090);
	e |= pm_write(AUDDEC_ANA_CON10, 0x7000);
	e |= pm_write(AUDDEC_ANA_CON6, 0x0092);
	e |= pm_write(AUDDEC_ANA_CON6, 0x0093);
	e |= pm_write(ZCD_CON3, 18);
	e |= pm_update(AUDDEC_ANA_CON13, 1u, 1u);                               /* decoder clock */
	e |= pm_write(AUDDEC_ANA_CON0, 0x0009);                                 /* the DAC */
	e |= pm_write(AUDDEC_ANA_CON9, 0x0001);
	e |= pm_write(AUDDEC_ANA_CON6, 0x009b);                                 /* HS mux to the DAC */
	say("tone: codec %s; the earpiece plays for 3 s\n", e ? "A PMIC WRITE FAILED" : "on");
	o = status(out, len, o);
	delay(3000000);
	/* the receiver off again (mt_rcv_event PRE_PMD): gain to -40 dB, HS driver off; the sine stopped */
	(void)pm_write(ZCD_CON3, 0x1f);
	(void)pm_write(AUDDEC_ANA_CON6, 0x0010);
	w32(afe, AFE_SINEGEN_CON0, 0);
	say("tone: done (receiver off, sine off)\n");
	return o;
}

static char answer[4096];

static int
sysctl_begonia_audio SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char cmd[32];
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
		if (!mapped()) {
			say("audio: registers not mapped\n");
		} else if (strncmp(cmd, "tone", 4) == 0) {
			o = tone(out, len, o);
		} else {
			o = status(out, len, o);
		}
		(void)o;
		return 0;
	}
	return SYSCTL_OUT(req, answer, strlen(answer) + 1);
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_audio, CTLTYPE_STRING | CTLFLAG_RW | CTLFLAG_LOCKED,
    0, 0, sysctl_begonia_audio, "A", "sound: status, tone");
