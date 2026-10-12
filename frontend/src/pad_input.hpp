// Input from an on-screen pad (the Android app's controls, frontend/android): set from any thread, read by the frontend.
#pragma once

#include <atomic>
#include <cstdint>

namespace ph::pad {

extern std::atomic<uint32_t> held;  // DS buttons held on the pad (ntr::Key bits)
extern std::atomic<bool> blow;      // the Mic button: the microphone hears blowing
extern std::atomic<bool> lid;       // the Lid button: the DS is closed
extern std::atomic<int> show;       // a screen layout asked for (0 touch screen, 1 top screen, 2 both); -1: none
extern std::atomic<int> smooth;     // the picture filter asked for (0 sharp pixels, 1 smooth); -1: none
extern std::atomic<bool> fps;       // the FPS counter is shown
extern std::atomic<int> fps_corner; // where, on the picture: 0 top left, 1 top right, 2 bottom left, 3 bottom right
extern std::atomic<int> stretch;    // the picture size asked for (0 fit, 1 stretched over the screen); -1: none
extern std::atomic<int> game_fps10; // the game's frames per second over the last second, x10 (for the menu)
extern std::atomic<int> state_save;  // save a state to this slot (1-12; 0: none)
extern std::atomic<int> state_load;  // load the state in this slot
extern std::atomic<int> video;       // a renderer asked for: (GPU ? 1 : 0) << 8 | internal resolution; -1: none
extern std::atomic<bool> gpu_failed; // the GPU renderer could not start (software only)

}  // namespace ph::pad
