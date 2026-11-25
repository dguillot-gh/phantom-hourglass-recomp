#include "game_builder.hpp"

#include <filesystem>
#include <vector>

#include "libtcc.h"
#include "ntr/rom.hpp"
#include "recomp/recomp.hpp"

namespace fs = std::filesystem;

namespace ph {
namespace {

std::string g_tcc_error;

void on_tcc_error(void*, const char* msg) {
    if (g_tcc_error.size() < 2000) g_tcc_error += std::string(msg) + "\n";
}

TCCState* new_tcc(const std::string& tcc_dir, int type) {
    TCCState* s = tcc_new();
    if (!s) return nullptr;
    tcc_set_error_func(s, nullptr, on_tcc_error);
    tcc_set_lib_path(s, tcc_dir.c_str());
    tcc_set_options(s, "-nostdlib -w");
    tcc_set_output_type(s, type);
    return s;
}

std::string u8(const fs::path& p) {
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace

std::string build_game(const std::string& rom_path, const std::string& assets, const std::string& out_module,
                       const std::string& work, std::atomic<int>& progress) {
    progress = 0;
    std::error_code ec;
    const fs::path gen = fs::u8path(work) / "gen", obj = fs::u8path(work) / "obj";
    fs::remove_all(fs::u8path(work), ec);
    fs::create_directories(gen, ec);
    fs::create_directories(obj, ec);

    // ---- 1. recompile (ntr-recomp's steps) ----
    try {
        ntr::Rom rom = ntr::Rom::open(rom_path);
        const ntr::KnownRom* known = ntr::find_known_rom(rom.sha1());
        if (!known || !known->supported) return "This ROM is not a supported Phantom Hourglass dump.";
        recomp::Program program = recomp::load_program(rom);
        // One recompiler config per release (config/azee = USA, config/azep = Europe), picked by the ROM's game code.
        std::string code = known->game_code;
        for (char& ch : code)
            if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        const std::string config = assets + "/config/" + code;
        if (!fs::is_directory(fs::u8path(config), ec)) return "No recompiler config for " + std::string(known->game_code) + ".";
        recomp::load_regions(program, config);
        program.config = recomp::load_config(program, config);
        recomp::Analysis analysis = recomp::analyze(program);
        recomp::emit_c(program, analysis, u8(gen));
    } catch (const std::exception& e) {
        return std::string("Recompiling failed: ") + e.what();
    }
    progress = 20;

    // ---- 2. compile each C file to an object (one TinyCC state each), then link the module ----
    std::vector<fs::path> sources;
    for (const auto& e : fs::directory_iterator(gen, ec))
        if (e.path().extension() == ".c") sources.push_back(e.path());
    if (sources.empty()) return "Recompiling wrote no C files.";
    const std::string tcc_dir = assets + "/tcc";
#ifdef _WIN32
    // No CRT: the DLL's entry point and __chkstk (TinyCC calls it for big stack frames) are compiled with the game.
    sources.push_back(fs::u8path(tcc_dir) / "lib" / "dllstart.c");
    sources.push_back(fs::u8path(tcc_dir) / "lib" / "chkstk.S");
#endif
    std::vector<std::string> objects;
    for (size_t i = 0; i < sources.size(); i++) {
        g_tcc_error.clear();
        TCCState* s = new_tcc(tcc_dir, TCC_OUTPUT_OBJ);
        if (!s) return "Out of memory starting the compiler.";
        tcc_add_include_path(s, u8(gen).c_str());
        tcc_add_include_path(s, (assets + "/rt").c_str());
        tcc_add_include_path(s, (tcc_dir + "/include").c_str());
        tcc_define_symbol(s, "NTR_GAME_MODULE", "1");
        tcc_define_symbol(s, "__builtin_memcpy", "memcpy");  // (TinyCC's PE target leaves the builtin unresolved)
        const std::string o = u8(obj / sources[i].stem()) + ".o";
        const bool ok = tcc_add_file(s, u8(sources[i]).c_str()) == 0 && tcc_output_file(s, o.c_str()) == 0;
        tcc_delete(s);
        if (!ok) return "Compiling " + u8(sources[i].filename()) + " failed:\n" + g_tcc_error;
        objects.push_back(o);
        progress = 20 + int(75 * (i + 1) / sources.size());
    }
    g_tcc_error.clear();
    TCCState* s = new_tcc(tcc_dir, TCC_OUTPUT_DLL);
    if (!s) return "Out of memory starting the linker.";
    bool ok = true;
    for (const auto& o : objects) ok = ok && tcc_add_file(s, o.c_str()) == 0;
#ifdef _WIN32
    tcc_add_library_path(s, (tcc_dir + "/lib").c_str());
    ok = ok && tcc_add_library(s, "msvcrt") == 0;  // memcpy and friends
#else
    ok = ok && tcc_add_file(s, "/system/lib64/libc.so") == 0;  // memcpy and friends
#endif
    const std::string tmp = out_module + ".tmp";
    fs::create_directories(fs::u8path(out_module).parent_path(), ec);
    ok = ok && tcc_output_file(s, tmp.c_str()) == 0;
    tcc_delete(s);
    if (!ok) return "Linking the game failed:\n" + g_tcc_error;
    fs::remove(fs::u8path(out_module), ec);
    fs::rename(fs::u8path(tmp), fs::u8path(out_module), ec);
    if (ec) return "Cannot store the game: " + ec.message();
    fs::remove_all(fs::u8path(work), ec);
    progress = 100;
    return "";
}

}  // namespace ph
