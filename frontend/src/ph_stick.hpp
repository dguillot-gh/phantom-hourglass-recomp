// An analog stick for Phantom Hourglass, like the 3DS's circle pad: the game moves Link toward a stylus held near
// him, faster the farther it is, so the stick holds a stylus at Link plus the stick's direction (a short way for a
// walk, farther to run).
//
// Where Link is on the screen comes from the game itself: while the stylus is down, its PlayerControl
// (gPlayerControl, zeldaret/ph include/Player/PlayerControl.hpp) keeps the touch point (mTouchX/Y), its distance in
// pixels from Link (mTouchDist) and the angle between them (mTouchAngle), so Link is that distance from the touch,
// along that angle. The stick only acts while the game lets the stylus move Link (mUnk_7c, false in cutscenes,
// menus and talk); otherwise it does nothing.
#pragma once

#include <cstdint>

namespace ntr {
class Runtime;
}

namespace ph {

class Stick {
public:
    // Stick position x, y in -1..1 (y down). Returns true and the touch point (DS pixels) to hold this frame, or false
    // to leave the touch screen alone (the stick at rest, or the game not taking stylus moves now).
    bool touch_for(const ntr::Runtime& rt, float x, float y, int& tx, int& ty);

private:
    bool holding_ = false;          // last frame's touch was the stick's
    float link_x_ = 128, link_y_ = 96;  // where Link is on the touch screen (DS pixels), as last worked out
    int last_tx_ = 0, last_ty_ = 0;
    int sense_ = 0;  // which way the game's angle points: 0 not known yet, 1 Link to touch, 2 touch to Link
    int last_pick_ = 0, votes_ = 0;
};

}  // namespace ph
