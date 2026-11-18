#!/bin/bash
# Make an adb-only copy of the phone's TWRP for use as the chained fallback kernel.
# With userdata given to iOS there is no /data/media, MTP never becomes ready and this
# TWRP never binds its USB gadget at all (no adb either). Force sys.usb.config to adb.
# The recovery partition is never touched: only private/twrp/recovery-adb.img is written.
#
#   tools/patch_twrp_usb.sh   (in WSL/Linux, as root so cpio keeps ownership; needs mkbootimg)
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); T=$R/private/twrp; W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
[ "$(sha256sum < "$T/recovery.img" | cut -d' ' -f1)" = "$(cat "$T/recovery.img.sha256")" ] || { echo "recovery.img checksum mismatch" >&2; exit 1; }
unpack_bootimg --boot_img "$T/recovery.img" --out "$W/img" --format=mkbootimg > "$W/args"
mkdir "$W/rd" && (cd "$W/rd" && gzip -dc "$W/img/ramdisk" | cpio -idm --quiet)
rc=$W/rd/init.recovery.usb.rc
grep -q 'property:sys.usb.ffs.ready=1 && property:sys.usb.config=adb' "$rc" || { echo "unexpected TWRP usb rc" >&2; exit 1; }
cat >> "$rc" <<'RC'

# begonia-ios: userdata holds raw iOS images, so MTP can never come up; keep adb alive.
on property:sys.usb.config=mtp,adb
    setprop sys.usb.config adb
on property:sys.usb.config=mtp
    setprop sys.usb.config adb
on property:sys.usb.controller=
    setprop sys.usb.controller musb-hdrc
on boot
    setprop sys.usb.controller musb-hdrc
RC
(cd "$W/rd" && find . | LC_ALL=C sort | cpio -o -H newc --quiet | gzip -9 -n) > "$W/ramdisk.new"
eval "mkbootimg $(cat "$W/args" | sed "s#$W/img/ramdisk#$W/ramdisk.new#") --output $T/recovery-adb.img"
python3 - "$T/recovery-adb.img" <<'P'
import sys
d = open(sys.argv[1], 'rb').read(); size = 64 << 20
assert d[:8] == b'ANDROID!' and len(d) <= size
open(sys.argv[1], 'wb').write(d + bytes(size - len(d)))
P
sha256sum < "$T/recovery-adb.img" | cut -d' ' -f1 > "$T/recovery-adb.img.sha256"
echo "private/twrp/recovery-adb.img $(cat "$T/recovery-adb.img.sha256")"
