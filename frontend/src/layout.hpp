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
inline Views compute_views(float win_w, float win_h, Layout layout, bool stretch = false) {
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
    const float scale = std::max(0.0f, std::min(win_w / kW, win_h / content_h));
    const float w = kW * scale, h = kH * scale;
    // (A tall window -- the phone held upright -- has the picture at the top, a small margin under the camera, and the
    // on-screen controls under it; DsPad.java follows this.)
    const bool tall = win_h > win_w * 1.25f;
    const float x = (win_w - w) / 2;
    const float y = tall ? std::max(0.0f, std::min((win_h - content_h * scale) / 2, win_h * 0.035f))
                         : (win_h - content_h * scale) / 2;
    if (layout == Layout::Both) {
        v.top_shown = v.bottom_shown = true;
        v.top = {x, y, w, h};
        v.bottom = {x, y + h, w, h};
    } else if (layout == Layout::Top) {
        v.top_shown = true;
        v.top = {x, y, w, h};
    } else {
        v.bottom_shown = true;
        v.bottom = {x, y, w, h};
    }
    return v;
}

}  // namespace ph
