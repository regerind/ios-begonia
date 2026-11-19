#!/bin/bash
# Linux port of tools/build_gpu_tests_ios.py for the two diagnostics a Metal image runs: metal_frontend_test (the
# Metal device through the plug-in: buffers, blits, an IOSurface texture, a clear) and vulkan_device_test (PanVK alone).
#   tools/build_gpu_tests_linux.sh       (WSL/Linux) -> out/native-gpu-tests/
set -e -o pipefail
R=$(cd "$(dirname "$0")/.." && pwd); H=$R/upstream/hakios-xnu-kernel
source "$H/xnu-env.sh"
SDK=$THEOS/sdks/iPhoneOS16.5.sdk; CC=$H/xnu-deps/host/bin/xnu-clang
O=$R/out/native-gpu-tests; mkdir -p "$O"
base=("$CC" -arch arm64 -isysroot "$SDK" -mios-version-min=16.5 -O1 -Wall -Wextra)
"${base[@]}" "$R/tests/metal_frontend_test.m" -fobjc-arc -framework Foundation -framework Metal -framework IOSurface \
	-framework IOKit -o "$O/metal_frontend_test"
"${base[@]}" "$R/tests/vulkan_device_test.c" -I "$R/upstream/mesa-vulkan/include" -o "$O/vulkan_device_test"
"${base[@]}" "$R/tests/spritekit_test.m" -fobjc-arc -framework Foundation -framework Metal -framework SpriteKit \
	-framework CoreGraphics -framework UIKit -o "$O/spritekit_test"
"${base[@]}" "$R/tests/scenekit_test.m" -fobjc-arc -framework Foundation -framework Metal -framework SceneKit \
	-framework UIKit -framework CoreGraphics -o "$O/scenekit_test"
"${base[@]}" "$R/tests/metal_game_test.m" -fobjc-arc -framework Foundation -framework Metal -o "$O/metal_game_test"
"${base[@]}" "$R/tests/wifi_cmd.c" -o "$O/wifi_cmd"
"${base[@]}" "$R/tests/cg_shapes_test.c" -framework CoreFoundation -framework CoreGraphics -framework IOSurface \
	-o "$O/cg_shapes_test"
"${base[@]}" "$R/tests/cpu_idle_test.c" -o "$O/cpu_idle_test"
"${base[@]}" "$R/tests/backlight_test.c" -o "$O/backlight_test"
"${base[@]}" "$R/tests/superpatch_test.c" -o "$O/superpatch_test"
"${base[@]}" "$R/tests/superpatch2_test.c" -o "$O/superpatch2_test"
"${base[@]}" "$R/tests/superpatch3_test.c" -o "$O/superpatch3_test"
"${base[@]}" "$R/tests/superpatch4_test.c" -o "$O/superpatch4_test"
"${base[@]}" "$R/tests/superpatch5_test.c" -o "$O/superpatch5_test"
"${base[@]}" "$R/tests/thread_cpu_test.c" -o "$O/thread_cpu_test"
"${base[@]}" "$R/tests/audio_test.c" -o "$O/audio_test"
ls -la "$O"
echo "gpu tests: built"
