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
# x57: a buffer closed while a job queued or running still names it is freed later, not waited for.
# freeBuffer() waited up to 2 s for the buffer's last job with the driver's lock held: x56 (glslow=4000) has
# glDeleteTextures over 4 ms 191 times in 35 s, 7 ms each, with the GPU's queue 10 to 15 ms deep.
change('\tstruct mali_cpu_cache_stamp cpuCache;\n};',
       '\tstruct mali_cpu_cache_stamp cpuCache;\n\tbool dying;                             /* closed by its client; freed when its last job has ended (reapBuffers()) */\n};')
change('\tuint32_t                fNextBuffer;\n',
       '\tuint32_t                fNextBuffer;\n\tuint16_t                fDying[512];            /* buffers closed and not freed yet */\n\tuint32_t                fDyingCount;\n\tbool                    fClosing;\n')
change('\tvoid freeBuffer(struct mali_buffer * b);\n',
       '\tvoid freeBuffer(struct mali_buffer * b);\n\tvoid reapBuffers(void);\n')
change('\tif (handle == 0 || handle > MALI_BUFFERS || fBuffers[handle - 1].memory == NULL) {\n\t\treturn NULL;\n\t}\n\treturn &fBuffers[handle - 1];',
       '\tif (handle == 0 || handle > MALI_BUFFERS || fBuffers[handle - 1].memory == NULL || fBuffers[handle - 1].dying) {\n\t\treturn NULL;\n\t}\n\treturn &fBuffers[handle - 1];')
change("\t/* not while a job queued or running may still use it (jobs are run in a queue) */\n\tif (b->last_seq != 0) {\n\t\t(void)hakios_mali_wait(b->last_seq, 2ull * NSEC_PER_SEC);\n\t}\n",
       "\t/* not while a job queued or running may still use it (jobs are run in a queue): later, then (reapBuffers()) */\n\tif (b->last_seq != 0 && !hakios_mali_wait(b->last_seq, 0)) {\n\t\tif (!fClosing && !b->dying && fDyingCount < sizeof(fDying) / sizeof(fDying[0])) {\n\t\t\tb->dying = true;\n\t\t\tfDying[fDyingCount++] = (uint16_t) (b - fBuffers);\n\t\t\treturn;\n\t\t}\n\t\t(void)hakios_mali_wait(b->last_seq, 2ull * NSEC_PER_SEC);\n\t}\n")
change("/*\n * A handle and GPU addresses for `size` bytes of `memory` from byte `from` on, every page entered into the GPU's tables.",
       "/* With the lock held: the buffers closed while a job still named them, those whose job has ended since. */\nvoid\nHakiosMaliUserClient::reapBuffers(void)\n{\n\tuint32_t kept = 0;\n\n\tfor (uint32_t i = 0; i < fDyingCount; i++) {\n\t\tstruct mali_buffer * b = &fBuffers[fDying[i]];\n\n\t\tif (b->memory == NULL || !b->dying) {\n\t\t\tcontinue;\n\t\t}\n\t\tif (fClosing || b->last_seq == 0 || hakios_mali_wait(b->last_seq, 0)) {\n\t\t\tfreeBuffer(b);\n\t\t} else {\n\t\t\tfDying[kept++] = fDying[i];\n\t\t}\n\t}\n\tfDyingCount = kept;\n}\n\n/*\n * A handle and GPU addresses for `size` bytes of `memory` from byte `from` on, every page entered into the GPU's tables.")
change('\t\tif (fBuffers[i].memory != NULL && fBuffers[i].surface == s) {',
       '\t\tif (fBuffers[i].memory != NULL && !fBuffers[i].dying && fBuffers[i].surface == s) {')
change('\tIOLockLock(gMaliLock);\n\tfor (uint32_t i = 0; fBuffers != NULL && i < MALI_BUFFERS; i++) {',
       '\tIOLockLock(gMaliLock);\n\tfClosing = true;\n\tfDyingCount = 0;\n\tfor (uint32_t i = 0; fBuffers != NULL && i < MALI_BUFFERS; i++) {')
change('\tIOLockLock(gMaliLock);\n\tif (fRequests++ < 32) {',
       '\tIOLockLock(gMaliLock);\n\tif (fDyingCount != 0) {\n\t\treapBuffers();\n\t}\n\tif (fRequests++ < 32) {')
(tree/'iokit/Kernel/begonia.mali.cpp').write_text(text)
for name in ['hakios.mali_va.h','hakios.mali_cpu_cache.h']:
    shutil.copyfile(source/name,tree/'bsd/dev'/name)
shutil.copyfile(source/'hakios.mali_cpu_cache.h',tree/'iokit/Kernel/hakios.mali_cpu_cache.h')
print('Prepared attributed S9+ transport; Exynos power/clock driver excluded')
