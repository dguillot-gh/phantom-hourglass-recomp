# Third-party components

Each component keeps its own licence.

| Component | How | Use | Licence |
|---|---|---|---|
| [melonDS](https://github.com/melonDS-emu/melonDS) | submodule `third_party/melonDS`, pinned; core and renderers only (no JIT, no frontend), patched at configure time by `third_party/CMakeLists.txt` (from FFCCEOT-Recomp) | Hardware model: ARM7, GPU, SPU, timers, DMA, cart, SPI, RTC, Wi-Fi, microphone; software renderer; ARMv5 interpreter fallback | GPL-3.0 |
| [SDL3](https://github.com/libsdl-org/SDL) | fetched by CMake (`release-3.4.18`), static | Window, input, audio | zlib |
| teakra, glad | bundled inside the melonDS tree and built from it | DSP emulation (DSi), OpenGL loader | MIT (teakra), MIT/public domain (glad); see each directory |

`compat/` holds small build shims for compiling the melonDS core with clang-cl/MSVC (from FFCCEOT-Recomp).

Developer-only, not in the build: the [zeldaret/ph](https://github.com/zeldaret/ph) decomp's dsd configuration is the
source of `frontend/config/azee/functions.txt` (addresses, sizes and names only), imported with `tools/import_dsd.py`.
