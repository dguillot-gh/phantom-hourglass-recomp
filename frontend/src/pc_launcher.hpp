// The PC launcher: a window shown before the game starts (PC builds that build the game from the player's ROM,
// PH_PC_SETUP), laid out like the OpenGOAL launcher: a header with the version, an icon rail (the game, settings,
// quit), the game's page (a drawn background, the title, PLAY / CONTINUE) and a settings page with two tabs (Display:
// the main settings; Game files: Change ROM, Rebuild game, Open data folder). Mouse, keyboard (arrows, Enter, Esc) and
// gamepad (D-pad, A, B, shoulder buttons). Text: the embedded Inter font (stb_truetype).
// It only shows what main.cpp gives it (View) and says what was chosen (Action); main.cpp does the work.
#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ph::launcher {

enum class Action { None, Play, Continue, ChangeRom, Rebuild, OpenFolder, Quit, Changed };

// A setting: its label and a line of choices (a disabled choice is greyed out and cannot be chosen).
struct Option {
    std::string label;
    std::vector<std::string> choices;
    int chosen = 0;
    std::vector<bool> enabled;
};

struct View {
    std::string app_name;     // the header: "Phantom Hourglass Recomp"
    std::string short_name;   // the rail's game tile: "PH"
    std::string series;       // over the title: "The Legend of Zelda"
    std::string title;        // "Phantom Hourglass"
    std::string version;      // "v0.1.0 (abc1234)"
    int art = 0;              // the background: 0 the sea, 1 railway tracks
    uint32_t accent = 0xF5A524;
    std::string rom_line;     // "azee.nds (USA), 64 MB" ("" = none kept)
    std::string game_line;    // "Built for this version, 41 MB"
    std::string data_dir;
    std::string resume_time;  // when the resume state was written ("" = none: no Continue)
    bool can_play = false;    // the game is built (else the big button builds it)
    std::vector<Option> options;
};

class Window {
public:
    Window(const char* title, const unsigned char* font, size_t font_size);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool ok() const { return renderer_ != nullptr; }
    // Shows `view` until the player picks something. Action::Changed: view.options[changed_option].chosen was changed.
    Action run(View& view);
    int changed_option = -1;
    void hide();
    void show();

private:
    struct Fonts;
    struct Stop {  // something the focus can be on
        SDL_FRect at{};
        Action action = Action::None;
        int rail_page = -1;  // a rail item: the page it shows
        int tab = -1;        // a settings tab
        int option = -1;     // a setting's row
        std::string text;
        bool enabled = true, primary = false, rail = false;
        std::vector<SDL_FRect> chips;
    };
    std::vector<Stop> layout(const View& view) const;
    void draw(const View& view, const std::vector<Stop>& stops);
    void draw_background(const View& view);
    void round_rect(const SDL_FRect& r, float radius, uint32_t rgb, int alpha = 255);
    void gradient(const SDL_FRect& r, uint32_t top, int top_alpha, uint32_t bottom, int bottom_alpha);
    void glow(float cx, float cy, float radius, uint32_t rgb, int alpha);
    float text(float x, float y, const std::string& s, int size, uint32_t rgb, int alpha = 255);
    float text_width(const std::string& s, int size) const;
    Action activate(View& view, const std::vector<Stop>& stops, int index);
    Action move(View& view, const std::vector<Stop>& stops, int dy, int dx);
    void focus_default(const std::vector<Stop>& stops);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* disc_ = nullptr;  // an anti-aliased white disc (round corners)
    SDL_Texture* soft_ = nullptr;  // a soft white spot (glows)
    std::unique_ptr<Fonts> fonts_;
    std::vector<SDL_Gamepad*> pads_;
    int page_ = 0, tab_ = 0, focus_ = -1, hover_ = -1;
    bool keys_ = false;  // the focus ring shows (keyboard or gamepad in use)
    uint64_t start_ = 0;
};

}  // namespace ph::launcher
