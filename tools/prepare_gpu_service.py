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
# y303: a buffer the driver made, closed, is kept for the next one of its size (boot-arg beg_park: 1 always, 2 in the odd
# 5-second slices of the time since boot). y302 with the home screen swiped: every frame the window server made one buffer
# of 12.7 MB and closed it again (hakios_mali_address 100 times in 5 s for 100 frames, 3112 pages entered each time): the
# kernel's pages taken, cleared, entered into the GPU's tables one by one, taken out and given back, with 1100 to 3000 pages
# free in all; the window server spends a quarter to a third of its processor time in the kernel while it draws. At most two
# buffers a client, each 4 to 24 MB, of the same size and flags as asked; cleared when taken (a new buffer reads zeros).
change('(reapBuffers()) */\n};',
       '(reapBuffers()) */\n\tuint32_t flags;                         /* y303: createBO\'s */\n\tbool made;                              /* by createBO: the driver\'s own pages */\n};')
change('\tbool                    fClosing;\n',
       '\tbool                    fClosing;\n\tstruct { IOMemoryDescriptor * memory; uint64_t address, size; uint32_t flags; } fParked[2];   /* y303 */\n')
change('static IOLock * gMaliLock;\n',
       'static IOLock * gMaliLock;\n\n'
       'extern "C" { uint32_t begonia_park_hits, begonia_park_kept; }\n'
       'static bool\nbegonia_park_now(void)\n{\n\tstatic uint32_t mode = 99;\n\n\tif (mode == 99) {\n\t\tuint32_t value = 0;\n\n'
       '\t\t(void)PE_parse_boot_argn("beg_park", &value, sizeof(value));\n\t\tmode = value;\n\t}\n'
       '\tif (mode == 2) {\n\t\tuint64_t ns = 0;\n\n\t\tabsolutetime_to_nanoseconds(mach_absolute_time(), &ns);\n\t\treturn ((ns / 5000000000ull) & 1) != 0;\n\t}\n'
       '\treturn mode == 1;\n}\n')
change('\thakios_mali_unmap(b->address, b->size);\n\tb->memory->release();\n\tif (b->surface != NULL) {\n\t\tMiniIOSurfaceDrop(b->surface);\n\t}\n\tbzero(b, sizeof(*b));\n}',
       '\tif (!fClosing && b->made && b->surface == NULL && b->size >= (4ull << 20) && b->size <= (24ull << 20) && begonia_park_now()) {\n'
       '\t\tfor (uint32_t k = 0; k < 2; k++) {\n\t\t\tif (fParked[k].memory == NULL) {\n'
       '\t\t\t\tfParked[k].memory = b->memory;\n\t\t\t\tfParked[k].address = b->address;\n\t\t\t\tfParked[k].size = b->size;\n\t\t\t\tfParked[k].flags = b->flags;\n'
       '\t\t\t\tbegonia_park_kept++;\n\t\t\t\tbzero(b, sizeof(*b));\n\t\t\t\treturn;\n\t\t\t}\n\t\t}\n\t}\n'
       '\thakios_mali_unmap(b->address, b->size);\n\tb->memory->release();\n\tif (b->surface != NULL) {\n\t\tMiniIOSurfaceDrop(b->surface);\n\t}\n\tbzero(b, sizeof(*b));\n}')
change('\tif (size == 0 || size > MALI_BUFFER_MAX) {\n\t\treturn kIOReturnBadArgument;\n\t}\n\tmemory = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,',
       '\tif (size == 0 || size > MALI_BUFFER_MAX) {\n\t\treturn kIOReturnBadArgument;\n\t}\n'
       '\tfor (uint32_t k = 0; k < 2; k++) {              /* y303: one kept of this size and these flags */\n'
       '\t\tif (fParked[k].memory == NULL || fParked[k].size != size || fParked[k].flags != request->flags) {\n\t\t\tcontinue;\n\t\t}\n'
       '\t\tstruct mali_buffer * kept = NULL;\n\n'
       '\t\tfor (uint32_t n = 0; n < MALI_BUFFERS; n++) {\n\t\t\tuint32_t i = (fNextBuffer + n) % MALI_BUFFERS;\n\n'
       '\t\t\tif (fBuffers[i].memory == NULL) {\n\t\t\t\tkept = &fBuffers[i];\n\t\t\t\trequest->handle = i + 1;\n\t\t\t\tfNextBuffer = i + 1;\n\t\t\t\tbreak;\n\t\t\t}\n\t\t}\n'
       '\t\tif (kept == NULL) {\n\t\t\tbreak;\n\t\t}\n'
       '\t\tvoid * bytes = ((IOBufferMemoryDescriptor *) fParked[k].memory)->getBytesNoCopy();\n\n'
       '\t\tif (bytes != NULL) {\n\t\t\tbzero(bytes, (size_t) size);\n\t\t}\n'
       '\t\tbzero(kept, sizeof(*kept));\n\t\tkept->memory = fParked[k].memory;\n\t\tkept->address = fParked[k].address;\n\t\tkept->size = size;\n'
       '\t\tkept->flags = request->flags;\n\t\tkept->made = true;\n\t\tfParked[k].memory = NULL;\n\t\trequest->offset = kept->address;\n'
       '\t\tbegonia_park_hits++;\n\t\treturn kIOReturnSuccess;\n\t}\n'
       '\tmemory = IOBufferMemoryDescriptor::inTaskWithOptions(kernel_task,')
change('\tb->memory = memory;\n\trequest->offset = b->address;\n\thakios_mali_tag(b->address, 1, 0, 0, 0, 0, 0);\n',
       '\tb->memory = memory;\n\tb->flags = request->flags;\n\tb->made = true;\n\trequest->offset = b->address;\n\thakios_mali_tag(b->address, 1, 0, 0, 0, 0, 0);\n')
change('\t\t\tfreeBuffer(&fBuffers[i]);\n\t\t}\n\t}\n\tIOLockUnlock(gMaliLock);',
       '\t\t\tfreeBuffer(&fBuffers[i]);\n\t\t}\n\t}\n'
       '\tfor (uint32_t k = 0; k < 2; k++) {              /* y303: the kept ones */\n\t\tif (fParked[k].memory != NULL) {\n'
       '\t\t\tif (hakios_mali_ready()) {\n\t\t\t\thakios_mali_unmap(fParked[k].address, fParked[k].size);\n\t\t\t\tfParked[k].memory->release();\n\t\t\t}\n'
       '\t\t\tfParked[k].memory = NULL;\n\t\t}\n\t}\n\tIOLockUnlock(gMaliLock);')
(tree/'iokit/Kernel/begonia.mali.cpp').write_text(text)
for name in ['hakios.mali_va.h','hakios.mali_cpu_cache.h']:
    shutil.copyfile(source/name,tree/'bsd/dev'/name)
shutil.copyfile(source/'hakios.mali_cpu_cache.h',tree/'iokit/Kernel/hakios.mali_cpu_cache.h')
print('Prepared attributed S9+ transport; Exynos power/clock driver excluded')
