#!/bin/bash
# Write a staged flash set (tools/stage_userdata_flash.py) to the phone in TWRP.
# Run from Git Bash on Windows (or any host whose adb sees the phone).
#
#   tools/flash_userdata_layout.sh DIR            write userdata images + boot, verify all
#   tools/flash_userdata_layout.sh DIR --boot-only    rewrite only the boot partition
#
# Writes ONLY to /dev/block/sdc46 (userdata, ERASED) and /dev/block/sdc34 (boot).
# Both are resolved by name and checked against this phone's GPT before any write.
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

verify() { # verify DEV SEEK_BLOCKS BLOCKS SHA256 WHAT
	local got; got=$(sh "dd if=$1 bs=4096 skip=$2 count=$3 2>/dev/null | sha256sum" | cut -d' ' -f1)
	[ "$got" = "$4" ] || die "$5: readback $got != $4"
	echo "flash: $5 verified ($4)"
}
write() { # write FILE DEV SEEK_BLOCKS WHAT
	echo "flash: writing $4 ($(stat -c %s "$1") bytes) at block $3 of $2"
	sh rm -f /tmp/flash.done
	# adb exec-in can return once stdin is sent, before the remote dd has finished:
	# wait for its own completion marker before anything reads the device back.
	adb exec-in "dd of=$2 bs=4096 seek=$3 conv=notrunc,fsync 2>/dev/null; echo \$? > /tmp/flash.done" < "$1"
	local n=0 rc
	until rc=$(sh 'cat /tmp/flash.done 2>/dev/null') && [ -n "$rc" ]; do
		n=$((n + 1)); [ $n -lt 600 ] || die "$4: dd did not finish"
		sleep 1
	done
	[ "$rc" = 0 ] || die "$4: dd exit status $rc"
}

if [ "$MODE" != --boot-only ]; then
	echo "flash: unmounting userdata"
	sh 'for m in $(grep sdc46 /proc/mounts | cut -d" " -f2 | sort -r); do umount $m || umount -l $m; done'
	[ -z "$(sh 'grep sdc46 /proc/mounts')" ] || die "userdata is still mounted"
	echo "flash: erasing the f2fs superblocks (first 1 MiB of userdata)"
	sh "dd if=/dev/zero of=$UD bs=4096 count=256 conv=fsync 2>/dev/null"
	verify "$UD" 0 256 "$(head -c 1048576 /dev/zero | sha256sum | cut -d' ' -f1)" "userdata head zeroed"
	write "$DIR/system.apfs" "$UD" "$SYSTEM_SEEK" system.apfs
	for i in $(seq 1 "$DATA_PIECES"); do
		eval "set -- \$DATA_PIECE_$i"   # name seek blocks sha
		write "$DIR/$1" "$UD" "$2" "data.hfs piece $i"
		verify "$UD" "$2" "$3" "$4" "data.hfs piece $i"
	done
	verify "$UD" "$SYSTEM_SEEK" "$SYSTEM_BLOCKS" "$SYSTEM_SHA256" system.apfs
fi
write "$DIR/boot.img" "$BOOT" 0 boot.img
sh sync
got=$(sh "head -c $BOOT_BYTES $BOOT | sha256sum" | cut -d' ' -f1)
[ "$got" = "$BOOT_SHA256" ] || die "boot readback $got != $BOOT_SHA256"
echo "flash: boot verified ($BOOT_SHA256)"
echo "flash: done. Reboot with: adb reboot   (TWRP stays on the recovery partition: Vol Up + Power)"
