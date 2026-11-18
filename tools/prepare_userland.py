#!/usr/bin/env python3
"""Apply recorded support fixes; build init and input library without rebuilding Apple assets."""
from pathlib import Path
import re,subprocess
root=Path(__file__).resolve().parents[1]
repo=root/'upstream/hakios-xnu-kernel'
patches=sorted((root/'userland/patches').glob('*.patch'))
# Later patches touch the context of earlier ones, so "already applied" cannot be tested patch by
# patch. Instead the files the series touches are reset to the pinned upstream revision and the
# whole series is applied in order, on every run: the result is always exactly the series.
touched=sorted({m for p in patches for m in re.findall(r'^\+\+\+ b/(\S+)',p.read_text(),re.M)})
subprocess.run(['git','-C',str(repo),'checkout','HEAD','--',*touched],check=True)
for patch in patches:
 subprocess.run(['git','-C',str(repo),'apply',str(patch)],check=True)
script='''set -e
source "$1/xnu-env.sh"
ccflags=(-arch arm64 -Os -static -nostdlib -ffreestanding -fno-stack-protector -fno-builtin -Wall -Wextra)
xnu-clang "${ccflags[@]}" -Ilibmini -c libmini/mini.c -o obj/mini.o
xnu-clang "${ccflags[@]}" -c libmini/start.S -o obj/start.o
xnu-clang "${ccflags[@]}" -Ilibmini -Wl,-e,_start -Wl,-pagezero_size,0x100000000 -o obj/init src/init.c obj/mini.o obj/start.o
cp obj/init root/sbin/init
xnu-clang -arch arm64 -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" -O1 -dynamiclib -install_name /redwood/lib/libhakios.dylib -framework CoreFoundation -framework IOKit -lobjc -o obj/libhakios.dylib src/inject.c
cp obj/libhakios.dylib root/lib/libhakios.dylib
# SpringBoard's library (src/inject_sb.c), as userland/build.sh builds it
xnu-clang -arch arm64 -isysroot "$THEOS/sdks/iPhoneOS16.5.sdk" -O1 -dynamiclib -install_name /redwood/lib/libhakios.sb.dylib -framework CoreFoundation -framework CoreGraphics -framework IOKit -framework IOSurface -lobjc -o obj/libhakios.sb.dylib src/inject_sb.c
cp obj/libhakios.sb.dylib root/lib/libhakios.sb.dylib
'''
subprocess.run(['bash','-c',script,'build-init',str(repo)],cwd=repo/'userland',check=True)
print('Built support init, input and SpringBoard libraries; persistent system groups are preserved.')
