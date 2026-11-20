# iOS 16 on Redmi Note 8 Pro (begonia)

> Fork of [tuxymuxy/ios-begonia](https://github.com/tuxymuxy/ios-begonia). The `android-userdata-port` branch carries this fork's work (Metal path, persistent images p1-p18).

Experimental iOS 16.5 / XNU port for Xiaomi Redmi Note 8 Pro, MediaTek MT6785 (MT6359 PMIC).

**Status** (p18, 2026-11-21): iOS 16.5 boots from raw `userdata` and runs as a persistent system (`begonia_persist=1`: iOS on every boot). SpringBoard, Safari, YouTube, Settings, sound, brightness, touch and Wi-Fi work. The window server (backboardd) draws through **Metal** on the Mali-G76 (metal2vulkan + PanVK): about 46-48 frames per second on home-screen swipes, 51-53 with SpringBoard's wallpaper hidden (p18). It is still **research code, not a daily driver**: there is no modem and no camera, and several visual and stability bugs are open (below).

---

## Feature checklist

Detailed and current open problems: [docs/regerind-docs/ACIK-HATALAR.md](docs/regerind-docs/ACIK-HATALAR.md) (Turkish).

### System
- ✅ XNU 8796 (HakiOS fork) boots on the MT6785 through the LK -> loader chain (`boot/`)
- ✅ iOS 16.5 (20F66) system mounted read-only from raw `userdata`, plus a 16 GiB HFS+ data volume
- ✅ Persistent boot: a restart comes back to iOS (`begonia_persist=1`); the phone's TWRP kernel is the fallback
- ✅ IMEI partitions untouched: only `boot` and `misc` are ever flashed
- ❌ Stable window server: backboardd is sometimes killed by jetsam under memory pressure and restarts

### Display and GPU
- ✅ Mali-G76 MC4 through Mesa Panfrost (GLES) and PanVK + metal2vulkan (Metal)
- ✅ backboardd draws through Metal; its drawing thread runs on an A76 core
- ✅ Tear-free output (panel scan-line counter), no black boxes on text or emoji
- ✅ Home-screen swipes about 46-48 fps (51-53 with SpringBoard's wallpaper hidden, as in p18)
- ❌ Steady 50-60 fps everywhere (blurs and the emoji keyboard are GPU-bound; YouTube is held near 30 fps)
- ❌ Hardware video decode (video is decoded in software)

### Input, sound, radios
- ✅ Touch (Novatek NT36672A), buttons, brightness (LM36273), flashlight
- ✅ Sound: earpiece (MT6359) and bottom speaker (TFA9874)
- ✅ Wi-Fi connects to networks (MT6785 port)
- ❌ Bluetooth (the stack initializes with synthetic HCI responses only)
- ❌ Modem, SIM, calls, mobile data (a CCCI/MD1 bridge to CommCenter is a large project)
- ❌ Camera

### Apps and services
- ✅ SpringBoard, Settings, Safari, YouTube
- ❌ Apple services such as the App Store (the device identity is invented and not activated)
- ❌ Today and App Library panels without freezes; Freeform, wallpaper settings, Safari history service
- ❌ Jailbreaking (Procursus, Sileo)

---

## Build

To build and flash the latest persistent image (p18) from scratch, follow [docs/BUILD-FROM-SCRATCH.md](docs/BUILD-FROM-SCRATCH.md). The development log is [docs/PROCESS.md](docs/PROCESS.md); all documentation is indexed in [docs/README.md](docs/README.md).

Host-side unit checks:
```sh
cc -std=c11 -Wall -Wextra -Werror tests/touch_report.c -o /tmp/test-touch && /tmp/test-touch
cc -std=c11 -Wall -Wextra -Werror tests/extent_policy.c -o /tmp/test-extents && /tmp/test-extents
```

---

## Source Layout

| Directory | Contents |
|-----------|----------|
| `boot/` | LK-to-XNU loader, early boot code |
| `kernel/` | Begonia-specific kernel drivers (Wi-Fi, BT, touch, GPU service, audio, backlight, UFS) |
| `patches/` | XNU patches and additions |
| `userland/` | iOS userland patches, Mesa patches, Metal frontend |
| `tools/` | Host build utilities (image build, flashing, layout) |
| `docs/` | All documentation; start at [docs/README.md](docs/README.md) |
| `firmware/` | Wi-Fi firmware, Mu experiment |
| `out/` | Build outputs (excluded from Git) |
| `private/` | Device-specific Apple firmware, captures (excluded from Git) |
| `upstream/` | Pinned upstream checkouts (HakiOS, Mesa, metal2vulkan, ...), see `docs/sources.lock.json` |

---

## Device Identity & References

- **Device**: Xiaomi Redmi Note 8 Pro (codename: `begonia`)
- **SoC**: MediaTek MT6785 (Helio G90T)
- **GPU**: ARM Mali-G76 MC4
- **Touch Controller**: Novatek NT36672A
- **PMIC**: MediaTek MT6359

---

## Licensing & Attribution

This project combines multiple components:
- **HakiOS XNU fork** (iOS 16 compatibility patches)
- **Mesa Panfrost / PanVK** (GPU drivers for Bifrost)
- **ios-sweet**, **metal2vulkan** and other pinned upstream work

See [NOTICE](NOTICE) for licensing details and attribution requirements.

**Never redistribute**: Apple firmware, private touch firmware, boot images, photos, or device dumps. These are excluded from Git intentionally.

---

## Important Notes

1. This is research work. Do not flash a phone unless you understand the risks; this replaces Android's userdata.
2. Keep the phone's original partition backups separately for recovery.
3. The repository excludes all private Apple files, device captures and firmware by design.
4. Build outputs in `out/` are regenerated per build and not tracked in Git.
