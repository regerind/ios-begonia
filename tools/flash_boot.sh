#!/bin/bash
# Flash a built image: ONLY the boot and misc partitions are written. Never nvram, nvdata, protect1/2, persist, proinfo
# (the IMEI lives there). Run in WSL/Linux with the phone in fastboot (Power + Volume-Down at power-on).
#
#   tools/flash_boot.sh TAG        flashes out/ios16-TAG.img, and private/misc/misc-bcb-cleared.img to misc
set -e
TAG=${1:?usage: flash_boot.sh TAG}
R=$(cd "$(dirname "$0")/.." && pwd); cd "$R"
[ "$(timeout 10 fastboot getvar product 2>&1 | awk '/^product:/{print $2}')" = begonia ] || { echo "no begonia in fastboot"; exit 1; }
sha256sum out/ios16-$TAG.img
timeout 120 fastboot flash boot out/ios16-$TAG.img
timeout 60 fastboot flash misc private/misc/misc-bcb-cleared.img
timeout 20 fastboot reboot
