/* MT6785 clock/power observation and opt-in guarded CPU/CCI correction.
 * Regulator writes only with begonia_cpu_opp= / begonia_gpu_khz= (stock operating points, below).
 * Register locations: pinned Xiaomi MT6785 clk/cpufreq sources (docs/CPU.md).
 * Enable explicitly with begonia_perf_probe=1. This is not a DVFS governor.
 */
#include <kern/clock.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <sys/sysctl.h>
#include <sys/errno.h>
#include "begonia_clock_policy.h"
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

static uint32_t
rd(vm_offset_t base, unsigned offset)
{
    return *(volatile uint32_t *)(base + offset);
}

/* AP WACS2 command channel: read and write commands. */
static void pw_wr(vm_offset_t base, unsigned offset, uint32_t value)
{
    *(volatile uint32_t *)(base+offset)=value;
    __asm__ volatile("dsb sy" ::: "memory");
}
/*
 * The channel is one state machine for everybody: the buttons' thread asks the PMIC for the power key
 * all the time, and this file reads and writes rails. A command given while the machine is not idle is
 * lost, and it is not idle for a moment after a read has been acknowledged -- which two reads in a row
 * got away with at the boot loader's 1175 MHz and not at 2000 (x10: after the A55s were raised every
 * second read failed, the GPU's rails "could not be read", the GPU stayed off and CoreAnimation drew
 * with the software rasterizer). So: one user at a time (begonia_pwrap_take), and idle is waited for.
 */
static volatile int pwrap_taken;
void begonia_pwrap_take(void);
void begonia_pwrap_give(void);
void begonia_pwrap_take(void)
{
    while (__atomic_exchange_n(&pwrap_taken, 1, __ATOMIC_ACQUIRE)) {
        delay(20);
    }
}
void begonia_pwrap_give(void)
{
    __atomic_store_n(&pwrap_taken, 0, __ATOMIC_RELEASE);
}
static int pmic_idle(vm_offset_t base)
{
    for (unsigned i=0;i<2000;i++) {
        uint32_t state=rd(base,0xc24);
        if ((state&(1u<<22)) && ((state>>16)&7)==0) return 0;
        delay(10);
    }
    return -1;
}
static int pmic_write(vm_offset_t base, unsigned address, uint32_t value)
{
    int result=-1;
    if ((address&1) || address>0xfffe || (value&~0xffff)) return -1;
    begonia_pwrap_take();
    if (!pmic_idle(base)) {
        pw_wr(base,0xc20,(1u<<31)|((address>>1)<<16)|(value&0xffff));
        delay(2);
        result=pmic_idle(base);
    }
    begonia_pwrap_give();
    return result;
}
static int pmic_read(vm_offset_t base, unsigned address, uint32_t *value)
{
    int result=-1;
    if ((address&1) || address>0xfffe) return -1;
    begonia_pwrap_take();
    if (!pmic_idle(base)) {
        pw_wr(base,0xc20,(address>>1)<<16);
        for (unsigned i=0;i<1000;i++) {
            uint32_t state=rd(base,0xc24);
            if (((state>>16)&7)==6) {
                *value=state&0xffff;
                pw_wr(base,0xc28,1);
                (void)pmic_idle(base);
                result=0;
                break;
            }
            delay(10);
        }
    }
    begonia_pwrap_give();
    return result;
}
static void pmic_probe(void)
{
    vm_offset_t base=ml_io_map(0x1000d000,0x1000);
    uint32_t hw=0,sw=0;
    if (!base || pmic_read(base,8,&hw) || pmic_read(base,10,&sw)) {
        printf("BEGONIA PMIC: read channel unavailable; no regulator changes\n");return;
    }
    printf("BEGONIA PMIC: HWCID=%04x SWCID=%04x\n",hw,sw);
    if ((hw&0xff00)!=0x5900) {
        printf("BEGONIA PMIC: identity differs from MT6359; voltage reads skipped\n");return;
    }
    static const unsigned addresses[]={0x1726,0x1728,0x17a6,0x17a8,0x1e8e,0x1eac,0x15a6,0x15a8};
    for (unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++) {
        uint32_t value=0;
        if (pmic_read(base,addresses[i],&value)) {
            printf("BEGONIA PMIC: read timeout at %04x; stopped\n",addresses[i]);return;
        }
        printf("BEGONIA PMIC: reg %04x=%04x\n",addresses[i],value);
    }
}

static void clock_correct(vm_offset_t pll, vm_offset_t div)
{
    uint32_t enabled=0;
    if(!PE_parse_boot_argn("begonia_cpu_boost",&enabled,sizeof(enabled)) || enabled!=1)return;
    vm_offset_t pmic=ml_io_map(0x1000d000,0x1000);
    uint32_t hw=0,proc=0,sram=0,a=0,b=0;
    if(!pmic || pmic_read(pmic,8,&hw) || hw!=0x5920 ||
       pmic_read(pmic,0x17a6,&proc) || pmic_read(pmic,0x1eac,&sram)) {
        printf("BEGONIA CLOCK: voltage/identity unavailable; unchanged\n");return;
    }
    uint32_t cpu_uv=400000+(proc&127)*6250;
    uint32_t sram_uv=500000+((sram>>8)&127)*6250;
    uint32_t ll=rd(pll,0x204),cci=rd(pll,0x2a4);
    uint32_t ll_div=rd(div,0x2a0),cci_div=rd(div,0x2e0);
    if(ll_div!=0x03110aff || cci_div!=0x83100aff ||
       begonia_clock_values(cpu_uv,sram_uv,ll,cci,&a,&b)) {
        printf("BEGONIA CLOCK: unexpected clock/rail state; unchanged\n");return;
    }
    /* Park on CLKSQ (mux0) while changing PCW, then restore ARMPLL (mux1).
     * Preserve divider/post-divider; register encoding/20us settle from MT6785.
     * Raise interconnect first. No PMIC write commands are issued. */
    boolean_t interrupts=ml_set_interrupts_enabled(FALSE);
    pw_wr(div,0x2e0,cci_div&~(3u<<9));
    pw_wr(pll,0x2a4,b);delay(20);
    pw_wr(div,0x2e0,cci_div);
    pw_wr(div,0x2a0,ll_div&~(3u<<9));
    pw_wr(pll,0x204,a);delay(20);
    pw_wr(div,0x2a0,ll_div);
    ml_set_interrupts_enabled(interrupts);
    printf("BEGONIA CLOCK: A55 %08x->%08x CCI %08x->%08x rails %u/%u uV unchanged\n",
       ll,rd(pll,0x204),cci,rd(pll,0x2a4),cpu_uv,sram_uv);
}

/*
 * begonia_cpu_opp=1|2|3: the boot loader leaves the A55s at 1175 MHz, the A76s at 1085 MHz and the
 * interconnect at 612 MHz, and nothing here ever raised them: every thread of the system ran at a
 * little over half of what the SoC is sold for. This puts each cluster on one fixed stock operating
 * point -- no governor, no thermal control, hence opt-in and three levels:
 *
 *      level   A55 (VPROC2)        A76 (VPROC1)        interconnect (VPROC2)
 *        1     1618 MHz  875.00 mV  1670 MHz  956.25 mV  1120 MHz  862.50 mV
 *        2     1800 MHz  950.00 mV  1860 MHz 1037.50 mV  1260 MHz  950.00 mV
 *        3     2000 MHz 1031.25 mV  2002 MHz 1100.00 mV  1400 MHz 1031.25 mV
 *
 * Frequencies, voltages and post dividers are rows of FY_6785Tbl in the pinned Xiaomi tree
 * (cpufreq_v1/src/mach/mt6785/mtk_cpufreq_opp_pv_table.h; voltage = 400 mV + n * 6.25 mV), the
 * rails and the order of work from mtk_cpufreq_platform.[ch]: the memory rail first and at least
 * 100 mV above the logic rail, never more than 225 mV above it, at most 1125 mV; voltage before
 * frequency; the interconnect before the cores. MT6359 registers: Linux
 * include/linux/mfd/mt6359/registers.h (VOSEL in *_ELR0 / LDO_VSRAM_PROC*_ELR, what the buck puts
 * out in *_DBG0 / *_VOSEL1). Every write is read back from the rail itself before the next; the
 * first thing that is not as expected ends it, with the clocks where they were.
 */
/* What the clock and rail work said, kept: the console's ring in RAM holds a few minutes, and these
 * lines are from the first seconds. begonia_mali_service.c says them again with its statistics. */
char begonia_perf_notes[4096];
void begonia_perf_note(const char *format,...) __printflike(1,2);
void begonia_perf_note(const char *format,...)
{
    char line[256];va_list arguments;
    va_start(arguments,format);vsnprintf(line,sizeof(line),format,arguments);va_end(arguments);
    printf("%s\n",line);
    strlcat(begonia_perf_notes,line,sizeof(begonia_perf_notes));
    strlcat(begonia_perf_notes," | ",sizeof(begonia_perf_notes));
}
/* The notes again, in pieces a console line holds (begonia_touch.c, every half minute). */
void begonia_perf_notes_say(void);
void begonia_perf_notes_say(void)
{
    size_t length=strlen(begonia_perf_notes);
    for(size_t at=0;at<length;at+=200)printf("BEGONIA NOTES %u: %.200s\n",(unsigned)(at/200),begonia_perf_notes+at);
}
struct begonia_opp {uint32_t mhz,uv,post;};
static const struct begonia_opp ll_opp[4]={{0,0,0},{1618,875000,2},{1800,950000,1},{2000,1031250,1}};
static const struct begonia_opp big_opp[4]={{0,0,0},{1670,956250,1},{1860,1037500,1},{2002,1100000,1}};
static const struct begonia_opp cci_opp[4]={{0,0,0},{1120,862500,2},{1260,950000,2},{1400,1031250,2}};
struct begonia_rail {const char *name;unsigned proc_set,proc_out,sram_set,sram_out;};
static const struct begonia_rail rail_ll={"VPROC2",0x17b2,0x17a6,0x1b42,0x1eac};
static const struct begonia_rail rail_big={"VPROC1",0x172e,0x1726,0x1b40,0x1e8e};

static int rail_read(vm_offset_t pmic,const struct begonia_rail *rail,uint32_t *proc,uint32_t *sram)
{
    uint32_t a=0,b=0;
    if(pmic_read(pmic,rail->proc_out,&a) || pmic_read(pmic,rail->sram_out,&b))return -1;
    *proc=400000+(a&127)*6250;*sram=500000+((b>>8)&127)*6250;
    return 0;
}
/* One selector, then the rail's own word for what it puts out, for up to 20 ms. */
static int rail_set(vm_offset_t pmic,unsigned set,unsigned out,unsigned shift,uint32_t base,uint32_t uv)
{
    uint32_t selector=(uv-base)/6250,now=0;
    if(uv<base || (uv-base)%6250 || selector>127 || pmic_read(pmic,set,&now))return -1;
    if(pmic_write(pmic,set,(now&~0x7fu)|selector))return -1;
    for(unsigned i=0;i<400;i++) {
        delay(50);
        if(!pmic_read(pmic,out,&now) && ((now>>shift)&127)==selector)return 0;
    }
    return -1;
}
static int rail_raise(vm_offset_t pmic,const struct begonia_rail *rail,uint32_t target)
{
    uint32_t proc=0,sram=0,sram_target=target+100000,first=0;
    int tracking=0;
    if(sram_target>1125000)sram_target=1125000;
    if(sram_target<850000)sram_target=850000;
    if(rail_read(pmic,rail,&proc,&sram) || proc<700000 || proc>1100000 || sram<850000 || sram>1125000 ||
       sram<proc+50000 || sram>proc+225000 || target>1100000) {
        begonia_perf_note("BEGONIA CPU: %s at %u/%u uV is not a state to start from; unchanged",rail->name,proc,sram);
        return -1;
    }
    first=proc;
    begonia_perf_note("BEGONIA CPU: %s starts at %u uV, its memory rail at %u uV; going to %u",rail->name,proc,sram,target);
    for(unsigned step=0;step<32;step++) {
        if(proc>=target && (tracking || sram>=sram_target)) {
            begonia_perf_note("BEGONIA CPU: %s now %u uV, its memory rail %u uV%s",rail->name,proc,sram,
                tracking?" (follows by itself)":"");
            return 0;
        }
        if(!tracking && sram<sram_target) {
            uint32_t next=proc+200000<sram_target?proc+200000:sram_target;
            if(next>sram && rail_set(pmic,rail->sram_set,rail->sram_out,8,500000,next)) {
                /* a memory rail the PMIC makes follow the logic rail takes no selector of ours */
                begonia_perf_note("BEGONIA CPU: %s memory rail did not take %u uV; trying whether it follows",rail->name,next);
                tracking=1;
            }
        }
        if(rail_read(pmic,rail,&proc,&sram))return -1;
        {
            uint32_t next=target;
            if(tracking) {
                if(next>proc+25000)next=proc+25000;
            } else if(sram<sram_target && next+100000>sram) {
                next=sram-100000;
            }
            if(next>proc && rail_set(pmic,rail->proc_set,rail->proc_out,0,400000,next)) {
                begonia_perf_note("BEGONIA CPU: %s did not take %u uV; stopped",rail->name,next);
                return -1;
            }
        }
        if(rail_read(pmic,rail,&proc,&sram))return -1;
        if(sram<proc+(tracking?75000:50000) && sram<1118750) {
            begonia_perf_note("BEGONIA CPU: %s at %u uV but its memory rail only %u uV; back to %u and stopped",rail->name,proc,sram,first);
            (void)rail_set(pmic,rail->proc_set,rail->proc_out,0,400000,first);
            return -1;
        }
    }
    begonia_perf_note("BEGONIA CPU: %s stuck at %u/%u uV short of %u; stopped",rail->name,proc,sram,target);
    return -1;
}
/* A cluster's PLL to `mhz`: its cores wait on the 26 MHz crystal while the PLL moves (as clock_correct). */
static int pll_move(vm_offset_t pll,vm_offset_t div,unsigned con1,unsigned mux,const struct begonia_opp *opp,const char *name)
{
    uint32_t was=rd(pll,con1),divider=rd(div,mux);
    uint32_t pcw=(uint32_t)((((uint64_t)opp->mhz*opp->post)<<14)/26);
    uint32_t value=(was&~((7u<<24)|0x3fffffu))|((opp->post==1?0u:opp->post==2?1u:2u)<<24)|(pcw&0x3fffffu)|0x80000000u;
    /* as inherited and decoded: the PLL selected (mux field 1), divided by one (selector 8) */
    if(((divider>>9)&3)!=1 || ((divider>>17)&31)!=8 || pcw>0x3fffffu || opp->mhz*opp->post<1500 || opp->mhz*opp->post>3800) {
        begonia_perf_note("BEGONIA CPU: %s divider %08x / PLL %08x not as expected; unchanged",name,divider,was);
        return -1;
    }
    boolean_t interrupts=ml_set_interrupts_enabled(FALSE);
    pw_wr(div,mux,divider&~(3u<<9));
    pw_wr(pll,con1,value);delay(40);
    pw_wr(div,mux,divider);
    ml_set_interrupts_enabled(interrupts);
    begonia_perf_note("BEGONIA CPU: %s PLL %08x -> %08x (%u MHz)",name,was,rd(pll,con1),opp->mhz);
    return 0;
}
static void cpu_opp(vm_offset_t pll,vm_offset_t div)
{
    uint32_t level=0,hw=0;
    if(!PE_parse_boot_argn("begonia_cpu_opp",&level,sizeof(level)) || level<1 || level>3)return;
    vm_offset_t pmic=ml_io_map(0x1000d000,0x1000);
    if(!pmic || pmic_read(pmic,8,&hw) || hw!=0x5920) {
        begonia_perf_note("BEGONIA CPU: PMIC identity %04x is not the MT6359 these registers are for; unchanged",hw);return;
    }
    uint32_t shared=ll_opp[level].uv>cci_opp[level].uv?ll_opp[level].uv:cci_opp[level].uv;
    if(!rail_raise(pmic,&rail_ll,shared)) {
        if(!pll_move(pll,div,0x2a4,0x2e0,&cci_opp[level],"interconnect"))
            (void)pll_move(pll,div,0x204,0x2a0,&ll_opp[level],"A55");
    }
    if(!rail_raise(pmic,&rail_big,big_opp[level].uv))
        (void)pll_move(pll,div,0x214,0x2a4,&big_opp[level],"A76");
}

uint32_t begonia_gpu_khz;       /* what begonia_mali_probe.c is to set MFGPLL to (0: leave it) */

void
begonia_perf_init(void)
{
    uint32_t enabled = 0;
    if (!PE_parse_boot_argn("begonia_perf_probe", &enabled, sizeof(enabled)) || enabled != 1)
        return;
    vm_offset_t pll = ml_io_map(0x1000c000, 0x1000);
    vm_offset_t div = ml_io_map(0x0c53a000, 0x1000);
    vm_offset_t spm = ml_io_map(0x10006000, 0x1000);
    if (!pll || !div || !spm) {
        printf("BEGONIA PERF: mapping failed; no register access\n");
        return;
    }
    /* Report raw mux/divider state: PLL output alone is not CPU clock rate. */
    printf("BEGONIA PERF: MPIDR=%llx MIDR=%llx CNTFRQ=%llu\n",
        __builtin_arm_rsr64("MPIDR_EL1"), __builtin_arm_rsr64("MIDR_EL1"),
        __builtin_arm_rsr64("CNTFRQ_EL0"));
    printf("BEGONIA PERF: LL pll=%08x div=%08x BIG pll=%08x div=%08x CCI pll=%08x div=%08x\n",
        rd(pll, 0x204), rd(div, 0x2a0), rd(pll, 0x214), rd(div, 0x2a4),
        rd(pll, 0x2a4), rd(div, 0x2e0));
    printf("BEGONIA PERF: MFG pll_con0=%08x pll_con1=%08x pll_power=%08x SPM=%08x/%08x\n",
        rd(pll, 0x250), rd(pll, 0x254), rd(pll, 0x25c), rd(spm, 0x160), rd(spm, 0x164));
    /* Do not read 0x13040000: a powered-down Mali can fault/hang the CPU.
     * Power/clock/bus-protection ownership must be established first. */
    printf("BEGONIA PERF: Mali MMIO untouched; no DVFS or power-state changes\n");
    pmic_probe();
    clock_correct(pll,div);
    uint32_t gpu_probe=0;
    if(PE_parse_boot_argn("begonia_gpu_probe",&gpu_probe,sizeof(gpu_probe)) && gpu_probe==1) {
        extern void begonia_mali_probe(uint32_t,uint32_t,int);
        vm_offset_t pw=ml_io_map(0x1000d000,0x1000);
        uint32_t hw=0,gv=0,ge=0,sv=0,se=0;
        if(pw && !pmic_read(pw,8,&hw) && hw==0x5920 &&
           !pmic_read(pw,0x15a6,&gv) && !pmic_read(pw,0x15a8,&ge) &&
           !pmic_read(pw,0x14a6,&sv) && !pmic_read(pw,0x14a8,&se)) {
            begonia_perf_note("BEGONIA GPU: rail readbacks core=%04x/%04x SRAM=%04x/%04x",gv,ge,sv,se);
            /* begonia_gpu_khz=620000|806000: stock rows of mt6785/mtk_gpufreq_core.h -- 620 MHz at
             * 700 mV (memory rail 850 mV), 806 MHz at 781.25 mV (881.25 mV). For 620 the memory rail is not
             * written: it is taken only if that voltage is already there. */
            uint32_t want=0,selector=0x22,sram_uv=400000+(sv&127)*6250;
            (void)PE_parse_boot_argn("begonia_gpu_khz",&want,sizeof(want));
            /* 806 MHz wants its memory rail (the "vpu" buck: mtk_gpufreq_core.c) at 881.25 mV, and stock
             * raises that one first. Only upwards, only from where the probe's own bounds allow, and
             * never more than 350 mV above the core rail (BUCK_DIFF_MAX). */
            if(want>=806000 && sram_uv>=850000 && sram_uv<881250 && 881250<=400000+(gv&127)*6250+350000) {
                int failed=rail_set(pw,0x14ac,0x14a6,0,400000,881250);
                (void)pmic_read(pw,0x14a6,&sv);
                sram_uv=400000+(sv&127)*6250;
                begonia_perf_note("BEGONIA GPU: memory rail asked to 881250 uV: %s, now %u uV",failed?"not taken":"taken",sram_uv);
            }
            if(want>=806000 && sram_uv>=881250 && sram_uv<=781250+350000) {begonia_gpu_khz=806000;selector=61;}
            else if(want>=620000 && sram_uv>=850000 && sram_uv<=700000+350000) {begonia_gpu_khz=620000;selector=48;}
            else if(want)begonia_perf_note("BEGONIA GPU: %u kHz not taken with the memory rail at %u uV",want,sram_uv);
            if ((gv & 0x7f) < selector) {
                if (!pmic_write(pw, 0x15ac, selector)) {
                    for (int retry = 0; retry < 2000; retry++) {
                        delay(50);
                        if (!pmic_read(pw, 0x15a6, &gv) && (gv & 0x7f) >= selector) {
                            break;
                        }
                    }
                    begonia_perf_note("BEGONIA GPU: raised core rail to %u uV (reg 15a6=%04x)", 400000+(gv&127)*6250, gv);
                }
            }
            if(begonia_gpu_khz && (gv&0x7f)<selector) {
                begonia_perf_note("BEGONIA GPU: core rail short of its selector; MFGPLL stays");begonia_gpu_khz=0;
            }
            begonia_mali_probe(400000+(gv&127)*6250,400000+(sv&127)*6250,(ge&se&1)!=0);
        } else begonia_perf_note("BEGONIA GPU: PMIC identity/readback failed (id %04x); untouched",hw);
    }
    cpu_opp(pll,div);
    begonia_perf_note("BEGONIA PERF: now LL %08x BIG %08x CCI %08x MFG %08x",rd(pll,0x204),rd(pll,0x214),rd(pll,0x2a4),rd(pll,0x254));
}

/*
 * kern.begonia_bind_cpu: the calling thread onto one processor (-1: off it again). XNU is given every
 * core as the same kind here (cluster-type 0 in the device tree: no P/E scheduling in this kernel
 * configuration), so the window server's drawing thread and SpringBoard's main thread ran wherever
 * there was room, mostly on an A55. The two A76s are XNU's processors 5 and 6 (MPIDR 0x600, 0x700;
 * 0x100 is the recovery supervisor's and is not XNU's). The scheduler's own kern.sched_thread_bind_cpu
 * does the same but only for root and behind a boot-arg; this one is for the two UI programs, which
 * are not root (userland: gl_cpu=, sb_cpu=).
 */
extern kern_return_t sysctl_thread_bind_cpuid(int32_t cpuid);
static int
sysctl_begonia_bind_cpu SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
    int32_t value = -1;
    int changed = 0;
    int error = sysctl_io_number(req, -1, sizeof(int32_t), &value, &changed);
    if (error || !changed) {
        return error;
    }
    return sysctl_thread_bind_cpuid(value) == KERN_SUCCESS ? 0 : EINVAL;
}
SYSCTL_PROC(_kern, OID_AUTO, begonia_bind_cpu, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED | CTLFLAG_ANYBODY,
    0, 0, sysctl_begonia_bind_cpu, "I", "bind the calling thread to a processor");
