#!/bin/bash
# Linux port of tools/build_panvk_ios.py (which needs macOS: xcrun, codesign, vtool, viva-gpu's macOS Mesa tools).
# Builds the native PanVK ICD (libvulkan_panfrost.dylib) for the Darwin userland with the kernel's cross compiler,
# the way tools/build_gles_linux.sh builds the GLES library.
#
#   tools/build_panvk_linux.sh       (WSL/Linux)
#
# Needs under upstream/: mesa-vulkan at 8e87a503 (Mesa 26.2.6), viva-gpu at ab2a1b43 (headers only),
# ios-s9plus-exynos at 2d092398. Steps: the native HakiosMali DRM transport as a static libdrm; the host tools this
# Mesa needs for Panfrost's precompiled shaders (mesa_clc, vtn_bindgen2, panfrost_compile); the cross build.
# Nothing is codesigned: the payload boots with code signing disabled (--disable-cs).
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); H=$R/upstream/hakios-xnu-kernel
M=$R/upstream/mesa-vulkan; V=$R/upstream/viva-gpu
source "$H/xnu-env.sh"
SDK=$THEOS/sdks/iPhoneOS16.5.sdk; CC=$H/xnu-deps/host/bin/xnu-clang
B=$R/out/panvk-linux; N=$R/out/native-vulkan-drm; S=$R/out/panvk-ios; mkdir -p "$B" "$S"
[ "$(git -C "$M" rev-parse HEAD)" = 8e87a503ebd91408c88596d1b9eeb5f4c0b2088d ] || { echo "Mesa revision mismatch" >&2; exit 1; }
for p in "$R"/userland/vulkan/patches/*.patch; do
	if git -C "$M" apply --reverse --check "$p" 2>/dev/null; then continue; fi
	git -C "$M" apply "$p" || { echo "PanVK patch does not apply: $p" >&2; exit 1; }
	echo "panvk: applied $(basename "$p")"
done

# ---- a Meson new enough for this Mesa (>= 1.4; the distribution's may be older) ---------
[ -f "$R/.tools/meson-src/meson.py" ] ||
	git clone -q --depth 1 -b 1.7.2 https://github.com/mesonbuild/meson "$R/.tools/meson-src"
MESON="python3 $R/.tools/meson-src/meson.py"

# ---- the native transport as libdrm -----------------------------------------------------
python3 "$R/tools/prepare_native_vulkan_drm.py" | tail -1
# begonia's changes to the staged transport (userland/vulkan/native-patches), on a fresh copy every build
for p in "$R"/userland/vulkan/native-patches/*.patch; do
	[ -f "$p" ] || continue
	patch --force -s -p1 -d "$N" -i "$p" || { echo "transport patch does not apply: $p" >&2; exit 1; }
	echo "panvk: transport $(basename "$p")"
done
objects=()
for n in hakios.drm hakios-native-posix hakios-native-sync hakios-native-surface-fd; do
	"$CC" -arch arm64 -isysroot "$SDK" -O2 -Wall -Wextra -I"$N" -I"$M/include" -I"$V/libdrm-xnu/include" \
		-I"$V/libdrm-xnu/include/libdrm" -c "$N/$n.c" -o "$N/$n.o" 2> "$N/$n-build.log" ||
		{ grep -E "error" -A3 "$N/$n-build.log" | head -20; exit 1; }
	objects+=("$N/$n.o")
done
rm -f "$N/libdrm-begonia-ios.a"; llvm-ar rcs "$N/libdrm-begonia-ios.a" "${objects[@]}"
mkdir -p "$N/pkgconfig"
cat > "$N/pkgconfig/libdrm.pc" <<PC
prefix=$N
Name: libdrm
Description: Begonia native HakiosMali transport
Version: 2.4.134
Libs: $N/libdrm-begonia-ios.a -framework IOKit -framework CoreFoundation
Cflags: -I$N -I$V/libdrm-xnu/include -I$V/libdrm-xnu/include/libdrm
PC
echo "panvk: native transport built"

# ---- host tools ---------------------------------------------------------------------------
if [ ! -x "$B/host/src/panfrost/clc/panfrost_compile" ]; then
	echo "panvk: building host tools (log: $B/host-build.log)"
	[ -f "$B/host/build.ninja" ] || $MESON setup "$B/host" "$M" --buildtype=release -Dgallium-drivers= \
		-Dvulkan-drivers=panfrost -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=false \
		-Dgles1=disabled -Dgles2=disabled -Dllvm=enabled -Dmesa-clc=enabled -Dprecomp-compiler=enabled \
		-Dzstd=disabled -Dexpat=disabled -Dshader-cache=disabled -Dxmlconfig=disabled -Dbuild-tests=false \
		> "$B/host-setup.log" 2>&1 || { tail -30 "$B/host-setup.log"; exit 1; }
	ninja -C "$B/host" src/compiler/clc/mesa_clc src/compiler/spirv/vtn_bindgen2 src/panfrost/clc/panfrost_compile \
		> "$B/host-build.log" 2>&1 || { grep -E "error|Error|unknown target" "$B/host-build.log" | head -30; exit 1; }
fi
export PATH="$B/host/src/compiler/clc:$B/host/src/compiler/spirv:$B/host/src/panfrost/clc:$PATH"

# ---- cross build ----------------------------------------------------------------------------
cat > "$B/cross.ini" <<INI
[binaries]
c = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK', '-I$R/userland/vulkan/compat', '-include', '$N/hakios-native-posix.h']
cpp = ['$CC++', '-arch', 'arm64', '-isysroot', '$SDK', '-I$R/userland/vulkan/compat', '-include', '$N/hakios-native-posix.h', '-fuse-cxa-atexit']
objc = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK']
ar = 'llvm-ar'
ranlib = 'llvm-ranlib'
strip = 'llvm-strip'
pkg-config = 'pkg-config'
cmake = 'false'
[properties]
needs_exe_wrapper = true
pkg_config_libdir = '$N/pkgconfig'
[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
INI
if [ ! -f "$B/ios/build.ninja" ]; then
	echo "panvk: configuring iOS build (log: $B/ios-setup.log)"
	$MESON setup "$B/ios" "$M" --cross-file "$B/cross.ini" --wrap-mode=nofallback --buildtype=release -Db_ndebug=true \
		-Dgallium-drivers= -Dvulkan-drivers=panfrost -Dllvm=disabled -Dplatforms= -Dopengl=false -Dgles1=disabled \
		-Dgles2=disabled -Degl=disabled -Dglx=disabled -Dgbm=disabled -Dmesa-clc=system -Dprecomp-compiler=system \
		-Dshader-cache=disabled -Dzstd=disabled -Dexpat=disabled -Dxmlconfig=disabled -Dbuild-tests=false \
		> "$B/ios-setup.log" 2>&1 || { tail -30 "$B/ios-setup.log"; exit 1; }
fi
echo "panvk: building libvulkan_panfrost (log: $B/ios-build.log)"
ninja -C "$B/ios" src/panfrost/vulkan/libvulkan_panfrost.dylib > "$B/ios-build.log" 2>&1 ||
	{ grep -E "error|Error" -A3 "$B/ios-build.log" | head -40; exit 1; }
lib=$S/libvulkan_panfrost.dylib
llvm-strip -x -o "$lib" "$B/ios/src/panfrost/vulkan/libvulkan_panfrost.dylib"
"$THEOS/toolchain/linux/iphone/bin/install_name_tool" -id /redwood/lib/libvulkan_panfrost.dylib "$lib"
llvm-objdump --macho --private-headers "$lib" | grep -q -i -E "platform +(2|ios)$" || { echo "PanVK is not an iOS binary" >&2; exit 1; }
llvm-nm -m -u "$lib" > "$S/undefined-symbol-audit.txt" || true
if grep "dynamically looked up" "$S/undefined-symbol-audit.txt" | grep -v "weak external"; then
	echo "panvk: required unresolved runtime symbols (above)" >&2; exit 1
fi
ls -la "$S"
echo "panvk: built out/panvk-ios/libvulkan_panfrost.dylib; hardware execution still needs a device test"
