// Phantom Hourglass's own code that the recompiled game calls: the hooks of frontend/config/azee/hooks.txt.
#pragma once

#include <string>

namespace ph {

// Picks the addresses for the release with this game code ("AZEE": USA). False for any other.
bool select_release(const std::string& game_code);

// A hook of hooks.txt by name, for the game module; null if there is none of that name.
void* find_hook(const char* name);

// Widescreen (a test): the 3D drawn `extra` DS pixels wider on each side (0: 4:3). Set with the runtime's
// VideoSettings::wide_extra (the GPU and Compute renderers).
void set_wide_extra(int extra);

}  // namespace ph
