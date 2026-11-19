#!/usr/bin/env python3
"""Adapt pinned S9+ IOKit transport to begonia's existing MiniIOSurface layout."""
from pathlib import Path
import shutil,subprocess
root=Path(__file__).resolve().parents[1];reference=root/'upstream/ios-s9plus-exynos'
if subprocess.check_output(['git','-C',str(reference),'rev-parse','HEAD'],text=True).strip()!='2d092398040f98685b0c5a07776df5e9f199b9dc':
    raise SystemExit('S9+ reference revision mismatch')
source=reference/'core/drivers/gpu';tree=root/'upstream/hakios-xnu-kernel/xnu-src/xnu-8796.121.2'
text=(source/'hakios.mali.cpp').read_text()
def change(before,after):
    global text
    if text.count(before)!=1:raise SystemExit('Reference context changed: '+before)
    text=text.replace(before,after)
change('&& s->scanPhys == 0','')
change('|| surface->scanPhys ||','||')
change('memory = s->scan != NULL ? IOSubMemoryDescriptor::withSubRange(s->scan, 0, size, kIODirectionInOut) :\n\t    IOSubMemoryDescriptor::withSubRange(s->memory, PAGE_SIZE, size, kIODirectionInOut);',
       'memory = IOSubMemoryDescriptor::withSubRange(s->memory, PAGE_SIZE, size, kIODirectionInOut);')
change('b = s->scan != NULL ? newBuffer(s->scan, 0, size, &request->handle, PANFROST_BO_NOEXEC) :\n\t    newBuffer(s->memory, PAGE_SIZE, size, &request->handle, PANFROST_BO_NOEXEC);',
       'b = newBuffer(s->memory, PAGE_SIZE, size, &request->handle, PANFROST_BO_NOEXEC);')
change('*options = b->surface != NULL && b->surface->scan != NULL ? kIOMapWriteCombineCache : 0;', '*options = 0;')
change('\t\tfSubmits++;','''        if (!r.submit.jc || !r.submit.bo_handle_count || r.submit.bo_handle_count > MALI_BUFFERS ||
            (r.submit.requirements & ~PANFROST_JD_REQ_FS)) {
            ret = kIOReturnBadArgument; break;
        }
        bool ownsChain = false;
        for (uint32_t i = 0; i < r.submit.bo_handle_count; i++) {
            uint32_t h;
            if (copyin((user_addr_t)(r.submit.bo_handles + i * sizeof(h)), &h, sizeof(h)) ||
                (b = buffer(h)) == NULL) { ret = kIOReturnBadArgument; break; }
            if (b->size >= 32 && r.submit.jc >= b->address && r.submit.jc - b->address <= b->size - 32)
                ownsChain = true;
        }
        if (ret != kIOReturnSuccess) break;
        if (!ownsChain) { ret = kIOReturnBadArgument; break; }
\t\tfSubmits++;''')
change('\t\tr.submit.jc = seq;', '\t\tr.submit.jc = seq;\n        if (!seq) ret = kIOReturnIOError;')
change('\thakios_mali_unmap(b->address, b->size);','''\tif (!hakios_mali_ready()) {
        // A failed GPU reset must never release pages it can still write.
        // Keep descriptor/surface references until the phone resets.
        bzero(b, sizeof(*b));
        return;
    }
\thakios_mali_unmap(b->address, b->size);''')
# x47: mcs=1 by default (mcs=0: every surface a job names cleaned at every submit, as before). x35 measured 87-91% of
# the submit request's time in cleaning whole 12.7 MB display surfaces the processor had not written; x36 on, with
# mcs=1 (cleaned on first use and when the surface's seed has moved; wrapped memory always), nothing stale was seen.
change('static uint32_t gMaliCacheSeed;','static uint32_t gMaliCacheSeed = 1;')
# x35: the cache profile (boot-arg mcp=1) goes to the console, where the test sessions' log is read from
# (loader_log_line's log is not), with what each request costs: count and time by selector.
if text.count('loader_log_line(line);')!=3:raise SystemExit('Reference context changed: loader_log_line')
text=text.replace('loader_log_line(line);','IOLog("%s", line);')
change('extern "C" void\nHakiosMaliCacheSecond(void)\n{\n\tif (!gMaliCacheProfile) return;',r'''/* x35 (begonia): what each request costs -- count and time by selector, said with the cache profile */
static uint64_t gMaliCallCount[128], gMaliCallTime[128], gMaliCallCountWas[128], gMaliCallTimeWas[128];
struct MaliCallTimer {
    uint32_t selector;
    uint64_t began;
    MaliCallTimer(uint32_t s) : selector(s & 127), began(mach_absolute_time()) {}
    ~MaliCallTimer() {
        __atomic_fetch_add(&gMaliCallTime[selector], mach_absolute_time() - began, __ATOMIC_RELAXED);
        __atomic_fetch_add(&gMaliCallCount[selector], 1, __ATOMIC_RELAXED);
    }
};
static void
maliCallsSaid(void)
{
    static const struct { uint32_t selector; const char * name; } calls[] = {
        { 0x40, "submit" }, { 0x41, "wait-bo" }, { 0x42, "create" }, { 0x43, "mmap" }, { 0x45, "offset" },
        { 0x09, "close" }, { 0x7e, "surface" }, { 0x7c, "surface-seq" }, { 0x7d, "wait-seq" },
    };
    char line[400];
    size_t used = 0;

    line[0] = 0;
    for (unsigned i = 0; i < sizeof(calls) / sizeof(calls[0]) && used < sizeof(line) - 1; i++) {
        uint32_t s = calls[i].selector;
        uint64_t count = __atomic_load_n(&gMaliCallCount[s], __ATOMIC_RELAXED);
        uint64_t time = __atomic_load_n(&gMaliCallTime[s], __ATOMIC_RELAXED), ns;

        absolutetime_to_nanoseconds(time - gMaliCallTimeWas[s], &ns);
        used += snprintf(line + used, sizeof(line) - used, " %s %llu/%llu", calls[i].name, count - gMaliCallCountWas[s],
            ns / 1000);
        gMaliCallCountWas[s] = count;
        gMaliCallTimeWas[s] = time;
    }
    IOLog("mali-calls (count/us in all):%s\n", line);
}
extern "C" void
HakiosMaliCacheSecond(void)
{
	if (!gMaliCacheProfile) return;
	maliCallsSaid();''')
change('\tIOReturn ret = kIOReturnSuccess;\n\n\tif (args->structureInput == NULL || size > sizeof(r) || fBuffers == NULL) {',
       '\tIOReturn ret = kIOReturnSuccess;\n\tMaliCallTimer timed(selector);\n\n\tif (args->structureInput == NULL || size > sizeof(r) || fBuffers == NULL) {')
(tree/'iokit/Kernel/begonia.mali.cpp').write_text(text)
for name in ['hakios.mali_va.h','hakios.mali_cpu_cache.h']:
    shutil.copyfile(source/name,tree/'bsd/dev'/name)
shutil.copyfile(source/'hakios.mali_cpu_cache.h',tree/'iokit/Kernel/hakios.mali_cpu_cache.h')
print('Prepared attributed S9+ transport; Exynos power/clock driver excluded')
