/* SPDX-License-Identifier: MIT
 * G76 backend for the S9+ HakiosMali IOKit transport. MT6785 power-up is owned
 * by begonia_mali_probe.c, never by the Exynos reference. No DVFS.
 *
 * Jobs: the glue (hakios.mali.cpp) is written for a backend that queues -- kPanfrostSubmit
 * "queued, not run here", the waits taken without its lock. The first transport here ran each
 * job inside the submit and polled the GPU until it was done, on the caller's thread: the window
 * server's drawing thread spent a third of its processor time in that poll (x8, glprof) and never
 * prepared a batch while the GPU drew one. With begonia_gpu_async=1 a submit only queues; one
 * kernel thread runs the jobs in order, one at a time as before, and is the only one to touch
 * the GPU's registers after start-up. The tables' memory is edited under vm_lock by whoever maps;
 * the thread writes them out and tells the MMU before each job, while the GPU is idle, exactly
 * where the synchronous transport did.
 */
#include <kern/clock.h>
#include <kern/cpu_number.h>
#include <kern/locks.h>
#include <kern/sched_prim.h>
#include <kern/thread.h>
#include <libkern/libkern.h>
#include <mach/mach_types.h>
#include <machine/machine_routines.h>
#include <pexpert/pexpert.h>
#include <sys/systm.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>
#include "begonia_gpu_vm.h"
#include "hakios.mali_va.h"
extern uint64_t kvtophys(vm_offset_t);
extern boolean_t pmap_valid_address(uint64_t);
extern void FlushPoC_DcacheRegion(vm_offset_t,size_t);
extern void HakiosMaliStart(void);
extern void begonia_perf_note(const char *format,...) __printflike(1,2);
static vm_offset_t gpu;
static struct beg_gpu_vm space;
static struct mali_va addresses;
static int ready;
static uint64_t submitted,completed,failed;
static uint32_t async_jobs=1;           /* boot-arg begonia_gpu_async=0: jobs run inside the submit, as at first */
static lck_grp_t *gpu_group;
static lck_mtx_t *vm_lock;              /* the tables' memory and their dirty marks */
static lck_mtx_t *queue_lock;           /* the queue, submitted, completed, failed */
#define JOB_QUEUE 1024
static struct {uint64_t chain,seq,queued;bool fragment;} queue[JOB_QUEUE];
static uint64_t queue_head,queue_tail;
/* what the GPU's time goes into, said every five seconds it has had work */
static uint64_t stat_jobs,stat_fragment,stat_busy,stat_longest,stat_waited,stat_since;
static unsigned stat_deepest;
/* x33: callers that slept in hakios_mali_wait() for a job (Mesa waiting on the GPU), and for how long in all */
static uint64_t stat_sleeps,stat_slept;
static int32_t job_cpu=4;               /* x34: begonia_gpu_cpu (job_thread()) */
static uint64_t stat_busy_fragment;
static uint64_t stat_sizes[6],stat_size_us[6];
/* y260: what the vertex/tiler slot's time is: chains with a compute job, chains of vertex/tiler jobs alone, others
 * (read-only: the jobs' type words are looked at, nothing is written). */
static uint64_t s1_kind_jobs[3],s1_kind_us[3];
/* y262: where the time outside a job goes: before it (the tables' flush, the locks) and between one job's end and the
 * next one's entry while more jobs were queued behind it (the thread's loop: the queue's lock, the wake-ups). */
static uint64_t gap_pre,gap_pre_n,gap_between,gap_between_n,gap_last_end;
/* y285: which process holds the wired memory (callback of proc_iterate: processes with 8 MB wired or 64 MB footprint) */
extern void proc_iterate(unsigned int flags,int (*callout)(void *,void *),void *arg,int (*filterfn)(void *,void *),void *filterarg);

extern uint64_t get_task_wired_mem(void *);extern uint64_t get_task_phys_footprint(void *);
static char memp[360];static unsigned memn;
static int mem_proc_cb(void *p,void *arg) {
 (void)arg;
 void *t=(void *)proc_task((proc_t)p);
 if(!t)return 0;
 uint64_t w=get_task_wired_mem(t)>>20,f=get_task_phys_footprint(t)>>20;
 if((w>=8||f>=64) && memn<sizeof memp-48){char nm[20];proc_name(proc_pid((proc_t)p),nm,sizeof nm);memn+=(unsigned)snprintf(memp+memn,sizeof memp-memn," %s[%d] w%lluM f%lluM",nm,proc_pid((proc_t)p),w,f);}
 return 0;
}
static unsigned mem_min_free=~0u;   /* y282: the fewest free pages seen at any job's end since the last report */
static char s1_sample[4][72];static unsigned s1_samples;   /* y268: the job types of some chains, in order */
static uint64_t s1_types[16],s1_chain_len_sum;   /* y261: every job of slot 1's chains by type (4 compute, 5 vertex, 7 tiler, ...) */
static unsigned fail_count,fail_slot;static uint32_t fail_irq,fail_mmu,fail_status;
static uint64_t fail_seq,fail_head,fail_tail,fail_fault,fail_chain,fail_at,fail_took;
static int job_bound=-1;
/*
 * y217: what the slowest pixel job of each 5 s was asked to draw -- the window server on Metal has full-screen jobs of
 * 10-24 ms where the GL path's take 3 (y214). The job's framebuffer descriptor, read through the GPU's own tables after
 * the job ended, said raw with the statistics (72 words: the parameters, then the first render target); decoded on the
 * host against Mesa's v7.xml. Read only.
 */
extern unsigned int ml_phys_read_word_64(addr64_t paddr);
static uint32_t slow_words[72];static uint64_t slow_took,slow_fbd;
static uint32_t rd(unsigned o) {return *(volatile uint32_t *)(gpu+o);}
static void wr(unsigned o,uint32_t v) {*(volatile uint32_t *)(gpu+o)=v;__asm__ volatile("dsb sy":::"memory");}
static int wait(unsigned o,uint32_t mask,uint32_t value) {
 for(unsigned i=0;i<10000;i++) {if((rd(o)&mask)==value)return 0;delay(10);}
 return -1;
}
/*
 * begonia: a job's end, waited for asleep. wait()'s delay(10) between looks either spins or sleeps and wakes every
 * 10 us, for as long as the GPU works (milliseconds a job, every frame the window server draws): a processor busy with
 * nothing whenever anything moves, the phone's heat. Ten quick looks (a short job), then naps of 50 us
 * (assert_wait_timeout: a sleep, whatever the processor's idle latency), until 100 ms as before.
 */
/*
 * y121: with YouTube the job thread ran 5000-5600 jobs a second, most of them ~66 us on the GPU, one at a time: a
 * nap's wake-up may cost more than such a job. Boot-arg begonia_gpu_spin=N: N quick looks (10 us apart) before the
 * naps (10 as before), to measure what the naps cost the queue.
 */
static uint32_t spin_looks=10;
static int wait_job(unsigned o,uint32_t mask,uint32_t value) {
 static int asked;
 if(!asked){asked=1;(void)PE_parse_boot_argn("begonia_gpu_spin",&spin_looks,sizeof spin_looks);if(spin_looks>2000)spin_looks=2000;}
 for(unsigned i=0;i<spin_looks;i++) {if((rd(o)&mask)==value)return 0;delay(10);}
 uint64_t deadline;
 nanoseconds_to_absolutetime(100*NSEC_PER_MSEC,&deadline);
 deadline+=mach_absolute_time();
 while(mach_absolute_time()<deadline) {
  if((rd(o)&mask)==value)return 0;
  assert_wait_timeout((event_t)&wait_job,THREAD_UNINT,50,NSEC_PER_USEC);
  thread_block(THREAD_CONTINUE_NULL);
 }
 return (rd(o)&mask)==value?0:-1;
}
static int reset(void) {
 wr(0x28,0);wr(0x24,0xffffffff);wr(0x30,1);
 if(wait(0x20,1u<<8,1u<<8)) {
  __atomic_store_n(&ready,0,__ATOMIC_RELEASE);
  printf("BEGONIA GPU SERVICE: reset failed; retain all GPU buffers until SoC reset\n");return -1;
 }
 wr(0x24,0xffffffff);return 0;
}
static vm_offset_t pool_page = 0;
static unsigned pool_index = 4;

static int allocate_table(void *context,struct beg_gpu_vm_table *table) {
 (void)context;
 if (pool_index >= 4) {
  if (kmem_alloc(kernel_map, &pool_page, PAGE_SIZE, KMA_KOBJECT|KMA_ZERO|KMA_DATA, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS) {
   printf("HakiosMali: allocate_table kmem_alloc failed\n");
   return -1;
  }
  bzero((void *)pool_page, PAGE_SIZE);
  FlushPoC_DcacheRegion(pool_page, PAGE_SIZE);
  pool_index = 0;
 }
 vm_offset_t chunk = pool_page + pool_index * 4096;
 uint64_t physical = kvtophys(chunk);
 if (physical < 0x100000000ull || physical > 0x1b7e00000ull - 4096 || !begonia_gpu_table_entry(physical)) {
  printf("HakiosMali: allocate_table invalid physical 0x%llx\n", physical);
  return -1;
 }
 pool_index++;
 table->physical = physical;
 table->entries = (uint64_t *)chunk;
 return 0;
}
bool hakios_mali_ready(void) {return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=0;}
void hakios_keep(const char *format,...) __printflike(1,2);
void hakios_keep(const char *format,...) {
 char line[384];va_list args;va_start(args,format);vsnprintf(line,sizeof line,format,args);va_end(args);
 printf("%s\n",line);
}
bool hakios_mali_parameter(uint32_t which,uint64_t *value) {
 if(!value || !hakios_mali_ready())return false;
 static const unsigned offsets[]={0,0,0x100,0x110,0x120,0xe00,0x18,0x1c,
  4,8,0xc,0x10,0x14,0xac,0xa0,0xa4,0xa8,0x300};
 if(which<sizeof offsets/sizeof offsets[0]) {
  *value=which==0?rd(0)>>16:which==1?rd(0)&0xffff:rd(offsets[which]);return true;
 }
 if(which>=18 && which<=21) {*value=rd(0xb0+4*(which-18));return true;}
 if(which>=22 && which<=37) {*value=rd(0xc0+4*(which-22));return true;}
 if(which==38) {*value=__builtin_popcount(rd(0x120));return true;}
 if(which==39) {*value=0;return true;}
 if(which==40) {*value=rd(0x4c);return true;}
 if(which==41) {
  uint32_t hi,lo,next;do {hi=rd(0x9c);lo=rd(0x98);next=rd(0x9c);}while(hi!=next);
  *value=((uint64_t)hi<<32)|lo;return true;
 }
 if(which==42) {*value=13000000;return true;}
 return false;
}
/* y290: the GPU address space each process holds (allocated minus unmapped by the same process), said with BEGONIA MEM: the
 * wired memory the window server's kills are about is mostly buffers and surfaces the GPU maps. Approximate (a free by
 * another process counts for that one). */
extern int proc_selfpid(void);extern void proc_selfname(char *buf,int size);
static struct {int pid;char name[20];int64_t net;} va_proc[8];
static int64_t va_live;
static void va_account(int64_t delta) {
 int pid=proc_selfpid(),slot=-1,least=0;
 for(int i=0;i<8;i++) {
  if(va_proc[i].pid==pid){slot=i;break;}
  if(va_proc[i].net<va_proc[least].net)least=i;
 }
 if(slot<0){slot=least;va_proc[slot].pid=pid;va_proc[slot].net=0;proc_selfname(va_proc[slot].name,sizeof(va_proc[slot].name));}
 va_proc[slot].net+=delta;va_live+=delta;
}
uint64_t hakios_mali_address(uint64_t size,bool executable) {
 if(!hakios_mali_ready() || !size || size>(512ull<<20) || (executable && size>MALI_VA_EXEC_LINE))return 0;
 uint64_t address=mali_va_alloc(&addresses,size,executable);
 if(address)va_account((int64_t)size);
 return address;
}
bool hakios_mali_map(uint64_t address,uint64_t physical) {
 if(!hakios_mali_ready()) {
  printf("HakiosMali: map failed: not ready\n");
  return false;
 }
 if(address<MALI_VA_FIRST || address>=MALI_VA_END) {
  printf("HakiosMali: map failed: va 0x%llx outside range [0x%llx, 0x%llx)\n",
         address, (uint64_t)MALI_VA_FIRST, (uint64_t)MALI_VA_END);
  return false;
 }
 if(physical<0x100000000ull || physical>=0x1b7e00000ull) {
  printf("HakiosMali: map failed: pa 0x%llx outside RAM\n", physical);
  return false;
 }
 if(!pmap_valid_address(physical)) {
  printf("HakiosMali: map failed: !pmap_valid_address(0x%llx)\n", physical);
  return false;
 }
 lck_mtx_lock(vm_lock);
 int err = beg_gpu_vm_map(&space,address,physical);
 lck_mtx_unlock(vm_lock);
 if(err != 0) {
  printf("HakiosMali: map failed: beg_gpu_vm_map(va=0x%llx, pa=0x%llx) err=%d tables=%u/%u\n",
         address, physical, err, space.count, space.limit);
  return false;
 }
 return true;
}
static int init_gpu_hardware(void) {
 wr(0xf04,rd(0xf04)|(1u<<17));wr(0xf00,(rd(0xf00)&~(0x3fu<<16))|(15u<<16));
 wr(0x1a0,1);wr(0x190,1);wr(0x180,15);
 if(wait(0x160,1,1) || wait(0x150,1,1) || wait(0x140,15,15))return -1;
 if(space.count && space.tables) {
  uint64_t root=space.tables[0].physical;
  wr(0x2400,(uint32_t)root);wr(0x2404,root>>32);
  wr(0x2408,0x88888888);wr(0x240c,0x88888888);
  wr(0x2430,BEG_GPU_TRANSCFG);wr(0x2434,0);wr(0x2418,1);
  if(wait(0x2428,1,0))return -1;
 }
 return 0;
}
/* vm_lock held, the GPU idle. */
static void flush_tables(void) {
 bool any = false;
 for(unsigned i=0;i<space.count;i++) {
  struct beg_gpu_vm_table *table=&space.tables[i];
  if(!table->dirty)continue;
  /* The GPU reads 512 entries per table regardless of XNU's page size. */
  FlushPoC_DcacheRegion((vm_offset_t)table->entries,512*sizeof(*table->entries));
  table->dirty=false;
  any = true;
 }
 if(any && gpu) {
  uint64_t root=space.tables[0].physical;
  wr(0x2400,(uint32_t)root);wr(0x2404,root>>32);
  /* x65: with the memory attributes and the translation's configuration, as init_gpu_hardware() writes them. That
   * one runs before there is any table and then writes none of this, so the first job of every boot ran with the
   * address space not set up, failed after 400 ms with JS_STATUS 0x42 (it could not read its own chain) and the GPU
   * was reset; only the recovery then set it up (x62, x63: "BEGONIA GPU FAILURES: ... job 1"). */
  wr(0x2408,0x88888888);wr(0x240c,0x88888888);
  wr(0x2430,BEG_GPU_TRANSCFG);wr(0x2434,0);
  wr(0x2418,1);
  (void)wait(0x2428,1,0);
 }
}
void hakios_mali_mapped(void) {
 /* queued jobs: the thread that runs them does this before each one (run_job) */
 if(async_jobs)return;
 lck_mtx_lock(vm_lock);
 flush_tables();
 lck_mtx_unlock(vm_lock);
}
void hakios_mali_unmap(uint64_t address,uint64_t size) {
 if(!hakios_mali_ready() || !size || address<MALI_VA_FIRST || address>=MALI_VA_END || size>MALI_VA_END-address)return;
 va_account(-(int64_t)size);
 lck_mtx_lock(vm_lock);
 for(uint64_t off=0;off<size;off+=4096)(void)beg_gpu_vm_unmap(&space,address+off);
 lck_mtx_unlock(vm_lock);
 mali_va_free(&addresses,address,size);
}
void hakios_mali_tag(uint64_t address,uint32_t kind,uint32_t width,uint32_t height,uint32_t row,
 uint32_t format,uint32_t offset) {
 (void)address;(void)kind;(void)width;(void)height;(void)row;(void)format;(void)offset;
}
/*
 * y292: the GPU's own counters (PRFCNT), boot-arg begonia_prfcnt=1. Where a pixel job's time goes is not seen from
 * outside: cycles its cores execute, quads, texture stalls, beats read from and written to memory outside the GPU's
 * caches. A page of the kernel is mapped for the GPU; between jobs (the GPU idle, this thread alone at its registers),
 * at most five times a second, the counters are sampled into it, added up here and cleared. Said with the 5-second
 * statistics: blocks J (job manager), T (tiler), M (MMU and L2), S (the shader cores, summed), 64 words each, the first
 * four a header. Three samples that never complete turn it off.
 */
static uint32_t prf_on;static vm_offset_t prf_page;static uint64_t prf_phys,prf_va,prf_last;static int prf_state;
static uint64_t prf_sum[4][64];static uint32_t prf_samples,prf_bad;
static void prf_program(void) {
 wr(0x68,0);
 wr(0x60,(uint32_t)prf_va);wr(0x64,(uint32_t)(prf_va>>32));
 wr(0x6c,0xffffffff);wr(0x70,0xffffffff);wr(0x7c,0xffffffff);wr(0x74,0xffffffff);
 wr(0x68,1);
 wr(0x30,3);
}
static void prf_start(void) {
 prf_state=-1;
 if(!PE_parse_boot_argn("begonia_prfcnt",&prf_on,sizeof prf_on) || !prf_on)return;
 if(kmem_alloc(kernel_map,&prf_page,PAGE_SIZE,KMA_KOBJECT|KMA_ZERO|KMA_DATA,VM_KERN_MEMORY_DIAG)!=KERN_SUCCESS)return;
 prf_phys=kvtophys(prf_page);
 if(prf_phys<0x100000000ull || prf_phys>=0x1b7e00000ull){begonia_perf_note("BEGONIA GPU PRFCNT: page at %llx, not used",prf_phys);return;}
 prf_va=mali_va_alloc(&addresses,4096,false);
 if(!prf_va)return;
 lck_mtx_lock(vm_lock);
 int err=beg_gpu_vm_map(&space,prf_va,prf_phys);
 flush_tables();
 lck_mtx_unlock(vm_lock);
 if(err){begonia_perf_note("BEGONIA GPU PRFCNT: map failed %d",err);return;}
 prf_program();
 prf_state=1;
 begonia_perf_note("BEGONIA GPU PRFCNT: on, va %llx pa %llx",prf_va,prf_phys);
}
static void prf_sample(void) {
 if(!prf_state)prf_start();
 if(prf_state!=1)return;
 uint64_t now=mach_absolute_time(),ns;
 absolutetime_to_nanoseconds(now-prf_last,&ns);
 if(prf_last && ns<200000000ull)return;
 prf_last=now;
 if((rd(0x68)&15)!=1){prf_program();return;}     /* after a reset of the GPU */
 FlushPoC_DcacheRegion(prf_page,4096);
 wr(0x24,1u<<16);
 wr(0x30,4);
 unsigned i;
 for(i=0;i<2000;i++){if(rd(0x20)&(1u<<16))break;delay(5);}
 wr(0x24,1u<<16);
 if(i==2000) {
  if(++prf_bad>=3){prf_state=-1;wr(0x68,0);begonia_perf_note("BEGONIA GPU PRFCNT: a sample never completed; off");}
  return;
 }
 FlushPoC_DcacheRegion(prf_page,4096);
 const volatile uint32_t *w=(const volatile uint32_t *)prf_page;
 for(unsigned b=0;b<7;b++)for(unsigned c=0;c<64;c++)prf_sum[b<3?b:3][c]+=w[b*64+c];
 prf_samples++;
 wr(0x30,3);
}
static void prf_say(void) {
 if(prf_state!=1 || !prf_samples)return;
 static const char names[4]={'J','T','M','S'};
 for(unsigned b=0;b<4;b++)for(unsigned c=4;c<64;c+=12) {
  const uint64_t *v=prf_sum[b]+c;
  printf("BEGONIA PRF %c%02u n%u: %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu\n",names[b],c,prf_samples,
   v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8],v[9],v[10],v[11]);
 }
 printf("BEGONIA PRF EN: J %llx T %llx M %llx S %llx\n",prf_sum[0][2],prf_sum[1][2],prf_sum[2][2],prf_sum[3][2]);
 for(unsigned b=0;b<4;b++)for(unsigned c=0;c<64;c++)prf_sum[b][c]=0;
 prf_samples=0;
}
static void say_submitters(void);
static void job_stats(uint64_t began,uint64_t ended,bool fragment,uint64_t queued,unsigned depth) {
 uint64_t took=ended-began,ns;
 stat_jobs++;stat_fragment+=fragment;stat_busy+=took;stat_waited+=began-queued;
 if(fragment)stat_busy_fragment+=took;      /* x45: the pixel jobs' share of the GPU's time */
 if(fragment) {                             /* x48: how long the pixel jobs are: a few long ones or many middling */
  static const uint64_t bounds_us[5]={200,500,1000,2000,4000};
  uint64_t us;unsigned k=0;
  absolutetime_to_nanoseconds(took,&us);us/=1000;
  while(k<5 && us>=bounds_us[k])k++;
  stat_sizes[k]++;stat_size_us[k]+=us;
 }
 if(took>stat_longest)stat_longest=took;
 if(depth>stat_deepest)stat_deepest=depth;
 if(!stat_since)stat_since=began;
 absolutetime_to_nanoseconds(ended-stat_since,&ns);
 if(ns<5000000000ull)return;
 uint64_t busy,longest,waited;
 absolutetime_to_nanoseconds(stat_busy,&busy);absolutetime_to_nanoseconds(stat_longest,&longest);
 absolutetime_to_nanoseconds(stat_waited,&waited);
 { extern void HakiosMaliCacheSecond(void); HakiosMaliCacheSecond(); }   /* x35: with mcp=1, begonia.mali.cpp's own counts */
 { extern void begonia_dram_said(void); begonia_dram_said(); }           /* x49: with begonia_dram_probe=1 */
 if(fail_count)printf("BEGONIA GPU FAILURES: %u so far; the last: job %llu (%s) %llu ms after boot, after %llu us: JS_STATUS %#x, "
  "job irq %x, mmu irq %x at %llx, chain %llx, head %llx, tail %llx\n",fail_count,fail_seq,fail_slot?"vertex/tiler":"fragment",
  fail_at/1000000,fail_took/1000,fail_status,fail_irq,fail_mmu,fail_fault,fail_chain,fail_head,fail_tail);
 { extern int begonia_notes_urgent; extern void begonia_perf_notes_say(void); if(begonia_notes_urgent)begonia_perf_notes_say(); }
 uint64_t sleeps=__atomic_exchange_n(&stat_sleeps,0,__ATOMIC_RELAXED),slept;
 absolutetime_to_nanoseconds(__atomic_exchange_n(&stat_slept,0,__ATOMIC_RELAXED),&slept);
 { uint64_t pixels;absolutetime_to_nanoseconds(stat_busy_fragment,&pixels);
   printf("BEGONIA GPU JOBS: fragment %llu, %llu us each; the others (vertex and tiler) %llu, %llu us each\n",stat_fragment,
    stat_fragment?pixels/1000/stat_fragment:0,stat_jobs-stat_fragment,
    stat_jobs>stat_fragment?(busy-pixels)/1000/(stat_jobs-stat_fragment):0);stat_busy_fragment=0; }
 printf("BEGONIA GPU SIZES: fragment jobs, count/ms in all: under 200 us %llu/%llu, to 500 %llu/%llu, to 1000 %llu/%llu, "
  "to 2000 %llu/%llu, to 4000 %llu/%llu, longer %llu/%llu\n",stat_sizes[0],stat_size_us[0]/1000,stat_sizes[1],
  stat_size_us[1]/1000,stat_sizes[2],stat_size_us[2]/1000,stat_sizes[3],stat_size_us[3]/1000,stat_sizes[4],
  stat_size_us[4]/1000,stat_sizes[5],stat_size_us[5]/1000);
 for(unsigned k=0;k<6;k++)stat_sizes[k]=stat_size_us[k]=0;
 printf("BEGONIA GPU WAITS: callers slept for a job %llu times, %llu ms in all (of %llu ms); jobs run on processor %d "
  "(begonia_gpu_cpu=%d: %s)\n",sleeps,slept/1000000,ns/1000000,cpu_number(),job_cpu,
  job_bound<0?"not asked":job_bound?"bound":"NOT bound");
 printf("BEGONIA GPU SERVICE: %llu jobs (%llu fragment) in %llu ms: %llu us each on the GPU, the longest %llu; "
  "GPU busy %llu%%; queued %llu us each before starting, queue up to %u deep; %s\n",stat_jobs,stat_fragment,
  ns/1000000,busy/1000/stat_jobs,longest/1000,busy*100/ns,waited/1000/stat_jobs,stat_deepest,
  async_jobs?"queued":"run in the submit");
 printf("BEGONIA GPU SLOT1: chains with a compute job %llu, %llu ms; vertex/tiler only %llu, %llu ms; other %llu, %llu ms\n",
  s1_kind_jobs[0],s1_kind_us[0]/1000,s1_kind_jobs[1],s1_kind_us[1]/1000,s1_kind_jobs[2],s1_kind_us[2]/1000);
 printf("BEGONIA GPU SLOT1 TYPES: jobs in slot 1's chains %llu: null %llu, write %llu, flush %llu, compute %llu, vertex %llu, "
  "geometry %llu, tiler %llu, fused %llu, other %llu\n",s1_chain_len_sum,s1_types[1],s1_types[2],s1_types[3],s1_types[4],
  s1_types[5],s1_types[6],s1_types[7],s1_types[8],
  s1_chain_len_sum-s1_types[1]-s1_types[2]-s1_types[3]-s1_types[4]-s1_types[5]-s1_types[6]-s1_types[7]-s1_types[8]);
 { uint64_t pre,between;absolutetime_to_nanoseconds(gap_pre,&pre);absolutetime_to_nanoseconds(gap_between,&between);
   printf("BEGONIA GPU GAPS: before a job (tables, locks) %llu us each over %llu jobs; between jobs with a queue behind "
    "%llu us each over %llu\n",gap_pre_n?pre/1000/gap_pre_n:0,gap_pre_n,gap_between_n?between/1000/gap_between_n:0,gap_between_n);
   gap_pre=gap_pre_n=gap_between=gap_between_n=0; }
 printf("BEGONIA GPU SLOT1 SAMPLES: [%s] [%s] [%s] [%s]\n",s1_sample[0],s1_sample[1],s1_sample[2],s1_sample[3]);
 { /* y281: the memory the window server's kills are about (pages of 16 KB: free, active, inactive, wired, speculative, purgeable) */
   extern unsigned int vm_page_free_count,vm_page_active_count,vm_page_inactive_count,vm_page_wire_count,vm_page_speculative_count,vm_page_purgeable_count;
   printf("BEGONIA MEM: pages free %u (lowest %u) active %u inactive %u wired %u speculative %u purgeable %u\n",vm_page_free_count,mem_min_free,
    vm_page_active_count,vm_page_inactive_count,vm_page_wire_count,vm_page_speculative_count,vm_page_purgeable_count);
   /* kept with the notes (they outlive the 25 s of the log) when the memory got low: under 30000 pages (~470 MB) free */
   if(mem_min_free<30000)begonia_perf_note("MEM LOW: lowest free %u pages, now free %u active %u inactive %u wired %u",mem_min_free,
    vm_page_free_count,vm_page_active_count,vm_page_inactive_count,vm_page_wire_count);
   mem_min_free=~0u;
   memn=0;memp[0]=0;proc_iterate(1,mem_proc_cb,NULL,NULL,NULL);
   printf("BEGONIA MEM PROCS:%s\n",memp);
   { char line[300];int o=snprintf(line,sizeof line,"BEGONIA GPU VA: live %lld MB;",(long long)(va_live>>20));
     for(int i=0;i<8;i++)if(va_proc[i].net>=(1<<20) && o<(int)sizeof(line)-50)
      o+=snprintf(line+o,sizeof(line)-o," %s[%d] %lld MB;",va_proc[i].name,va_proc[i].pid,(long long)(va_proc[i].net>>20));
     printf("%s\n",line); } }
 { /* y292: what the pages are: file-backed and anonymous pageable ones, and what the compressor holds */
   extern unsigned int vm_page_pageable_internal_count,vm_page_pageable_external_count;extern int32_t c_segment_pages_compressed;
   printf("BEGONIA MEM KINDS: pageable anonymous %u, file-backed %u, held compressed %d (pages of 16 KB)\n",
    vm_page_pageable_internal_count,vm_page_pageable_external_count,c_segment_pages_compressed); }
 prf_say();
 for(unsigned k=0;k<16;k++)s1_types[k]=0;s1_chain_len_sum=0;
 for(unsigned k=0;k<3;k++)s1_kind_jobs[k]=s1_kind_us[k]=0;
 stat_jobs=stat_fragment=stat_busy=stat_longest=stat_waited=stat_deepest=0;stat_since=ended;
 say_submitters();
 if(slow_took) {
  uint64_t us;absolutetime_to_nanoseconds(slow_took,&us);us/=1000;
  for(unsigned row=0;row<6;row++) {
   const uint32_t *w=slow_words+12*row;
   printf("BEGONIA GPU FBD %u (%llu us, %llx): %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",row,us,slow_fbd,
    w[0],w[1],w[2],w[3],w[4],w[5],w[6],w[7],w[8],w[9],w[10],w[11]);
  }
  slow_took=0;
 }
}
/* One job on the GPU, start to finish; nobody else touches its registers meanwhile. 1: done. */
static void note_slow(uint64_t chain,uint64_t took) {
 if(took<=slow_took)return;
 lck_mtx_lock(vm_lock);
 uint64_t at=beg_gpu_vm_translate(&space,chain+40);
 uint64_t fbd=at?(ml_phys_read_word_64(at)|((uint64_t)ml_phys_read_word_64(at+4)<<32)):0;
 for(unsigned i=0;i<72;i++) {
  uint64_t word=fbd?beg_gpu_vm_translate(&space,(fbd&~63ull)+4*i):0;
  slow_words[i]=word?ml_phys_read_word_64(word):0xdeadbeef;
 }
 lck_mtx_unlock(vm_lock);
 slow_took=took;slow_fbd=fbd;
}
/* y260: 0 a compute job (type 4) in the chain, 1 vertex/tiler jobs alone (5, 6, 7, 8) and writes/flushes (2, 3), 2 else.
 * Job header: word 4 bits 1..7 the type, words 6 and 7 the next job. */
static unsigned chain_kind(uint64_t chain) {
 unsigned kind=1;char pat[72];unsigned pn=0;
 lck_mtx_lock(vm_lock);
 for(unsigned n=0;chain && n<64;n++) {
  uint64_t at=beg_gpu_vm_translate(&space,chain);
  if(!at){kind=2;break;}
  unsigned type=(ml_phys_read_word_64(at+16)>>1)&0x7f;
  uint64_t next=ml_phys_read_word_64(at+24)|((uint64_t)ml_phys_read_word_64(at+28)<<32);
  s1_types[type&15]++;s1_chain_len_sum++;if(pn<68){pat[pn++]=(char)(type<10?48+type:65+type-10);pat[pn++]=' ';}
  if(type==4)kind=0;
  if(type!=2 && type!=3 && type!=4 && type!=5 && type!=6 && type!=7 && type!=8 && kind!=0)kind=2;
  chain=next;
 }
 lck_mtx_unlock(vm_lock);
 { static unsigned seen; if(pn && (++seen%701)==0){pat[pn]=0;memcpy(s1_sample[s1_samples%4],pat,pn+1);s1_samples++;} }
 return kind;
}
static int run_job(uint64_t chain,bool fragment,uint64_t seq,uint64_t queued,unsigned depth) {
 int success=0;
 uint64_t entered=mach_absolute_time();
 if(gap_last_end && depth>1){gap_between+=entered-gap_last_end;gap_between_n++;}
 lck_mtx_lock(vm_lock);
 flush_tables();
 lck_mtx_unlock(vm_lock);

 unsigned slot=fragment?0:1,base=0x1800+slot*0x80;
 uint64_t began=mach_absolute_time();
 gap_pre+=began-entered;gap_pre_n++;
 wr(0x2004,0xffffffff);wr(0x1008,0);wr(0x1004,0xffffffff);
 wr(base+0x40,(uint32_t)chain);wr(base+0x44,chain>>32);
 wr(base+0x50,15);wr(base+0x54,0);wr(base+0x58,(3u<<8)|(3u<<12)|(8u<<16));
 wr(base+0x60,1);
 if(!wait_job(0x1000,(1u<<slot)|(1u<<(slot+16)),1u<<slot) && !rd(0x241c)) {
  success=1;
 } else {
  static unsigned failures;
  /* x62: the slot's own account of it (JS_STATUS: 0x58 a data fault, 0x42/0x43 a read or write the bus refused, 0x60
   * out of tiler memory, ...; nothing there and no interrupt: the job did not end in the 100 ms it is waited for),
   * kept for the 5 s report: the window server starts anew after one (x46, x52, x61) and the log loses the moment. */
  fail_count=failures+1;fail_seq=seq;fail_slot=slot;fail_irq=rd(0x1000);fail_mmu=rd(0x241c);fail_status=rd(base+0x24);
  fail_head=rd(base)|((uint64_t)rd(base+4)<<32);fail_tail=rd(base+8)|((uint64_t)rd(base+12)<<32);
  fail_fault=rd(0x2420)|((uint64_t)rd(0x2424)<<32);fail_chain=chain;
  absolutetime_to_nanoseconds(mach_absolute_time(),&fail_at);absolutetime_to_nanoseconds(mach_absolute_time()-began,&fail_took);
  if(failures++<4)begonia_perf_note("BEGONIA GPU SERVICE: job%llu slot%u irq=%x mmu=%x FAILED (%u)",seq,slot,rd(0x1000),rd(0x241c),failures);
  printf("BEGONIA GPU SERVICE: job%llu slot%u irq=%x mmu=%x FAILED; recovering\n",seq,slot,rd(0x1000),rd(0x241c));
  if(reset() || init_gpu_hardware()) {
   printf("BEGONIA GPU SERVICE: hardware recovery failed\n");
  }
 }
 { uint64_t ended=mach_absolute_time();gap_last_end=ended;{extern unsigned int vm_page_free_count;unsigned f=vm_page_free_count;if(f<mem_min_free)mem_min_free=f;}if(fragment && success)note_slow(chain,ended-began);
   if(!fragment && success){unsigned k=chain_kind(chain);uint64_t us;absolutetime_to_nanoseconds(ended-began,&us);s1_kind_jobs[k]++;s1_kind_us[k]+=us/1000;}
   job_stats(began,ended,fragment,queued,depth); }
 prf_sample();
 return success;
}
/*
 * x34: the thread that runs the jobs waits for each by polling the GPU (wait(): delay(10)), at kernel priority.
 * Woken by a submit, it tended to run where the submitter ran -- the window server's drawing thread, which is
 * bound to processor 6 (gl_cpu) and so could not go elsewhere while the job ran: x33, 74 us a draw and the
 * drawing thread off its processor most of the time (x32 glprof). Boot-arg begonia_gpu_cpu=N: this thread on
 * processor N; default 4, an A55 (XNU's 5 and 6 are the A76s); -1: wherever the scheduler puts it.
 */
extern kern_return_t sysctl_thread_bind_cpuid(int32_t cpuid);
static void job_thread(void *argument,wait_result_t result) {
 (void)argument;(void)result;
 (void)PE_parse_boot_argn("begonia_gpu_cpu",&job_cpu,sizeof job_cpu);
 if(job_cpu>=0)job_bound=sysctl_thread_bind_cpuid(job_cpu)==KERN_SUCCESS;
 for(;;) {
  lck_mtx_lock(queue_lock);
  while(queue_head==queue_tail)
   (void)lck_mtx_sleep(queue_lock,LCK_SLEEP_DEFAULT,(event_t)&queue_tail,THREAD_UNINT);
  uint64_t chain=queue[queue_head%JOB_QUEUE].chain,seq=queue[queue_head%JOB_QUEUE].seq;
  bool fragment=queue[queue_head%JOB_QUEUE].fragment;uint64_t queued=queue[queue_head%JOB_QUEUE].queued;
  unsigned depth=(unsigned)(queue_tail-queue_head);
  lck_mtx_unlock(queue_lock);
  int success=run_job(chain,fragment,seq,queued,depth);
  lck_mtx_lock(queue_lock);
  queue_head++;
  if(!success)__atomic_store_n(&failed,seq,__ATOMIC_RELEASE);
  __atomic_store_n(&completed,seq,__ATOMIC_RELEASE);
  thread_wakeup((event_t)&completed);
  thread_wakeup((event_t)&queue_head);
  lck_mtx_unlock(queue_lock);
 }
}
/*
 * y117: with YouTube open the GPU was 82-91% busy with 4000-5000 jobs a second and a queue 349 deep; whose jobs?
 * Counted by the process that submits (the submit runs on its thread), said with the 5-second statistics.
 */
extern int proc_selfpid(void);
extern void proc_selfname(char *buf,int size);
static struct {int pid;char name[20];uint32_t jobs,fragments;} by_proc[8];
static void count_submitter(bool fragment) {
 int pid=proc_selfpid(),slot=-1,least=0;
 for(int i=0;i<8;i++) {
  if(by_proc[i].pid==pid){slot=i;break;}
  if(by_proc[i].jobs<by_proc[least].jobs)least=i;
 }
 if(slot<0){slot=least;by_proc[slot].pid=pid;by_proc[slot].jobs=by_proc[slot].fragments=0;proc_selfname(by_proc[slot].name,sizeof(by_proc[slot].name));}
 by_proc[slot].jobs++;by_proc[slot].fragments+=fragment;
}
static void say_submitters(void) {
 char line[400];int o=0;
 for(int i=0;i<8;i++)if(by_proc[i].jobs && o<(int)sizeof(line)-60)
  o+=snprintf(line+o,sizeof(line)-o," %s[%d] %u/%u;",by_proc[i].name,by_proc[i].pid,by_proc[i].jobs,by_proc[i].fragments);
 if(o)printf("BEGONIA GPU BY PROCESS (jobs/fragment in 5 s):%s\n",line);
 for(int i=0;i<8;i++)by_proc[i].jobs=by_proc[i].fragments=0;
}
uint64_t hakios_mali_enqueue(uint64_t chain,bool fragment) {
 if(!hakios_mali_ready() || !space.count || !chain || (chain&63))return 0;
 count_submitter(fragment);
 lck_mtx_lock(vm_lock);
 uint64_t mapped=beg_gpu_vm_translate(&space,chain);
 lck_mtx_unlock(vm_lock);
 if(!mapped)return 0;
 if(!async_jobs) {
  uint64_t seq=++submitted;
  int success=run_job(chain,fragment,seq,mach_absolute_time(),0);
  if(!success)__atomic_store_n(&failed,seq,__ATOMIC_RELEASE);
  __atomic_store_n(&completed,seq,__ATOMIC_RELEASE);
  return success?seq:0;
 }
 lck_mtx_lock(queue_lock);
 while(queue_tail-queue_head>=JOB_QUEUE)
  (void)lck_mtx_sleep(queue_lock,LCK_SLEEP_DEFAULT,(event_t)&queue_head,THREAD_UNINT);
 uint64_t seq=++submitted;
 queue[queue_tail%JOB_QUEUE].chain=chain;queue[queue_tail%JOB_QUEUE].seq=seq;
 queue[queue_tail%JOB_QUEUE].fragment=fragment;queue[queue_tail%JOB_QUEUE].queued=mach_absolute_time();
 queue_tail++;
 thread_wakeup((event_t)&queue_tail);
 lck_mtx_unlock(queue_lock);
 return seq;
}
uint32_t hakios_mali_submit(uint64_t chain,bool fragment) {return (uint32_t)hakios_mali_enqueue(chain,fragment);}
/* y264: the number of the last job handed to the service (the display driver waits for it: boot-arg fb_gpuseq=1). */
uint64_t hakios_mali_submitted_seq(void) {return __atomic_load_n(&submitted,__ATOMIC_ACQUIRE);}
/* Job `seq` done, and well, within `timeout` nanoseconds (0: is it done now). */
bool hakios_mali_wait(uint64_t seq,uint64_t timeout) {
 if(!seq)return true;
 if(!hakios_mali_ready())return false;
 if(async_jobs && timeout && seq>__atomic_load_n(&completed,__ATOMIC_ACQUIRE)) {
  uint64_t interval,deadline;
  nanoseconds_to_absolutetime(timeout,&interval);
  deadline=mach_absolute_time()+interval;
  if(deadline<interval)deadline=~0ull;
  uint64_t asleep=mach_absolute_time();
  lck_mtx_lock(queue_lock);
  while(seq>completed && hakios_mali_ready()) {
   if(lck_mtx_sleep_deadline(queue_lock,LCK_SLEEP_DEFAULT,(event_t)&completed,THREAD_UNINT,deadline)==THREAD_TIMED_OUT)break;
  }
  lck_mtx_unlock(queue_lock);
  __atomic_fetch_add(&stat_sleeps,1,__ATOMIC_RELAXED);
  __atomic_fetch_add(&stat_slept,mach_absolute_time()-asleep,__ATOMIC_RELAXED);
 }
 /*
  * begonia (y43): a job that failed has ended too. Answered "not done", a program's wait for it (WAIT_BO: a timeout) was
  * asked again for ever -- a Metal program froze on the fragment job of a multisampled pass that faulted -- and the
  * buffers it named were never freed. The failure is said (BEGONIA GPU FAILURES) and the GPU recovered; what the job
  * should have written is not there, which is no worse than a program that never wakes.
  */
 return seq<=__atomic_load_n(&completed,__ATOMIC_ACQUIRE);
}
void begonia_mali_service_begin(vm_offset_t registers) {
 uint32_t enabled=0;
 if(!PE_parse_boot_argn("begonia_gpu_service",&enabled,sizeof enabled) || enabled!=1 || gpu)return;
 gpu=registers;
 if(rd(0)>>16!=0x7211 || reset()) {begonia_perf_note("BEGONIA GPU SERVICE: identity or reset failed; not started");return;}
 gpu_group=lck_grp_alloc_init("begonia.gpu",LCK_GRP_ATTR_NULL);
 vm_lock=lck_mtx_alloc_init(gpu_group,LCK_ATTR_NULL);
 queue_lock=lck_mtx_alloc_init(gpu_group,LCK_ATTR_NULL);
 if(!gpu_group || !vm_lock || !queue_lock)return;
 (void)PE_parse_boot_argn("begonia_gpu_async",&async_jobs,sizeof async_jobs);
 vm_offset_t tbls = 0;
 size_t tbls_size = (sizeof(struct beg_gpu_vm_table) * BEG_GPU_VM_MAX_TABLES + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
 if(kmem_alloc(kernel_map, &tbls, tbls_size, KMA_KOBJECT|KMA_ZERO|KMA_DATA, VM_KERN_MEMORY_DIAG) != KERN_SUCCESS) {
  printf("HakiosMali: failed to allocate VM table metadata\n");
  return;
 }
 bzero((void *)tbls, tbls_size);
 space.tables = (struct beg_gpu_vm_table *)tbls;
 space.limit=BEG_GPU_VM_MAX_TABLES;space.allocate=allocate_table;
 if(init_gpu_hardware()) {
  begonia_perf_note("HakiosMali: init_gpu_hardware failed");
  return;
 }
 if(async_jobs) {
  thread_t thread=THREAD_NULL;
  if(kernel_thread_start(job_thread,NULL,&thread)!=KERN_SUCCESS) {
   printf("BEGONIA GPU SERVICE: no job thread; jobs run in the submit\n");async_jobs=0;
  } else {
   thread_set_thread_name(thread,"begonia.gpu.jobs");
   thread_deallocate(thread);
  }
 }
 __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
 begonia_perf_note("BEGONIA GPU SERVICE: ready; jobs %s",async_jobs?"queued to a kernel thread":"run inside the submit");
 HakiosMaliStart();
}
