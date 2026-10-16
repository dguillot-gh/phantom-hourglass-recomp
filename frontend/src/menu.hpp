// The PC's side menu (F1 or Esc): a panel over the right part of the window, the game still running beside it
// (laid out like the Android app's menu). Two pages: Settings (screen, renderer and resolution (1x-8x with the GPU
// renderer), picture, FPS counter, fullscreen, continue from where you quit, quit) and Save states (12 slots with a picture of each, save and load).
// The main thread handles its input; the game thread draws it (both from the same geometry).
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

#include "layout.hpp"

namespace ph {

class GlPresenter;

constexpr int kStateSlots = 12;

// What the menu shows and changes.
struct MenuState {
    Layout layout = Layout::Bottom;
    bool gpu = false;          // the GPU (OpenGL) renderer, else the software one
    int scale = 4;             // the GPU renderer's internal resolution: 2, 4, 6 or 8
    bool smooth = false;       // smooth instead of sharp pixels
    bool fps = false;          // FPS counter
    bool fullscreen = false;
    bool gpu_failed = false;   // the GPU renderer could not start (the game is on the software one)
    int slot = 1;              // the save state slot chosen (1-12)
    std::array<std::string, kStateSlots> slot_time{};  // when each slot was saved ("" = empty)
    int fps_corner = 1;        // the FPS counter's corner of the picture (0 top left .. 3 bottom right)
    bool stretch = false;      // the picture stretched over the window
    std::string resume_time;   // when the resume state (Continue) was written ("" = none)
    bool stick = false;        // a gamepad's left stick moves Link as an analog stick (else it is the D-pad)
    bool wide = false;         // widescreen 16:9 (a test; the GPU renderer only)
    int textures = 0;          // the 3D's textures (GPU renderer): 0 the DS's pixels, 1 smooth, 2 sharp
    int fixes = 0;             // 3D fixes, bits: 1 fewer seams (GPU renderer), 2 stable textures
    bool compute = false;      // with gpu: the compute-shader renderer (no widescreen or texture filtering)
};

enum class MenuResult { None, Changed, Quit, SaveState, LoadState, Continue };

class Menu {
public:
    bool open = false;
    int page = 0;  // 0 Settings, 1 Save states
    int selected = 0;

    MenuResult key(SDL_Scancode key, MenuState& s);
    // A click at window pixel (x, y); a click beside the panel closes it.
    MenuResult click(float x, float y, float win_w, float win_h, float ui, MenuState& s);
    // `thumbnail(slot, where)` draws a slot's picture (slot 0-11).
    template <class Thumb>
    void draw(GlPresenter& gl, const MenuState& s, float win_w, float win_h, float ui, float fps, Thumb thumbnail) const {
        draw_panel(gl, s, win_w, win_h, ui, fps);
        if (page == 1) {
            const Geometry g = geometry(s, win_w, win_h, ui);
            for (int i = 0; i < kStateSlots; i++)
                if (!s.slot_time[size_t(i)].empty()) thumbnail(i, g.thumbs[size_t(i)]);
        }
    }

private:
    struct Row {
        std::string label;
        std::vector<std::string> choices;  // one choice: a button
        int chosen = 0;
        std::vector<bool> enabled;
        Rect label_at;
        std::vector<Rect> chips;
    };
    struct Geometry {
        Rect panel;
        std::array<Rect, 2> tabs;
        std::vector<Row> rows;                  // Settings
        std::array<Rect, kStateSlots> cards{};  // Save states: each slot's card, its picture, the save/load buttons
        std::array<Rect, kStateSlots> thumbs{};
        std::array<Rect, 2> buttons{};
    };
    static Geometry geometry(const MenuState& s, float win_w, float win_h, float ui);
    static Rect panel(float win_w, float win_h, float ui);
    static MenuResult choose(int row, int choice, MenuState& s);
    void draw_panel(GlPresenter& gl, const MenuState& s, float win_w, float win_h, float ui, float fps) const;
};

}  // namespace ph
