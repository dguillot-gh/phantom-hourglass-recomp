// Where the DS screens go in the window: the shown screen(s) as large as fits at the DS's aspect ratio, centred, with
// black bars (never cropped), or stretched over the whole window. Used to draw them and to map the mouse or a finger
// onto the touch screen.
#pragma once

#include <algorithm>

namespace ph {

enum class Layout { Bottom, Top, Both };  // the touch screen, the top screen, or both stacked

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct Views {
    bool top_shown = false, bottom_shown = false;
    Rect top, bottom;  // in window pixels
};

// `stretch`: fill the whole window instead (the DS's shape is lost; Both splits the window between the two screens).
// Widescreen: `wide_extra` DS pixels more on each side of the screen in layer `wide_layer` (0 top, 1 bottom).
inline Views compute_views(float win_w, float win_h, Layout layout, bool stretch = false, int wide_extra = 0,
                           int wide_layer = 1) {
    constexpr float kW = 256, kH = 192;
    Views v;
    if (stretch) {
        v.top_shown = layout != Layout::Bottom;
        v.bottom_shown = layout != Layout::Top;
        if (layout == Layout::Both) {
            v.top = {0, 0, win_w, win_h / 2};
            v.bottom = {0, win_h / 2, win_w, win_h / 2};
        } else {
            v.top = v.bottom = {0, 0, win_w, win_h};
        }
        return v;
    }
    const float content_h = layout == Layout::Both ? kH * 2 : kH;
    const float top_w = kW + (wide_layer == 0 ? 2.0f * float(wide_extra) : 0.0f);
    const float bottom_w = kW + (wide_layer == 1 ? 2.0f * float(wide_extra) : 0.0f);
    const float content_w = layout == Layout::Both ? std::max(top_w, bottom_w) : layout == Layout::Top ? top_w : bottom_w;
    const float scale = std::max(0.0f, std::min(win_w / content_w, win_h / content_h));
    const float h = kH * scale, tw = top_w * scale, bw = bottom_w * scale;
    // (A tall window -- the phone held upright -- has the picture at the top, a small margin under the camera, and the
    // on-screen controls under it; DsPad.java follows this.)
    const bool tall = win_h > win_w * 1.25f;
    const float tx = (win_w - tw) / 2, bx = (win_w - bw) / 2;
    const float y = tall ? std::max(0.0f, std::min((win_h - content_h * scale) / 2, win_h * 0.035f))
                         : (win_h - content_h * scale) / 2;
    if (layout == Layout::Both) {
        v.top_shown = v.bottom_shown = true;
        v.top = {tx, y, tw, h};
        v.bottom = {bx, y + h, bw, h};
    } else if (layout == Layout::Top) {
        v.top_shown = true;
        v.top = {tx, y, tw, h};
    } else {
        v.bottom_shown = true;
        v.bottom = {bx, y, bw, h};
    }
    return v;
}

// A point in a shown screen `r` to the DS's pixel: with `extra` wide pixels on each side, the DS's 256 are the
// middle ones (a point beside them goes to the nearest edge).
inline void to_ds(const Rect& r, int extra, float px, float py, int& x, int& y) {
    const float total = 256.0f + 2.0f * float(extra);
    x = std::clamp(int((px - r.x) / r.w * total) - extra, 0, 255);
    y = std::clamp(int((py - r.y) / r.h * 192.0f), 0, 191);
}

}  // namespace ph
