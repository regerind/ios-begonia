# Handoff: backboardd on Metal (single GPU path) — brief for a one-shot agent

You are continuing work on **iOS 16 running on a Redmi Note 8 Pro (codename begonia, MediaTek MT6785, Mali-G76)**.
The repo is `c:\Users\user\Documents\dev\begonia-ios` (git branch `android-userdata-port`, local only — never push).
Read `CLAUDE.md` at the repo root first; its rules are binding. This file gives you everything else you need.

---

## 1. Goal

Make **backboardd** (the CoreAnimation render server, which composites the whole screen) draw through **Metal**,
like a real iPhone, so that the system has **one GPU path**:

```
today:   backboardd ─ CoreAnimation GL path ─ GL bridge (userland/src/eagl.c) ─ Mesa panfrost (OSMesa) ─┐
         SpringBoard/WebKit/apps ─ Metal ─ HakiosMetal frontend ─ metal2vulkan ─ Mesa panvk (Vulkan) ──┴─ kernel GPU service ─ Mali
goal:    everything ─ Metal ─ HakiosMetal ─ metal2vulkan ─ panvk ─ kernel GPU service ─ Mali
```

### Acceptance criteria (all must hold before you produce a persistent image)
1. backboardd runs on Metal (`metal=3`, `backboardd` in the Metal client list) and **stays up** for a 60 s session
   (same pid at the end; check `processes:` / "backboardd[13" in the log and the kernel exit notes).
2. **Frame rate**: the `INTERVALS` line shows normal animation rates (the GL path today: home screen idle 10 fps,
   animations 45–60 fps; YouTube 41–51 fps). Not 0–1 fps.
3. **Pictures are correct** — one screenshot each, all four, with the final settings:
   home screen, Control Center (`y_script_cc.txt`), Today view (`y_script_today4.txt`), YouTube (`y_script_youtube.txt`).
   Compare against the reference shots in `C:\begonia-private\logs\` (y163 Control Center, y199 Today, y200 YouTube).
   No black screens, no black boxes, blur/transparency visible.
4. **No new crash loops**: `grep "crashed [0-9] times"` — only the known `com.apple.Safari.History` is allowed.
5. Seek test (`y_script_seek.txt`) survives (no jetsam kills of backboardd/SpringBoard).
6. Only then build the persistent image **p12** (see §4.4). Do **not** flash it unless the owner says so.

---

## 2. Hard safety rules (never relax)

- **Never write the IMEI partitions** (nvram, nvdata, protect1/2, persist, proinfo, …). The phone had an IMEI repair.
  Flashing writes **only `boot` and `misc`** — the scripts below already do exactly that; do not add other partitions.
- **No voltage, clock or PMIC rail changes.** Not even "temporarily".
- Test sessions are **60 s** (`WATCH=60`). Do not hide the wallpaper, lower the resolution, or turn on Reduce
  Motion / Reduce Transparency to win speed. No per-app hacks (no special-casing app names to make a test pass).
- A test image (tag `yNNN`) runs iOS **once** (one-shot latch); a rerun needs a **new tag** (new build).
- Persistent images (tag `pNN`, boot-arg `begonia_persist=1`) boot iOS every time. Build only when §1 holds;
  flash only on the owner's explicit OK.

---

## 3. Documentation duties (the owner reads these; write them in **Turkish**)

- Before or as each build starts, add its row to `regerind-docs/BUILD-LOG.md` (tag, time, what changed); fill in the
  result after reading the log (numbers where possible). Use `date +%H:%M` for the time — do not guess.
- Open problems → `regerind-docs/ACIK-HATALAR.md`; solved → `regerind-docs/COZULEN-HATALAR.md` (with build, how fixed,
  how verified, remaining doubt — a "fix" may only hide the symptom; say so).
- Big root causes → `regerind-docs/NASIL-COZDUK.md`. Commit docs as you go (`git commit`, never push).
- Commit message trailer: `Co-Authored-By: <your model> <noreply@anthropic.com>` if your harness requires one.

---

## 4. Environment and the test loop

Host: Windows. Use **Git Bash** for the scripts below; builds run in **WSL2 Ubuntu** (`wsl -e bash -lc '…'`,
prefix with `MSYS_NO_PATHCONV=1` from Git Bash). The WSL working copy is `~/begonia-ios` (= `/root/begonia-ios`);
`auto_build.sh` syncs it from the Windows repo by `git fetch` + checkout of the branch, so **commit your changes
in the Windows repo before building**.

Scripts and logs: `C:\begonia-private\sh\` and `C:\begonia-private\logs\`. Python helpers you write: use the Write
tool, not shell heredocs (heredocs mangle `\n`).

### 4.1 Phone state at handoff
The phone currently runs **p9** (persistent). To start, the owner (or you, if the phone is already in the fallback)
must put it in **fastboot (hold Power + Volume-Down)**. After the first one-shot `y` image ran, the phone falls back
to a headless TWRP with a serial console, and every later flash works unattended (`auto_flash.sh` reboots it into
fastboot over serial). Before starting any new cycle: `ps aux | grep -E "y_cycle|auto_|fastboot_cycle"` must be empty.

### 4.2 One test cycle (build → flash → 60 s session → pull log → summary)
Standard setup (use this unless you have a reason not to; it adds the usual boot-args and Metal clients):

```bash
bash /c/begonia-private/sh/y_std.sh y204 "extra boot-args" /c/begonia-private/sh/<script>.txt [proc_cpu_test|none] \
     > /c/begonia-private/logs/y204-ycycle.txt 2>&1      # run in the background; ~5–6 min
```

`y_std.sh` does **not** put backboardd on Metal. For backboardd-on-Metal runs call `y_cycle.sh` directly:

```bash
bash /c/begonia-private/sh/y_cycle.sh y204 \
  "bl_power=1 begonia_wifi_rails=2 metal=3 begonia_audio=1 gllivecopy=0 cs_relax_platform_task_ports=1" \
  /c/begonia-private/sh/y_script_cc.txt \
  "!MobileSlideShow !Measure !MediaRemoteUI !mediaserverd @apps com.apple.WebKit.GPU com.apple.WebKit.WebContent SpringBoard backboardd" \
  proc_cpu_test > /c/begonia-private/logs/y204-ycycle.txt 2>&1
```

Arguments: TAG, extra boot-args, touch script, Metal client list, Metal tests (`proc_cpu_test` = CPU profiler +
10 ms stack sampler, needs `cs_relax_platform_task_ports=1`; or `none`).
Outputs: `logs/<TAG>-console.txt` (the kernel log ring, ~256 KB = roughly the **last 25 s** of the session),
`logs/<TAG>-ycycle.txt` (summary: fps per 5 s, `INTERVALS`, processes, slow frames), `logs/<TAG>-shot1.png`
(screenshot, if requested; view it with an image-capable Read tool).

### 4.3 Touch scripts and screenshots
- Scripts: `y_script_cc.txt` (Control Center + in-script shot), `y_script_today4.txt` (Today view + in-script shot),
  `y_script_youtube.txt` (YouTube video), `y_script_seek.txt` (video seeking), `y_script_panes.txt`.
- **Timing gotcha:** rwtouch's `wait N` counts from rwtouch's own start; the boot-arg `fb_shot=N` counts kernel time.
  For a picture after a touch, put `wait 3 notify com.hakios.shot` in the same script line (the cc/today4 scripts do).
  Add `fb_shot_step=12` to the boot-args so the shot fits in the log ring. For YouTube use `fb_shot=80 fb_shot_n=1
  fb_shot_step=12` (kernel-timed is fine there).
- If a shot is missing ("no SHOT lines"), something else filled the log ring; run again with less logging.

### 4.4 Building a persistent image (only after §1 holds, and only the build — flash needs the owner's OK)
```bash
MSYS_NO_PATHCONV=1 wsl -e bash -lc 'bash /mnt/c/begonia-private/sh/auto_build.sh p12 0 \
  " begonia_persist=1 metal=3 begonia_wifi_rails=2 begonia_audio=1 bl_power=1 <your final args>" "" \
  "!MobileSlideShow !Measure !MediaRemoteUI <your final client list>" "none"; sha256sum ~/begonia-ios/out/ios16-p12.img'
# flash (owner's OK only):  bash /c/begonia-private/sh/auto_flash.sh p12
```

### 4.5 Limits and gotchas
- **Kernel command line ≤ 600 characters** (the build asserts). Trace boot-args add up fast.
- The log ring keeps only the end of the session; an event in the first ~30 s (backboardd's start) is usually gone.
  Log less, or make the interesting thing happen late, or print counters periodically.
- Userland lines appear twice and interleaved in the log; dedupe when grepping.
- Rebuild the test binaries after editing `tests/*.c`:
  `wsl -e bash -lc 'cd ~/begonia-ios && git fetch -q origin && git checkout -q -B android-userdata-port origin/android-userdata-port && bash tools/build_gpu_tests_linux.sh'`.
- Userland patches live in `userland/patches/NNNN-*.patch` and are applied in order onto a fresh checkout by
  `tools/prepare_userland.py` every build (a later patch may change an earlier one's lines). Generate a new patch by
  diffing the WSL tree file (which has the series applied after a build) against your edited copy.
- Mesa patches (`userland/mesa-patches/`) are applied **once** (reverse-dry-run check): a later Mesa patch must never
  change an earlier one's lines; fold such changes into the earlier patch and restore pristine files from
  `out/mesa-25.0.7.tar.xz` if needed.

---

## 5. Where the Metal path lives

| Piece | Source | Build |
|---|---|---|
| HakiosMetal frontend (Obj-C++ `MTLDevice` etc.) | pinned in `upstream/ios-s9plus-exynos/metal-wip/frontend/` + `core/userland/src/`; begonia changes are **text patches inside `tools/build_metal_frontend_ios.py`** (asserted replacements); generated copy in `out/metal-frontend-ios/metalvk/NativeBridgeMetalFrontend.mm` | in WSL: `cd ~/begonia-ios && python3 tools/build_metal_frontend_ios.py --sdk $THEOS/sdks/iPhoneOS16.5.sdk --cc $PWD/upstream/hakios-xnu-kernel/xnu-deps/host/bin/xnu-clang --experimental-mps-stubs` → `out/metal-frontend-ios/HakiosMetal` |
| metal2vulkan (Rust: Metal → Vulkan runtime + shader translation) | `upstream/metal2vulkan/crates/` (`metal2vulkan-runtime`, `metal2vulkan`) | `tools/build_metal2vulkan_linux.sh`; staged from `out/metal2vulkan-target-ios-small/aarch64-apple-ios/release/libmetal2vulkan_bridge.dylib` (keep this unstripped copy for symbols) |
| panvk (Vulkan driver) | Mesa | `tools/build_panvk_linux.sh` |
| Staging into the image | `tools/make_gles_support.py` (`--metal-client`, client list file `HakiosMetal.clients`) | run by `auto_build.sh` |
| Which process gets Metal | `hakios_metal_shown.c` (client list: names, `!Name` excludes, `@apps`); boot-arg `metal=N` (`metal=3` also hands Metal to daemons not in the list — see §6.3) | |

`auto_build.sh` **does not rebuild** the frontend, metal2vulkan or panvk — it stages what is in `out/`. Rebuild them
yourself after changes, then run a cycle.

There is an existing, unused option: **`build_metal_frontend_ios.py --quartzcore`** — "enable the iOS 16 QuartzCore
pipeline library adapter" (it implements `newFunctionWithName:constantValues:functionCache:error:` and removes the
refusing `newPipelineLibraryWithFilePath:`). The builds so far did **not** pass it. Try it early (§7 step 2).

---

## 6. What is known (evidence)

### 6.1 Earlier attempt (y123, 2026-11-20 01:21)
`metal=3` + backboardd in the client list: the first backboardd crashed (crash line lost from the ring); the new one
started building Metal passes but produced almost no frames.

### 6.2 y202 / y203 (2026-11-20 09:44 / 09:50) — current code
Setup as in §4.2 (y203 also had the profiler). Result: **black screen, 0–1 fps**, backboardd did **not** crash
(pid 13 throughout) and logged **no errors**; it sent ~4500 GPU jobs per 5 s (~900/s) but presented almost nothing.
The CPU sampler (symbolized against the `-small` unstripped metal2vulkan and `out/metal-frontend-ios/HakiosMetal`):
- `-[NBDeviceMethods newRenderPipelineStateWithDescriptor:options:reflection:error:]` → metal2vulkan
  `Device::new_render_pipeline_state` → `pipeline::compile::StageEntries::specialized`,
  `pipeline::render::GraphicsState::create_adding`, pipeline cache `cache::codec::Codec::get`.
  **Hypothesis A:** CoreAnimation keeps asking for render pipeline states (normally it creates and caches them once);
  either our pipeline objects fail (nil/stub) or something makes CA's cache miss, so it compiles every frame.
  The frontend has many paths that return a **stub pipeline** (`state.handle=nullptr`) and warn
  ("returning stub pipeline") — a stub draws nothing, which would explain the black screen.
- `_platform_memmove` under `metal2vulkan_runtime::command_buffer::Transition::run` on the runtime's
  user-interactive submit thread (`exit_guard::spawn_user_interactive`). `Transition::run` only runs the command
  buffer's scheduled/completed handlers → the memmove is in a handler (frontend's or CoreAnimation's).
  **Hypothesis B:** a per-frame CPU copy of a full-screen surface (drawable/IOSurface) in a completion handler.

The frontend counts pipelines and timings (`gPipelines`, `gTimePipeline`, `native_bridge_metal_telemetry()` rows
"S c… p…", "T pipeline n=…", "V vkpipe n=…") **but nothing prints them in this build**. Making backboardd print that
telemetry every ~5 s (e.g. from the commit path, guarded by a time check) is the cheapest next measurement.

### 6.3 Side effect of `metal=3`: mediaserverd
`metal=3` gives Metal to mediaserverd even with `!mediaserverd` in the list. It crash-loops (16 restarts per session):
`texture refused: resource options: … UndefinedValue { ty: "MTLResourceOptions", raw: 524320 }` (0x80020 = Private
storage + an undefined bit 19, an Apple-private option metal2vulkan's `ResourceOptions` parser rejects —
`metal2vulkan-runtime/src/values.rs`) and `asked and not implemented: -[NBLibrary
newFunctionWithName:constantValues:pipelineLibrary:error:]`. Either keep mediaserverd off Metal (find where
`metal=3` overrides the list — `hakios_metal_shown.c` / the kernel plug-in naming) or fix both (mask unknown
private option bits; implement the selector like the `--quartzcore` functionCache variant).

### 6.4 Symbolizing samples
The sampler prints `image+offset`; for long image names the **offset is truncated to 4 hex digits** — the real
offset is those digits `<< 8` (e.g. `libmetal2vulkan.dylib+753d` → 0x753d00..0x753dff). Map against
`out/metal2vulkan-target-ios-small/.../libmetal2vulkan_bridge.dylib` with `llvm-nm -n --defined-only -C`
(the non-small target does **not** match the staged library). Shared-cache frames (`Image!offset`) cannot be named:
the cache has no local symbols. `C:\begonia-private\sh\samples_sym.py LOG TAG` symbolizes our GL libraries.

### 6.5 The GL path's lessons that apply here
- CoreAnimation draws every frame into a **new** display IOSurface; reads of the display while drawing elsewhere
  (backdrop blurs, 1:1 corner copies of widgets) must see the current frame's content; fake "previous frame" reads
  caused visual bugs (see COZULEN-HATALAR: widget corner marks; ACIK-HATALAR 10f: p9 black Control Center).
- Large per-frame costs are usually **per-frame full-screen work** (copies, clears, reloads), not shader math.

---

## 7. Suggested plan (adapt as evidence says)

1. **Baseline**: reproduce y203 under a new tag (your first build also proves your build/flash loop works).
2. Rebuild the frontend **with `--quartzcore`**; run the §4.2 cycle. Compare fps, pipeline count, picture.
3. **Instrument**: make the frontend (via a text patch in `build_metal_frontend_ios.py`) print
   `native_bridge_metal_telemetry()` rows from backboardd every 5 s, plus the first N **stub pipeline** warnings with
   the function names and the reason (they go to the console via `NBWARN`/`nbConsole`). Find which CA shaders fail.
4. Fix the failing pipelines in metal2vulkan / the frontend (unsupported features, function constants, pipeline
   libraries, vertex descriptors, …) until CA's pipeline creation count stops growing after warm-up.
5. Find and remove the per-frame memmove (Hypothesis B): sample deeper, or time the completion handlers; check how
   `CAMetalLayer`/drawables are implemented in the frontend (present path, IOSurface-backed drawables, copies).
6. Get the four screenshots of §1 right; then frame rate; then the seek test.
7. Handle mediaserverd (§6.3).
8. Build **p12** (§4.4), write the summary in Turkish in `regerind-docs/` (BUILD-LOG rows, NASIL-COZDUK entry), and
   tell the owner what changed, what was verified (log / screenshot), and what remains uncertain.

If the single path cannot reach §1 in your session, do not ship a broken p12: leave the GL path as the default,
document precisely how far you got and the next step, and stop.

---

## 8. Current defaults (so you know what the GL path does today)
Bridge patches on by default: 0054 (late clear of new display targets), 0055, 0057–0059 (1:1 display copies read
live — these cause the p9 black Control Center; **use `gllivecopy=0`** in every image until fixed), 0051 (backdrops
from the previous frame), Mesa 0009 (rename instead of flushing readers; unsafe with `glbackdrop=0` unless
`glenv=PAN_RENAME=0`). On a working Metal-only backboardd none of the GL bridge matters any more.
Reference builds: p10 (= p9 + `gllivecopy=0`, built, not flashed, SHA256 96022caf…de47) is the best GL-path image.
