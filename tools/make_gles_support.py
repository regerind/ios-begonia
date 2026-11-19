#!/usr/bin/env python3
"""Stage a private hardware GLES render test or opt-in EAGL compositor image."""
from pathlib import Path
import argparse,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--base-root',type=Path,required=True);p.add_argument('--output-dir',type=Path,required=True);p.add_argument('--ui',action='store_true')
p.add_argument('--metal-client',action='append',default=[],help='a program that is shown the Metal device (PanVK, metal2vulkan and the HakiosMetal bundle are staged; boot with metal=1)')
a=p.parse_args()
base=a.base_root.resolve();out=a.output_dir.resolve();dest=out/'root'
if base==dest or base in dest.parents or dest in base.parents or dest.exists():p.error('use a fresh separate output directory')
shutil.copytree(base,dest,symlinks=True)
stage=root/'out/gles-ios'
for name in ('libvulkan_panfrost.dylib','libmetal2vulkan.dylib'):
 (dest/'lib'/name).unlink(missing_ok=True)
metal_tests=''
if a.metal_client:
 # Metal for applications (the window server stays on OpenGL ES): the Vulkan driver, the Metal-on-Vulkan runtime, the
 # plug-in Metal loads and the list of programs that are shown the device. Built by tools/build_panvk_linux.sh,
 # tools/build_metal2vulkan_linux.sh and tools/build_metal_frontend_ios.py --cc. The kernel names the plug-in by a
 # path that leads from the system volume's Extensions to ext/ here (kernel patch 0018).
 # (stripped, and the runtime built for size if there is such a build: the boot partition's 64 MB hold this image, the
 # kernel and the fallback's ramdisk, and y21 did not fit)
 small=root/'out/metal2vulkan-target-ios-small/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib'
 full=root/'out/metal2vulkan-target-ios/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib'
 runtime=max((f for f in (small,full) if f.exists()),key=lambda f:f.stat().st_mtime)  # the one built last
 for source,name in ((root/'out/panvk-ios/libvulkan_panfrost.dylib','libvulkan_panfrost.dylib'),(runtime,'libmetal2vulkan.dylib')):
  subprocess.run(['llvm-strip','-S','-x','-o',str(dest/'lib'/name),str(source)],check=True)
 clients=''.join(n+'\n' for n in dict.fromkeys(['metal_frontend_test','metal_test','spritekit_test',*a.metal_client]))
 for bundle in (dest/'ext/HakiosMetal.bundle',):
  bundle.mkdir(parents=True,exist_ok=True)
  shutil.copy2(root/'out/metal-frontend-ios/HakiosMetal',bundle/'HakiosMetal')
  shutil.copy2(root/'upstream/hakios-xnu-kernel/userland/src/HakiosMetal.plist',bundle/'Info.plist')
  (bundle.parent/'HakiosMetal.clients').write_text(clients)
 # The shader libraries of the applications shown the device: the system image has no .metallib at all
 # (select-system.py skips them), so they come along here, ext/metallib/<bundle identifier>/, where the plug-in looks
 # when a bundle has none (userland/metal/frontend_reflection.mm.inc). Taken from sysimage/metallibs/, every
 # .metallib of the firmware's root filesystem, extracted from the IPSW with 7z (docs/SESSION-2026-11-19d.md).
 import plistlib
 sysimage=root/'upstream/hakios-xnu-kernel/sysimage'
 for app in sorted((sysimage/'rootfs/private/var/staged_system_apps').glob('*.app'))+sorted((sysimage/'rootfs/Applications').glob('*.app')):
  try:info=plistlib.loads((app/'Info.plist').read_bytes())
  except Exception:continue
  if '@apps' not in a.metal_client and info.get('CFBundleExecutable') not in a.metal_client:continue
  libraries=sysimage/'metallibs'/app.relative_to(sysimage/'rootfs')
  kept=dest/'ext/metallib'/info['CFBundleIdentifier'];kept.mkdir(parents=True,exist_ok=True)
  for library in sorted(libraries.glob('*.metallib')):
   shutil.copy2(library,kept/library.name);print('metal: '+library.name+' of '+info['CFBundleIdentifier'])
 # The frameworks' libraries that games draw with and that fit in the boot image (SceneKit's 2.1 MB and
 # MetalPerformanceShaders' 95 MB do not: they need the system volume), by their bundle's path.
 for path in ('System/Library/Frameworks/SpriteKit.framework/default.metallib',):
  if (sysimage/'metallibs'/path).exists():
   (dest/'ext/metallib'/path).parent.mkdir(parents=True,exist_ok=True)
   shutil.copy2(sysimage/'metallibs'/path,dest/'ext/metallib'/path);print('metal: /'+path)
 for t in ('metal_frontend_test','vulkan_device_test','spritekit_test'):
  if (root/'out/native-gpu-tests'/t).exists():
   shutil.copy2(root/'out/native-gpu-tests'/t,dest/'bin'/t);metal_tests+='/redwood/bin/'+t+'\n'
shutil.copy2(stage/'libOSMesa.8.dylib',dest/'lib/libOSMesa.8.dylib')
shutil.copy2(stage/'gles_render_test',dest/'bin/gles_render_test')
if a.ui:
 shutil.copy2(stage/'libhakios.gl.dylib',dest/'lib/libhakios.gl.dylib')
 # a scripted touch workload of the base image (make_ui_support.py --touch-script) is kept
 touch=''.join(line for line in (dest/'etc/rc.jobs').read_text().splitlines(True) if '/bin/rwtouch ' in line)
 # (the Metal tests late: the log keeps only the last half minute of a session)
 jobs=touch+'/redwood/bin/gles_render_test\n'+('sleep 50\n'+metal_tests if metal_tests else 'sleep 60\n')+'/bin/ps -A\n'
else:
 keep={'com.hakios.demo.plist','com.apple.notifyd.plist','com.apple.cfprefsd.xpc.daemon.plist'}
 for f in (dest/'LaunchDaemons').glob('*.plist'):
  if f.name not in keep:f.unlink()
 jobs='/redwood/bin/gles_render_test\n'
(dest/'etc/rc.jobs').write_text(jobs)
subprocess.run([sys.executable,str(root/'upstream/hakios-xnu-kernel/tools/apfs/mkapfs.py'),'-z','--plain','fw','-o',str(out/'root.apfs'),'redwood='+str(dest)],check=True)
print('Built private GLES support; no system image or phone writes')
