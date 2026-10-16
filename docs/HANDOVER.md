# Phantom Hourglass Recomp — technical handover (2026-10-16)

For the next AI assistant working on this repository on the owner's **home PC**. It covers what the project is, how
it is built and tested, everything changed on 2026-10-16, the open problem being worked on (the compute renderer on
the phone), and what to do next. `PH-PORT.md` is the running port log; `README.md` is the user-facing description.

---

## 0. What you may do on the home PC

You have **full access** on the home PC. Earlier work happened on a locked-down work PC where nothing could be built;
that restriction does **not** apply to you. You are expected to:

- **Build** the PC game and the Android APK yourself (the scripts in `scripts/`, CMake, Gradle).
- **Run** the PC game, including scripted test runs (`4-TEST.ps1`) and manual runs with command-line options.
- **Install** the APK on the owner's phone (a Google Pixel 8, Android, USB debugging on) with
  `scripts/6-INSTALL-PHONE.ps1` or `adb install`.
- **Read the phone's log** over adb and **take screenshots** of the phone screen (`adb exec-out screencap -p`), and
  look at them to judge rendering.
- Edit, commit and push to this repository.

Rules that still apply:

- **Never commit or share game data**: no ROMs (`*.nds`), APKs, game folders, recompiled C output, saves (`*.sav`),
  save states. `tools/check_no_game_data.py` is the guard; run it before every commit. The repository is private and
  will be made public by the owner later; keep it publishable.
- **Protect the saves.** The phone's save is `files/saves/azee.sav` inside the app; the PC's is
  `games/PhantomHourglass/data/saves/azee.sav`. Never overwrite them; `6-INSTALL-PHONE.ps1` never replaces a phone save.
- **Commit identity**: `dguillot-gh <dguillot90@gmail.com>`. Do not use any other email.
- **Phone logs: filter to this app only** (the tags below). Do not dump the phone's whole log: other apps' data is private.

---

## 1. The project

A native port of *The Legend of Zelda: Phantom Hourglass* (Nintendo DS, USA, game code AZEE) to Windows and Android by
**static recompilation**:

- `recompiler/` (`ntr-recomp`): reads the ROM, finds every ARM9 function (boundaries from the zeldaret/ph
  decompilation's ds-decomp analysis, `frontend/config/azee/functions.txt`) and emits C. Hooks are declared in
  `frontend/config/azee/hooks.txt` (`<module> <address> replace|before|instead <c_function>`), resolved by name through
  `ph::find_hook` (`frontend/src/ph_hooks.cpp`).
- `runtime/` (`ntr-runtime`): runs the recompiled code natively and drives a **pinned melonDS core** for the rest of
  the console (ARM7, timers, DMA, IRQs, cartridge, 2D/3D GPUs, sound). Guest threads run on host fibers
  (`runtime/src/fiber.cpp`, ARM64 asm on Android). Save states: `runtime/src/savestate.cpp`.
- `third_party/`: melonDS as a git submodule, **patched at CMake configure time**: `third_party/CMakeLists.txt` reads
  melonDS sources, applies text patches with `ffcc_patch(var file from to)` (fails the configure if `from` is gone)
  and writes them to `<build>/third_party/melonds_patched/`. The patches tagged `// ffcc patch` came with the engine
  (forked from JonathanDC64/FFCCEOT-Recomp); ours are tagged `// ph patch`.
- `frontend/`: the program. `main.cpp` (SDL3 window, input, game thread, save files, settings.ini, Continue/resume,
  menus wiring), `gl_present.cpp` (draws the runtime's GL texture; desktop GL 4.3/3.3 or GLES 3.2 on the phone),
  `menu.cpp` (PC F1 side menu), `layout.hpp` (screen placement, touch mapping, widescreen), `ph_hooks.cpp` (PH hooks),
  `ph_stick.cpp` (analog stick), `gles_compat.cpp` (desktop-GL calls emulated on GLES, function loading),
  `android_bridge.cpp` (JNI for the Android app).
- `android/`: Gradle app `com.phrecomp.ph`. `LauncherActivity` (start screen: ROM/save import, Continue, Play),
  `GameActivity` (SDLActivity in process `:game`; quitting ends the process), `DsPad` (on-screen controls),
  `PhMenu` (side menu), `PhInput` (JNI declarations), `FpsBadge`.
- The game logic runs at the DS's 59.83 Hz. The renderer choices: Software (melonDS soft renderer, exact, 1x),
  GPU (melonDS OpenGL renderer, 2x-8x), **Compute** (melonDS compute-shader renderer, desktop GL 4.3; being ported to
  GLES 3.2 for the phone, see section 4).

### Workspace layout (the USB drive; at home it is usually `J:`)

```
<workspace>\
  ph-recomp\            this repository
  roms\                 the owner's ROM dumps (found by SHA-1; never commit)
  games\PhantomHourglass\  the PC game folder made by 3-MAKE-GAME-FOLDER (data\ = settings, saves, states)
  apks\                 PhantomHourglass-latest.apk (never commit)
  logs\                 every script's log; 4-TEST screenshots
  work\                 intermediate files (ds-decomp analysis, patched ROMs)
```

Supported dumps (SHA-1): clean USA `4c8f52dd719918bbcd46e73a8bae8628139c1b85`, D-pad patch
`7e0d26d143992f1eee182b24f1abb9e7f038eacf` (the owner plays the D-pad patch).

---

## 2. Building, running, testing

All scripts take `-DryRun`. Run from the workspace with `powershell -NoProfile -ExecutionPolicy Bypass -File ...`.

| Script | Does |
|---|---|
| `scripts/RUN-ALL.ps1` | 1-INSTALL-TOOLS, 2-BUILD, 3-MAKE-GAME-FOLDER, 4-TEST |
| `scripts/2-BUILD.ps1 [-Clean] [-Jobs N] [-GenerateOnly]` | CMake presets `release` and `gles-test` (Ninja, clang-cl). Outputs `build/release/frontend/PhantomHourglass.exe` + `ph_game.dll`; regenerates the recompiled C when hooks/config change |
| `scripts/3-MAKE-GAME-FOLDER.ps1` | `games/PhantomHourglass/` (never overwrites a save) |
| `scripts/4-TEST.ps1` | ~1 min scripted runs (software, GPU 4x, gles-test), screenshots and logs in `logs/test-*`, save state slot test |
| `scripts/RUN-ANDROID.ps1` | ANDROID-TOOLS, 5-BUILD-ANDROID, 6-INSTALL-PHONE |
| `scripts/5-BUILD-ANDROID.ps1` | Gradle APK into `apks/` (runs `tools/check_gles_shaders.py` first) |
| `scripts/6-INSTALL-PHONE.ps1` | installs the APK, pushes the ROM into the app, a save only if the phone has none |

PC program options (`PhantomHourglass.exe <rom> ...`): `--renderer software|gpu|compute`, `--scale 2|4|6|8`,
`--wide`, `--textures pixels|smooth|sharp`, `--fixes 0-3`, `--continue`, `--data-dir D`, `--save-file F`,
`--input-script S`, `--exit-after-frames N`, `--unpaced`, `--fullscreen`. PC keys: F1/Esc menu, F5/F8 save/load
state, F9 continue, Tab/F2 screens, F3 smooth, F4 FPS, F11 fullscreen, F12 screenshot, \` fast-forward, M mic, C lid.

### Checks that need no build

```
python -I tools/check_no_game_data.py                       # before every commit
python tools/check_gles_shaders.py <cmake.exe> <glslc.exe>   # the GPU renderer's GLES shaders (25/25)
python tools/check_gles_compute.py <cmake.exe> <glslc.exe> [scale]   # the compute shaders as GLSL ES 3.20 (33/33)
```

`glslc` is in the Android NDK: `<sdk>/ndk/28.2.13676358/shader-tools/windows-x86_64/glslc.exe`; CMake is in the SDK
(`<sdk>/cmake/3.22.1/bin/cmake.exe`) or installed. **glslc accepting a shader does not prove the Mali driver accepts it
or runs it correctly** (see section 4).

### The phone: install, log, screenshots

```
adb devices                                   # the Pixel 8
adb logcat -c
adb logcat -v time phrecomp:V AndroidRuntime:E DEBUG:F libc:F *:S      # the game's log only (tag phrecomp)
adb shell pidof com.phrecomp.ph:game          # the game process (logcat --pid=<pid> for just it)
adb exec-out screencap -p > shot.png          # a screenshot to look at
```

- The program's stdout/stderr go to logcat under tag `phrecomp` (`android_bridge.cpp`); melonDS's messages appear as
  `[melonDS] ...`. Renderer changes log `video: ...` lines; the compute renderer logs `Scale:`/`TileScale:` and one
  "from cache was rejected" line per shader (harmless: melonDS's shader cache is disabled).
- Toggling Renderer GPU → Compute in the phone menu recompiles the compute shaders and logs them again (useful when
  the start of the log was missed).
- Use **one** adb (the SDK's). Two different adb versions (e.g. the drive's and a system one) kill each other's server
  and any running `logcat` stops.
- The app's files: `adb shell run-as com.phrecomp.ph ls files` (`rom/azee.nds`, `saves/azee.sav`, `states/`,
  `settings.ini`). Save states are `states/slot-NN.{state,png,txt}`; the Continue state is `states/resume.*`.

---

## 3. What was done on 2026-10-16 (all pushed to `main`)

In order (newest last). Each commit message explains the change in detail.

| Commit | Change |
|---|---|
| `ecc27ad` | **Wireless removed** (FFCC's local wireless, same-PC LAN, libslirp NAT; `Platform::MP_*`/`Net_*` are stubs). **Continue**: resume state written on quit and when the phone goes to the background; `--continue`, PC F9/menu, phone start-screen button; deleted when the game saves itself after 30 s of play. **Phone layout editor** (move/resize/hide controls, one layout per orientation, prefs `lay_land_<id>`/`lay_port_<id>`). **Analog stick** (`ph_stick.cpp`). **Widescreen test** (hook on `G3i_PerspectiveW_`). `Runtime::peek32`. |
| `1f164dc` `9414e59` `7064cf9` | Widescreen HUD: sprite clusters moved to the wide edges (`FfccHudEdges`), recorded per frame (`FfccHudMoved`, `Frame::hud_move`) so touches map back (`ph::to_ds`); `FfccHudSides` (rupee digits with their icon, the bottom menu bar with the Menu button); sticky moves (a sprite moved last frame, same tile/palette/place, keeps its move: the Menu button under a dialogue box). 3D texture filtering menu (`texture_smoothing`). |
| `ad69e06` | README: clean dump vs D-pad patch (use the analog stick with the clean dump). |
| `e41d874` | "3D fixes" menu: better polygons / stable textures (did not fix the streaks). |
| `52cfaee` | GL renderer: texture wrap modes through sampler objects bound per draw (did not fix the streaks; kept, correct). |
| `7ec0aa3` `c35329a` | README: known issue, pictures (PC 16:9/4:3/software/menu/states, phone stick/4:3/menu). |
| `cda3650` | PC menu: Renderer Software / GPU / **Compute**; phone menu switches drawn with a visible track/knob (the app's old theme drew them as faint ON/OFF text). |
| `5f92fd2` → `69afb3f` | **The compute renderer on the phone** (section 4). |

The owner also has a PS1 recomp framework (separate repo `dguillot-gh/psxrecomp-android`, branch `android`) where a
Continue button was added the same day (`58e9fd9c`); not part of this repository.

### Phantom Hourglass addresses used (USA, from zeldaret/ph `config/usa/arm9/symbols.txt`)

| What | Address | Where used |
|---|---|---|
| `gPlayerControl` (PlayerControl*) | `0x027E0FB8` (DTCM) | `ph_stick.cpp`: touch X/Y `+0x10/+0x14`, `mTouchDist` `+0x58` (q20, 4096 = 1 px), `mTouchAngle` `+0x6A` (s16), `mUnk_7c` `+0x7C` (stylus moves Link now) |
| `G3i_PerspectiveW_` | main `0x02005BFC` (ARM) | `hooks.txt` before-hook `ph_perspective_aspect`: r2 = aspect (0x1555 = 4:3) |
| `OS_SaveContext` / `OS_LoadContext` / `OS_InitContext` | `0x0200DD04` / `0x0200DD50` / `0x0200DC98` | threads on fibers |
| `OS_IrqHandler` | ITCM `0x01FFA48C` | IRQ dispatch |

The analog stick works by holding a virtual stylus at Link + the stick direction (22-80 px out). Link's screen
position = touch point minus the game's own `mTouchDist` along `mTouchAngle`; which way the angle points is learned in
the first 30 frames (logged `stick: the game's touch angle points ...`). The owner reports it works very well.

---

## 4. The open problem: streak-free 3D on the phone

### 4.1 The GPU (OpenGL) renderer streaks

Walking around, some ground textures (rock borders, edges where surfaces overlap) smear into horizontal streaks.
Seen with melonDS's **OpenGL renderer on the PC and on the phone**, at every internal resolution, with or without
widescreen. **Not** seen with the software renderer, and **not with the compute renderer on the PC** (owner tested).

Ruled out: widescreen, scale, texture filtering, better polygons, stable textures (the "3D fixes"), wrap state
(sampler objects), the GLES 1555REV converter (the texture cache is RGBA8UI), depth format (D24S8 like the PC).
The cause is inside melonDS's OpenGL renderer as used here (not investigated further: the decision was to use Compute).

### 4.2 Goal: the compute renderer on the phone (OpenGL ES 3.2, Mali-G715)

melonDS's compute renderer (`GPU3D_Compute.cpp`, shaders in `GPU3D_Compute_shaders.h`, 33 variants assembled in
`ComputeRenderer3D::CompileShader` with #defines and tile constants per scale) is desktop GLSL 4.30. What was done:

1. **`5f92fd2`**: an ES 3.2 header on Android (`#version 320 es`, precisions for float/int/usampler2D/usampler2DArray/
   sampler2DArray/uimageBuffer/image2D), patched into a copy of `GPU3D_Compute.cpp` in `melonds_patched/` (its header
   is written beside it so `#include "GPU3D_Compute_shaders.h"` picks the patched one). gles_compat loads
   `glDispatchCompute(Indirect)`, `glMemoryBarrier`, `glBindImageTexture`, `glTexBuffer`, `glDebugMessageCallback`,
   `glDeleteSamplers` (glad loads them only for desktop 4.3). Runtime `compute_available()` (`runtime/src/runtime.cpp`)
   allows Compute on ES 3.1+; else falls back to OpenGL and the phone menu says so. Phone Compute is capped at 4x
   (tile memory ~ 4*TileSize^2*MaxWorkTiles bytes per layer). Menu: Renderer Software/GPU/Compute; GameActivity passes
   `--renderer compute`.
2. **`bb7e1f3`**: the Pixel's driver (Mali-G715, `v1.r54p3`) has **no `GL_EXT_shader_implicit_conversions`** (116
   extensions). So **`third_party/gles_compute_fixes.cmake`** writes out every int-to-uint conversion desktop GLSL makes
   implicitly (~97 `gles_compute_fix(from to)` text replacements: `0` → `0U` beside uints, `uint(...)` around ints
   assigned to uints or added to uint offsets, the division `startTable` as an int array read through `uint()`, three
   ambiguous `min()`). The PC compiles the same fixed shaders. `tools/check_gles_compute.py` runs that file through CMake
   and compiles all 33 variants with glslc: they pass as GLSL ES 3.20 and as desktop GLSL 4.30, 2x-8x.
3. **`e1100f6`**: on the phone 32 of 33 compiled; Mali rejected **BinCombined**: `Function call discards 'readonly'
   access qualifier` (a `Polygon` passed straight out of the readonly `PolygonBuffer` to `BinPolygon()`). Fixed by
   copying to a local first. The 3D was blank until then (no binning).
4. **`4e9ec3f`**: all shaders compile and Compute runs at ~60 fps on the phone, **but the 3D comes out in scrambled
   tiles**. Added `memoryBarrierShared()` before `barrier()` in BinCombined (its shared `mergedMaskShared`), and
   `GL_TEXTURE_FETCH_BARRIER_BIT` after the final pass (the 2D compositor samples the result as a texture).
   **Still scrambled.**
5. **`69afb3f` (NOT YET TESTED on the phone)**: the scramble's block size follows the tile size (bigger blocks at 2x
   than 4x): tiles draw each other's work slots, i.e. the work offsets/counts are wrong. Those come from `atomicAdd` on
   **one component of a uvec4** (`VariantWorkCount[i].z/.w` in the `BinResultBuffer` SSBO). GLSL ES allows atomics on
   buffer integer variables and array elements; a component of a vector is doubtful on Mali. `VariantWorkCount` is now
   a flat `uint[MaxVariants*4]` (same memory; the host already declares `u32 VariantWorkCount[MaxVariants*4]` in
   `BinResultHeader`), all atomics on plain elements.

**Next step: build `69afb3f`, run Compute on the phone, screenshot.** If still scrambled:

- **First, run Compute on the PC with the same build.** The PC now compiles the fixed shaders too. If the PC is also
  scrambled, one of the conversions in `gles_compute_fixes.cmake` changed a meaning: bisect them (each is
  independent; reverting one at a time on the PC is quick). If the PC is clean, it is a Mali/driver difference.
- The compute pipeline per frame (`GPU3D_Compute.cpp`, around `RenderFrame`): ClearCoarseBinMask → ClearIndirectWorkCount
  → InterpXSpans (writes `XSpanSetups`) → barrier → **BinCombined** (coarse/fine bin masks, `WorkDescs` unsorted,
  per-variant counts via atomics) → barrier → CalcOffsets (`SortedWorkOffset` prefix via atomicAdd, `SortWorkWorkCount`)
  → barrier → SortWork (indirect; sorted `WorkDescs`, bits 11-31 of `.y` = the unsorted index = the tile slot) → barrier
  → per variant: **Rasterise** (indirect, z = work count; writes `ColorTiles/DepthTiles/AttrTiles` at slot*TileSize^2)
  → barrier → **DepthBlend** (walks the bin masks; slot = `BinningWorkOffsets[tile]` + n-th set bit; writes
  `ResultValue`) → barrier → **FinalPass** (image `FinalFB`). Scrambled tiles mean Rasterise's slot for a work item
  and DepthBlend's slot for the same tile/polygon disagree, or the positions in `WorkDescs` are wrong.
- **Debug by reading buffers back**: after a frame, `glMapBufferRange` the `BinResultMemory` (VariantWorkCount,
  SortedWorkOffset, bin masks, work offsets) and `WorkDescMemory`, log a few tiles, and compare the PC and the phone at
  the same save state and frame (the save states load on both; use the same slot). The pass whose output differs is
  the one to fix.
- Candidates still open: Mali behaviour of atomics in general (try `atomicAdd` results vs a non-atomic two-pass count),
  `gl_WorkGroupID.z` dispatch limits (`GL_MAX_COMPUTE_WORK_GROUP_COUNT` z, logs: `glGetIntegeri_v`), SSBO size/binding
  limits (`GL_MAX_SHADER_STORAGE_BLOCK_SIZE`, `GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS`: Rasterise uses 7 buffers),
  `rgba16ui` image buffer reads (`SetupIndices`), std430 layout of the structs with `bool` members (`YSpanSetup`),
  `uaddCarry`/`umulExtended` correctness on Mali (would scramble texture coordinates rather than whole tiles).
- melonDS-android (rafaelvcaetano) and other forks were searched: no one has run this compute renderer on Android; a
  fork (WatermelonDS) had a Vulkan compute backend and removed it.

After Compute works on the phone: carry widescreen (`FfccWideExtra`, which only the OpenGL renderer implements) and
texture filtering over to Compute; make Compute the default where it is available.

---

## 5. Other open items (owner's wish list, in rough order)

- **Verify Continue on quit on the phone**: once the resume state at quit did not complete (the app was killed by a
  reinstall mid-save). Check a normal Quit writes `states/resume.*` (`state: resume state written (for Continue)`).
- **90/120 Hz on the phone**: the game runs at 60; smoother display would come from the engine's 3D interpolation
  (`VideoSettings::interpolate`/`display_rate`, `Runtime::render_between`). First measure how often PH makes a new 3D
  frame (`RuntimeOptions::fps_probe` logs "new 3D n/60"); then ask Android for the high refresh rate and present
  several blended frames per game frame.
- **Text boxes / 2D**: drawn at the DS's 256x192 and enlarged. A pixel-art upscaling filter (xBRZ/ScaleFX) on the 2D
  layers in the compositor is the cheaper big win; true HD text would hook the game's text drawing (decomp message code).
- **HD texture packs** (later): hash textures in the texture cache and substitute.
- **Release**: the owner wants a solid baseline, then makes the repository public themself. README pictures are done;
  update them when Compute works on the phone.

---

## 6. Gotchas learned the hard way

- Two adb versions on one machine kill each other's server: running `logcat` stops. Use one.
- The phone GPU renderer at 6x stutters when warm; Compute is capped at 4x on the phone.
- On the phone, the app runs in process `com.phrecomp.ph:game`; `adb logcat --pid` needs that process's pid.
- `third_party/CMakeLists.txt` patches fail the configure loudly if melonDS changes; keep patches exact-text.
- The engine is a credited GPL-3 fork of FFCCEOT-Recomp; melonDS is GPL-3. NOTICE.md lists credits.
- Settings live in `settings.ini` (PC data dir) and Android SharedPreferences `ph_pad` (renderer `gpu`/`compute`,
  `scale`, `wide`, `textures`, `fixes`, `stick`, layout keys); `GameActivity.getArguments()` turns the prefs into
  command-line options.
