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

- 2026-10-16 (written, not built yet; syntax-checked for PC and NDK, javac clean):
  - Wireless removed: FFCC's local wireless with other instances (local_wireless.cpp), the same-PC LAN
    (instance_lan.cpp), the internet NAT (melonDS's Net_Slirp + libslirp) and the Wi-Fi Connection options are gone.
    The Platform::MP_* / Net_* hooks are stubs: the console's Wi-Fi hears no one (battle mode finds nobody).
  - Continue: a resume state (data/states/resume.*) is written when the game quits (PC window, menu, phone Quit) and
    when the phone sends it to the background (the game runs a frame or two more, undrawn, for the moment to take
    it). --continue loads it at frame 2; PC: F9, menu "Continue from ..", a note at start; phone: Continue on the
    start screen. A cartridge save after 30 s of play deletes it (it would go back before that save).
  - Phone: "Move and resize controls" (DsPad layout editor, a layout per orientation: lay_land_<id> / lay_port_<id>,
    hide_*), like the PS1 apps' PadOverlay editor.
  - Analog stick (frontend/src/ph_stick.cpp): holds the stylus at Link + the stick's direction (22-80 px out). Link's
    screen position comes from gPlayerControl (0x027E0FB8): touch point (+0x10/+0x14) minus mTouchDist (+0x58, q20)
    along mTouchAngle (+0x6A); which way the angle points is learned in the first 30 frames (logged "stick: ...").
    Acts only while mUnk_7c (+0x7C) is set. Runtime::peek32 added for it. Phone: the D-pad becomes a stick (menu
    switch); PC: menu "Gamepad's left stick: D-pad / Analog". To check at home: Link walks the stick's way, no taps
    land on dialogs, and the log line appears.
  - Widescreen test (16:9, GPU renderer only; PC menu "Widescreen (test, GPU)", --wide, phone menu switch): the
    engine's FFCC widescreen path (FfccWideExtra: 3D, engine A's 2D and the final pass drawn 2 x 43 pixels wider),
    switched on by a "before" hook on G3i_PerspectiveW_ = main 0x02005BFC (found in the generated C: the only
    function the five r2 = 0x1555 call sites reach, using the hardware divider at 0x04000290), which swaps the 4:3
    aspect for the wide one and sets ffcc_wide_scene. Frame::wide_layer (engine A's screen, POWCNT1 bit 15) tells
    the frontend which screen is wide (in PH usually the touch screen); touches beside the DS's 256 pixels go to the
    nearest edge. Expect objects to appear late at the sides (the game culls to its 4:3 view) and some rooms to show
    past their walls.
  - 2026-10-16 home test: 16:9 worked, the stick "worked amazing"; the HUD (hearts, Menu) stayed in the middle 4:3.
    Now HUD edges are on with widescreen: the sprite pass records each cluster it moves (FfccHudMoved, a ph patch in
    third_party/CMakeLists.txt), Frame::hud_move carries them, and ph::to_ds maps a touch on a moved cluster back to
    the DS pixel the game expects. Only sprite clusters touching the left/right edge move; HUD drawn as background
    layers would stay in the middle.
  - Then (FfccHudSides, PH): clusters in the top 56 / bottom 42 rows go to their side (the rupee digits), the
    full-width bottom menu bar goes left with the Menu button, and a sprite moved last frame that is still in the same
    place with the same tile and palette keeps its move (the Menu button merges with the dialogue box's sprites, too
    big to move). Up to 32 moved boxes per frame for the touch mapping.
  - 3D texture filtering in both menus (Pixels / Smooth / Sharp, GPU renderer: VideoSettings::texture_smoothing,
    the engine's FfccSmoothTex shader patch); --textures. The PC menu puts labels beside the chips when the window is
    too short for them stacked.

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
