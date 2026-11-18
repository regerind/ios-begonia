#!/bin/bash
# Build everything for the raw-userdata layout (tools/userdata_layout.py), with
# TWRP (from the phone's own recovery backup) as the chained fallback kernel.
# Run in WSL/Linux from the repository root after the kernel, system.apfs and the
# support volume have been built. No device access.
#
#   tools/build_userdata_boot.sh TAG [extra make_native_payload.py options...]
#
# Inputs (private/, never committed):
#   private/twrp/recovery.img          full dump of the phone's recovery partition
#                                      (RECOVERY=private/twrp/recovery-adb.img: tools/patch_twrp_usb.sh)
#   private/twrp/recovery.img.sha256   its SHA256 as read on the phone
#   private/touch/novatek_ts_fw.bin    118784-byte NT36672A firmware (SHA256 0b57dd62...)
# Outputs: out/userdata/{system.ext,data.ext,data.hfs}, out/begonia-native.adt,
#          out/native-TAG.payload, out/ios16-TAG.img
set -e -o pipefail
TAG=${1:?usage: build_userdata_boot.sh TAG [payload options]}; shift
R=$(cd "$(dirname "$0")/.." && pwd); H=$R/upstream/hakios-xnu-kernel; cd "$R"
SYSTEM=$H/userland/system.apfs
SUPPORT=${SUPPORT:-$R/out/begonia-ui-support/root.apfs}
KERNEL=$R/out/kernel.begonia-native-ufs
RECOVERY=${RECOVERY:-$R/private/twrp/recovery.img}
TOUCH=$R/private/touch/novatek_ts_fw.bin
for f in "$SYSTEM" "$SUPPORT" "$KERNEL" "$RECOVERY" "$RECOVERY.sha256" "$TOUCH" "$H/sysimage/DeviceTree.d22ap.im4p"; do
	[ -f "$f" ] || { echo "missing input: $f" >&2; exit 1; }
done
[ "$(sha256sum < "$TOUCH" | cut -d' ' -f1)" = 0b57dd62863f0f20ea285263a667af3fdd86f48473e23ac638efeba5c8f6ad03 ] ||
	{ echo "touch firmware checksum mismatch" >&2; exit 1; }

mkdir -p out/userdata
python3 tools/userdata_layout.py maps "$(stat -c %s "$SYSTEM")" out/userdata
python3 tools/userdata_layout.py plan "$(stat -c %s "$SYSTEM")" > out/userdata/plan.json

# Data image: sparse 16 GiB HFS+; only its formatted blocks get written to the phone.
if [ ! -f out/userdata/data.hfs ]; then
	truncate -s 16G out/userdata/data.hfs.new
	mkfs.hfsplus -J -v iOSData out/userdata/data.hfs.new >/dev/null
	[ "$(dd if=out/userdata/data.hfs.new bs=1 skip=1024 count=2 2>/dev/null)" = "H+" ]
	mv out/userdata/data.hfs.new out/userdata/data.hfs
fi

python3 "$H/xnuloader/im4p.py" "$H/sysimage/DeviceTree.d22ap.im4p" out/d22ap.adt.lzfse >/dev/null
lzfse -decode -i out/d22ap.adt.lzfse -o out/d22ap.adt   # apt install lzfse
python3 tools/make_begonia_adt.py out/begonia-native.adt \
	--extents out/userdata/system.ext --data-extents out/userdata/data.ext \
	--touch-firmware "$TOUCH" --identity out/d22ap.adt >/dev/null

python3 tools/build_probe.py --chain --ramoops --xnu --force-xnu-test >/dev/null
python3 tools/make_native_payload.py "out/native-$TAG.payload" --support-root "$SUPPORT" "$@"

rm -f "out/ios16-$TAG.img"
python3 tools/pack_chain.py "$RECOVERY" out/begonia-chain-xnu-forced/Image \
	--payload "out/native-$TAG.payload" --sha256 "$(cat "$RECOVERY.sha256")" --output "out/ios16-$TAG.img"
sha256sum "out/ios16-$TAG.img" "out/native-$TAG.payload" out/begonia-native.adt out/userdata/*.ext
