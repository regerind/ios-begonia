# Why the UI ran at ~4 FPS, and the road to 60

Analysis of 2026-11-18, from the previous owner's records (history/STATE.md, PROCESS.md, git history)
and the pinned HakiOS sources. Figures are theirs, from checksum-validated hardware captures
unless marked otherwise.

## Multi-core was not the missing piece

- v28/v29 (PROCESS.md:626-646): "firmware accepted CPU2 but 1 of 2 processors running". Cause,
  found in git: the secondary trampoline was `br x0`, so PSCI started cores entered XNU at **EL2**
  (MT6785's ATF starts cores at EL2; XNU runs at EL1). The bulk commit `2f1ef27` replaced it with an
  EL2->EL1 drop (HCR_EL2.RW, CNTHCTL/CNTVOFF, CPTR, ICC_SRE_EL2, SCTLR_EL1, eret) — `boot/start.S`.
- v78 capture (STATE "Independent v79 and v80 audit"): `GenericARMPE: 7 of 7 processors are running`,
  and still **4.28 FPS** median, 22.88 ms per frame inside the display driver.
- So SMP very likely works with the current tree; it simply did not move the bottleneck.
  `GenericARMPE.cpp:294` prints the line; the payload builder reserves MPIDR 0x100 for the
  recovery supervisor, so XNU gets at most CPU0 + 0x200..0x700 (six A55 + two A76 = 7).

## Where a frame's time actually goes

| Stage | Measured | Source |
|---|---|---|
| GPU (Mali-G76, Panfrost) | 0.45–0.8 ms | v62/v82/v83 captures |
| Display driver, 1125x2436 scaled to 1080x2340 | ~23 ms | v78 audit |
| Display driver, native 1080x2340 canvas (`fbwidth/fbheight`) | 1.7–3 ms | v62/v83 |
| CPU side of the GL bridge, ~433 GL calls/frame | ~43 ms | v84 (claimed, not re-verified) |
| Frame clock: CoreAnimation got no refresh notifications | 0 `tl: y` events | v83 (claimed) |

The GPU is idle almost all the time. The frame is bound by **one CPU thread** — CoreAnimation's
render server in backboardd plus the EAGL bridge — running on slow cores at low clocks:

- A55 cores: 1.175 GHz (1.275 GHz with `--cpu-boost`, verified v31).
- A76 cores: **1.085 GHz** from the PLL readback (v27), against ~2.05 GHz on Linux. An A76 at
  2 GHz does roughly 3–4x the work per second of an A55 at 1.2 GHz.
- The ADT gives every core `cluster-type 0`; XNU does not know which cores are fast.

## The order of work for 60 FPS

1. **Confirm SMP on this tree** (s-series builds, 7 cores, logs via the adb TWRP fallback).
2. **Native canvas + frame clock**: `fbwidth=1080 fbheight=2340` and `ca_vbl=1` (both upstream
   options: MiniIOMobileFramebuffer arms `kFBNotifyVSync`; inject.c sets `CA_FORCE_TIMER`).
3. **Measure** with the upstream timing lines (`tl:` per-frame, `glprof=`, `glslow=`), not guesses.
4. **Put the render thread on an A76** and raise the A76 cluster clock from the captured OPP
   table (needs the MT6359 VPROC rail for the big cluster: readback first, as done for VPROC2).
5. **Cut CPU work per frame** in the GL bridge where the profile says (call overhead, texture
   uploads/copies, `STILL_LOOKS` hashing), and hide the wallpaper tree (`wallpaper_freeze=2`).
6. **Zero-copy scanout**: point OVL layer 1 at the presented IOSurface instead of copying 10 MB
   per frame on the CPU.

Unverified claims in history/STATE.md v83–v100 (60 FPS, `anim_max`, `dl_max`, the lost `eagl.c` edits)
are not relied on here.
