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
extern std::atomic<bool> resume_request;  // write the resume state now (the app is about to quit)
extern std::atomic<bool> resume_done;     // the resume state asked for is written (or could not be)
extern std::atomic<int> stick_x, stick_y;  // the on-screen analog stick, -1000..1000 (y down); 0, 0 at rest
extern std::atomic<int> stick_mode;        // the analog stick moves Link (1) or the D-pad does (0); -1: no change
extern std::atomic<int> textures;          // the 3D textures asked for (0 pixels, 1 smooth, 2 sharp); -1: no change
extern std::atomic<int> compute;           // the compute renderer asked for (1) or the OpenGL one (0); -1: no change
extern std::atomic<bool> compute_failed;   // the compute renderer cannot run on this phone (the OpenGL one does)
extern std::atomic<int> fixes;             // the 3D fixes asked for (bits: 1 fewer seams, 2 stable textures); -1: no change
extern std::atomic<int> wide_mode;         // widescreen asked for (0 off, 1 16:9, 2 21:9); -1: no change

}  // namespace ph::pad
