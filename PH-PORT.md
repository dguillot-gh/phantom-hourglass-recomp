# Phantom Hourglass port: notes

Fork of [FFCCEOT-Recomp](https://github.com/JonathanDC64/FFCCEOT-Recomp)'s engine (recompiler, runtime, melonDS
glue; GPL-3.0, credited in NOTICE.md) with our own program in `frontend/`. FFCC's frontend, menus, art, fonts, theme
and game layer were removed (2026-10-12).

Build on the home PC with `..\RUN-ALL.ps1` (see `..\START-HERE.md`).

## Our parts

| File | What |
|---|---|
| `frontend/src/main.cpp` | the program: one SDL window, one screen (the touch screen by default, Tab for the top, F2 both), letterboxed so it is never cropped; mouse = stylus; keyboard + gamepad; M / right-stick click = blow into the mic; 48 kHz audio; saves written atomically; `--input-script` for unattended tests |
| `frontend/src/game_module.cpp` | loads `ph_game.dll` (the recompiled game) and hands it the runtime's services and our hooks |
| `frontend/src/ph_hooks.cpp` | the NitroSDK hooks with Phantom Hourglass's addresses |
| `frontend/src/png.cpp` | screenshots (minimal PNG writer) |
| `frontend/config/azee/` | functions.txt (24,608 functions from the zeldaret/ph decomp, `tools/import_dsd.py`), hooks.txt, regions.txt |
| `frontend/res/app.ico` | our icon (`tools/gen_icon.py`, drawn from shapes) |
| `runtime/src/melon_platform.cpp` | engine change: `ntr::set_mic_blow` feeds the mic loud noise while held |
| `common/src/known_roms.cpp` | engine change: the two supported Phantom Hourglass dumps |

## Addresses (AZEE), found by instructions; the decomp leaves them unnamed

| Function | Address | How it was matched |
|---|---|---|
| OS_InitContext | main 0x0200DC98 (0x6C) | stores pc+4 at +0x40, sp at +0x44, sp-0x40 at +0x38, cpsr 0x1F/0x3F, zeroes r0-r12 and lr |
| OS_SaveContext | main 0x0200DD04 (0x4C) | `add r0,#0x48` / CP_SaveContext / `mrs r2,cpsr; str r2,[r1],#4` |
| OS_LoadContext | main 0x0200DD50 (0x44) | CPi_RestoreContext / `mrs r1,cpsr; bic r1,#0x1F; orr r1,#0xD3` |
| OS_IrqHandler | ITCM 0x01FFA48C (0x58) | `mov ip,#0x4000000; add ip,#0x210`; literals OS_IRQTable 0x027E0014, ThreadSwitch 0x01FFA4E4 |
| OS_IrqHandler_ThreadSwitch | ITCM 0x01FFA4E4 (0x15C) | the literal LR above |

The D-pad patched ROM (sha1 7e0d26d1...) has every function of the decomp's USA config where the config says, so it
shares the config with the clean dump (sha1 4c8f52dd...). melonDS's ROM list gives AZEE save type 6 (512 KB flash).

## History

- 2026-10-12, first build (with FFCC's program): built in 3 min; recompiler: 24,621 functions (13 discovered),
  927,030 instructions, 1,390 jump tables, 3 undefined instructions reached, 0 global warnings. Booted to the sea
  intro, the title and the file select with the save.
- 2026-10-12: FFCC's frontend replaced by ours; microphone button added.
- 2026-10-12: Android built (6.8 min, 47 MB APK) and runs on the Pixel: ROM and save imported in the launcher, the
  game saved. Then: PS1-style side menu (PhMenu: screen, sharp/smooth, FPS counter, controls on/off, opacity, size;
  internal resolution and save states shown as coming), PS1-style launcher, FPS counter (F4 on PC).

- 2026-10-12: PC GPU renderer 2x-8x verified (test run at 4x, and 8x switched live from the menu). Android menu
  installed on the Pixel.
- 2026-10-12: save states written (runtime/src/savestate.cpp): taken at an OS_LoadContext while every other thread
  is parked by OS_SaveContext; loading resumes each parked thread on the interpreter at its OS_SaveContext return
  address, every call from there running recompiled. 12 slots on PC (menu page, F5/F8) and phone (menu grid).
  4-TEST saves at the file select, moves on, loads back.

- 2026-10-12: GPU renderer on the phone (OpenGL ES 3.2): ES copies of melonDS's 23 shaders made at configure time
  (gles_shaders.cmake + gles_shader_fixes.cmake: version/precision header, fragment output locations, ~60 int-to-float
  fixes); all 25 variants (WBuffer included) compile with the NDK's glslc. gles_compat.cpp: glDrawBuffer, glClearDepth,
  glDepthRange, glMapBuffer, glBindFragDataLocation shims, ES 3.0+ functions glad skips, 1_5_5_5_REV converted on
  upload/readback. The phone now draws on the game thread with GlPresenter (ES), falling back to SDL's renderer.
  Phone menu: Renderer Software/GPU, resolution 2x-8x. gles-test preset + 4-TEST run it on the PC.

## Next

1. Build and test save states and the phone's GPU renderer (4-TEST: slot 1, the gles run; then the phone).
2. Fast saving: find CARDi_RequestStreamCommand and the card library's common state.
3. Ahead-of-time speed is already there (the whole game, overlays included, is compiled at build time); watch for code
   the game copies at run time (it runs on the interpreter: list it in `regions.txt` if it matters).
4. Android (written 2026-10-12, not built yet): `android/` (Gradle; LauncherActivity imports the ROM and saves,
   GameActivity = SDL + DsPad, our DS on-screen pad: D-pad, TOP, MIC, LID, menu; A B X Y, L R, Start Select optional),
   `frontend/src/android_bridge.cpp` (JNI, logcat), `runtime/src/fiber.cpp` ARM64 context switch (bionic has no
   swapcontext). Built by `..-BUILD-ANDROID.ps1` from the PC build's recomp_generated and SDL source.
5. Save states: in-session only is realistic (the guest threads' native stacks hold host addresses that change
   between launches): melonDS's DoSavestate + RuntimeImpl state + each fiber's used stack, restored in the same
   process. The PS1 project's states are a reference for the slot UI.
6. PC side menu and upscaling (written 2026-10-12, not built yet): the PC frontend draws with OpenGL on the game thread
   (`gl_present.cpp`: context 4.3/3.3 core, melonDS's glad, one shader for screens / boxes / 8x8 text from font8x8),
   the runtime's GPU renderer at 2x-8x; F1 menu (`menu.cpp`), FPS counter. 4-TEST adds a GPU 4x run. Phone GPU
   rendering later (melonDS's GL renderer is desktop GL, not GLES). Android still draws with SDL's renderer.
