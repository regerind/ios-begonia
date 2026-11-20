# Building the latest persistent image (p18) from scratch

For someone with a Redmi Note 8 Pro (begonia, MT6785) and a new checkout. This is the shortest path that is known to
work; the reasons behind each step are in [PROCESS.md](PROCESS.md) and [history/LEARNED-2026-11-19.md](history/LEARNED-2026-11-19.md).
The newest numbers and the build log are in [regerind-docs/](regerind-docs/README.md) (Turkish).

> **This replaces Android on the phone's userdata.** The iOS system and data volumes live in raw `userdata`. Back
> everything up first. Only `boot` and `misc` are ever flashed; the IMEI partitions (nvram, nvdata, protect1/2,
> persist, proinfo, ...) must never be written. The tools in this repo refuse to map them (`tools/userdata_layout.py`).
> You cannot get iOS firmware from this repo: you bring your own IPSW. Experimental; not a daily driver.

## 0. What "p18" is

A *p* image boots iOS on every start (`begonia_persist=1`), with no test deadline. p18 = the Metal path for
backboardd (`metal=1`), backboardd's drawing thread on an A76, kext surface parking, the fast metal2vulkan runtime and
**no wallpaper** (`sb_nowall=1`: SpringBoard's wallpaper windows are hidden; about 51-53 fps on the home screen,
instead of about 46-48 with it). Build with `NO_WALLPAPER=0` to keep the wallpaper. The boot line is

```
begonia_persist=1 metal=1 begonia_wifi_rails=2 begonia_audio=1 bl_power=1 gllivecopy=0 fb_gpuseq=1 metal_swapwait=0 sb_nowall=1
```

Ways out of a bad image: **Power + Volume-Down at power-on** (LK fastboot, runs before our code), then flash another
image; or **hold Volume-Up while the logo shows** (the loader chains the TWRP fallback with a serial gadget).

## 1. Host

Windows 10/11 with **WSL2 Ubuntu 24.04**, or plain Linux. Work inside the Linux file system (`~/begonia-ios`), not
`/mnt/c`: unpacking the iOS root creates names NTFS rejects. 16 GB RAM, ~60 GB free (the IPSW alone unpacks to ~8 GB).

```sh
sudo apt install 7zip clang lld lz4 python3 git make build-essential llvm cmake ninja-build meson pkg-config \
     python3-mako flex bison android-sdk-libsparse-utils fastboot adb
```

(`7zip` must be 23 or newer. The Mesa/PanVK and Rust steps may ask for more packages; install what they name.) Also
needed: the **theos toolchain with the iPhoneOS 16.5 SDK** (the userland is cross-compiled for iOS with clang), and a
**Rust toolchain with the `aarch64-apple-ios` target** for the metal2vulkan runtime.

For the phone from Windows: `usbipd-win`, then `usbipd attach --wsl --hardware-id 18d1:d00d` while the phone is in
fastboot, and run `fastboot` inside WSL. (The device can be listed and briefly not attachable while it enumerates.)

## 2. Get the sources

```sh
git clone https://github.com/<you>/ios-begonia ~/begonia-ios && cd ~/begonia-ios
git remote add upstream https://github.com/tuxymuxy/ios-begonia   # this project is a fork; keep the link
mkdir -p upstream out private
```

`upstream/` holds the pinned third-party trees; their URLs and commits are in [sources.lock.json](sources.lock.json)
(HakiOS XNU kernel `upstream/hakios-xnu-kernel`, Mesa for GLES `mesa-gles-25` and for Vulkan `mesa-vulkan`,
`metal2vulkan`, `ios-sweet`, `viva-gpu`, ...). Clone each at its pinned commit into `upstream/<name>`.
Then prepare the HakiOS XNU tree as its own README says (`xnu/apply.sh`, `xnu-deps/setup.sh`): `prepare_kernel.py`
stops with "Prepare the pinned common XNU source first" if that was not done.

## 3. Bring your own inputs (`private/`, never committed)

| Needed | Where | How |
|---|---|---|
| iOS 16.5 IPSW for iPhone10,3/10,6 (build 20F66), SHA1 `6ee6e5b447902dbb36a3991ca20d032d6b4cc759` | unpacked into `upstream/hakios-xnu-kernel` (its `unpack.sh`) | your own download; the system image `userland/system.apfs` is built from it |
| The phone's own recovery dump, as `private/twrp/recovery.img` + `.sha256` | `private/twrp/` | `adb`/`fastboot` backup of your `recovery`; then `tools/patch_twrp_usb.sh` makes `recovery-adb.img` (the fallback kernel/ramdisk the loader chains) |
| Cleared misc | `private/misc/misc-bcb-cleared.img` | a zeroed BCB; a stale `boot-recovery` left by Android makes LK boot recovery forever |
| Novatek touch firmware `private/touch/novatek_ts_fw.bin` | 118784 bytes, SHA256 `0b57dd62863f0f20ea285263a667af3fdd86f48473e23ac638efeba5c8f6ad03` | from your phone's firmware (`vendor/firmware`) |
| Wi-Fi/BT firmware | `firmware/wifi/` | in the repo |

`tools/build_userdata_boot.sh` checks all of these and stops at the first one that is missing.

## 4. Build the Metal pieces (once, or when their sources change)

```sh
bash tools/build_gles_linux.sh                 # Mesa/OSMesa + Panfrost for the GL path, out/gles-ios
bash tools/build_panvk_linux.sh                # PanVK (Vulkan on Mali), out/panvk-linux ; then tools/build_panvk_ios.py
bash tools/build_metal2vulkan_linux.sh         # metal2vulkan host CLI + out/metal2vulkan-target-*
python3 tools/build_metal2vulkan_ios.py        # its iOS runtime (libmetal2vulkan_bridge.dylib); M2V_HOT=1 = built for speed
python3 tools/build_metal_frontend_ios.py --cc # the HakiosMetal plug-in, out/metal-frontend-ios
bash tools/build_system_with_metallibs.sh      # system image + the apps' .metallib files
```

p18 used the *hot* runtime (`M2V_HOT=1`: only the `metal2vulkan-runtime` crate at opt-level 3, the rest for size).
**Size matters:** the boot partition is 64 MiB and holds the image, the kernel and the fallback's ramdisk. p18 is
67,053,568 bytes: about 55 KB of headroom, so nothing larger fits (a full `M2V_OPT=3` runtime, 18.2 MB, did not). The
loader's payload limit is 48 MiB (`boot/xnu_loader.c`); `make_native_payload.py` rejects a larger payload at build time.

## 5. Build the image

```sh
tools/build_persistent.sh p19          # p19 = any new tag; writes out/ios16-p19.img and prints its SHA256
```

It runs `prepare_kernel.py`, builds the XNU kernel, the userland and the Metal support, stages the clients
(`!MobileSlideShow !Measure !MediaRemoteUI !mediaserverd @apps com.apple.WebKit.GPU com.apple.WebKit.WebContent
SpringBoard backboardd`) and packs the boot image with the p18 boot line. Variants:
`NO_WALLPAPER=0 tools/build_persistent.sh p19` keeps the wallpaper;
`EXTRA_ARGS=" begonia_persist=1 ..." tools/build_persistent.sh p19` sets another line (the kernel command line must stay
under ~600 characters). Check that a new string really is in the built library (`grep -c`): userland and Mesa compile
errors are hidden by the wrappers; look at `out/kbuild.log` for the kernel.

## 6. Flash

Put the phone in fastboot (Power + Volume-Down at power-on), attach it to WSL, then:

```sh
tools/flash_boot.sh p19                # writes boot and misc only, then reboots
```

The first flash of a userdata layout is a bigger job (the iOS system and data volumes are written into raw
`userdata` by `tools/flash_userdata_layout.sh` / `stage_userdata_flash.py`, with bounds in `tools/userdata_layout.py`);
see [STORAGE.md](STORAGE.md) and [PROCESS.md](PROCESS.md). Later p images only need the boot flash above, because the
system and data volumes stay.

A persistent image has no serial console: it is "working" when SpringBoard appears (about 2 minutes after the logo).
For test images (with a 60 s session, a log pulled over serial, scripted touches) the owner's workflow is in
[history/LEARNED-2026-11-19.md](history/LEARNED-2026-11-19.md) section 8; those images run iOS **once**, then fall back.

## 7. If something is wrong

- Redmi logo and nothing else: the loader's fallback. Either you flashed a test image that already ran (rebuild
  under a new tag), or the payload was refused (over 48 MiB, or the boot partition is full).
- iOS starts, the screen stays still, no touch: backboardd's virtual touch screen; see TOUCH.md.
- Anything else: [regerind-docs/ACIK-HATALAR.md](regerind-docs/ACIK-HATALAR.md) lists the known open problems.

## Not verified here

This guide was assembled from the repository and the build scripts of the person who built p18; the steps in section 4
were not re-run from an empty machine. If a step fails because a package or an upstream checkout is missing, fix it
and send the correction back as a pull request.
