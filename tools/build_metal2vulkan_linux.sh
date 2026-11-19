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
python3 "$R/tools/prepare_vulkan_userland.py" | tail -1
bridge=$R/out/metal2vulkan-bridge; mkdir -p "$bridge"
for f in "$R"/upstream/ios-s9plus-exynos/core/tools/metal/bridge/*; do [ -f "$f" ] && cp "$f" "$bridge/"; done
sed -i 's#\.\./src/crates/metal2vulkan#../../upstream/metal2vulkan/crates/metal2vulkan#g' "$bridge/Cargo.toml"
# begonia's additions to the bridge (userland/metal/bridge): pipeline reflection, in the same file's scope
cp "$R"/userland/metal/bridge/begonia_*.rs "$bridge/"
echo 'include!("begonia_reflection.rs");' >> "$bridge/NativeMetalObjects.rs"
cat "$R/userland/metal/bridge/begonia_reflection.h" >> "$bridge/NativeMetalObjects.h"
linker=$R/out/metal2vulkan-ios-linker.sh
printf '#!/bin/bash\nexec "%s" -arch arm64 -isysroot "%s" "$@"\n' "$CC" "$SDK" > "$linker"; chmod +x "$linker"
# Built for size by default (out/metal2vulkan-target-ios-small): the boot partition has no room for the faster
# build (y21 did not fit). M2V_FULL=1: the faster one, into out/metal2vulkan-target-ios.
if [ "${M2V_FULL:-0}" = 1 ]; then T=$R/out/metal2vulkan-target-ios; else
	T=$R/out/metal2vulkan-target-ios-small
	export CARGO_PROFILE_RELEASE_OPT_LEVEL=s CARGO_PROFILE_RELEASE_LTO=fat CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1
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
