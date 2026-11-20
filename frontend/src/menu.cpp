#include "menu.hpp"

#include <algorithm>
#include <cstdio>

#include "gl_present.hpp"

namespace ph {
namespace {

// The Android menu's colours.
constexpr uint32_t kShade = 0x55000000, kPanel = 0xF0181820, kRow = 0xFF2A2A34, kRowOff = 0xFF202028,
                   kAccent = 0xFF8AB4FF, kWhite = 0xFFFFFFFF, kHint = 0xFF9090A0, kOff = 0xFF606068,
                   kEmpty = 0xFF121218, kGold = 0xFFF5C542, kGoldBg = 0xFF2D2514, kDanger = 0xFF9E3A3A;

std::vector<const AchievementView*> filtered_achievements(const AchievementsSnapshot& snap, int filter) {
    std::vector<const AchievementView*> out;
    out.reserve(snap.items.size());
    for (const auto& a : snap.items) {
        if (filter == 1 && !a.unlocked) continue;
        if (filter == 2 && a.unlocked) continue;
        out.push_back(&a);
    }
    return out;
}

enum RowId { kScreen, kRenderer, kResolution, kTextures, kFixes, kPicture, kSize, kWide, kFps, kFullscreen, kStick, kContinue, kQuit,
             kRowCount };
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
    const float third = (p.w - 2 * pad - 2 * gap) / 3;
    g.tabs = {
        Rect{p.x + pad, y, third, chip_h},
        Rect{p.x + pad + third + gap, y, third, chip_h},
        Rect{p.x + pad + 2 * (third + gap), y, third, chip_h},
    };
    y += chip_h + 18 * ui;

    // Settings: each row's label over its line of chips.
    std::vector<Row>& r = g.rows;
    r.resize(kRowCount);
    r[kScreen] = {"Screen", {"Touch", "Top", "Both"}, int(s.layout), {true, true, true}, {}, {}};
    r[kRenderer] = {"Renderer", {"Software", "GPU", "Compute"}, !s.gpu ? 0 : s.compute ? 2 : 1,
                    {true, !s.gpu_failed, !s.gpu_failed}, {}, {}};
    {
        int chosen = 0;
        for (int i = 0; i < 4; i++)
            if (s.gpu && kScales[i] == s.scale) chosen = i + 1;
        r[kResolution] = {"Resolution", {"1x", "2x", "4x", "6x", "8x"}, chosen, {!s.gpu, s.gpu, s.gpu, s.gpu, s.gpu}, {}, {}};
    }
    const bool gl = s.gpu && !s.compute;  // texture filtering is only implemented by the classic OpenGL renderer
    r[kTextures] = {"3D textures (GPU)", {"Pixels", "Smooth", "Sharp"}, gl ? s.textures : 0, {true, gl, gl}, {}, {}};
    // (Seams: the GPU renderer's better polygons. Warp: textures the DS bends with the view, cut into a grid.)
    r[kFixes] = {"3D fixes", {"Off", "Seams", "Warp", "Both"}, s.gpu ? s.fixes : s.fixes & 2, {true, s.gpu, true, s.gpu}, {}, {}};
    r[kPicture] = {"Picture", {"Sharp", "Smooth"}, s.smooth ? 1 : 0, {true, true}, {}, {}};
    r[kSize] = {"Picture size", {"Fit", "Stretch"}, s.stretch ? 1 : 0, {true, true}, {}, {}};
    r[kWide] = {"Widescreen (test)", {"4:3", "16:9"}, s.wide && s.gpu ? 1 : 0, {true, s.gpu}, {}, {}};
    r[kFps] = {"FPS counter", {"Off", "Top L", "Top R", "Bot L", "Bot R"}, s.fps ? s.fps_corner + 1 : 0,
               {true, true, true, true, true}, {}, {}};
    r[kFullscreen] = {"Fullscreen", {"Off", "On"}, s.fullscreen ? 1 : 0, {true, true}, {}, {}};
    r[kStick] = {"Gamepad's left stick", {"D-pad", "Analog"}, s.stick ? 1 : 0, {true, true}, {}, {}};
    r[kContinue] = {"", {s.resume_time.empty() ? std::string("Continue: nothing yet") : "Continue from " + s.resume_time},
                    -1, {!s.resume_time.empty()}, {}, {}};
    r[kQuit] = {"", {"Quit the game"}, -1, {true}, {}, {}};
    // Each row's label over its chips, or (when that does not fit the window) beside them.
    float stacked = y;
    for (const Row& row : r) stacked += (row.label.empty() ? 0 : text + 8 * ui) + chip_h + 14 * ui;
    const bool beside = stacked > win_h - pad - 10 * ui * 1.5f;
    const float label_w = beside ? (p.w - 2 * pad) * 0.36f : 0;
    float ry = y;
    for (Row& row : r) {
        float cx = p.x + pad, cwidth = p.w - 2 * pad;
        if (!row.label.empty()) {
            if (beside) {
                row.label_at = {cx, ry + (chip_h - text) / 2, label_w - gap, text};
                cx += label_w, cwidth -= label_w;
            } else {
                row.label_at = {cx, ry, cwidth, text};
                ry += text + 8 * ui;
            }
        }
        const int n = int(row.choices.size());
        const float cw = (cwidth - gap * float(n - 1)) / float(n);
        for (int i = 0; i < n; i++) row.chips.push_back({cx + float(i) * (cw + gap), ry, cw, chip_h});
        ry += chip_h + (beside ? 8 : 14) * ui;
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

    // Achievements page layout: summary header, filter chips, scrollable card list, bottom nav/reset bar
    const float ach_filter_y = y + 48 * ui;
    const float f_h = 30 * ui;
    g.ach_filters = {
        Rect{p.x + pad, ach_filter_y, third, f_h},
        Rect{p.x + pad + third + gap, ach_filter_y, third, f_h},
        Rect{p.x + pad + 2 * (third + gap), ach_filter_y, third, f_h},
    };
    const float list_top = ach_filter_y + f_h + 10 * ui;
    const float footer_h = 34 * ui + 24 * ui;
    const float list_h = std::max(80 * ui, win_h - pad - footer_h - list_top);
    g.ach_list = {p.x + pad, list_top, p.w - 2 * pad, list_h};
    const float nav_y = list_top + list_h + 8 * ui;
    g.ach_buttons = {
        Rect{p.x + pad, nav_y, third, 30 * ui},
        Rect{p.x + pad + third + gap, nav_y, third, 30 * ui},
        Rect{p.x + pad + 2 * (third + gap), nav_y, third, 30 * ui},
    };
    return g;
}

MenuResult Menu::choose(int row, int choice, MenuState& s) {
    switch (row) {
        case kScreen: s.layout = Layout(choice); break;
        case kRenderer:
            if (choice >= 1 && s.gpu_failed) return MenuResult::None;
            s.gpu = choice >= 1;
            s.compute = choice == 2;
            break;
        case kResolution:
            if (!s.gpu || choice == 0) return MenuResult::None;  // (1x is the software renderer's)
            s.scale = kScales[choice - 1];
            break;
        case kTextures:
            if (choice != 0 && (!s.gpu || s.compute)) return MenuResult::None;
            s.textures = choice;
            break;
        case kFixes:
            if ((choice & 1) && !s.gpu) return MenuResult::None;
            s.fixes = choice;
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
        page = (page + 1) % 3;
        ach_reset_confirm = false;
        return MenuResult::None;
    }
    if (page == 2) {
        const auto items = filtered_achievements(s.achievements, ach_filter);
        const int max_scroll = std::max(0, int(items.size()) - 1);
        if (key == SDL_SCANCODE_UP) ach_scroll = std::max(0, ach_scroll - 1);
        else if (key == SDL_SCANCODE_DOWN) ach_scroll = std::min(max_scroll, ach_scroll + 1);
        else if (key == SDL_SCANCODE_PAGEUP) ach_scroll = std::max(0, ach_scroll - 6);
        else if (key == SDL_SCANCODE_PAGEDOWN) ach_scroll = std::min(max_scroll, ach_scroll + 6);
        else if (key == SDL_SCANCODE_LEFT) { ach_filter = (ach_filter + 2) % 3; ach_scroll = 0; ach_reset_confirm = false; }
        else if (key == SDL_SCANCODE_RIGHT) { ach_filter = (ach_filter + 1) % 3; ach_scroll = 0; ach_reset_confirm = false; }
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

void Menu::wheel(float delta_y, const MenuState& s) {
    if (!open || page != 2) return;
    const auto items = filtered_achievements(s.achievements, ach_filter);
    const int max_scroll = std::max(0, int(items.size()) - 1);
    const int step = delta_y > 0 ? -3 : (delta_y < 0 ? 3 : 0);
    ach_scroll = std::clamp(ach_scroll + step, 0, max_scroll);
}

MenuResult Menu::click(float x, float y, float win_w, float win_h, float ui, MenuState& s) {
    const Geometry g = geometry(s, win_w, win_h, ui);
    if (!g.panel.contains(x, y)) {
        open = false;
        ach_reset_confirm = false;
        return MenuResult::None;
    }
    for (int t = 0; t < 3; t++)
        if (g.tabs[size_t(t)].contains(x, y)) {
            page = t;
            ach_reset_confirm = false;
            return MenuResult::None;
        }
    if (page == 2) {
        for (int f = 0; f < 3; f++) {
            if (g.ach_filters[size_t(f)].contains(x, y)) {
                ach_filter = f;
                ach_scroll = 0;
                ach_reset_confirm = false;
                return MenuResult::None;
            }
        }
        const auto items = filtered_achievements(s.achievements, ach_filter);
        const int max_scroll = std::max(0, int(items.size()) - 1);
        if (g.ach_buttons[0].contains(x, y)) {
            ach_scroll = std::max(0, ach_scroll - 5);
            ach_reset_confirm = false;
            return MenuResult::None;
        }
        if (g.ach_buttons[1].contains(x, y)) {
            ach_scroll = std::min(max_scroll, ach_scroll + 5);
            ach_reset_confirm = false;
            return MenuResult::None;
        }
        if (g.ach_buttons[2].contains(x, y)) {
            if (!ach_reset_confirm) {
                ach_reset_confirm = true;
                return MenuResult::None;
            }
            ach_reset_confirm = false;
            ach_scroll = 0;
            return MenuResult::ResetAchievements;
        }
        ach_reset_confirm = false;
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
    chip(gl, g.tabs[1], "States", page == 1, true, text, ui);
    chip(gl, g.tabs[2], "Trophies", page == 2, true, text, ui);

    if (page == 2) {
        const auto& snap = s.achievements;
        const float sum_y = g.tabs[0].y + g.tabs[0].h + 14 * ui;
        const int pct = snap.total_count > 0 ? (snap.unlocked_count * 100 / snap.total_count) : 0;
        char hdr[96];
        std::snprintf(hdr, sizeof hdr, "%d / %d unlocked  |  %d / %d pts (%d%%)",
                      snap.unlocked_count, snap.total_count, snap.unlocked_points, snap.total_points, pct);
        const float hdr_sz = std::min(13 * ui, (p.w - 2 * pad) / float(std::max<size_t>(1, std::strlen(hdr))));
        gl.text(p.x + pad, sum_y, hdr_sz, hdr, kGold);
        // Progress bar
        const Rect bar{p.x + pad, sum_y + hdr_sz + 8 * ui, p.w - 2 * pad, 8 * ui};
        gl.fill(bar, kRowOff);
        if (snap.total_count > 0 && snap.unlocked_count > 0) {
            const float frac = float(snap.unlocked_count) / float(snap.total_count);
            gl.fill({bar.x, bar.y, bar.w * frac, bar.h}, kGold);
        }

        const int locked_cnt = std::max(0, snap.total_count - snap.unlocked_count);
        chip(gl, g.ach_filters[0], "All (" + std::to_string(snap.total_count) + ")", ach_filter == 0, true, 12 * ui, ui);
        chip(gl, g.ach_filters[1], "Done (" + std::to_string(snap.unlocked_count) + ")", ach_filter == 1, true, 12 * ui, ui);
        chip(gl, g.ach_filters[2], "Left (" + std::to_string(locked_cnt) + ")", ach_filter == 2, true, 12 * ui, ui);

        const auto items = filtered_achievements(snap, ach_filter);
        const float card_h = 54 * ui;
        const float card_gap = 6 * ui;
        const int visible = std::max(1, int((g.ach_list.h + card_gap) / (card_h + card_gap)));
        const int max_scroll = std::max(0, int(items.size()) - visible);
        const int start = std::clamp(ach_scroll, 0, max_scroll);

        if (items.empty()) {
            const char* empty_msg = ach_filter == 1 ? "No achievements unlocked yet." : "All achievements unlocked!";
            gl.text(g.ach_list.x + 8 * ui, g.ach_list.y + 16 * ui, 12 * ui, empty_msg, kHint);
        } else {
            for (int i = 0; i < visible && start + i < int(items.size()); i++) {
                const AchievementView& a = *items[size_t(start + i)];
                const Rect card{g.ach_list.x, g.ach_list.y + float(i) * (card_h + card_gap), g.ach_list.w, card_h};
                gl.fill(card, a.unlocked ? kGoldBg : kRowOff);
                // Left accent stripe
                gl.fill({card.x, card.y, 4 * ui, card.h}, a.unlocked ? kGold : kOff);

                const float tx = card.x + 10 * ui;
                const float inner_w = card.w - 18 * ui;
                const std::string pts_str = "+" + std::to_string(a.points) + " pts";
                const float pts_sz = 10 * ui;
                const float pts_w = pts_sz * float(pts_str.size());
                const std::string title_line = std::string(a.unlocked ? "[*] " : "[ ] ") + a.title;
                const float title_max_w = std::max(60 * ui, inner_w - pts_w - 8 * ui);
                const float title_sz = std::min(12 * ui, title_max_w / float(std::max<size_t>(1, title_line.size())));
                gl.text(tx, card.y + 7 * ui, title_sz, title_line, a.unlocked ? kGold : kWhite);
                gl.text(card.x + card.w - 8 * ui - pts_w, card.y + 8 * ui, pts_sz, pts_str, a.unlocked ? kGold : kHint);

                std::string desc = a.description;
                if (a.progress_max > 0) {
                    desc += " (" + std::to_string(a.progress_cur) + "/" + std::to_string(a.progress_max) + ")";
                }
                const float desc_sz = 9.5f * ui;
                const size_t max_chars = size_t(std::max(18.0f, inner_w / desc_sz));
                if (desc.size() > max_chars && max_chars > 4) desc = desc.substr(0, max_chars - 3) + "...";
                const float desc_draw = std::min(desc_sz, inner_w / float(std::max<size_t>(1, desc.size())));
                gl.text(tx, card.y + 24 * ui, desc_draw, desc, a.unlocked ? kWhite : kHint);

                if (a.progress_max > 0) {
                    const Rect pbar{tx, card.y + card.h - 10 * ui, inner_w, 4 * ui};
                    gl.fill(pbar, 0xFF14141C);
                    if (a.progress_cur > 0) {
                        const float f = std::clamp(float(a.progress_cur) / float(a.progress_max), 0.0f, 1.0f);
                        gl.fill({pbar.x, pbar.y, pbar.w * f, pbar.h}, a.unlocked ? kGold : kAccent);
                    }
                }
            }
        }

        chip(gl, g.ach_buttons[0], "Prev", false, start > 0, 12 * ui, ui);
        chip(gl, g.ach_buttons[1], "Next", false, start < max_scroll, 12 * ui, ui);
        if (ach_reset_confirm) {
            gl.fill(g.ach_buttons[2], kDanger);
            const std::string lbl = "Confirm?";
            const float sz = std::min(12 * ui, (g.ach_buttons[2].w - 8 * ui) / float(lbl.size()));
            gl.text(g.ach_buttons[2].x + (g.ach_buttons[2].w - sz * float(lbl.size())) / 2,
                    g.ach_buttons[2].y + (g.ach_buttons[2].h - sz) / 2, sz, lbl, kWhite);
        } else {
            chip(gl, g.ach_buttons[2], "Reset", false, snap.unlocked_count > 0, 12 * ui, ui);
        }
        gl.text(p.x + pad, win_h - pad - small, small, "Arrows/Wheel: scroll. Left/Right: filter. Tab: page.", kHint);
        return;
    }

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
        if (!row.label.empty()) {  // (beside its chips: as large as its room allows)
            const float size = std::min(text, row.label_at.w / float(row.label.size()));
            gl.text(row.label_at.x, row.label_at.y + (text - size) / 2, size, row.label, kAccent);
        }
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
