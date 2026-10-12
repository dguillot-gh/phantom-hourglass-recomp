# Building Phantom Hourglass, step by step

The scripts build everything into the *workspace*: the folder that holds this repository (`ph-recomp\`). Put your ROM
there (any file name; it is found by its SHA-1). They only ever read the ROM.

```
workspace\
  ph-recomp\                 the repository (these scripts are in ph-recomp\scripts)
  your-dump.nds              your ROM
  PhantomHourglass\          made by step 3: the game, ready to play (do not share it: it contains the game)
  apks\                      made by RUN-ANDROID: the phone app (the same)
  logs\                      every step's log, and the test's screenshots
```

Run them from PowerShell, in order, or `RUN-ALL.ps1` for steps 1-4. Each one stops with a FAIL line if something is
wrong; `logs\progress.log` has every step, `logs\<script>-<time>.log` the full output. Add `-DryRun` to any of them to
check everything without installing or building anything.

```
powershell -NoProfile -ExecutionPolicy Bypass -File ph-recomp\scripts\RUN-ALL.ps1
```

| Step | What it does | Time |
|---|---|---|
| `1-INSTALL-TOOLS.ps1` | Installs Git, Python, CMake 3.28+, Ninja, LLVM and Visual Studio Build Tools (C++) with winget; skips what is there. Build Tools asks for admin once (about 3 GB). | 10-30 min, once |
| `2-BUILD.ps1` | Recompiles the ROM to C and builds the game and the program (`-Clean` for a fresh build). Needs internet the first time (SDL3). Also builds a test copy of the program with the phone's renderer. | 10-30 min the first time, then a minute |
| `3-MAKE-GAME-FOLDER.ps1` | Makes `PhantomHourglass\` with `PLAY.cmd`: the game, ready to play. Never overwrites the save there. | seconds |
| `4-TEST.ps1` | Plays the game for about a minute by itself on a copy of the save (a save state is saved and loaded too, and the GPU renderers are tried) and takes screenshots into `logs\test-<time>\`. | 2 min |

Then double-click `PhantomHourglass\PLAY.cmd`. The game's own save is `PhantomHourglass\data\saves\azee.sav`: to carry
on from a save you already have (from an emulator, 512 KB), put it there as `azee.sav` before the first start.

## Android (after the PC build)

```
powershell -NoProfile -ExecutionPolicy Bypass -File ph-recomp\scripts\RUN-ANDROID.ps1
```

| Step | What it does |
|---|---|
| `ANDROID-TOOLS.ps1` | Uses the Java 17 and Android SDK you have (Android Studio's, `JAVA_HOME`, `ANDROID_HOME`), or installs them: Java with winget, the SDK (platform 35, NDK 28.2, adb, about 1.5 GB) into the tools folder. Shows Android's SDK licence for you to accept. Once. |
| `5-BUILD-ANDROID.ps1` | Builds the APK into `apks\` from the PC build's C. |
| `6-INSTALL-PHONE.ps1` | Installs it on the phone, copies your ROM in, and a save only if the phone has none yet; opens the app. |

The phone needs USB debugging on (Settings, About phone: tap Build number seven times; then Developer options, USB
debugging) and to be plugged in; accept the "Allow USB debugging?" prompt on it. Without the phone, `RUN-ANDROID.ps1
-NoPhone` builds the APK only: copy it to the phone and install it, and its start screen imports the ROM and a save.

The tools folder is `PH_TOOLS_CACHE` if you set it, else `..\recomp-backups\tools-cache` beside the workspace, else
`tools-cache` in the workspace. An optional `debug.keystore` there signs the APK (the same key on every PC, so an update
installs over the app); without one Android's own debug key is used.

## On the phone

Touch the game directly: it is the stylus. The D-pad is on the left; the small buttons are the menu (≡), TOP (the top
screen), ⟳ (portrait: both screens, like a DS held open), MIC (hold: blow into the microphone) and LID (close the DS).
The menu switches every control on or off, adds A B X Y, L R and Start/Select, sets their opacity and size, and has
the renderer (GPU for 2x-6x), the picture size, the FPS counter and the save state slots.
