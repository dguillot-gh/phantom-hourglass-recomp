# Notices

PH-Recomp is a native PC port of *The Legend of Zelda: Phantom Hourglass* (Nintendo DS) by static recompilation. It
contains **no** game code or data: the game is recompiled on your PC from your own copy of the ROM. *The Legend of
Zelda: Phantom Hourglass* is © Nintendo. This project is not affiliated with or endorsed by Nintendo.

## Where the code comes from

| Part | Origin | Licence |
|---|---|---|
| `frontend/` (the program, its window, input, saves, icon), `frontend/config/azee`, `tools/gen_icon.py` | written for this project | GPL-3.0 |
| `recompiler/`, `runtime/`, `common/`, `cmake/`, `third_party/CMakeLists.txt`, `third_party/compat`, the `tools/` they use | [FFCCEOT-Recomp](https://github.com/JonathanDC64/FFCCEOT-Recomp) by JonathanDC64: its game-agnostic recompiler (`ntr-recomp`) and DS runtime (`ntr-runtime`), used as the engine of this fork with small changes (Phantom Hourglass ROM list, microphone input) | GPL-3.0 |
| `third_party/melonDS` | [melonDS](https://github.com/melonDS-emu/melonDS) | GPL-3.0 |
| `third_party/tinycc` (the C compiler that builds the game on the player's PC or phone: linked into `PhantomHourglass.exe`, and into its own `libphbuild.so` in the Android app) | [TinyCC](https://repo.or.cz/tinycc.git) (mob e9e9e7f, see `third_party/tinycc/SOURCE.txt`) | LGPL-2.1 (`third_party/tinycc/COPYING`) |
| `tools/ondevice/` (the small C headers, DLL entry and `chkstk_ms.S` the game is built with, `fetch_termux_clang.py`, `build_device_llvm.sh`, `hot_profile.py`), `tools/embed_toolchain.cmake`, `tools/embed_clang.cmake`, `frontend/src/pc_optimizer.*`, `GameOptimizer.java`, `OptimizeService.java` | written for this project | GPL-3.0 |
| clang + lld inside `PhantomHourglass.exe` (the compiler that builds the game on the player's PC; unpacked only while building): `clang.exe`, `ld.lld.exe`, `libLLVM-*.dll`, `libclang-cpp.dll`, `libc++.dll`, `libunwind.dll` | [LLVM](https://llvm.org) (clang, lld, libc++, libunwind) as built by [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) release 20261020 | Apache-2.0 WITH LLVM-exception |
| clang + lld inside the APK (`lib/arm64-v8a/libclang_exe.so`, `liblld_exe.so`, `libLLVM.so`, `libclang-cpp.so`, `libc++_shared.so`: the compiler that builds the game on the player's phone) | [LLVM](https://llvm.org) as built and packaged for Android by [Termux](https://github.com/termux/termux-packages) (version in the build's `VERSION.txt`); packed by `tools/ondevice/fetch_termux_clang.py` (library names changed only) | Apache-2.0 WITH LLVM-exception |
| Libraries that clang/lld in the APK use: `libxml2_16.so`, `libz1.so`, `libzstd1.so`, `libffi.so`, `libiconv.so` | [libxml2](https://gitlab.gnome.org/GNOME/libxml2), [zlib](https://zlib.net), [Zstandard](https://github.com/facebook/zstd), [libffi](https://github.com/libffi/libffi), [GNU libiconv](https://www.gnu.org/software/libiconv/), as built by Termux | MIT, zlib, BSD-3-Clause, MIT, LGPL-2.1 (each a separate shared library; sources at the links) |
| SDL3 (fetched at build time) | [SDL](https://libsdl.org) | zlib |
| `frontend/src/font8x8_basic.h` (the menu's text) | [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper, after the IBM VGA fonts | public domain |
| `third_party/stb/stb_truetype.h` (TrueType rasterizer for HD text) | [stb](https://github.com/nothings/stb) by Sean Barrett | MIT / public domain |
| `frontend/res/fonts/Inter-Bold.ttf` (optional HD dialogue font) | [Inter](https://github.com/rsms/inter) by Rasmus Andersson | SIL OFL 1.1 (`frontend/res/fonts/Inter-OFL.txt`) |
| Function addresses and names in `frontend/config/azee/functions.txt` | the [zeldaret/ph](https://github.com/zeldaret/ph) decompilation project's dsd configuration | facts only (addresses, sizes, names) |

FFCCEOT-Recomp's own frontend, menus, art, fonts, theme and game layer (Final Fantasy Crystal Chronicles specific) are
not part of this project. The whole program is GPL-3.0 (`LICENSE`).
