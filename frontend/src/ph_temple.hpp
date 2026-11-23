// Temple of the Ocean King floor checkpoints: the lobby's stairs down can lead to any floor Link has reached before.
//
// The game finds the exit Link takes in MapBase::FindExit (overlay 0 0x0208033C, hooked: ph_find_exit). Every exit
// into the temple (course 37, dngn_main) is recorded: its map (the floor) and entrance, kept in a file beside the
// settings. In the hall (course 38, dngn_main_f) a picker on the game screen chooses the floor (L), and 1F's stairs
// down (course 37, map 0) then lead there instead of B1: Link still passes through 1F, where the sand timer starts.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace ph::temple {

// Where the floors reached are kept (the data folder); loads them.
void set_dir(const std::filesystem::path& dir);

// Once a frame (game thread): reads where Link is (peek32 reads the game's memory) and takes the DS buttons held
// (L chooses the floor while the picker is shown). Returns the buttons the game must not see this frame.
uint32_t frame(const std::function<uint32_t(uint32_t)>& peek32, uint32_t held);

// The picker's text while Link is in the lobby, empty otherwise ("Stairs down: B5   < L  R >").
std::string picker_text();

}  // namespace ph::temple
