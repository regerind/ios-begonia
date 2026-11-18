#!/bin/bash
# Linux port of tools/build_gles_ios.py --frontend (which needs macOS: xcrun, codesign, vtool).
# Builds the Panfrost GLES compositor for the Darwin userland with the kernel's cross compiler
# (xnu-clang + theos ld64), the same way upstream tools/mk-mesa.sh builds its freedreno variant.
#
#   tools/build_gles_linux.sh        (WSL/Linux; after tools/prepare_gles_userland.py)
#
# Panfrost in Mesa 25.0.7 needs no host CL precompilers (only Intel/Asahi do). It cross-builds
# libOSMesa.8.dylib (panfrost + softpipe), then the EAGL frontend libhakios.gl.dylib and the
# gles_render_test program, into out/gles-ios/ exactly where make_gles_support.py expects them.
# Signatures: the payload boots with AMFI/code signing disabled (--disable-cs), as upstream's
# Linux Mesa build does; nothing here is codesigned.
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); H=$R/upstream/hakios-xnu-kernel
M=$R/upstream/mesa-gles-25/mesa-25.0.7
source "$H/xnu-env.sh"
SDK=$THEOS/sdks/iPhoneOS16.5.sdk; CC=$H/xnu-deps/host/bin/xnu-clang
B=$R/out/gles-linux; S=$R/out/gles-ios; mkdir -p "$B" "$S"
[ -f "$M/.begonia-gles-patches.json" ] || { echo "run tools/prepare_gles_userland.py first" >&2; exit 1; }


# ---- cross build -----------------------------------------------------------------------
cat > "$B/cross.ini" <<INI
[binaries]
c = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK', '-I$R/userland/vulkan/compat']
cpp = ['$CC++', '-arch', 'arm64', '-isysroot', '$SDK', '-I$R/userland/vulkan/compat', '-fuse-cxa-atexit']
objc = ['$CC', '-arch', 'arm64', '-isysroot', '$SDK']
ar = 'llvm-ar'
ranlib = 'llvm-ranlib'
strip = 'llvm-strip'
pkg-config = 'false'
cmake = 'false'
[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
[properties]
needs_exe_wrapper = true
INI
if [ ! -f "$B/ios/build.ninja" ]; then
	echo "gles: configuring iOS build (log: $B/ios-setup.log)"
	meson setup "$B/ios" "$M" --cross-file "$B/cross.ini" --wrap-mode=nofallback --buildtype=release -Db_ndebug=true \
		-Dplatforms= -Dgallium-drivers=panfrost,softpipe -Dvulkan-drivers= -Dglx=disabled -Degl=disabled -Dgbm=disabled \
		-Dosmesa=true -Dllvm=disabled -Dshared-glapi=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true \
		-Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled -Dshader-cache=disabled -Dvalgrind=disabled \
		-Dlibunwind=disabled -Dglvnd=disabled -Dvideo-codecs= -Dtools= -Dgallium-vdpau=disabled -Dgallium-va=disabled \
		-Dgallium-xa=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dmesa-clc=auto -Dprecomp-compiler=system \
		> "$B/ios-setup.log" 2>&1 || { tail -30 "$B/ios-setup.log"; exit 1; }
fi
echo "gles: building libOSMesa (log: $B/ios-build.log)"
ninja -C "$B/ios" src/gallium/targets/osmesa/libOSMesa.8.dylib > "$B/ios-build.log" 2>&1 ||
	{ grep -E "error|Error" "$B/ios-build.log" | head -30; exit 1; }

lib=$S/libOSMesa.8.dylib
llvm-strip -x -o "$lib" "$B/ios/src/gallium/targets/osmesa/libOSMesa.8.dylib"
"$THEOS/toolchain/linux/iphone/bin/install_name_tool" -id /redwood/lib/libOSMesa.8.dylib "$lib"
llvm-objdump --macho --private-headers "$lib" | grep -q -i -E "platform +(2|ios)$" || { echo "libOSMesa is not an iOS binary" >&2; exit 1; }
FLAGS=(-arch arm64 -isysroot "$SDK" -I"$R/userland/vulkan/compat")
"$CC" "${FLAGS[@]}" -O1 -Wall -Wextra -Werror -I"$M/include" "$R/tests/gles_render_test.c" -o "$S/gles_render_test"

U=$H/userland; llvm-nm -gU "$lib" | awk '$3 ~ /^_gl/ { print substr($3, 2) }' > "$S/exported-gl.txt"
python3 "$U/mkeagl.py" "$U/src/eagl_slots.txt" "$M/src/mapi/glapi/registry/gl.xml" "$S/exported-gl.txt" "$U/src/eagl.c" > "$S/eagl_gen.h"
"$CC" "${FLAGS[@]}" -O2 -dynamiclib -install_name /redwood/lib/libhakios.gl.dylib -I"$S" \
	-framework CoreFoundation -framework IOKit -framework IOSurface -lobjc "$lib" \
	-o "$S/libhakios.gl.dylib" "$U/src/eagl.c" "$U/src/gl_osmesa.c"
ls -la "$S"
echo "gles: built out/gles-ios (libOSMesa.8.dylib, libhakios.gl.dylib, gles_render_test); hardware not yet qualified"
