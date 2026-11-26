# Wishlist

Ideas for Phantom Hourglass Recomp and Spirit Tracks Recomp (the same list in both repositories). Each item has a short
status or caution note; nothing here is promised.

## 1. Architecture and build pipeline

- **A shared core (`ntr-recomp-core` submodule).** Both projects keep their own copies of `recompiler/`, `runtime/`,
  `android/` and the shared parts of `frontend/`, so every engine fix and feature (HD text, the language selector,
  European ROMs, overlay probes, jump tables, Game Mode) is applied twice. Moving the shared code into one repository
  used by both lets an engine change land once; the game-specific hooks, configs and branding stay in each game.
  *Note: a one-time move, best done between feature pushes, after the open pull requests are merged.*
- **Nix / Linux build and button-controls ROM support for Spirit Tracks.** Phantom Hourglass has `flake.nix`,
  `build-linux.sh` and the Button controls patch ROM (Krutonium); porting them makes Spirit Tracks playable on Linux and
  the Steam Deck (which the 16:10 widescreen mode targets).
- **Faster first builds.** Unity builds (`CMAKE_UNITY_BUILD`) for the generated C chunks and cached toolchains would cut
  the long first build. *Caution: bigger compile units need more memory per compiler process; keep batch sizes small
  enough for 16 GB machines (ccache already speeds up rebuilds).*
- **Decoupled launcher and runtime ROM loading.** Prebuilt launchers that ask for the `.nds` on first start, so players
  need no build tools. *Caution: no ROMs, no built game modules or APKs may be shared (the recompiled code is derived
  from the ROM). An empty launcher would have to make the game's code on the player's own device (an ARM-to-ARM64
  translator: a research project). A nearer step: a Windows setup app that builds the game for the player.*

## 2. Gameplay, controls and accessibility

- **Native analog movement and camera, no ROM patch.** Clean dumps rely on the stylus unless the D-pad patch is applied
  (and there is none for Europe). Driving Link's movement and the camera's yaw / pitch directly from hooks (the
  decompilations name the functions) would give 360-degree analog movement and right-stick camera control on clean
  dumps of every region. *Note: Phantom Hourglass's stick (`ph_stick.cpp`) already works through the game's touch
  control; Spirit Tracks's goes through the D-pad patch's routine.*
- **Spirit Flute accessibility and auto-play (Spirit Tracks, issue #4).** The flute lessons and duets are hard with touch
  and a blow button, and a barrier for players who are deaf or hard of hearing:
  - visual pitch and timing cues: an on-screen rhythm bar or highlighted pipes showing each note and its length;
  - a one-button or auto-play mode: hook the flute's state machine to accept simple button taps instead of slide and
    blow, or pass the section outright (an accessibility setting, off by default).
  *Next step: a save state at the first flute lesson, to watch the flute's code run.*
- **Model and texture replacement.** A runtime hook that loads external meshes (`.obj` / `.gltf`) and high-resolution
  textures by model ID or texture hash, for HD character models (a Wind Waker style Link) or community texture packs,
  without changing the ROM. *Note: research on HD models for Phantom Hourglass has started.*
- **Achievements in both games.** Phantom Hourglass has 67 offline achievements (from the RetroAchievements set, by
  chouette-cc). Porting the system and its viewer to Spirit Tracks, with its own RetroAchievements set, would give both
  projects the same feature.

- **Faster travel.**
  - *Phantom Hourglass:* a configurable boat cruise speed multiplier, and an option to skip the salvage crane after the
    first time, to pair with the Temple of the Ocean King floor checkpoints.
  - *Spirit Tracks:* a train speed boost toggle (independent of the QoL ROM patch) and a "warp to unlocked gates" menu
    option, to cut late-game rail backtracking.
- **Challenge toggles.** Switches in the menu: Hero Mode (double damage taken, no heart drops), one-hit KO, infinite
  ship / train health. A built-in randomizer is a much bigger project; the toggles come first.
- **Portable saves and backups.** Keep the `.sav`, save states and `settings.ini` together in one `userdata/` folder,
  with automatic rolling backups (on room changes), so progress syncs between PC and phone (Syncthing, Google Drive).
  *Note: the PC game folder already keeps everything in `data\` (portable.txt); the phone keeps it in the app's own
  storage, which a sync app cannot reach without an export / import.*

## 3. Graphics and presentation

- **60 fps by interpolation.** Both games draw their 3D at 30 fps on the DS. Interpolating the camera matrices, Link's
  skeletal transforms and moving objects between game ticks in the runtime would show 60 fps (or 120 on phones that
  offer it) without touching the game's frame-counted physics. *Note: the engine has a start on this
  (`VideoSettings::interpolate`, `Runtime::render_between`, and `fps_probe` to count new 3D frames).*
- **Dual-screen layouts on PC.** A configurable side-by-side or picture-in-picture layout, like the phone's corner
  minimap, so the sea chart, rail map or a boss's weak point on the top screen is in view without switching screens.
  *Note: the PC has Bottom / Top / Both today; the corner minimap is phone-only.*

## 4. Smaller items (from play-testing, 2026-11-24)

- **Touch in the wide sides.** In widescreen the game only takes touches in its 4:3 middle; a path or tap in the side
  areas is pinned to the edge. Feeding the game positions beyond 0..255 (if its touch code allows them) would fix it.
- **On-screen controls over a 21:9 picture.** The minimap and buttons cover parts of the picture at 21:9 (a path stroke
  started on them goes to them); they could fade or move aside while drawing.
- **Minimap on menu screens.** A black minimap box shows on some menu screens (Spirit Tracks's collection).

## 5. Developer and community tooling

- **CI on every push (GitHub Actions).** Build the Windows, Linux and Android programs and run the tests on every push,
  so a platform-specific break shows at once, in both repositories. *Note: without a ROM there is no game code to
  compile (the recompiler makes it from the player's ROM); CI can build the program, the runtime, the recompiler and
  the tests, and the recompiler can be run on a small synthetic test image.*
- **A debug tab in the menu.** Current room, Link's coordinates, active flags and the loaded overlays, live, to speed up
  finding addresses (European releases, new hooks). *Note: the menu is the port's own drawing code (not ImGui).*

## 6. Performance (from the phone profile, 2026-11-25)

Measured on a Moto G Stylus 2025 (60 Hz) and a Pixel 8 (120 Hz), both games, clang and TinyCC game modules, four
renderers: [the report](https://github.com/dguillot-gh/phantom-hourglass-recomp/blob/main/docs/PROFILE-2026-11-25.md)
(in the Phantom Hourglass repository). The game build with clang (v0.2.0) already cut the game's own code from 20-31%
of the CPU to 5-11%; what is left, most visible gain first:

- **Ask Android for 60 fps, not 120.** The game's surface votes for 120 fps (`dumpsys SurfaceFlinger --timestats`:
  `frameRate = 120.00`) while the game makes 60, so on 120 Hz phones frames land 16.7 or 25 ms apart: 12-16% late
  frames on the Pixel 8 even with clang, at a 61.7 fps average. Setting the surface's frame rate to 60 with fixed-source
  compatibility (`Surface.setFrameRate` / `ANativeWindow_setFrameRate`, Android 11+) should show every frame for
  exactly two refreshes. *Check: the present-to-present histogram in timestats before and after.*
- **Renderer default by phone.** The Moto cannot hold 60 fps in Phantom Hourglass with the Compute renderer (47 fps at
  2x, 36 at 4x, with either compiler); OpenGL 2x gives ~56, Software 1x 60. Start such phones on OpenGL, or drop to
  OpenGL when the game misses 60 for a few seconds. *Check first that the GPU, not the CPU, is the limit there (GPU
  load at Compute 2x).*
- **The ARM7 interpreter.** melonDS interprets the DS's second CPU (sound, touch, timers) one instruction at a time,
  with its bus reads and cycle counting: 22-31% of the app's CPU, now the biggest single cost. Options: melonDS's ARM
  JIT for the ARM7 on arm64 (if it builds for Android), skipping the ARM7's idle loops, or cheaper ARM7 timing.
  *Caution: the ARM7 runs the sound and the touch screen; check audio and touch after any change.*
- **Phantom Hourglass's extra Android work.** PH spends 6-12% of the app's CPU in `SDLThread` and binder threads,
  against 3-6% for Spirit Tracks. Find what its Java side does every frame (on-screen pad, FPS badge, vibration,
  sensors) with a call-graph profile (`simpleperf record -g`).
- **Runtime function lookups.** `ntr::RuntimeImpl::lookup` and `advance` take ~5%; caching each indirect call's
  target (or calling known targets directly) would cut most of it.
- **Allocations every frame.** memcpy and malloc/free take 5-7%; find the callers with `-g` and reuse buffers.
- **Measure again, properly.** Three runs per cell with the order alternated, both games paced at 60 (Spirit Tracks's
  test app needs `bench_paced`), and a moving scene (an input script or busier save spots), not only standing still.
