// The game build with clang on Windows (first-run setup, main.cpp): the recompiler's C (game_builder.cpp without
// TinyCC) is compiled with clang -O2, several files at a time, and linked with lld into game/ph_game.dll. clang, lld
// and their DLLs travel inside the exe (tools/embed_clang.cmake, -DPH_PC_CLANG_DIR) and are unpacked only while
// building. When this fails, the setup builds the game with TinyCC instead.
#pragma once

#include <atomic>
#include <filesystem>
#include <string>

namespace ph {

struct ClangPackEntry {
    const char* name;
    unsigned long long offset, size;
};
extern const ClangPackEntry kClangPack[];
extern const int kClangPackCount;

// True when this exe carries clang.
bool pc_opt_available();

// Where the recompiler leaves the C (gen/) and its headers (inc/, rt/) for clang: data/opt.
std::filesystem::path pc_opt_dir(const std::filesystem::path& data_dir);

// Compiles data/opt into `out_module`; returns "" when done, else what failed. `progress` goes 0-100. A log goes to
// data/opt-build.log. data/opt is removed when done.
std::string pc_clang_build(const std::filesystem::path& data_dir, const std::filesystem::path& out_module,
                           std::atomic<int>& progress);

}  // namespace ph
