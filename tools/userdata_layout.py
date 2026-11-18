#!/usr/bin/env python3
"""Raw iOS layout on begonia's userdata partition (sdc46, LUN2), and its extent maps.

The phone runs no Linux root of its own any more: userdata is dedicated to iOS and
holds two images back to back, each as ONE contiguous run of 4 KiB blocks:

    userdata + 1 MiB    system.apfs   read-only root (begonia-ufs node)
    userdata + 3 GiB    data.hfs      16 GiB HFS+ /private/var (begonia-data node)

Every number below is checked against userdata's GPT bounds, which the kernel
also enforces on its own (kernel/bsd/dev/begonia_ufs.c, BEGONIA_USERDATA_*).
Everything below userdata on the same LUN (nvram, nvdata, persist, protect*...)
carries the IMEI; nothing here may ever point outside userdata.

  userdata_layout.py maps SYSTEM_SIZE OUTDIR   write system.ext / data.ext (BEGEXT01 blobs)
  userdata_layout.py plan SYSTEM_SIZE          print byte offsets for the flasher (JSON)
"""
import argparse, json, pathlib, struct

BLOCK = 4096
# GPT of this phone, read in TWRP from /sys/class/block/sdc46/{start,size} (512-byte units):
# start 15286272, size 109641664  ->  4 KiB blocks 0x1d2800 .. 0xee47f8
USERDATA_FIRST = 15286272 * 512 // BLOCK
USERDATA_BLOCKS = 109641664 * 512 // BLOCK
USERDATA_END = USERDATA_FIRST + USERDATA_BLOCKS
assert (USERDATA_FIRST, USERDATA_END) == (0x1d2800, 0xee47f8)

SYSTEM_OFFSET = 1 << 20          # bytes from the start of userdata
DATA_OFFSET = 3 << 30
DATA_SIZE = 16 << 30             # the kernel requires exactly 16 GiB
MAGIC = 0x4245474558543031       # "BEGEXT01", checked by begonia_ufs.c


def plan(system_size):
    if system_size <= 0 or system_size % BLOCK:
        raise SystemExit(f'system image size {system_size} is not a positive multiple of 4096')
    if SYSTEM_OFFSET + system_size > DATA_OFFSET:
        raise SystemExit('system image would overlap the data image')
    if DATA_OFFSET + DATA_SIZE > USERDATA_BLOCKS * BLOCK:
        raise SystemExit('data image does not fit in userdata')
    return {
        'userdata_first_block': USERDATA_FIRST, 'userdata_end_block': USERDATA_END,
        'userdata_bytes': USERDATA_BLOCKS * BLOCK,
        'system': {'offset': SYSTEM_OFFSET, 'size': system_size},
        'data': {'offset': DATA_OFFSET, 'size': DATA_SIZE},
    }


def blob(offset, size):
    """One-extent BEGEXT01 map: u64 magic, size, first_lba, end_lba; u32 count, pad;
    then {logical, physical, count} in 4 KiB blocks."""
    physical = USERDATA_FIRST + offset // BLOCK
    count = size // BLOCK
    assert offset % BLOCK == 0 and size % BLOCK == 0
    assert USERDATA_FIRST <= physical and physical + count <= USERDATA_END
    return struct.pack('<QQQQII', MAGIC, size, USERDATA_FIRST, USERDATA_END, 1, 0) + \
        struct.pack('<QQQ', 0, physical, count)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    m = sub.add_parser('maps'); m.add_argument('system_size', type=int); m.add_argument('outdir', type=pathlib.Path)
    q = sub.add_parser('plan'); q.add_argument('system_size', type=int)
    a = p.parse_args()
    layout = plan(a.system_size)
    if a.cmd == 'plan':
        print(json.dumps(layout, indent=1))
        return
    a.outdir.mkdir(parents=True, exist_ok=True)
    (a.outdir / 'system.ext').write_bytes(blob(SYSTEM_OFFSET, a.system_size))
    (a.outdir / 'data.ext').write_bytes(blob(DATA_OFFSET, DATA_SIZE))
    print(f"system: blocks {USERDATA_FIRST + SYSTEM_OFFSET // BLOCK:#x}+{a.system_size // BLOCK:#x}; "
          f"data: blocks {USERDATA_FIRST + DATA_OFFSET // BLOCK:#x}+{DATA_SIZE // BLOCK:#x}; "
          f"bounds {USERDATA_FIRST:#x}..{USERDATA_END:#x}")


if __name__ == '__main__':
    main()
