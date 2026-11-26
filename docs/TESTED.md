# Tested devices and measurements

- **Phones:** two test phones, a high-end one and a 2017 one:
  - **Google Pixel 8** (Tensor G3, Mali-G715, Android 14+)
  - **LG V30** (Snapdragon 835, Adreno 540, Android 8, rooted for measuring)

  Other phones should work (Android 8 or newer, 64-bit ARM), but GPU drivers differ: if the picture looks wrong, the
  software renderer (1x, in the menu) is the safe fallback.
- **PC:** Windows 11 and Linux (built with the Nix flake; GCC on x86-64 Linux).
- **Status:** plays on PC, the Pixel 8 and the LG V30.
- **Measured** (2026-11-03, this `main` build on both phones): each renderer set the way the menu does, the game
  continued from the same spot in gameplay, 20 s to settle, then 30 s of the frames the phone actually showed
  (Android's SurfaceFlinger: `--latency` on the V30, `--timestats` on the Pixel 8). Every run started the renderer it
  asked for (checked in the game's log).

  | Renderer | LG V30 fps | V30 1% low | Pixel 8 fps | Pixel 1% low |
  |---|---:|---:|---:|---:|
  | Software (1x) | 31.7 | 15 | 61.5 | 40 |
  | OpenGL 2x | 34.4 | 20 | 61.5 | 30 |
  | **OpenGL 4x** | **45.2** | 20 | 61.5 | 30 |
  | Compute 2x | 15.6 | 7 | 61.4 | 30 |
  | Compute 4x | 13.3 | 5 | 61.6 | 30 |
  | 6x (asked for) | runs at 4x: 45.3 | 30 | runs at 4x: 61.5 | 30 |

  The Pixel 8 runs every setting at full speed (~60 fps; it measures slightly over 60 on its 120 Hz display).
  The phone app caps the resolution at 4x; asking for 6x gives 4x on both phones. 1% low = the frame rate of the
  slowest 1% of frames.

  **Best settings:** **OpenGL 4x** on older phones like the V30; **Compute** (the more exact renderer) on fast phones
  like the Pixel 8. On the V30, 4x runs faster than 2x: the heavier load makes the phone raise its CPU and GPU clocks
  (fast cores ~1.7 GHz and the GPU at 710 MHz at 4x, against ~1.2 GHz and 257 MHz at 2x, measured with the
  `phoneperf` tool). Earlier the same day `phoneperf` measured 59.8 fps at OpenGL 4x on the V30 with a test build
  (`feature/opengl`: the same fixes plus measuring code and an ARM7 timing change that is not on `main`); the gap to the
  45 here is not explained yet (different build, and a different way of counting frames).
- **On-device compiler** (2026-11-25, test build `fastcc-1`): the game is now built on the phone with clang -O2 + lld
  (Termux's arm64 LLVM inside the APK), TinyCC only as the fallback. USA D-pad ROM, 65 files / 76 MB of C, 5
  compiles at once, from tapping "Build the game" to the game ready:

  | Phone | Recompile (ROM to C) | clang -O2 compile | Link (lld) | Total | Module |
  |---|---:|---:|---:|---:|---:|
  | Moto G Stylus 2025 | 2.4 s | 380 s | 0.3 s | **383 s** (6 min 23 s) | 42 MB |
  | Pixel 8 | 1.6 s | 362 s | 0.4 s | **365 s** (6 min 5 s) | 42 MB |

  The game started from the clang module on both. TinyCC builds the game in about 10 s (Spirit Tracks: 10.3 s on
  the Moto G). No fps A/B for Phantom Hourglass yet; Spirit Tracks measured the same steady-state fps with both
  compilers (within ~3%), only the first run of a series was slower with TinyCC.
- **Known issue:** in **Software** mode the bottom of the screen flickers on both phones (seen 2026-11-03 during the
  measurements above); OpenGL and Compute are not affected. Not yet investigated.
- **OpenGL on phones** (2026-11-03): OpenGL is a menu choice next to Software and Compute. Fixed on the way:
  flickering cut grass (melonDS's OpenGL renderer approximated the DS's "equal depth" test), a rare crash at start, and
  OpenGL failing to start in a running game on the V30. Changing to OpenGL or Compute, or the widescreen mode, now
  restarts the game at the same spot.
- Play tested up until the Fire Temple (which prompted a fix for using the microphone to essentially finish the rest of the game). Some boss fights do require you to swap or rotate screens in order for you to complete them properly. a small price to pay for having this game portable. 90% can be played on a single screen.
