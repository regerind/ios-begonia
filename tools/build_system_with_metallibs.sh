#!/bin/bash
# The system volume with the firmware's shader libraries: upstream/hakios-xnu-kernel/userland/system-metal.apfs.
#
# sysimage/select-system.py leaves every .metallib out of the system image (142 files, 192 MB): the port had no Metal.
# With Metal for applications (docs/history/SESSION-2026-11-19d.md) every framework and application that draws with Metal
# needs its library: MetalPerformanceShaders, CoreImage, SceneKit, Photos' frameworks... They were extracted with 7z
# from the IPSW's root image into sysimage/metallibs/ (same paths). This links each one into sysimage/rootfs where its
# bundle is (a library whose bundle the image has not got is left out), then runs the system-image section of
# userland/build.sh as it is -- the same steps, the same inputs -- into system-metal.apfs. The image in use,
# system.apfs, is not touched: boot images take the system's size from it (build_userdata_boot.sh), so it is replaced
# only when the new one is flashed (tools/make_userdata_head.sh, fastboot flash userdata).
#   tools/build_system_with_metallibs.sh      (WSL/Linux)
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); H=$R/upstream/hakios-xnu-kernel; U=$H/userland; S=$H/sysimage
[ -d "$S/metallibs" ] || { echo "no sysimage/metallibs: extract them first (7z x rootfs.dmg, the .metallib paths of rootfs.slt)" >&2; exit 1; }
python3 - "$S" <<'PY'
import os, sys
s = sys.argv[1]
src, dst = os.path.join(s, 'metallibs'), os.path.join(s, 'rootfs')
added = skipped = present = 0
for top, _, files in os.walk(src):
    for name in files:
        if not name.endswith('.metallib'):
            continue
        rel = os.path.relpath(os.path.join(top, name), src)
        target = os.path.join(dst, rel)
        parent = os.path.dirname(target)
        if not os.path.isdir(parent):
            skipped += 1
            continue
        if os.path.exists(target):
            present += 1
            continue
        os.link(os.path.join(top, name), target)
        added += 1
print('metallibs: %d added to rootfs, %d there already, %d left out (their bundle is not in the image)' % (added, present, skipped))
PY
# the system-image section of userland/build.sh, its body as it is, written to system-metal.apfs
start=$(grep -n '^# --- the system image' "$U/build.sh" | cut -d: -f1)
then_line=$(awk -v s="$start" 'NR>s && /; then$/ {print NR; exit}' "$U/build.sh")
end=$(awk -v s="$start" 'NR>s && /^fi$/ {print NR; exit}' "$U/build.sh")
{
	echo 'set -e'
	echo 'linkcopy() { cp -al "$1" "$2"; }'
	echo "cd '$U'"
	echo 'source ../xnu-env.sh'
	echo 'DARWIN=../sysimage; CACHE=$DARWIN/20F66__iPhone10,3_6'
	grep '^SILEO_BIN=' "$U/build.sh"
	echo 'CHECK=; MODEM='
	sed -n "$((then_line + 1)),$((end - 1))p" "$U/build.sh" | sed 's/-o system\.apfs/-o system-metal.apfs/; s/check-image\.sh system\.apfs/check-image.sh system-metal.apfs/'
} > /tmp/begonia-system-step.sh
grep -q 'system-metal.apfs' /tmp/begonia-system-step.sh || { echo "the system step was not found in build.sh" >&2; exit 1; }
echo "system image: building (lines $then_line to $end of userland/build.sh)"
bash /tmp/begonia-system-step.sh
ls -la "$U/system.apfs" "$U/system-metal.apfs"
