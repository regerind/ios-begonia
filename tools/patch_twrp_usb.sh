#!/bin/bash
# Make a headless, adb-only copy of the phone's TWRP for use as the chained fallback kernel.
# On this raw-iOS userdata the TWRP program crashes and restarts in a loop, re-initialising USB
# each time, so adb never stays up (only sideload works). This copy never starts it: init + adbd.
# The recovery partition is never touched: only private/twrp/recovery-adb.img is written.
#
#   tools/patch_twrp_usb.sh   (in WSL/Linux, as root so cpio keeps ownership; needs unpack_bootimg)
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); T=$R/private/twrp; W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
[ "$(sha256sum < "$T/recovery.img" | cut -d' ' -f1)" = "$(cat "$T/recovery.img.sha256")" ] || { echo "recovery.img checksum mismatch" >&2; exit 1; }
unpack_bootimg --boot_img "$T/recovery.img" --out "$W/img" >/dev/null
mkdir "$W/rd" && (cd "$W/rd" && gzip -dc "$W/img/ramdisk" | cpio -idm --quiet)
rc=$W/rd/init.recovery.usb.rc
grep -q 'property:sys.usb.ffs.ready=1 && property:sys.usb.config=adb' "$rc" || { echo "unexpected TWRP usb rc" >&2; exit 1; }
# Headless: TWRP's own program (service recovery) crashes on this raw-iOS userdata (its FBE
# decryption) and every restart re-initialises USB, so adbd's connection is torn down every
# ~50 ms and the host never sees the phone. Keep init, the USB gadget script and adbd; do not
# start the TWRP program. The phone's real TWRP (recovery partition) is unchanged.
svc=$W/rd/init.recovery.service.rc
grep -q '^service recovery /system/bin/recovery' "$svc" || { echo "unexpected TWRP service rc" >&2; exit 1; }
sed -i '/^service recovery \/system\/bin\/recovery/a\    disabled' "$svc"
cat >> "$rc" <<'RC'

# begonia-ios headless fallback: adb only, nothing else touches the gadget.
on boot
    setprop sys.usb.controller musb-hdrc
    setprop sys.usb.config adb
RC
(cd "$W/rd" && find . | LC_ALL=C sort | cpio -o -H newc --quiet | gzip -9 -n) > "$W/ramdisk.new"
python3 - "$T/recovery.img" "$W/ramdisk.new" "$T/recovery-adb.img" <<'PY'
"""Replace the ramdisk of an Android v2 boot image; keep every other section and field."""
import hashlib, struct, sys
src = open(sys.argv[1], 'rb').read(); rd = open(sys.argv[2], 'rb').read()
assert src[:8] == b'ANDROID!'
ks, ka, rs, ra, ss, sa, ta, ps, ver, osv = struct.unpack_from('<10I', src, 8)
assert ver == 2
rds, rdo = struct.unpack_from('<IQ', src, 1632); ds, = struct.unpack_from('<I', src, 1648)
align = lambda n: (n + ps - 1) // ps * ps
off = ps; parts = []
for size in (ks, rs, ss, rds, ds):
    parts.append(src[off:off + size]); off += align(size)
parts[1] = rd
hdr = bytearray(src[:ps]); struct.pack_into('<I', hdr, 16, len(rd))
struct.pack_into('<Q', hdr, 1636, (ps + align(ks) + align(len(rd)) + align(ss)) if rds else 0)
h = hashlib.sha1()
for i, d in enumerate(parts):
    if i == 3 and not rds: continue
    h.update(d); h.update(struct.pack('<I', len(d)))
hdr[576:608] = h.digest() + bytes(12)
out = bytes(hdr) + b''.join(d + bytes(align(len(d)) - len(d)) for d in parts)
size = 64 << 20; assert len(out) <= size
open(sys.argv[3], 'wb').write(out + bytes(size - len(out)))
PY
sha256sum < "$T/recovery-adb.img" | cut -d' ' -f1 > "$T/recovery-adb.img.sha256"
echo "private/twrp/recovery-adb.img $(cat "$T/recovery-adb.img.sha256")"
