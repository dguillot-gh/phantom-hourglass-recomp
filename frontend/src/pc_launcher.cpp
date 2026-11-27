// The PC launcher's window (pc_launcher.hpp): SDL's 2D renderer, the menu's 8x8 font and colours.
#include "pc_launcher.hpp"

#include <algorithm>
#include <cstdint>

#include "font8x8_basic.h"

namespace ph::launcher {
namespace {

constexpr float kW = 720, kH = 760;  // the layout's size (scaled to the window, with bars)
constexpr float kPad = 24, kGap = 8;
constexpr uint32_t kBack = 0x181820, kSide = 0x10101A, kRow = 0x2A2A34, kRowOff = 0x202028, kAccent = 0x8AB4FF,
                   kWhite = 0xFFFFFF, kHint = 0x9090A0, kOff = 0x606068, kDark = 0x101018, kPlay = 0x2F9E6E;

void fill(SDL_Renderer* r, const SDL_FRect& f, uint32_t rgb) {
    SDL_SetRenderDrawColor(r, Uint8(rgb >> 16), Uint8(rgb >> 8), Uint8(rgb), 255);
    SDL_RenderFillRect(r, &f);
}

void outline(SDL_Renderer* r, const SDL_FRect& f, uint32_t rgb, float t) {
    fill(r, {f.x - t, f.y - t, f.w + 2 * t, t}, rgb);
    fill(r, {f.x - t, f.y + f.h, f.w + 2 * t, t}, rgb);
    fill(r, {f.x - t, f.y, t, f.h}, rgb);
    fill(r, {f.x + f.w, f.y, t, f.h}, rgb);
}

bool inside(const SDL_FRect& f, float x, float y) { return x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h; }

float text_width(const std::string& s, float scale) { return float(s.size()) * 8 * scale; }

}  // namespace

Window::Window(const char* title) {
    // As large as fits the desktop (with room to spare), at the layout's shape.
    SDL_Rect usable{0, 0, 1280, 720};
    SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    scale = std::min({scale, usable.w * 0.9f / kW, usable.h * 0.9f / kH});
    window_ = SDL_CreateWindow(title, int(kW * scale), int(kH * scale), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) return;
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) return;
    SDL_SetRenderLogicalPresentation(renderer_, int(kW), int(kH), SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer_, 1);
    // The font: the 128 characters side by side, white on transparent (coloured when drawn).
    std::vector<uint32_t> px(128 * 8 * 8, 0);
    for (int c = 0; c < 128; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (font8x8_basic[c][y] & (1u << x)) px[size_t(y * 1024 + c * 8 + x)] = 0xFFFFFFFF;
    font_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1024, 8);
    if (font_) {
        SDL_UpdateTexture(font_, nullptr, px.data(), 1024 * 4);
        SDL_SetTextureBlendMode(font_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(font_, SDL_SCALEMODE_NEAREST);
    }
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++)
            if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) pads_.push_back(pad);
        SDL_free(ids);
    }
}

Window::~Window() {
    for (SDL_Gamepad* pad : pads_) SDL_CloseGamepad(pad);
    if (font_) SDL_DestroyTexture(font_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void Window::hide() { if (window_) SDL_HideWindow(window_); }
void Window::show() {
    if (!window_) return;
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
}

void Window::text(float x, float y, const std::string& s, float scale, uint32_t rgb) {
    if (!font_) return;
    SDL_SetTextureColorMod(font_, Uint8(rgb >> 16), Uint8(rgb >> 8), Uint8(rgb));
    for (const char ch : s) {
        const unsigned c = static_cast<unsigned char>(ch) < 128 ? static_cast<unsigned char>(ch) : '?';
        const SDL_FRect src{float(c * 8), 0, 8, 8}, dst{x, y, 8 * scale, 8 * scale};
        SDL_RenderTexture(renderer_, font_, &src, &dst);
        x += 8 * scale;
    }
}

std::vector<Window::Stop> Window::layout(const View& v) const {
    std::vector<Stop> stops;
    const float w = kW - 2 * kPad;
    float y = 128;
    // The big buttons: Continue (when there is a resume state), Start game (or Build the game).
    if (v.can_play && !v.resume_time.empty()) {
        Stop s;
        s.at = {kPad, y, w, 60}, s.action = Action::Continue, s.big = true;
        s.text = "Continue", s.sub = "Where you last quit - " + v.resume_time;
        stops.push_back(s);
        y += 60 + kGap;
    }
    {
        Stop s;
        s.at = {kPad, y, w, 60}, s.big = true;
        if (v.can_play) s.action = Action::Play, s.text = "Start game", s.sub = "From the title screen (in-game saves)";
        else s.action = Action::Rebuild, s.text = "Build the game", s.sub = "From the kept ROM (about a minute)";
        stops.push_back(s);
        y += 60 + 14;
    }
    // The small buttons, two by two.
    const float half = (w - kGap) / 2;
    const std::pair<const char*, Action> small[] = {{"Change ROM...", Action::ChangeRom}, {"Rebuild game", Action::Rebuild},
                                                    {"Open data folder", Action::OpenFolder}, {"Quit", Action::Quit}};
    for (int i = 0; i < 4; i++) {
        Stop s;
        s.at = {kPad + float(i % 2) * (half + kGap), y + float(i / 2) * (40 + kGap), half, 40};
        s.text = small[i].first, s.action = small[i].second;
        stops.push_back(s);
    }
    y += 2 * (40 + kGap) + 40;  // (then the "Settings" heading)
    // The settings: a label, then the choices.
    const float label_w = 230;
    for (size_t o = 0; o < v.options.size(); o++) {
        Stop s;
        s.option = int(o);
        s.at = {kPad, y, w, 36};
        const size_t n = v.options[o].choices.size();
        const float cw = (w - label_w - kGap * float(n - 1)) / float(n);
        for (size_t i = 0; i < n; i++) s.chips.push_back({kPad + label_w + float(i) * (cw + kGap), y, cw, 36});
        stops.push_back(s);
        y += 36 + 8;
    }
    return stops;
}

void Window::draw(const View& v, const std::vector<Stop>& stops) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    fill(renderer_, {0, 0, kW, kH}, kBack);
    fill(renderer_, {0, 0, kW, 116}, kSide);
    fill(renderer_, {0, 112, kW, 4}, kAccent);
    text(kPad, 22, v.title, 3, kWhite);
    text(kPad, 58, v.info, 2, kHint);
    text(kPad, 82, v.status, 2, v.can_play ? kHint : kAccent);
    for (size_t i = 0; i < stops.size(); i++) {
        const Stop& s = stops[i];
        const bool focused = int(i) == focus_;
        if (s.option < 0) {
            const bool play = s.big && (s.action == Action::Play || s.action == Action::Continue) && i == 0;
            fill(renderer_, s.at, !s.enabled ? kRowOff : play ? kPlay : kRow);
            if (focused) outline(renderer_, s.at, kAccent, 3);
            const uint32_t ink = s.enabled ? kWhite : kOff;
            if (s.big) {
                text(s.at.x + 16, s.at.y + 10, s.text, 2, ink);
                text(s.at.x + 16, s.at.y + 36, s.sub, 2, s.enabled ? (play ? 0xD8F0E4 : kHint) : kOff);
            } else {
                text(s.at.x + (s.at.w - text_width(s.text, 2)) / 2, s.at.y + 12, s.text, 2, ink);
            }
            continue;
        }
        const Option& o = v.options[size_t(s.option)];
        if (s.option == 0) text(kPad, s.at.y - 30, "Settings", 2, kAccent);
        text(s.at.x, s.at.y + 10, o.label, 2, focused ? kWhite : kHint);
        if (focused) fill(renderer_, {s.at.x - 12, s.at.y + 4, 4, s.at.h - 8}, kAccent);
        for (size_t c = 0; c < s.chips.size(); c++) {
            const bool on = int(c) == o.chosen, enabled = c < o.enabled.size() ? o.enabled[c] : true;
            fill(renderer_, s.chips[c], on ? (enabled ? kAccent : kOff) : enabled ? kRow : kRowOff);
            const std::string& t = o.choices[c];
            text(s.chips[c].x + (s.chips[c].w - text_width(t, 2)) / 2, s.chips[c].y + 10, t, 2,
                 on ? kDark : enabled ? kWhite : kOff);
        }
    }
    text(kPad, kH - 30, "Enter / A: choose   Arrows / D-pad: move   Esc: quit", 1.5f, kOff);
    SDL_RenderPresent(renderer_);
}

// Up/down: the next button or setting; left/right: the button beside, or the setting's next choice.
Action Window::move(View& v, const std::vector<Stop>& stops, int dy, int dx) {
    const int n = int(stops.size());
    if (n == 0) return Action::None;
    focus_ = std::clamp(focus_, 0, n - 1);
    const Stop& s = stops[size_t(focus_)];
    if (dx != 0 && s.option >= 0) {
        Option& o = v.options[size_t(s.option)];
        for (int c = o.chosen + dx; c >= 0 && c < int(o.choices.size()); c += dx)
            if (c >= int(o.enabled.size()) || o.enabled[size_t(c)]) {
                o.chosen = c;
                changed_option = s.option;
                return Action::Changed;
            }
        return Action::None;
    }
    if (dx != 0 && !s.big) {  // (the small buttons are two by two)
        const int to = focus_ + dx;
        if (to >= 0 && to < n && stops[size_t(to)].option < 0 && !stops[size_t(to)].big) focus_ = to;
        return Action::None;
    }
    const int step = dy;
    if (!s.big && s.option < 0 && dy != 0) {  // from a small button: the one above or below it, else past the pair
        const int to = focus_ + 2 * dy;
        if (to >= 0 && to < n && stops[size_t(to)].option < 0 && !stops[size_t(to)].big) {
            focus_ = to;
            return Action::None;
        }
    }
    for (int i = focus_ + step; i >= 0 && i < n; i += step)
        if (stops[size_t(i)].enabled) {
            focus_ = i;
            break;
        }
    return Action::None;
}

Action Window::run(View& v) {
    if (!renderer_) return Action::Play;
    changed_option = -1;
    for (;;) {
        const std::vector<Stop> stops = layout(v);
        focus_ = std::clamp(focus_, 0, int(stops.size()) - 1);
        draw(v, stops);
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 250)) continue;
        do {
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            const auto activate = [&]() -> Action {
                const Stop& s = stops[size_t(focus_)];
                return s.option < 0 && s.enabled ? s.action : Action::None;
            };
            Action a = Action::None;
            switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return Action::Quit;
                case SDL_EVENT_KEY_DOWN:
                    switch (e.key.scancode) {
                        case SDL_SCANCODE_ESCAPE: return Action::Quit;
                        case SDL_SCANCODE_RETURN:
                        case SDL_SCANCODE_KP_ENTER:
                        case SDL_SCANCODE_SPACE: a = activate(); break;
                        case SDL_SCANCODE_UP: a = move(v, stops, -1, 0); break;
                        case SDL_SCANCODE_DOWN:
                        case SDL_SCANCODE_TAB: a = move(v, stops, 1, 0); break;
                        case SDL_SCANCODE_LEFT: a = move(v, stops, 0, -1); break;
                        case SDL_SCANCODE_RIGHT: a = move(v, stops, 0, 1); break;
                        default: break;
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) pads_.push_back(pad);
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_SOUTH:
                        case SDL_GAMEPAD_BUTTON_START: a = activate(); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: a = move(v, stops, -1, 0); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: a = move(v, stops, 1, 0); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: a = move(v, stops, 0, -1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: a = move(v, stops, 0, 1); break;
                        default: break;
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    for (size_t i = 0; i < stops.size(); i++)
                        if (stops[i].enabled && inside(stops[i].at, e.motion.x, e.motion.y)) focus_ = int(i);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (e.button.button != SDL_BUTTON_LEFT) break;
                    for (size_t i = 0; i < stops.size(); i++) {
                        const Stop& s = stops[i];
                        if (s.option < 0) {
                            if (s.enabled && inside(s.at, e.button.x, e.button.y)) focus_ = int(i), a = s.action;
                            continue;
                        }
                        Option& o = v.options[size_t(s.option)];
                        for (size_t c = 0; c < s.chips.size(); c++)
                            if (inside(s.chips[c], e.button.x, e.button.y) &&
                                (c >= o.enabled.size() || o.enabled[c]) && int(c) != o.chosen) {
                                focus_ = int(i);
                                o.chosen = int(c);
                                changed_option = s.option;
                                a = Action::Changed;
                            }
                    }
                    break;
                default: break;
            }
            if (a != Action::None) return a;
        } while (SDL_PollEvent(&e));
    }
}

}  // namespace ph::launcher
