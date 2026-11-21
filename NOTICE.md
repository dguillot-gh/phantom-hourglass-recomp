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
| SDL3 (fetched at build time) | [SDL](https://libsdl.org) | zlib |
| `frontend/src/font8x8_basic.h` (the menu's text) | [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper, after the IBM VGA fonts | public domain |
| `third_party/stb/stb_truetype.h` (TrueType rasterizer for HD text) | [stb](https://github.com/nothings/stb) by Sean Barrett | MIT / public domain |
| `frontend/res/fonts/Inter-Bold.ttf` (optional HD dialogue font) | [Inter](https://github.com/rsms/inter) by Rasmus Andersson | SIL OFL 1.1 (`frontend/res/fonts/Inter-OFL.txt`) |
| Function addresses and names in `frontend/config/azee/functions.txt` | the [zeldaret/ph](https://github.com/zeldaret/ph) decompilation project's dsd configuration | facts only (addresses, sizes, names) |

FFCCEOT-Recomp's own frontend, menus, art, fonts, theme and game layer (Final Fantasy Crystal Chronicles specific) are
not part of this project. The whole program is GPL-3.0 (`LICENSE`).
