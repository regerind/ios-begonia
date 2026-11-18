#!/bin/bash
# Write a staged flash set (tools/stage_userdata_flash.py) to the phone in TWRP.
# Run from Git Bash on Windows (or any host whose adb sees the phone).
#
#   tools/flash_userdata_layout.sh DIR            write userdata images + boot, verify all
#   tools/flash_userdata_layout.sh DIR --boot-only    rewrite only the boot partition
#
# Writes ONLY to /dev/block/sdc46 (userdata, ERASED) and /dev/block/sdc34 (boot).
# Both are resolved by name and checked against this phone's GPT before any write.
# The f2fs at the start of userdata (TWRP's /data) is written by fastboot: tools/make_userdata_head.sh.
# Every write is read back and compared by SHA256. Never touches nvram, nvdata,
# nvcfg, persist, protect1/2, proinfo, md1img or anything else.
set -e -o pipefail
export MSYS_NO_PATHCONV=1
DIR=${1:?usage: flash_userdata_layout.sh DIR [--boot-only]}; MODE=${2:-all}
# shellcheck disable=SC1091
source "$DIR/layout.env"
die() { echo "flash: $*" >&2; exit 1; }
sh() { adb shell "$@" | tr -d '\r'; }

[ "$(adb get-state 2>/dev/null)" = recovery ] || die "phone is not in TWRP (adb state: $(adb get-state 2>&1))"
[ "$(sh getprop ro.product.device)" = begonia ] || die "not a begonia"
[ "$(sh id -u)" = 0 ] || die "adb is not root"
UD=$(sh readlink -f /dev/block/by-name/userdata); BOOT=$(sh readlink -f /dev/block/by-name/boot)
[ "$UD" = /dev/block/sdc46 ] || die "userdata is $UD, expected /dev/block/sdc46"
[ "$BOOT" = /dev/block/sdc34 ] || die "boot is $BOOT, expected /dev/block/sdc34"
[ "$(sh blockdev --getsize64 $UD)" = "$USERDATA_BYTES" ] || die "userdata size changed"
[ "$(sh cat /sys/class/block/sdc46/start)" = "$USERDATA_START_SECTORS" ] || die "userdata start changed"
[ "$(sh blockdev --getsize64 $BOOT)" = 67108864 ] || die "boot size changed"
[ "$BOOT_BYTES" -le 67108864 ] || die "boot image too large"


hash_of() { # hash_of DEV SEEK_BLOCKS BLOCKS
	sh "dd if=$1 bs=4096 skip=$2 count=$3 2>/dev/null | sha256sum" | cut -d' ' -f1
}
# put FILE DEV SEEK_BLOCKS SHA256 WHAT: write unless already there, wait, verify by readback
put() {
	local blocks=$(( $(stat -c %s "$1") / 4096 ))
	[ $(( blocks * 4096 )) = "$(stat -c %s "$1")" ] || die "$5: size not a multiple of 4096"
	if [ "$(hash_of "$2" "$3" "$blocks")" = "$4" ]; then
		echo "flash: $5 already on the phone ($4)"; return
	fi
	echo "flash: writing $5 ($(stat -c %s "$1") bytes) at block $3 of $2"
	# adb exec-in may return before the remote dd ends, and its shell may be killed
	# once stdin closes; so wait until no dd is writing this device, then verify.
	adb exec-in "dd of=$2 bs=4096 seek=$3 conv=notrunc,fsync 2>/dev/null" < "$1"
	local n=0
	while [ -n "$(sh "ps -A -o ARGS | grep '^dd of=$2 ' | grep -v grep")" ]; do
		n=$((n + 1)); [ $n -lt 900 ] || die "$5: dd did not finish"
		sleep 1
	done
	sh sync
	local got; got=$(hash_of "$2" "$3" "$blocks")
	[ "$got" = "$4" ] || die "$5: readback $got != $4"
	echo "flash: $5 verified ($4)"
}

if [ "$MODE" != --boot-only ]; then
	echo "flash: unmounting userdata"
	sh 'for m in $(grep sdc46 /proc/mounts | cut -d" " -f2 | sort -r); do umount $m || umount -l $m; done'
	[ -z "$(sh 'grep sdc46 /proc/mounts')" ] || die "userdata is still mounted"
	put "$DIR/system.apfs" "$UD" "$SYSTEM_SEEK" "$SYSTEM_SHA256" system.apfs
	for i in $(seq 1 "$DATA_PIECES"); do
		eval "set -- \$DATA_PIECE_$i"   # name seek blocks sha
		put "$DIR/$1" "$UD" "$2" "$4" "data.hfs piece $i"
	done
fi
put "$DIR/boot.img" "$BOOT" 0 "$BOOT_SHA256" boot.img
echo "flash: done. Reboot with: adb reboot   (TWRP stays on the recovery partition: Vol Up + Power)"
