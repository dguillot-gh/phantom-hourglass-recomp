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

  The game started from the clang module on both. When clang fails, the app builds with TinyCC instead (checked by
  stopping clang mid-build): 9.8 s on the Moto G, 6.9 s on the Pixel 8, recompile included.

  **fps, clang vs TinyCC** (2026-11-25, test build `fastcc-2`, the same C compiled by both, default renderer Compute
  2x, widescreen off): the game continued from two save spots, run unpaced (as fast as it goes) for 600 frames,
  3 runs per module per spot, TinyCC and clang alternating; battery 36-37 C. Mean of the warm runs:

  | Phone | Spot | TinyCC fps | clang fps | Runs (TinyCC / clang) |
  |---|---|---:|---:|---|
  | Moto G Stylus 2025 | resume | 99.4 | 101.5 | 101.4, 97.4 / 101.5, 101.4, 101.6 |
  | Moto G Stylus 2025 | slot 1 | 91.2 | 91.1 | 90.3, 91.5, 91.9 / 91.4, 90.8, 91.1 |
  | Pixel 8 | resume | 222 | 207 | 232.0, 211.6 / 224.8, 239.7, 156.7 |
  | Pixel 8 | slot 1 | 148 | 137 | 110.6, 201.7, 130.6 / 112.0, 170.1, 127.4 |

  The first run of the series (TinyCC, resume) was slow on both phones and is left out (Moto 75.0, Pixel 128.1 fps):
  the phone warming up, not the compiler. On the Moto, clang and TinyCC run the game at the same speed (within 2%);
  the Pixel 8 varies too much from run to run (±30%) to tell them apart. Both phones hold the DS's 60 fps with room to
  spare with either compiler. Same result as Spirit Tracks: the clang build gives no measurable fps gain in these
  scenes, so the emulated hardware (melonDS's ARM7 and the renderer), not the game's own code, sets the pace here.
- **Known issue:** in **Software** mode the bottom of the screen flickers on both phones (seen 2026-11-03 during the
  measurements above); OpenGL and Compute are not affected. Not yet investigated.
- **OpenGL on phones** (2026-11-03): OpenGL is a menu choice next to Software and Compute. Fixed on the way:
  flickering cut grass (melonDS's OpenGL renderer approximated the DS's "equal depth" test), a rare crash at start, and
  OpenGL failing to start in a running game on the V30. Changing to OpenGL or Compute, or the widescreen mode, now
  restarts the game at the same spot.
- Play tested up until the Fire Temple (which prompted a fix for using the microphone to essentially finish the rest of the game). Some boss fights do require you to swap or rotate screens in order for you to complete them properly. a small price to pay for having this game portable. 90% can be played on a single screen.
