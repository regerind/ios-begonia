# iOS 16 on Redmi Note 8 Pro (begonia)

> Fork of [tuxymuxy/ios-begonia](https://github.com/tuxymuxy/ios-begonia). The `android-userdata-port` branch carries this fork's work (Metal path, persistent images p1-p18).

Experimental iOS 16.5 / XNU port for Xiaomi Redmi Note 8 Pro, MediaTek MT6785 (MT6359 PMIC).

**Status** (p18, 2026-11-21): iOS 16.5 boots from raw `userdata` and runs as a persistent system (`begonia_persist=1`: iOS on every boot). SpringBoard, Safari, YouTube, Settings, sound, brightness, touch and Wi-Fi work. The window server (backboardd) draws through **Metal** on the Mali-G76 (metal2vulkan + PanVK): about 46-48 frames per second on home-screen swipes, 51-53 with SpringBoard's wallpaper hidden (p18). It is still **research code, not a daily driver**: there is no modem and no camera, and several visual and stability bugs are open (below).

---

## What works

- **Boot**: LK -> loader (`boot/`) -> XNU 8796 (HakiOS fork). The iOS 16.5 (20F66) system is mounted read-only from raw `userdata`, with a 16 GiB HFS+ data volume. Only `boot` and `misc` are ever flashed; the IMEI partitions are never written. The phone's own TWRP kernel is chained as the fallback.
- **Display and GPU**: Mali-G76 MC4 through Mesa Panfrost (GLES path) and PanVK + metal2vulkan (Metal path, used by backboardd). Tear-free output (panel scan-line counter), no black boxes on text or emoji. backboardd's drawing thread runs on an A76 core.
- **Input and panel**: touch (Novatek NT36672A), buttons, brightness (LM36273), flashlight.
- **Sound**: earpiece (MT6359) and bottom speaker (TFA9874).
- **Wi-Fi**: ported to MT6785 and connects to networks. Bluetooth only initializes (synthetic HCI responses).
- **Apps**: Safari and YouTube run; YouTube is held near 30 fps by GPU load.
- **Persistent boot**: a restart comes back to iOS. Power + Volume-Down at power-on reaches fastboot; Volume-Up at the logo chains the serial fallback.

## What does not work, or only partly

The current, detailed list is [docs/regerind-docs/ACIK-HATALAR.md](docs/regerind-docs/ACIK-HATALAR.md) (Turkish). In short:

- **Modem, SIM, calls, mobile data**: no support (a CCCI/MD1 bridge to CommCenter is a large project). **Camera**: none.
- **Apple services** (App Store etc.): the device identity is invented and not activated.
- **Video** is decoded in software (hardware decode is not wired up).
- **Smoothness**: the goal is 50-60 fps; heavy scenes (blurs, emoji keyboard) are GPU-bound.
- **Stability**: backboardd is sometimes killed by jetsam under memory pressure, which restarts the window server; the Today and App Library panels have freezes; Freeform, wallpaper settings and the Safari history service have known problems.
- **Jailbreaking** (Procursus, Sileo) does not work.

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
