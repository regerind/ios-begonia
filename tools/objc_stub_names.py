#!/usr/bin/env python3
"""The selector names of objc_msgSend stubs (__objc_stubs) in an arm64 Mach-O, for reading a stripped binary's
disassembly: a call "bl STUB" is a message whose selector is the stub's adrp/ldr x1 target.

  objc_stub_names.py BINARY STUB-ADDRESS...      (hex addresses; prints "address selector")
  objc_stub_names.py BINARY --annotate DIS       (writes DIS with "; -> selector" after every bl to a stub)
"""
import re, struct, sys

data = open(sys.argv[1], 'rb').read()
ncmds = struct.unpack_from('<I', data, 16)[0]
at, segments = 32, []
for _ in range(ncmds):
    cmd, size = struct.unpack_from('<II', data, at)
    if cmd == 0x19:                                             # LC_SEGMENT_64
        vmaddr, vmsize, fileoff, filesize = struct.unpack_from('<QQQQ', data, at + 24)
        segments.append((vmaddr, vmsize, fileoff, filesize))
    at += size
base = min(s[0] for s in segments if s[2] == 0 and s[3] > 0)


def offset(vm):
    for vmaddr, vmsize, fileoff, filesize in segments:
        if vmaddr <= vm < vmaddr + filesize:
            return fileoff + vm - vmaddr
    return None


def cstring(vm):
    o = offset(vm)
    if o is None:
        return None
    end = data.index(b'\0', o)
    return data[o:end].decode('latin-1')


def selector(stub):
    o = offset(stub)
    if o is None:
        return None
    adrp, ldr = struct.unpack_from('<II', data, o)
    if (adrp & 0x9f00001f) != 0x90000001 or (ldr & 0xffc003ff) != 0xf9400021:
        return None
    imm = ((adrp >> 29) & 3) | (((adrp >> 5) & 0x7ffff) << 2)
    if imm & (1 << 20):
        imm -= 1 << 21
    page = (stub & ~0xfff) + (imm << 12)
    selref = page + ((ldr >> 10) & 0xfff) * 8
    so = offset(selref)
    raw = struct.unpack_from('<Q', data, so)[0]
    target = raw & 0xfffffffff                                  # chained rebase: the target in the low 36 bits
    if target < base:
        target += base
    return cstring(target)


if len(sys.argv) > 3 and sys.argv[2] == '--annotate':
    cache = {}
    with open(sys.argv[3]) as dis, open(sys.argv[3] + '.sel', 'w') as out:
        for line in dis:
            m = re.search(r'\tbl\t0x([0-9a-f]+)$', line.rstrip('\n'))
            if m:
                t = int(m.group(1), 16)
                if t not in cache:
                    cache[t] = selector(t)
                if cache[t]:
                    line = line.rstrip('\n') + ' ; -> ' + cache[t] + '\n'
            out.write(line)
    print('wrote', sys.argv[3] + '.sel', len(cache), 'targets')
else:
    for a in sys.argv[2:]:
        print(a, selector(int(a, 16)))
