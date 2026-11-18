#!/usr/bin/env python3
"""Stage a flash set for tools/flash_userdata_layout.sh (run in WSL/Linux).

Copies system.apfs, the written (non-hole) 1 MiB-aligned ranges of the sparse
data.hfs and the boot image into DEST, with layout.env (offsets in 4 KiB blocks,
from tools/userdata_layout.py) and SHA256 sums of every piece.
"""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, sys
R = pathlib.Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('boot', type=pathlib.Path); p.add_argument('dest', type=pathlib.Path)
p.add_argument('--system', type=pathlib.Path, default=R / 'upstream/hakios-xnu-kernel/userland/system.apfs')
p.add_argument('--data', type=pathlib.Path, default=R / 'out/userdata/data.hfs')
a = p.parse_args()
sha = lambda f: hashlib.file_digest(open(f, 'rb'), 'sha256').hexdigest()
plan = json.loads(subprocess.check_output([sys.executable, str(R / 'tools/userdata_layout.py'), 'plan', str(a.system.stat().st_size)]))
assert a.data.stat().st_size == plan['data']['size']
a.dest.mkdir(parents=True, exist_ok=True)
B = 4096; M = 1 << 20
env = [f"USERDATA_BYTES={plan['userdata_bytes']}", 'USERDATA_START_SECTORS=15286272',
       f"SYSTEM_SEEK={plan['system']['offset'] // B}", f"SYSTEM_BLOCKS={plan['system']['size'] // B}",
       f"SYSTEM_SHA256={sha(a.system)}", f"BOOT_SHA256={sha(a.boot)}", f"BOOT_BYTES={a.boot.stat().st_size}"]
shutil.copyfile(a.system, a.dest / 'system.apfs'); shutil.copyfile(a.boot, a.dest / 'boot.img')
fd = os.open(a.data, os.O_RDONLY); off = 0; ranges = []
while off < plan['data']['size']:
    try: d = os.lseek(fd, off, os.SEEK_DATA)
    except OSError: break
    h = os.lseek(fd, d, os.SEEK_HOLE); s, e = d // M * M, -(-h // M) * M
    if ranges and s <= ranges[-1][1]: ranges[-1][1] = max(ranges[-1][1], e)
    else: ranges.append([s, e])
    off = h
names = []
with open(a.data, 'rb') as f:
    for s, e in ranges:
        f.seek(s); chunk = f.read(e - s); name = f'data-{s:011x}.bin'
        (a.dest / name).write_bytes(chunk); names.append(name)
        env.append(f"DATA_PIECE_{len(names)}='{name} {(plan['data']['offset'] + s) // B} {(e - s) // B} {hashlib.sha256(chunk).hexdigest()}'")
env.append(f'DATA_PIECES={len(names)}')
(a.dest / 'layout.env').write_text('\n'.join(env) + '\n')
print((a.dest / 'layout.env').read_text())
