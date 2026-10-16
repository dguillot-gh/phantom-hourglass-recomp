#include "menu.hpp"

#include <algorithm>
#include <cstdio>

#include "gl_present.hpp"

namespace ph {
namespace {

// The Android menu's colours.
constexpr uint32_t kShade = 0x55000000, kPanel = 0xF0181820, kRow = 0xFF2A2A34, kRowOff = 0xFF202028,
                   kAccent = 0xFF8AB4FF, kWhite = 0xFFFFFFFF, kHint = 0xFF9090A0, kOff = 0xFF606068,
                   kEmpty = 0xFF121218;

enum RowId { kScreen, kRenderer, kResolution, kPicture, kSize, kWide, kFps, kFullscreen, kStick, kContinue, kQuit, kRowCount };
constexpr int kScales[] = {2, 4, 6, 8};
constexpr int kColumns = 4;  // of the save state slots

}  // namespace

Rect Menu::panel(float win_w, float win_h, float ui) {
    const float w = std::min(win_w, std::max(420.0f * ui, win_w * 0.42f));
    return {win_w - w, 0, w, win_h};
}

Menu::Geometry Menu::geometry(const MenuState& s, float win_w, float win_h, float ui) {
    Geometry g;
    g.panel = panel(win_w, win_h, ui);
    const Rect& p = g.panel;
    const float pad = 16 * ui, text = 16 * ui, chip_h = 36 * ui, gap = 6 * ui;
    float y = pad + text * 2.2f;
    const float half = (p.w - 2 * pad - gap) / 2;
    g.tabs = {Rect{p.x + pad, y, half, chip_h}, Rect{p.x + pad + half + gap, y, half, chip_h}};
    y += chip_h + 18 * ui;

    // Settings: each row's label over its line of chips.
    std::vector<Row>& r = g.rows;
    r.resize(kRowCount);
    r[kScreen] = {"Screen", {"Touch", "Top", "Both"}, int(s.layout), {true, true, true}, {}, {}};
    r[kRenderer] = {"Renderer", {"Software", "GPU"}, s.gpu ? 1 : 0, {true, !s.gpu_failed}, {}, {}};
    {
        int chosen = 0;
        for (int i = 0; i < 4; i++)
            if (s.gpu && kScales[i] == s.scale) chosen = i + 1;
        r[kResolution] = {"Resolution", {"1x", "2x", "4x", "6x", "8x"}, chosen, {!s.gpu, s.gpu, s.gpu, s.gpu, s.gpu}, {}, {}};
    }
    r[kPicture] = {"Picture", {"Sharp", "Smooth"}, s.smooth ? 1 : 0, {true, true}, {}, {}};
    r[kSize] = {"Picture size", {"Fit", "Stretch"}, s.stretch ? 1 : 0, {true, true}, {}, {}};
    r[kWide] = {"Widescreen (test, GPU)", {"4:3", "16:9"}, s.wide && s.gpu ? 1 : 0, {true, s.gpu}, {}, {}};
    r[kFps] = {"FPS counter", {"Off", "Top L", "Top R", "Bot L", "Bot R"}, s.fps ? s.fps_corner + 1 : 0,
               {true, true, true, true, true}, {}, {}};
    r[kFullscreen] = {"Fullscreen", {"Off", "On"}, s.fullscreen ? 1 : 0, {true, true}, {}, {}};
    r[kStick] = {"Gamepad's left stick", {"D-pad", "Analog"}, s.stick ? 1 : 0, {true, true}, {}, {}};
    r[kContinue] = {"", {s.resume_time.empty() ? std::string("Continue: nothing yet") : "Continue from " + s.resume_time},
                    -1, {!s.resume_time.empty()}, {}, {}};
    r[kQuit] = {"", {"Quit the game"}, -1, {true}, {}, {}};
    float ry = y;
    for (Row& row : r) {
        if (!row.label.empty()) {
            row.label_at = {p.x + pad, ry, p.w - 2 * pad, text};
            ry += text + 8 * ui;
        }
        const int n = int(row.choices.size());
        const float cw = (p.w - 2 * pad - gap * float(n - 1)) / float(n);
        for (int i = 0; i < n; i++) row.chips.push_back({p.x + pad + float(i) * (cw + gap), ry, cw, chip_h});
        ry += chip_h + 14 * ui;
    }

    // Save states: a grid of cards (the picture, then the slot's number and time), then Save and Load.
    const float cw = (p.w - 2 * pad - gap * float(kColumns - 1)) / float(kColumns);
    const float th = cw * 0.75f, label = 10 * ui, ch = th + label + 10 * ui;
    for (int i = 0; i < kStateSlots; i++) {
        const float cx = p.x + pad + float(i % kColumns) * (cw + gap), cy = y + float(i / kColumns) * (ch + gap);
        g.cards[size_t(i)] = {cx, cy, cw, ch};
        g.thumbs[size_t(i)] = {cx + 2 * ui, cy + 2 * ui, cw - 4 * ui, th - 4 * ui};
    }
    const float by = y + float((kStateSlots + kColumns - 1) / kColumns) * (ch + gap) + 12 * ui;
    g.buttons = {Rect{p.x + pad, by, half, chip_h + 6 * ui}, Rect{p.x + pad + half + gap, by, half, chip_h + 6 * ui}};
    return g;
}

MenuResult Menu::choose(int row, int choice, MenuState& s) {
    switch (row) {
        case kScreen: s.layout = Layout(choice); break;
        case kRenderer:
            if (choice == 1 && s.gpu_failed) return MenuResult::None;
            s.gpu = choice == 1;
            break;
        case kResolution:
            if (!s.gpu || choice == 0) return MenuResult::None;  // (1x is the software renderer's)
            s.scale = kScales[choice - 1];
            break;
        case kPicture: s.smooth = choice == 1; break;
        case kSize: s.stretch = choice == 1; break;
        case kWide:
            if (choice == 1 && !s.gpu) return MenuResult::None;
            s.wide = choice == 1;
            break;
        case kFps:
            s.fps = choice != 0;
            if (choice) s.fps_corner = choice - 1;
            break;
        case kFullscreen: s.fullscreen = choice == 1; break;
        case kStick: s.stick = choice == 1; break;
        case kContinue: return s.resume_time.empty() ? MenuResult::None : MenuResult::Continue;
        case kQuit: return MenuResult::Quit;
        default: return MenuResult::None;
    }
    return MenuResult::Changed;
}

MenuResult Menu::key(SDL_Scancode key, MenuState& s) {
    if (key == SDL_SCANCODE_ESCAPE || key == SDL_SCANCODE_F1) {
        open = false;
        return MenuResult::None;
    }
    if (key == SDL_SCANCODE_TAB) {
        page ^= 1;
        return MenuResult::None;
    }
    if (page == 1) {  // arrows choose the slot; S saves to it, L (or Enter) loads it
        const int i = s.slot - 1;
        int to = i;
        if (key == SDL_SCANCODE_LEFT) to = (i + kStateSlots - 1) % kStateSlots;
        if (key == SDL_SCANCODE_RIGHT) to = (i + 1) % kStateSlots;
        if (key == SDL_SCANCODE_UP) to = (i + kStateSlots - kColumns) % kStateSlots;
        if (key == SDL_SCANCODE_DOWN) to = (i + kColumns) % kStateSlots;
        if (to != i) {
            s.slot = to + 1;
            return MenuResult::Changed;
        }
        if (key == SDL_SCANCODE_S) return MenuResult::SaveState;
        if ((key == SDL_SCANCODE_L || key == SDL_SCANCODE_RETURN) && !s.slot_time[size_t(i)].empty())
            return MenuResult::LoadState;
        return MenuResult::None;
    }
    const std::vector<Row> r = geometry(s, 1000, 1000, 1).rows;  // (choices only: the geometry does not matter here)
    if (key == SDL_SCANCODE_UP) selected = (selected + kRowCount - 1) % kRowCount;
    if (key == SDL_SCANCODE_DOWN) selected = (selected + 1) % kRowCount;
    const Row& row = r[size_t(selected)];
    const int n = int(row.choices.size());
    auto step = [&](int dir) {
        for (int i = 1; i < n; i++) {  // the next enabled choice that way
            const int c = (std::max(row.chosen, 0) + dir * i + n * 4) % n;
            if (row.enabled[size_t(c)]) return choose(selected, c, s);
        }
        return MenuResult::None;
    };
    if (key == SDL_SCANCODE_LEFT) return step(-1);
    if (key == SDL_SCANCODE_RIGHT) return step(1);
    if (key == SDL_SCANCODE_RETURN || key == SDL_SCANCODE_SPACE) {
        if (selected == kQuit || selected == kContinue) return choose(selected, 0, s);  // (buttons)
        return step(1);
    }
    return MenuResult::None;
}

MenuResult Menu::click(float x, float y, float win_w, float win_h, float ui, MenuState& s) {
    const Geometry g = geometry(s, win_w, win_h, ui);
    if (!g.panel.contains(x, y)) {
        open = false;
        return MenuResult::None;
    }
    for (int t = 0; t < 2; t++)
        if (g.tabs[size_t(t)].contains(x, y)) {
            page = t;
            return MenuResult::None;
        }
    if (page == 1) {
        for (int i = 0; i < kStateSlots; i++)
            if (g.cards[size_t(i)].contains(x, y)) {
                s.slot = i + 1;
                return MenuResult::Changed;
            }
        if (g.buttons[0].contains(x, y)) return MenuResult::SaveState;
        if (g.buttons[1].contains(x, y) && !s.slot_time[size_t(s.slot - 1)].empty()) return MenuResult::LoadState;
        return MenuResult::None;
    }
    for (size_t i = 0; i < g.rows.size(); i++)
        for (size_t c = 0; c < g.rows[i].chips.size(); c++)
            if (g.rows[i].chips[c].contains(x, y) && g.rows[i].enabled[c]) {
                selected = int(i);
                return choose(int(i), int(c), s);
            }
    return MenuResult::None;
}

namespace {
void chip(GlPresenter& gl, const Rect& b, const std::string& t, bool chosen, bool on, float text, float ui) {
    gl.fill(b, chosen ? kAccent : on ? kRow : kRowOff);
    const float size = std::min(text, (b.w - 8 * ui) / float(std::max<size_t>(t.size(), 1)));
    gl.text(b.x + (b.w - size * float(t.size())) / 2, b.y + (b.h - size) / 2, size, t,
            chosen ? 0xFF000000 : on ? kWhite : kOff);
}
}  // namespace

void Menu::draw_panel(GlPresenter& gl, const MenuState& s, float win_w, float win_h, float ui, float fps) const {
    const Geometry g = geometry(s, win_w, win_h, ui);
    const Rect& p = g.panel;
    const float pad = 16 * ui, text = 16 * ui;
    gl.fill({0, 0, win_w, win_h}, kShade);
    gl.fill(p, kPanel);
    gl.text(p.x + pad, pad, text * 1.4f, "Menu", kWhite);
    char rate[32];
    std::snprintf(rate, sizeof rate, "%.1f fps", double(fps));
    const float small = 8 * ui * 1.5f;
    gl.text(p.x + p.w - pad - small * float(std::string(rate).size()), pad + text * 0.3f, small, rate, kHint);
    chip(gl, g.tabs[0], "Settings", page == 0, true, text, ui);
    chip(gl, g.tabs[1], "Save states", page == 1, true, text, ui);

    if (page == 1) {
        const float label = 10 * ui;
        for (int i = 0; i < kStateSlots; i++) {
            const Rect& c = g.cards[size_t(i)];
            const std::string& when = s.slot_time[size_t(i)];
            if (i + 1 == s.slot) gl.fill(c, kAccent);  // the chosen slot: framed
            const Rect& t = g.thumbs[size_t(i)];
            gl.fill(t, when.empty() ? kEmpty : kRow);  // (the picture is drawn over this, see draw())
            if (i + 1 != s.slot) gl.fill({c.x, t.y + t.h + 2 * ui, c.w, c.h - t.h - 4 * ui}, kRowOff);
            std::string name = std::to_string(i + 1) + (when.empty() ? " empty" : " " + when);
            const float size = std::min(label, (c.w - 6 * ui) / float(name.size()));
            gl.text(c.x + 3 * ui, t.y + t.h + 4 * ui, size, name, i + 1 == s.slot ? 0xFF000000 : when.empty() ? kOff : kWhite);
        }
        const bool used = !s.slot_time[size_t(s.slot - 1)].empty();
        chip(gl, g.buttons[0], "Save to " + std::to_string(s.slot), false, true, text, ui);
        chip(gl, g.buttons[1], "Load " + std::to_string(s.slot), false, used, text, ui);
        gl.text(p.x + pad, win_h - pad - small * 2.6f, small, "Arrows: slot. S: save. L or Enter: load.", kHint);
        gl.text(p.x + pad, win_h - pad - small, small, "F5 saves, F8 loads (menu closed). Tab: page.", kHint);
        return;
    }

    for (size_t i = 0; i < g.rows.size(); i++) {
        const Row& row = g.rows[i];
        if (!row.label.empty()) gl.text(row.label_at.x, row.label_at.y, text, row.label, kAccent);
        if (int(i) == selected) {  // the keyboard's place: a bar beside the row's chips
            const Rect& c0 = row.chips.front();
            gl.fill({c0.x - 8 * ui, c0.y, 3 * ui, c0.h}, kAccent);
        }
        for (size_t c = 0; c < row.chips.size(); c++)
            chip(gl, row.chips[c], row.choices[c], int(c) == row.chosen, row.enabled[c], text, ui);
    }
    const char* hint = s.gpu_failed ? "The GPU renderer could not start: software only."
                                    : "F1 or Esc: close. Arrows: choose. Tab: page.";
    gl.text(p.x + pad, win_h - pad - small, small, hint, kHint);
}

}  // namespace ph
