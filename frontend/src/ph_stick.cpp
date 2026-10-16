#include "ph_stick.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ntr/runtime.hpp"

namespace ph {
namespace {

// AZEE (USA), zeldaret/ph config/usa/arm9/symbols.txt: PlayerControl* gPlayerControl (DTCM).
constexpr uint32_t kPlayerControl = 0x027E0FB8;
// PlayerControl's fields (include/Player/PlayerControl.hpp, TouchControl.hpp).
constexpr uint32_t kTouchX = 0x10, kTouchY = 0x14;  // s32, DS pixels
constexpr uint32_t kTouchDist = 0x58;               // q20 (4096 = 1 pixel): from the touch to Link
constexpr uint32_t kTouchAngle = 0x6A;              // s16: 0 down, 0x4000 right, 0x8000 up, 0xC000 left
constexpr uint32_t kMoveAllowed = 0x7C;             // u8 mUnk_7c: the stylus moves Link this frame

constexpr float kDeadZone = 0.22f;
constexpr float kNear = 22.0f, kFar = 80.0f;  // the stylus's distance from Link: just past the game's 20-pixel dead
                                              // zone (a slow walk) to a run

bool in_main_ram(uint32_t a) { return a >= 0x02000000u && a < 0x02400000u; }

}  // namespace

bool Stick::touch_for(const ntr::Runtime& rt, float x, float y, int& tx, int& ty) {
    const float mag = std::min(1.0f, std::hypot(x, y));
    const uint32_t pc = rt.peek32(kPlayerControl);
    const bool allowed = in_main_ram(pc) && (rt.peek32(pc + kMoveAllowed) & 0xFF) != 0;

    // Where Link is: the last touch the game saw, its distance from Link and the angle between them. Which way the
    // angle points (touch to Link or Link to touch) is told apart by which answer stays near the last one, and then
    // kept once it has agreed for a while.
    if (holding_ && in_main_ram(pc)) {
        const int gx = int(rt.peek32(pc + kTouchX)), gy = int(rt.peek32(pc + kTouchY));
        const float dist = float(int32_t(rt.peek32(pc + kTouchDist))) / 4096.0f;
        const int16_t angle = int16_t(rt.peek32(pc + (kTouchAngle & ~3u)) >> (8 * (kTouchAngle & 3u)));
        const bool sane = gx >= 0 && gx < 256 && gy >= 0 && gy < 192 && dist > 0.5f && dist < 400.0f &&
                          std::abs(gx - last_tx_) <= 2 && std::abs(gy - last_ty_) <= 2;
        if (sane) {
            const float a = float(angle) * (6.2831853f / 65536.0f);
            const float dx = std::sin(a) * dist, dy = std::cos(a) * dist;
            const float ax = float(gx) - dx, ay = float(gy) - dy;  // the angle points from Link to the touch
            const float bx = float(gx) + dx, by = float(gy) + dy;  // from the touch to Link
            int pick = sense_;
            if (pick == 0) {
                const float da = std::hypot(ax - link_x_, ay - link_y_), db = std::hypot(bx - link_x_, by - link_y_);
                pick = da <= db ? 1 : 2;
                votes_ = pick == last_pick_ ? votes_ + 1 : 1;
                last_pick_ = pick;
                if (votes_ >= 30) {
                    sense_ = pick;
                    std::fprintf(stderr, "stick: the game's touch angle points %s\n",
                                 pick == 1 ? "from Link to the touch" : "from the touch to Link");
                }
            }
            link_x_ = pick == 1 ? ax : bx;
            link_y_ = pick == 1 ? ay : by;
        }
    }

    if (mag < kDeadZone || !allowed) {
        holding_ = false;
        return false;
    }
    const float r = kNear + (mag - kDeadZone) / (1.0f - kDeadZone) * (kFar - kNear);
    const float px = std::clamp(link_x_, 16.0f, 240.0f) + x / mag * r;
    const float py = std::clamp(link_y_, 16.0f, 176.0f) + y / mag * r;
    tx = std::clamp(int(std::lround(px)), 4, 251);
    ty = std::clamp(int(std::lround(py)), 4, 187);
    holding_ = true;
    last_tx_ = tx, last_ty_ = ty;
    return true;
}

}  // namespace ph
