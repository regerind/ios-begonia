#!/bin/bash
# Linux port of tools/build_metal2vulkan_ios.py (which needs macOS: xcrun, codesign, vtool).
# Builds the Metal-to-Vulkan runtime with its C bridge (libmetal2vulkan_bridge.dylib) for the Darwin userland:
# Rust's aarch64-apple-ios target, linked by the kernel's cross compiler (xnu-clang + theos ld64).
#
#   tools/build_metal2vulkan_linux.sh        (WSL/Linux)
#
# Needs upstream/metal2vulkan at 720ef1ce, upstream/ios-s9plus-exynos at 2d092398 and the pinned Rust (1.98.1 with
# the aarch64-apple-ios target) under .tools/rust. Nothing is codesigned (the payload boots with --disable-cs).
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); H=$R/upstream/hakios-xnu-kernel
source "$H/xnu-env.sh"
SDK=$THEOS/sdks/iPhoneOS16.5.sdk; CC=$H/xnu-deps/host/bin/xnu-clang
export RUSTUP_HOME=$R/.tools/rust/rustup CARGO_HOME=$R/.tools/rust/cargo
# (begonia's runtime patches come off first, last one first: the reference patch's own "already applied" check below
# fails on a tree that has them on top, and the build stopped there on every build after the first)
for p in $(ls -r "$R"/userland/metal/runtime-patches/*.patch 2>/dev/null); do
	if git -C "$R/upstream/metal2vulkan" apply --reverse --check "$p" 2>/dev/null; then
		git -C "$R/upstream/metal2vulkan" apply --reverse "$p"
	fi
done
python3 "$R/tools/prepare_vulkan_userland.py" | tail -1
# begonia's changes to the runtime itself (userland/metal/runtime-patches), on top of the pinned reference patch: each
# applied once (already applied: left alone; neither: the build stops)
for p in "$R"/userland/metal/runtime-patches/*.patch; do
	[ -f "$p" ] || continue
	if git -C "$R/upstream/metal2vulkan" apply --reverse --check "$p" 2>/dev/null; then continue; fi
	git -C "$R/upstream/metal2vulkan" apply "$p" || { echo "runtime patch does not apply: $p" >&2; exit 1; }
	echo "m2v: runtime $(basename "$p")"
done
bridge=$R/out/metal2vulkan-bridge; mkdir -p "$bridge"
for f in "$R"/upstream/ios-s9plus-exynos/core/tools/metal/bridge/*; do [ -f "$f" ] && cp "$f" "$bridge/"; done
sed -i 's#\.\./src/crates/metal2vulkan#../../upstream/metal2vulkan/crates/metal2vulkan#g' "$bridge/Cargo.toml"
# begonia's changes to the bridge's own files (userland/metal/bridge/patches), on the fresh copy
for p in "$R"/userland/metal/bridge/patches/*.patch; do
	[ -f "$p" ] || continue
	patch --force -s -p1 -d "$bridge" -i "$p" || { echo "bridge patch does not apply: $p" >&2; exit 1; }
	echo "m2v: bridge $(basename "$p")"
done
# begonia's additions to the bridge (userland/metal/bridge): pipeline reflection, in the same file's scope
cp "$R"/userland/metal/bridge/begonia_*.rs "$bridge/"
echo 'include!("begonia_reflection.rs");' >> "$bridge/NativeMetalObjects.rs"
echo 'include!("begonia_indirect.rs");' >> "$bridge/NativeMetalObjects.rs"
echo 'include!("begonia_arguments.rs");' >> "$bridge/NativeMetalObjects.rs"
echo 'include!("begonia_surface.rs");' >> "$bridge/NativeMetalObjects.rs"
cat "$R/userland/metal/bridge/begonia_reflection.h" >> "$bridge/NativeMetalObjects.h"
cat "$R/userland/metal/bridge/begonia_surface.h" >> "$bridge/NativeMetalObjects.h"
linker=$R/out/metal2vulkan-ios-linker.sh
printf '#!/bin/bash\nexec "%s" -arch arm64 -isysroot "%s" "$@"\n' "$CC" "$SDK" > "$linker"; chmod +x "$linker"
# Built for size by default (out/metal2vulkan-target-ios-small): the boot partition has no room for the faster
# build (y21 did not fit). M2V_FULL=1: the faster one, into out/metal2vulkan-target-ios.
if [ "${M2V_FULL:-0}" = 1 ]; then T=$R/out/metal2vulkan-target-ios; else
	T=$R/out/metal2vulkan-target-ios-small${M2V_OPT:+-o$M2V_OPT}
	export CARGO_PROFILE_RELEASE_OPT_LEVEL=${M2V_OPT:-s} CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1
fi
export CARGO_TARGET_DIR=$T SDKROOT=$SDK IPHONEOS_DEPLOYMENT_TARGET=16.5
export CARGO_TARGET_AARCH64_APPLE_IOS_LINKER=$linker CC_aarch64_apple_ios=$linker AR_aarch64_apple_ios=llvm-ar
export RUSTFLAGS=-Dwarnings M2V_INSTALL_NAME=@rpath/libmetal2vulkan.dylib M2V_RUNTIME_ID=begonia-720ef1ce-s9-2d092398-dba02deb
echo "m2v: building (log: $R/out/metal2vulkan-ios-bridge-build.log)"
"$CARGO_HOME/bin/cargo" build --manifest-path "$bridge/Cargo.toml" --target aarch64-apple-ios --release \
	> "$R/out/metal2vulkan-ios-bridge-build.log" 2>&1 ||
	{ grep -E "^error" -A12 "$R/out/metal2vulkan-ios-bridge-build.log" | head -60; exit 1; }
lib=$CARGO_TARGET_DIR/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib
llvm-objdump --macho --private-headers "$lib" | grep -q -i -E "platform +(2|ios)$" || { echo "the runtime is not an iOS binary" >&2; exit 1; }
ls -la "$lib"
echo "m2v: built the iOS Metal-to-Vulkan runtime"
