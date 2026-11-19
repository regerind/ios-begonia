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
static int job_bound=-1;
static uint32_t rd(unsigned o) {return *(volatile uint32_t *)(gpu+o);}
static void wr(unsigned o,uint32_t v) {*(volatile uint32_t *)(gpu+o)=v;__asm__ volatile("dsb sy":::"memory");}
static int wait(unsigned o,uint32_t mask,uint32_t value) {
 for(unsigned i=0;i<10000;i++) {if((rd(o)&mask)==value)return 0;delay(10);}
 return -1;
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
uint64_t hakios_mali_address(uint64_t size,bool executable) {
 if(!hakios_mali_ready() || !size || size>(512ull<<20) || (executable && size>MALI_VA_EXEC_LINE))return 0;
 return mali_va_alloc(&addresses,size,executable);
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
 lck_mtx_lock(vm_lock);
 for(uint64_t off=0;off<size;off+=4096)(void)beg_gpu_vm_unmap(&space,address+off);
 lck_mtx_unlock(vm_lock);
 mali_va_free(&addresses,address,size);
}
void hakios_mali_tag(uint64_t address,uint32_t kind,uint32_t width,uint32_t height,uint32_t row,
 uint32_t format,uint32_t offset) {
 (void)address;(void)kind;(void)width;(void)height;(void)row;(void)format;(void)offset;
}
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
 stat_jobs=stat_fragment=stat_busy=stat_longest=stat_waited=stat_deepest=0;stat_since=ended;
}
/* One job on the GPU, start to finish; nobody else touches its registers meanwhile. 1: done. */
static int run_job(uint64_t chain,bool fragment,uint64_t seq,uint64_t queued,unsigned depth) {
 int success=0;
 lck_mtx_lock(vm_lock);
 flush_tables();
 lck_mtx_unlock(vm_lock);

 unsigned slot=fragment?0:1,base=0x1800+slot*0x80;
 uint64_t began=mach_absolute_time();
 wr(0x2004,0xffffffff);wr(0x1008,0);wr(0x1004,0xffffffff);
 wr(base+0x40,(uint32_t)chain);wr(base+0x44,chain>>32);
 wr(base+0x50,15);wr(base+0x54,0);wr(base+0x58,(3u<<8)|(3u<<12)|(8u<<16));
 wr(base+0x60,1);
 if(!wait(0x1000,(1u<<slot)|(1u<<(slot+16)),1u<<slot) && !rd(0x241c)) {
  success=1;
 } else {
  static unsigned failures;
  if(failures++<4)begonia_perf_note("BEGONIA GPU SERVICE: job%llu slot%u irq=%x mmu=%x FAILED (%u)",seq,slot,rd(0x1000),rd(0x241c),failures);
  printf("BEGONIA GPU SERVICE: job%llu slot%u irq=%x mmu=%x FAILED; recovering\n",seq,slot,rd(0x1000),rd(0x241c));
  if(reset() || init_gpu_hardware()) {
   printf("BEGONIA GPU SERVICE: hardware recovery failed\n");
  }
 }
 job_stats(began,mach_absolute_time(),fragment,queued,depth);
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
uint64_t hakios_mali_enqueue(uint64_t chain,bool fragment) {
 if(!hakios_mali_ready() || !space.count || !chain || (chain&63))return 0;
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
 return seq<=__atomic_load_n(&completed,__ATOMIC_ACQUIRE) && seq!=__atomic_load_n(&failed,__ATOMIC_ACQUIRE);
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
