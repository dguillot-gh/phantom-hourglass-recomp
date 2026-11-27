// The PC launcher: a small window shown before the game starts (PC builds that build the game from the player's ROM,
// PH_PC_SETUP), like the Android app's start screen. Continue / Start game, Change ROM, Rebuild game, Open data
// folder, Quit, and the main settings as rows of choices. Mouse, keyboard (arrows, Enter, Esc) and gamepad (D-pad, A).
// It only shows what main.cpp gives it (View) and says what was chosen (Action); main.cpp does the work.
#pragma once

#include <SDL3/SDL.h>

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
    std::string title;        // the game's name, large
    std::string info;         // the ROM line
    std::string status;       // the game line (built or not)
    std::string resume_time;  // when the resume state was written ("" = none: no Continue button)
    bool can_play = false;    // the game is built (else the first button builds it)
    std::vector<Option> options;
};

class Window {
public:
    explicit Window(const char* title);
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
    struct Stop {  // something the focus can be on: a button, or a setting's row
        SDL_FRect at{};
        Action action = Action::None;
        int option = -1;
        std::string text, sub;
        bool enabled = true, big = false;
        std::vector<SDL_FRect> chips;
    };
    std::vector<Stop> layout(const View& view) const;
    void draw(const View& view, const std::vector<Stop>& stops);
    void text(float x, float y, const std::string& s, float scale, uint32_t rgb);
    Action move(View& view, const std::vector<Stop>& stops, int dy, int dx);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* font_ = nullptr;
    std::vector<SDL_Gamepad*> pads_;
    int focus_ = 0;
};

}  // namespace ph::launcher
