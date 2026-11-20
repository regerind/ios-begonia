#!/bin/bash
# Build a persistent ("p") image: kernel + userland + Metal support + boot image, no touch script, no session deadline.
# Run in WSL/Linux from a checkout that already has upstream/ and private/ prepared (docs/BUILD-FROM-SCRATCH.md).
#
#   tools/build_persistent.sh TAG            e.g.  tools/build_persistent.sh p19
#
# Output: out/ios16-TAG.img (flash it with tools/flash_boot.sh TAG). The boot line is the one of p18 (see
# docs/erkam-docs/BUILD-LOG.md). Set EXTRA_ARGS to change it; set NO_WALLPAPER=0 for a build that keeps the wallpaper.
# Derived from the working auto_build.sh of the project's owner, minus the git reset and the test-session parts.
set -e -o pipefail
TAG=${1:?usage: build_persistent.sh TAG}
R=$(cd "$(dirname "$0")/.." && pwd); cd "$R"
NO_WALLPAPER=${NO_WALLPAPER:-1}
EXTRA_ARGS=${EXTRA_ARGS:-" begonia_persist=1 metal=1 begonia_wifi_rails=2 begonia_audio=1 bl_power=1 gllivecopy=0 fb_gpuseq=1 metal_swapwait=0"}
[ "$NO_WALLPAPER" = 1 ] && EXTRA_ARGS="$EXTRA_ARGS sb_nowall=1"
# The programs that are shown the Metal device (p18): everything except the four excluded, plus the system's own.
METAL_CLIENTS=${METAL_CLIENTS:-"!MobileSlideShow !Measure !MediaRemoteUI !mediaserverd @apps com.apple.WebKit.GPU com.apple.WebKit.WebContent SpringBoard backboardd"}
MC=""; for c in $METAL_CLIENTS; do MC="$MC --metal-client $c"; done

python3 tools/prepare_kernel.py > out/prep.log 2>&1 || { tail -5 out/prep.log; echo "PREPARE FAILED"; exit 1; }
( cd upstream/hakios-xnu-kernel && source xnu-env.sh && { make begonia_defconfig > "$R/out/kbuild.log" 2>&1 && make kernel JOBS=${JOBS:-8} >> "$R/out/kbuild.log" 2>&1 || true; } )
if grep -n "error:" out/kbuild.log | head -10 | grep .; then echo "KERNEL BUILD ERRORS (out/kbuild.log)"; exit 1; fi
cp upstream/hakios-xnu-kernel/hakios-kernel out/kernel.begonia-native-ufs
python3 tools/prepare_userland.py | tail -1
bash tools/build_gles_linux.sh 2>&1 | grep -E "error|gles: built" | head -8
rm -rf out/begonia-ui-support-quiet out/begonia-gles-support-q3
python3 tools/make_ui_support.py --physical-touch --quiet --output-dir out/begonia-ui-support-quiet | tail -1
python3 tools/make_gles_support.py --ui $MC --base-root out/begonia-ui-support-quiet/root --output-dir out/begonia-gles-support-q3 | tail -1
rm -f out/ios16-$TAG.img out/native-$TAG.payload
SUPPORT=out/begonia-gles-support-q3/root.apfs RECOVERY=private/twrp/recovery-adb.img bash tools/build_userdata_boot.sh $TAG \
  --data --disable-cs --touch --perf-probe --gpu-probe --gpu-selftest --gpu-mapped-selftest \
  --gpu-shader-selftest --gpu-service --gles-compositor --cpus 7 --smp-trampoline 0x400801c0 --watch-seconds 0 \
  --extra-args "fbwidth=1080 fbheight=2340 ca_vbl=1 begonia_console_off=1$EXTRA_ARGS" 2>&1 | grep -E "Temporary|rror|too long"
[ -f out/ios16-$TAG.img ] || { echo "BUILD FAILED: no image"; exit 1; }
sha256sum out/ios16-$TAG.img; ls -l out/ios16-$TAG.img
