#!/bin/bash
# Build out/userdata/head.simg: the first 3 GiB of userdata as an Android sparse image for
# `fastboot flash userdata`: a 256 MiB f2fs (TWRP's /data; without it this TWRP brings up no
# USB at all) and system.apfs after it (tools/userdata_layout.py). Nothing is written at or
# beyond the data volume's offset, so the 16 GiB iOS data volume and its contents survive.
#   tools/make_userdata_head.sh        (WSL/Linux; needs f2fs-tools, img2simg)
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); cd "$R"
SYSTEM=upstream/hakios-xnu-kernel/userland/system.apfs; O=out/userdata; mkdir -p "$O"
eval "$(python3 tools/userdata_layout.py plan "$(stat -c %s "$SYSTEM")" | python3 -c 'import json,sys; p=json.load(sys.stdin); print("F2FS=%d SYS_OFF=%d SYS_SIZE=%d DATA_OFF=%d" % (p["f2fs"]["size"], p["system"]["offset"], p["system"]["size"], p["data"]["offset"]))')"
[ $((SYS_OFF + SYS_SIZE)) -le "$DATA_OFF" ] || { echo "head would overlap the data volume" >&2; exit 1; }
rm -f "$O/f2fs.img" "$O/head.raw" "$O/head.simg"
truncate -s "$F2FS" "$O/f2fs.img"
mkfs.f2fs -q -f -l data -t 0 "$O/f2fs.img"
truncate -s $((SYS_OFF + SYS_SIZE)) "$O/head.raw"
dd if="$O/f2fs.img" of="$O/head.raw" conv=notrunc,sparse bs=1M status=none
dd if="$SYSTEM" of="$O/head.raw" bs=1M seek=$((SYS_OFF >> 20)) conv=notrunc,sparse status=none
img2simg "$O/head.raw" "$O/head.simg" 4096
rm -f "$O/head.raw"
echo "head.simg: f2fs ${F2FS} bytes at 0, system.apfs at ${SYS_OFF}, ends at $((SYS_OFF + SYS_SIZE)) < data at ${DATA_OFF}"
ls -la "$O/head.simg"; sha256sum "$SYSTEM" "$O/f2fs.img"
