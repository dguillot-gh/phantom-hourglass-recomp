#include "game_module.hpp"

#include <SDL3/SDL.h>

#include "ntr/recomp.h"
#include "ph_hooks.hpp"

namespace ph {

std::filesystem::path default_module_path() {
#ifdef __ANDROID__
    return "libph_game.so";  // (packaged in the APK: the dynamic linker finds it by name in the app's library folder)
#endif
    std::filesystem::path dir = SDL_GetBasePath() ? std::filesystem::path(SDL_GetBasePath()) : std::filesystem::path(".");
#if defined(_WIN32)
    return dir / "ph_game.dll";
#elif defined(__APPLE__)
    return dir / "libph_game.dylib";
#else
    return dir / "libph_game.so";
#endif
}

bool load_game_module(const std::filesystem::path& path, ntr::GameCode& code, std::string& error) {
    SDL_SharedObject* lib = SDL_LoadObject(path.string().c_str());
    if (!lib) {
        error = "cannot load the game module " + path.string() + ": " + SDL_GetError();
        return false;
    }
    auto load = reinterpret_cast<NtrGameLoadFn>(SDL_LoadFunction(lib, NTR_GAME_LOAD_SYMBOL));
    if (!load) {
        error = path.string() + " is not a game module (no " NTR_GAME_LOAD_SYMBOL ")";
        SDL_UnloadObject(lib);
        return false;
    }
    // The API table must outlive the module's use of it: the module keeps a copy, but keep ours for its life too.
    static NtrHostApi api;
    ntr_fill_host_api(&api);
    api.find_hook = &ph::find_hook;
    const NtrGameInfo* info = load(&api);
    if (!info || info->abi != NTR_ABI_VERSION) {
        error = path.string() + " was built for another version of this program: rebuild it (2-BUILD.ps1)";
        SDL_UnloadObject(lib);
        return false;
    }
    code.functions = info->functions;
    code.function_count = info->function_count;
    code.overlays = info->overlays;
    code.overlay_count = info->overlay_count;
    return true;  // (never unloaded: the game's code runs until exit)
}

}  // namespace ph
