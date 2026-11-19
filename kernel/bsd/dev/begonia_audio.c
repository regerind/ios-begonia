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
#include <kern/clock.h>
#include <mach/mach_time.h>

void begonia_perf_note(const char *format, ...) __printflike(1, 2);

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

/*
 * The ring the AFE plays from (DL1), in the 4 MiB the Wi-Fi driver's window reserves at 0xab000000 (its EMI from 0,
 * its DMA at +3 MiB for 512 KiB): the last 512 KiB is nobody's. 64 KiB = 16384 frames of 16-bit stereo, 341 ms.
 */
#define RING_PA                 0xab380000u
#define RING_BYTES              0x10000u
#define RING_FRAMES             (RING_BYTES / 4)
#define AFE_CONN3               0x002c          /* O03 (the DAC link's left): bit 5 = DL1_CH1 */
#define AFE_CONN4               0x0030          /* O04 (its right): bit 6 = DL1_CH2 */
#define AFE_DL1_CON0            0x004c          /* 24..28 rate (10 = 48 kHz), 8 mono, 0..1 HD (0 = 16 bits) */
#define AFE_DL1_BASE_MSB        0x0050
#define AFE_DL1_BASE            0x0054
#define AFE_DL1_CUR             0x005c
#define AFE_DL1_END_MSB         0x0060
#define AFE_DL1_END             0x0064
#define DL1_ON                  (1u << 4)
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
static volatile int16_t *ring;
static uint64_t dl1_t0;                 /* mach_absolute_time() when DL1 was switched on (it starts at BASE) */

/*
 * Everything up to sound coming out of the earpiece: the clocks and the domain, the AFE, the DAC link, then either the
 * AFE's sine (a test) or DL1 playing the ring, then the codec and its receiver at gain index GAIN (8 = 0 dB, 18 =
 * -10 dB, 0x1f = -40 dB). *OK: whether every write went through.
 */
static size_t
hw_up(char *out, size_t len, size_t o, bool sine, unsigned gain, bool *ok)
{
	*ok = false;
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
	if (sine) {
		/* 5. the sine into O3/O4 (the DAC link): 48 kHz both channels, amplitude 1/8 */
		m32(afe, AFE_SINEGEN_CON2, 0x3f, 34);
		w32(afe, AFE_SINEGEN_CON0, 0x04a82a81);
		m32(afe, AFE_DAC_CON0, 1u, 1u);
	} else {
		/* 5. DL1 plays the ring into O3/O4 (mt6785-afe-pcm.c's memif DL1; mt6785-dai-adda.c's DL1_CH1/CH2 mix) */
		if (ring == NULL) {
			ring = (volatile int16_t *)ml_io_map_wcomb(RING_PA, RING_BYTES);
		}
		if (ring == NULL) {
			say("stream: the ring not mapped\n");
			return o;
		}
		for (unsigned i = 0; i < RING_BYTES / 2; i++) {
			ring[i] = 0;
		}
		__builtin_arm_dsb(0xf);
		w32(afe, AFE_SINEGEN_CON0, 0);
		m32(afe, AFE_SINEGEN_CON2, 0x3f, 0x3f);
		w32(afe, AFE_DL1_BASE_MSB, 0);
		w32(afe, AFE_DL1_BASE, RING_PA);
		w32(afe, AFE_DL1_END_MSB, 0);
		w32(afe, AFE_DL1_END, RING_PA + RING_BYTES - 1);
		m32(afe, AFE_DL1_CON0, (0x1fu << 24) | (1u << 8) | 3u, 10u << 24);
		m32(afe, AFE_CONN3, 1u << 5, 1u << 5);
		m32(afe, AFE_CONN4, 1u << 6, 1u << 6);
		m32(afe, AFE_DAC_CON0, DL1_ON | 1u, DL1_ON | 1u);
		dl1_t0 = mach_absolute_time();
	}
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
	e |= pm_write(ZCD_CON3, gain);
	e |= pm_update(AUDDEC_ANA_CON13, 1u, 1u);                               /* decoder clock */
	e |= pm_write(AUDDEC_ANA_CON0, 0x0009);                                 /* the DAC */
	e |= pm_write(AUDDEC_ANA_CON9, 0x0001);
	e |= pm_write(AUDDEC_ANA_CON6, 0x009b);                                 /* HS mux to the DAC */
	say("audio: codec %s (receiver gain index %u)\n", e ? "A PMIC WRITE FAILED" : "on", gain);
	*ok = e == 0;
	return o;
}

static size_t
tone(char *out, size_t len, size_t o)
{
	bool ok;
	if (!permitted()) {
		say("tone: not permitted (boot-arg begonia_audio=1)\n");
		return o;
	}
	o = hw_up(out, len, o, true, 18, &ok);
	say("tone: the earpiece plays for 3 s\n");
	o = status(out, len, o);
	delay(3000000);
	/* the receiver off again (mt_rcv_event PRE_PMD): gain to -40 dB, HS driver off; the sine stopped */
	(void)pm_write(ZCD_CON3, 0x1f);
	(void)pm_write(AUDDEC_ANA_CON6, 0x0010);
	w32(afe, AFE_SINEGEN_CON0, 0);
	say("tone: done (receiver off, sine off)\n");
	return o;
}

/*
 * ---- Darwin's sound to the earpiece: the sink of /dev/hakios-audio (upstream's drivers/audio/hakios.audio.c) ----
 * The audio server's plug-in writes what it has mixed, 48 kHz 16-bit stereo; here it goes into the ring just ahead of
 * where DL1 reads (AFE_DL1_CUR), at Darwin's volume, left and right mixed (the earpiece is one speaker; the bottom
 * speaker, and the dual-speaker stereo, come with the TFA98xx). A timer clears what DL1 has played, so that silence,
 * not the last sound again, plays when nothing is written. The hardware comes up on a thread call at the first write
 * (the writer is the audio server's real-time thread and must not wait); frames before that are dropped.
 */
/*
 * ---- The bottom speaker: the TFA9874 smart amp (I2C 6, 0x34), fed by the AFE's I2S3 out ----
 * Xiaomi's tfa98xx.cnt (646 bytes, C:\begonia-private\firmware\vendor-firmware) is a TFA9874 with no DSP program
 * ("9874_dummy.patch"): its device list (the hidden-register key, then 47 bit fields: TDM 2 slots of 32 bits, the
 * speaker from slot 0, the boost and protection settings) and its one profile "speaker" (48 kHz) are below as they are
 * in the file; a bit field is (register << 8 | first bit << 4 | bits - 1). The amp's own TFA2 start: I2C reset,
 * the settings, MANSCONF (0x0120, "I2C configured"), AMPE (0x0030), PWDN (0x0000) 0 -- with the I2S clocks running,
 * which its PLL locks to. The device tree: speaker on I2S3 out (mtk_spk_i2s_out = <3>), pins GPIO67 LRCK, 68 DO,
 * 71 BCK (mode 1); I2S3 is AFE_I2S_CON3 (its I2S4_* fields): 48 kHz (10), I2S format, 32-bit words (BCK 64 fs).
 * DL1's left channel goes to I2S3's (AFE_CONN0 bit 5), its right to I2S3's right (AFE_CONN1 bit 6).
 */
int begonia_i2c_xfer(int bus, uint8_t addr, const uint8_t *out, unsigned n, uint8_t *in, unsigned m);
int begonia_i2c_amp_ready(uint16_t *rev);

#define AFE_CONN0               0x0020          /* O00 (I2S3's left): bit 5 = DL1_CH1 */
#define AFE_CONN1               0x0024          /* O01 (I2S3's right): bit 6 = DL1_CH2 */
#define AFE_I2S_CON3            0x0040
#define AMP_BUS                 6
#define AMP_ADDR                0x34

static const struct { uint8_t reg; uint16_t value; } amp_keys[] = { { 0x0f, 0x5a6b }, { 0xa1, 0x005a }, { 0xa0, 0x005a } };
static const uint16_t amp_device[][2] = {       /* { bit field, value }: tfa98xx.cnt's device list, in its order */
	{ 0x20c3, 0x2 }, { 0x2144, 0x1f }, { 0x2224, 0x17 }, { 0x2103, 0x1 }, { 0x2330, 0x1 }, { 0x2340, 0x1 }, { 0x26c3, 0x1 },
	{ 0x2703, 0x0 }, { 0x6802, 0x2 }, { 0x2603, 0x0 }, { 0x2643, 0x1 }, { 0x2310, 0x0 }, { 0x6163, 0x4 }, { 0x6123, 0xf },
	{ 0x6831, 0x1 }, { 0x6851, 0x2 }, { 0x0255, 0x16 }, { 0x5257, 0xbe }, { 0x64e1, 0x2 }, { 0x5350, 0x1 }, { 0x5690, 0x0 },
	{ 0x6f02, 0x5 }, { 0x6f72, 0x1 }, { 0x7071, 0x3 }, { 0x7430, 0x1 }, { 0x7494, 0x8 }, { 0x75f0, 0x1 }, { 0x74f0, 0x0 },
	{ 0x8001, 0x0 }, { 0x8305, 0x19 }, { 0x8364, 0x1e }, { 0x8453, 0x4 }, { 0x5150, 0x0 }, { 0x5844, 0x1 }, { 0x6f50, 0x1 },
	{ 0x8510, 0x1 }, { 0x8530, 0x0 }, { 0x8540, 0x0 }, { 0x8801, 0x2 }, { 0x8440, 0x0 }, { 0xc4d0, 0x0 }, { 0xb050, 0x0 },
	{ 0x7033, 0xc }, { 0x7444, 0xa }, { 0x7534, 0x6 }, { 0x7584, 0x10 }, { 0x7635, 0x1e }, { 0x7695, 0x3f },
};
static const uint16_t amp_speaker[][2] = {      /* the profile "speaker", up to its "default" section */
	{ 0x0040, 0x1 }, { 0x7090, 0x1 }, { 0x64e1, 0x2 }, { 0x62e1, 0x0 }, { 0x0203, 0x8 },
};

static int
amp_read(uint8_t reg, uint16_t *v)
{
	uint8_t b[2] = { 0, 0 };
	int e = begonia_i2c_xfer(AMP_BUS, AMP_ADDR, &reg, 1, b, 2);
	*v = (uint16_t)(b[0] << 8 | b[1]);
	return e;
}

static int
amp_write(uint8_t reg, uint16_t v)
{
	uint8_t b[3] = { reg, (uint8_t)(v >> 8), (uint8_t)v };
	return begonia_i2c_xfer(AMP_BUS, AMP_ADDR, b, 3, NULL, 0);
}

static int
amp_field(uint16_t field, uint16_t value)
{
	uint8_t reg = (uint8_t)(field >> 8);
	unsigned pos = (field >> 4) & 15, bits = (field & 15) + 1;
	uint16_t mask = (uint16_t)(((1u << bits) - 1) << pos), v;
	int e = amp_read(reg, &v);
	if (e == 0) {
		e = amp_write(reg, (uint16_t)((v & ~mask) | ((value << pos) & mask)));
	}
	return e;
}

static bool speaker_on;

static size_t
speaker_up(char *out, size_t len, size_t o)
{
	uint16_t rev = 0, r[8];
	int e;

	if (begonia_i2c_amp_ready(&rev) != 0 || (rev & 0xff) != 0x74) {
		say("speaker: the amp did not answer as a TFA9874 (rev %#x)\n", rev);
		return o;
	}
	/* I2S3 out: the pins, DL1 into it, 48 kHz 32-bit I2S, on (before the amp: its PLL locks to BCK) */
	m32(gpio, 0x300 + 0x10 * 8, 0xfu << 12 | 0xfu << 16 | 0xfu << 28, 1u << 12 | 1u << 16 | 1u << 28);     /* 67, 68, 71 */
	m32(afe, AFE_CONN0, 1u << 5, 1u << 5);
	m32(afe, AFE_CONN1, 1u << 6, 1u << 6);
	m32(afe, AFE_I2S_CON3, 0xffffeffe, (10u << 8) | (1u << 3) | (1u << 1));
	m32(afe, AFE_I2S_CON3, 1u, 1u);
	delay(2000);
	/* the amp: reset over I2C, the file's settings, then configured, amplifier on, powered up */
	e = amp_field(0x0010, 1);
	delay(5000);
	for (unsigned i = 0; i < sizeof(amp_keys) / sizeof(amp_keys[0]); i++) {
		e |= amp_write(amp_keys[i].reg, amp_keys[i].value);
	}
	for (unsigned i = 0; i < sizeof(amp_device) / sizeof(amp_device[0]); i++) {
		e |= amp_field(amp_device[i][0], amp_device[i][1]);
	}
	for (unsigned i = 0; i < sizeof(amp_speaker) / sizeof(amp_speaker[0]); i++) {
		e |= amp_field(amp_speaker[i][0], amp_speaker[i][1]);
	}
	e |= amp_field(0x0120, 1);              /* MANSCONF */
	e |= amp_field(0x0030, 1);              /* AMPE */
	e |= amp_field(0x0000, 0);              /* PWDN 0 */
	/* what it says: its status registers over 200 ms (0x10/0x11: PLL, clocks, amp; 0x13; 0x14: its state machine) */
	static const uint8_t look[8] = { 0x00, 0x01, 0x02, 0x10, 0x11, 0x13, 0x14, 0x15 };
	for (int t = 0; t < 5; t++) {
		delay(40000);
		for (int i = 0; i < 8; i++) {
			r[i] = 0xdead;
			(void)amp_read(look[i], &r[i]);
		}
		if (t == 0 || t == 4) {
			say("speaker: TFA9874 rev %#x%s; after %d ms: 00 %04x 01 %04x 02 %04x 10 %04x 11 %04x 13 %04x 14 %04x 15 %04x; "
			    "I2S3 %#x\n", rev, e ? " (SOME I2C WRITES FAILED)" : "", (t + 1) * 40, r[0], r[1], r[2], r[3], r[4], r[5],
			    r[6], r[7], r32(afe, AFE_I2S_CON3));
		}
	}
	begonia_perf_note("BEGONIA AUDIO: speaker TFA9874 %#x%s: 00 %04x 01 %04x 10 %04x 11 %04x 13 %04x 14 %04x I2S3 %x", rev,
	    e ? " I2C FAILED" : "", r[0], r[1], r[3], r[4], r[5], r[6], r32(afe, AFE_I2S_CON3));
	speaker_on = e == 0;
	return o;
}

#include <sys/hakios.audio.h>
#include <kern/thread_call.h>
#include <kern/clock.h>

static const uint16_t gain_eighths[48] = { 32768, 32300, 31838, 31383, 30935, 30493, 30057, 29628, 29205, 28787, 28376,
	27970, 27571, 27177, 26789, 26406, 26029, 25657, 25290, 24929, 24573, 24221, 23875, 23534, 23198, 22867, 22540, 22218,
	21900, 21587, 21279, 20975, 20675, 20380, 20089, 19802, 19519, 19240, 18965, 18694, 18427, 18164, 17904, 17648, 17396,
	17147, 16902, 16661 };
static volatile int stream_state;               /* 0 down, 1 coming up, 2 playing, -1 failed */
static uint32_t wp;                             /* the next frame written, in the ring */
static uint32_t cleared;                        /* the next frame the timer clears */
static thread_call_t up_call, clear_call;
static uint64_t frames_in, frames_dropped;
static volatile int audio_route;                /* 0 both (speaker left, earpiece right), 1 earpiece, 2 speaker */

static int
sysctl_begonia_audio_route SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int value = audio_route, changed = 0;
	int error = sysctl_io_number(req, value, sizeof(int), &value, &changed);
	if (error == 0 && changed) {
		if (value < 0 || value > 2) {
			return EINVAL;
		}
		if (value != audio_route) {
			begonia_perf_note("BEGONIA AUDIO: route %d (%s)", value, value == 1 ? "earpiece: a call" :
			    value == 2 ? "speaker" : "both");
		}
		audio_route = value;
	}
	return error;
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_audio_route, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    0, 0, sysctl_begonia_audio_route, "I", "where the sound goes: 0 speaker + earpiece, 1 earpiece (calls), 2 speaker");

static volatile uint32_t volume_gain = 32768;   /* Darwin's volume, 0..32768, read by the timer (not under audio_lock) */

static void
volume_follow(void)
{
	uint32_t below = 0, generation = 0;
	bool muted = false;

	(void)hakios_audio_volume(&below, &muted, &generation);
	volume_gain = muted ? 0 : (uint32_t)gain_eighths[below % 48] >> (below / 48 > 15 ? 15 : below / 48);
}

static uint32_t
read_frame(void)
{
	/*
	 * y95: with CUR taken at its word the sink saw the ring full after its first 341 ms (12.9 s of 14.7 lost, the
	 * same 341 ms heard again and again): CUR did not read as an address in the ring. Where it does, it is used;
	 * otherwise DL1's place is reckoned from the time since it was switched on, at 48000 frames a second.
	 */
	static unsigned bad;
	uint32_t cur = r32(afe, AFE_DL1_CUR);
	if (cur >= RING_PA && cur < RING_PA + RING_BYTES) {
		return (cur - RING_PA) / 4;
	}
	if (bad++ < 4) {
		begonia_perf_note("BEGONIA AUDIO: DL1_CUR read %#x, not in the ring: DL1's place from the time", cur);
	}
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time() - dl1_t0, &ns);
	return (uint32_t)((ns / 1000 * 48 / 1000) % RING_FRAMES);
}

/* DL1's registers twice, 5 ms apart: which of them moves (the place DL1 reads), kept in the notes */
static void
dl1_regs_noted(void)
{
	uint32_t a[12], b[12];
	for (int i = 0; i < 12; i++) {
		a[i] = r32(afe, 0x40 + 4 * i);
	}
	delay(5000);
	for (int i = 0; i < 12; i++) {
		b[i] = r32(afe, 0x40 + 4 * i);
	}
	begonia_perf_note("BEGONIA AUDIO: AFE 0x40.. %x %x %x %x %x %x %x %x %x %x %x %x", a[0], a[1], a[2], a[3], a[4], a[5],
	    a[6], a[7], a[8], a[9], a[10], a[11]);
	begonia_perf_note("BEGONIA AUDIO: +5 ms     %x %x %x %x %x %x %x %x %x %x %x %x", b[0], b[1], b[2], b[3], b[4], b[5],
	    b[6], b[7], b[8], b[9], b[10], b[11]);
}

static void
clear_played(__unused thread_call_param_t a, __unused thread_call_param_t b)
{
	volume_follow();
	uint32_t rd = read_frame();
	/*
	 * y97: DL1 read from the ring for a second or so and the sink then found it full (34 of 39 s refused): DL1, or what
	 * feeds it, stopped. What it looked like at 0.5, 1, 2, 4 and 8 s, and when DL1's place first stood still for 100 ms.
	 */
	{
		static unsigned ticks, still, said_still;
		static uint32_t last_rd;
		ticks++;
		still = rd == last_rd ? still + 1 : 0;
		last_rd = rd;
		if (ticks == 25 || ticks == 50 || ticks == 100 || ticks == 200 || ticks == 400 || (still == 5 && said_still++ < 2)) {
			begonia_perf_note("BEGONIA AUDIO: t%u%s rd %u wp %u in %llu lost %llu DAC %x TOP %x DL1CON %x dom %d cfg6 %x "
			    "infraCG %x", ticks, still >= 5 ? " STILL" : "", rd, wp, frames_in, frames_dropped, r32(afe, AFE_DAC_CON0),
			    r32(afe, AUDIO_TOP_CON0), r32(afe, AFE_DL1_CON0), audio_domain_on(), r32(topck, CLK_CFG_6),
			    r32(infra, INFRA1_CG_STA));
		}
	}
	/* what DL1 has played: silence (the writer's place is the sink's alone; a writer left behind is the sink's to see) */
	while (cleared != rd) {
		ring[cleared * 2] = 0;
		ring[cleared * 2 + 1] = 0;
		cleared = (cleared + 1) % RING_FRAMES;
	}
	__builtin_arm_dsb(0xf);
	uint64_t deadline;
	clock_interval_to_deadline(20, NSEC_PER_MSEC, &deadline);
	thread_call_enter_delayed(clear_call, deadline);
}

static void
come_up(__unused thread_call_param_t a, __unused thread_call_param_t b)
{
	static char log[2048];
	bool ok;
	/*
	 * The receiver at +4 dB (index 4; 8 = 0 dB): y100, with the bottom speaker beside it at -6 dB, the earpiece at
	 * 0 dB and half level was not heard at all (it is made for an ear against it).
	 */
	size_t o = hw_up(log, sizeof(log), 0, false, 4, &ok);
	(void)o;
	printf("%s", log);
	if (!ok || ring == NULL) {
		stream_state = -1;
		printf("BEGONIA AUDIO: the stream did not come up\n");
		return;
	}
	dl1_regs_noted();
	/*
	 * The bottom speaker too. Then the two outputs take the ring's two channels: left to the speaker (I2S3), right
	 * to the earpiece (the DAC link: O3 and O4 both from DL1_CH2). The sink decides what goes in each (route).
	 */
	{
		static char spk[1024];
		size_t so = speaker_up(spk, sizeof(spk), 0);
		(void)so;
		printf("%s", spk);
		m32(afe, AFE_CONN3, 1u << 5 | 1u << 6, 1u << 6);
		m32(afe, AFE_CONN4, 1u << 5 | 1u << 6, 1u << 6);
		printf("BEGONIA AUDIO: the speaker %s\n", speaker_on ? "is on (left channel); the earpiece the right" :
		    "DID NOT COME UP: the earpiece alone");
		begonia_perf_note("BEGONIA AUDIO: speaker %s; CONN0 %x CONN1 %x CONN3 %x CONN4 %x ZCD3 %x", speaker_on ? "on" : "OFF",
		    r32(afe, AFE_CONN0), r32(afe, AFE_CONN1), r32(afe, AFE_CONN3), r32(afe, AFE_CONN4), pm(ZCD_CON3));
	}
	cleared = read_frame();
	wp = (cleared + 1920) % RING_FRAMES;            /* 40 ms ahead of DL1 */
	clear_call = thread_call_allocate(clear_played, NULL);
	clear_played(NULL, NULL);
	stream_state = 2;
	printf("BEGONIA AUDIO: Darwin's sound plays on the earpiece (DL1 at 0x%x, 0 dB receiver, Darwin's volume)\n", RING_PA);
}

static uint32_t
sink(const int16_t *frames, uint32_t count)
{
	if (stream_state == 0) {
		stream_state = 1;
		thread_call_enter(up_call);
		return 0;
	}
	if (stream_state != 2) {
		return 0;
	}
	/*
	 * Darwin's volume as the timer last read it: the sink is called with hakios.audio.c's audio_lock held, and
	 * hakios_audio_volume() takes that lock again (y94: "Mutex audio_lock unexpectedly owned", a panic at YouTube's
	 * first sound).
	 */
	uint32_t g = volume_gain;
	/*
	 * How far the writer is ahead of DL1. It is never let ahead by more than half the ring (170 ms), so "more than
	 * half" means DL1 has passed it (a gap in what Darwin wrote: buffering, a pause): it starts again 40 ms ahead.
	 * y97/y98: a writer left behind looked like a ring all but full, and almost everything after a gap was refused
	 * (34 of 39 s; the user heard a second of sound).
	 */
	uint32_t rd = read_frame();
	uint32_t ahead = (wp + RING_FRAMES - rd) % RING_FRAMES;
	if (ahead > RING_FRAMES / 2) {
		static unsigned said;
		if (said++ < 4) {
			begonia_perf_note("BEGONIA AUDIO: the writer was behind DL1 (a gap); 40 ms ahead again");
		}
		wp = (rd + 1920) % RING_FRAMES;
		ahead = 1920;
	}
	uint32_t room = RING_FRAMES / 2 - ahead;
	uint32_t n = count < room ? count : room;
	/*
	 * Where the sound goes (kern.begonia_audio_route): 0 both -- Darwin's left to the bottom speaker, its right to the
	 * earpiece, the "dual speaker"; 1 the earpiece alone (calls: SpringBoard sets it while one is in progress);
	 * 2 the speaker alone. Without the speaker: the earpiece alone. Levels: the speaker at half (-6 dB: it runs with
	 * no DSP, so no speaker protection), the earpiece at half when it is the second speaker (a small driver).
	 */
	int route = speaker_on ? audio_route : 1;
	for (uint32_t i = 0; i < n; i++) {
		int32_t l = frames[2 * i], r = frames[2 * i + 1], m = (l + r) / 2;
		int32_t spk, ear;
		switch (route) {
		case 1:  spk = 0;      ear = m;      break;
		case 2:  spk = m / 2;  ear = 0;      break;
		default: spk = l / 2;  ear = r;      break;
		}
		ring[wp * 2] = (int16_t)((spk * (int32_t)g) >> 15);
		ring[wp * 2 + 1] = (int16_t)((ear * (int32_t)g) >> 15);
		wp = (wp + 1) % RING_FRAMES;
	}
	__builtin_arm_dsb(0xf);
	frames_in += n;
	frames_dropped += count - n;
	return n;                                       /* (what did not fit is counted as lost by hakios.audio.c) */
}

void
begonia_audio_init(void)
{
	if (!permitted()) {
		return;
	}
	if (!mapped()) {
		printf("BEGONIA AUDIO: registers not mapped\n");
		return;
	}
	up_call = thread_call_allocate(come_up, NULL);
	hakios_audio_set_sink("begonia earpiece (MT6359 receiver)", sink);
	printf("BEGONIA AUDIO: sink set: Darwin's sound goes to the earpiece when it first plays\n");
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
