// The PC launcher's window (pc_launcher.hpp): SDL's 2D renderer, the embedded Inter font through stb_truetype (its
// implementation is in the HD text's source), round corners from an anti-aliased disc texture.
#include "pc_launcher.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "stb_truetype.h"

namespace ph::launcher {
namespace {

constexpr float kW = 960, kH = 600;  // the layout's size (scaled to the window, with bars)
constexpr float kHeader = 48, kRail = 68;
constexpr uint32_t kBlack = 0x0B0B0E, kPanel = 0x111827, kChip = 0x1F2937, kChipOff = 0x151B26, kText = 0xE5E7EB,
                   kGrey = 0x9CA3AF, kDim = 0x4B5563, kInk = 0x111111, kGood = 0x34D399;
constexpr float kSizes[] = {12, 14, 16, 20, 26, 58};
enum { kSmall, kNote, kBody, kButton, kHeading, kTitle };
// The characters drawn: printable ASCII, the middle dot, the en and em dashes, the ellipsis.
constexpr int kExtra[] = {0xB7, 0x2013, 0x2014, 0x2026};
constexpr int kGlyphs = 95 + 4;

uint8_t R(uint32_t c) { return uint8_t(c >> 16); }
uint8_t G(uint32_t c) { return uint8_t(c >> 8); }
uint8_t B(uint32_t c) { return uint8_t(c); }
SDL_FColor fcolor(uint32_t c, int a) { return {R(c) / 255.0f, G(c) / 255.0f, B(c) / 255.0f, a / 255.0f}; }

bool inside(const SDL_FRect& f, float x, float y) { return x >= f.x && x < f.x + f.w && y >= f.y && y < f.y + f.h; }

// The next code point of UTF-8 text (bad bytes as '?').
int next_code(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i++]);
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (n == 0) return '?';
    int code = c & (0x3F >> n);
    while (n-- > 0 && i < s.size()) code = (code << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    return code;
}

int glyph_index(int code) {
    if (code >= 32 && code < 127) return code - 32;
    for (int i = 0; i < 4; i++)
        if (kExtra[i] == code) return 95 + i;
    return '?' - 32;
}

SDL_Texture* make_texture(SDL_Renderer* r, int size, bool soft) {
    std::vector<uint32_t> px(size_t(size * size));
    const float c = size / 2.0f;
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const float d = std::hypot(x + 0.5f - c, y + 0.5f - c);
            float a = soft ? std::max(0.0f, 1.0f - d / c) : std::clamp(c - d + 0.5f, 0.0f, 1.0f);
            if (soft) a = a * a;
            px[size_t(y * size + x)] = (uint32_t(a * 255.0f + 0.5f) << 24) | 0xFFFFFF;
        }
    SDL_Texture* t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, size, size);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, px.data(), size * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

}  // namespace

// The font: every size packed into one texture, at the window's pixel scale (sharp on high-DPI screens).
struct Window::Fonts {
    SDL_Texture* atlas = nullptr;
    int aw = 2048, ah = 2048;
    float scale = 1;  // atlas pixels per layout unit
    std::array<std::array<stbtt_packedchar, kGlyphs>, std::size(kSizes)> chars{};
    std::array<float, std::size(kSizes)> ascent{};
};

Window::Window(const char* title, const unsigned char* font, size_t font_size) {
    SDL_Rect usable{0, 0, 1280, 720};
    SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    scale = std::min({scale, usable.w * 0.92f / kW, usable.h * 0.9f / kH});
    window_ = SDL_CreateWindow(title, int(kW * scale), int(kH * scale), SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_) return;
    SDL_SetWindowMinimumSize(window_, int(kW / 2), int(kH / 2));
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (!renderer_) return;
    SDL_SetRenderVSync(renderer_, 1);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    disc_ = make_texture(renderer_, 64, false);
    soft_ = make_texture(renderer_, 128, true);
    start_ = SDL_GetTicks();

    // The font, packed at the window's pixel scale (at least 1, at most 2.5 to keep the atlas in size).
    fonts_ = std::make_unique<Fonts>();
    int pw = 0, ph = 0;
    SDL_GetCurrentRenderOutputSize(renderer_, &pw, &ph);
    fonts_->scale = std::clamp(std::min(pw / kW, ph / kH), 1.0f, 2.5f);
    std::vector<unsigned char> alpha(size_t(fonts_->aw * fonts_->ah));
    stbtt_fontinfo info;
    const int offset = font ? stbtt_GetFontOffsetForIndex(font, 0) : -1;
    bool packed = false;
    if (offset >= 0 && font_size > 0 && stbtt_InitFont(&info, font, offset)) {
        int codes[kGlyphs];
        for (int i = 0; i < 95; i++) codes[i] = 32 + i;
        for (int i = 0; i < 4; i++) codes[95 + i] = kExtra[i];
        stbtt_pack_range ranges[std::size(kSizes)];
        for (size_t s = 0; s < std::size(kSizes); s++) {
            ranges[s] = {};
            ranges[s].font_size = kSizes[s] * fonts_->scale;
            ranges[s].array_of_unicode_codepoints = codes;
            ranges[s].num_chars = kGlyphs;
            ranges[s].chardata_for_range = fonts_->chars[s].data();
            int asc = 0, desc = 0, gap = 0;
            stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
            fonts_->ascent[s] = asc * stbtt_ScaleForPixelHeight(&info, kSizes[s]);
        }
        stbtt_pack_context pc;
        if (stbtt_PackBegin(&pc, alpha.data(), fonts_->aw, fonts_->ah, 0, 1, nullptr)) {
            stbtt_PackSetOversampling(&pc, 1, 1);
            packed = stbtt_PackFontRanges(&pc, font, 0, ranges, int(std::size(kSizes))) != 0;
            stbtt_PackEnd(&pc);
        }
    }
    if (packed) {
        std::vector<uint32_t> px(alpha.size());
        for (size_t i = 0; i < alpha.size(); i++) px[i] = (uint32_t(alpha[i]) << 24) | 0xFFFFFF;
        fonts_->atlas = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, fonts_->aw, fonts_->ah);
        if (fonts_->atlas) {
            SDL_UpdateTexture(fonts_->atlas, nullptr, px.data(), fonts_->aw * 4);
            SDL_SetTextureBlendMode(fonts_->atlas, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(fonts_->atlas, SDL_SCALEMODE_LINEAR);
        }
    }
    SDL_SetRenderLogicalPresentation(renderer_, int(kW), int(kH), SDL_LOGICAL_PRESENTATION_LETTERBOX);

    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        for (int i = 0; i < count; i++)
            if (SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) pads_.push_back(pad);
        SDL_free(ids);
    }
}

Window::~Window() {
    for (SDL_Gamepad* pad : pads_) SDL_CloseGamepad(pad);
    if (fonts_ && fonts_->atlas) SDL_DestroyTexture(fonts_->atlas);
    if (disc_) SDL_DestroyTexture(disc_);
    if (soft_) SDL_DestroyTexture(soft_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void Window::hide() { if (window_) SDL_HideWindow(window_); }
void Window::show() {
    if (!window_) return;
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);
}

// ---- drawing ----

void Window::round_rect(const SDL_FRect& r, float radius, uint32_t rgb, int alpha) {
    SDL_SetRenderDrawColor(renderer_, R(rgb), G(rgb), B(rgb), Uint8(alpha));
    radius = std::min({radius, r.w / 2, r.h / 2});
    if (radius < 1 || !disc_) {
        SDL_RenderFillRect(renderer_, &r);
        return;
    }
    const SDL_FRect mid{r.x + radius, r.y, r.w - 2 * radius, r.h}, left{r.x, r.y + radius, radius, r.h - 2 * radius},
        right{r.x + r.w - radius, r.y + radius, radius, r.h - 2 * radius};
    SDL_RenderFillRect(renderer_, &mid);
    SDL_RenderFillRect(renderer_, &left);
    SDL_RenderFillRect(renderer_, &right);
    SDL_SetTextureColorMod(disc_, R(rgb), G(rgb), B(rgb));
    SDL_SetTextureAlphaMod(disc_, Uint8(alpha));
    const SDL_FRect q[4] = {{0, 0, 32, 32}, {32, 0, 32, 32}, {0, 32, 32, 32}, {32, 32, 32, 32}};
    const SDL_FRect d[4] = {{r.x, r.y, radius, radius}, {r.x + r.w - radius, r.y, radius, radius},
                            {r.x, r.y + r.h - radius, radius, radius}, {r.x + r.w - radius, r.y + r.h - radius, radius, radius}};
    for (int i = 0; i < 4; i++) SDL_RenderTexture(renderer_, disc_, &q[i], &d[i]);
}

void Window::gradient(const SDL_FRect& r, uint32_t top, int top_alpha, uint32_t bottom, int bottom_alpha) {
    const SDL_FColor a = fcolor(top, top_alpha), b = fcolor(bottom, bottom_alpha);
    const SDL_Vertex v[4] = {{{r.x, r.y}, a, {}}, {{r.x + r.w, r.y}, a, {}}, {{r.x + r.w, r.y + r.h}, b, {}},
                             {{r.x, r.y + r.h}, b, {}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
}

void Window::glow(float cx, float cy, float radius, uint32_t rgb, int alpha) {
    if (!soft_) return;
    SDL_SetTextureColorMod(soft_, R(rgb), G(rgb), B(rgb));
    SDL_SetTextureAlphaMod(soft_, Uint8(alpha));
    const SDL_FRect d{cx - radius, cy - radius, 2 * radius, 2 * radius};
    SDL_RenderTexture(renderer_, soft_, nullptr, &d);
}

float Window::text_width(const std::string& s, int size) const {
    if (!fonts_ || !fonts_->atlas) return float(s.size()) * kSizes[size] * 0.55f;
    float w = 0;
    for (size_t i = 0; i < s.size();) w += fonts_->chars[size_t(size)][size_t(glyph_index(next_code(s, i)))].xadvance;
    return w / fonts_->scale;
}

// Draws `s` with its top at y; returns its width.
float Window::text(float x, float y, const std::string& s, int size, uint32_t rgb, int alpha) {
    if (!fonts_ || !fonts_->atlas) return 0;
    Fonts& f = *fonts_;
    SDL_SetTextureColorMod(f.atlas, R(rgb), G(rgb), B(rgb));
    SDL_SetTextureAlphaMod(f.atlas, Uint8(alpha));
    const float baseline = std::round((y + f.ascent[size_t(size)]) * f.scale) / f.scale;
    x = std::round(x * f.scale) / f.scale;
    float bx = 0, by = 0;
    for (size_t i = 0; i < s.size();) {
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(f.chars[size_t(size)].data(), f.aw, f.ah, glyph_index(next_code(s, i)), &bx, &by, &q, 1);
        const SDL_FRect src{q.s0 * f.aw, q.t0 * f.ah, (q.s1 - q.s0) * f.aw, (q.t1 - q.t0) * f.ah};
        const SDL_FRect dst{x + q.x0 / f.scale, baseline + q.y0 / f.scale, (q.x1 - q.x0) / f.scale, (q.y1 - q.y0) / f.scale};
        if (src.w > 0) SDL_RenderTexture(renderer_, f.atlas, &src, &dst);
    }
    return bx / f.scale;
}

// The game's page background (no game art is shipped): the sea at dusk (PH) or railway tracks into the sunset (ST),
// darkened at the bottom like a key art for the title.
void Window::draw_background(const View& v) {
    const float x0 = kRail, y0 = kHeader, w = kW - kRail, h = kH - kHeader;
    const float t = float(SDL_GetTicks() - start_) / 1000.0f;
    if (v.art == 1) {
        const float horizon = y0 + h * 0.48f, vx = x0 + w * 0.62f;
        gradient({x0, y0, w, horizon - y0}, 0x1B1030, 255, 0x7A3A1E, 255);
        gradient({x0, horizon, w, kH - horizon}, 0x2A1810, 255, 0x120B08, 255);
        glow(vx, horizon - 56, 300, 0xFF8A3D, 160);
        glow(vx, horizon - 56, 110, 0xFFD28A, 220);
        round_rect({vx - 26, horizon - 84, 52, 52}, 26, 0xFFE6B0, 255);
        // Hills along the horizon.
        std::vector<SDL_Vertex> hills;
        std::vector<int> idx;
        for (int i = 0; i <= 48; i++) {
            const float x = x0 + w * float(i) / 48;
            const float top = horizon - 18 - 22 * std::sin(float(i) * 0.37f) * std::sin(float(i) * 0.11f + 1.3f) - 10 * std::sin(float(i) * 0.9f);
            hills.push_back({{x, top}, fcolor(0x24140E, 255), {}});
            hills.push_back({{x, horizon + 2}, fcolor(0x24140E, 255), {}});
            if (i) {
                const int b = 2 * i;
                idx.insert(idx.end(), {b - 2, b - 1, b, b - 1, b + 1, b});
            }
        }
        SDL_RenderGeometry(renderer_, nullptr, hills.data(), int(hills.size()), idx.data(), int(idx.size()));
        // Sleepers (closer: lower, wider, further apart), moving towards the viewer, then the two rails.
        const auto at = [&](float d, float spread) { return std::pair<float, float>{vx + (spread * d), horizon + (kH - horizon) * d}; };
        for (int i = 0; i < 26; i++) {
            float d = std::fmod(float(i) / 26.0f + t * 0.04f, 1.0f);
            d = d * d;
            const float y = horizon + (kH - horizon) * d, half = 2 + 400 * d, thick = 1 + 14 * d;
            const SDL_FRect sleeper{vx - half, y, 2 * half, thick};
            SDL_SetRenderDrawColor(renderer_, 0x4A, 0x30, 0x22, Uint8(80 + 160 * d));
            SDL_RenderFillRect(renderer_, &sleeper);
        }
        for (const float side : {-1.0f, 1.0f}) {
            const auto [ax, ay] = at(0, side * 30);
            const auto [bx, by] = at(1, side * 330);
            const SDL_FColor c = fcolor(0xC9B8A8, 230), far = fcolor(0xC9B8A8, 60);
            const SDL_Vertex rail[4] = {{{ax - 0.5f, ay}, far, {}}, {{ax + 0.5f, ay}, far, {}}, {{bx + 5, by}, c, {}}, {{bx - 5, by}, c, {}}};
            const int ri[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, rail, 4, ri, 6);
        }
    } else {
        const float horizon = y0 + h * 0.5f;
        gradient({x0, y0, w, horizon - y0}, 0x0B1630, 255, 0x3A5A8A, 255);
        gradient({x0, horizon, w, kH - horizon}, 0x0E3550, 255, 0x061522, 255);
        glow(x0 + w * 0.7f, horizon - 30, 260, 0xFFC86B, 120);
        glow(x0 + w * 0.7f, horizon - 30, 50, 0xFFF1C9, 255);
        round_rect({x0 + w * 0.7f - 16, horizon - 46, 32, 32}, 16, 0xFFF6DE, 255);
        // Clouds drifting, and an island on the horizon.
        if (soft_) {
            SDL_SetTextureColorMod(soft_, 0xC8, 0xD4, 0xF0);
            const float drift[][4] = {{0.12f, 0.18f, 220, 26}, {0.42f, 0.10f, 300, 30}, {0.78f, 0.24f, 260, 22}, {0.30f, 0.32f, 200, 18}};
            for (const auto& c : drift) {
                const float cx = x0 + std::fmod(w * c[0] + t * 4, w + 300) - 150;
                SDL_SetTextureAlphaMod(soft_, 34);
                const SDL_FRect d{cx - c[2] / 2, y0 + h * c[1], c[2], c[3]};
                SDL_RenderTexture(renderer_, soft_, nullptr, &d);
            }
        }
        {
            std::vector<SDL_Vertex> vs;
            std::vector<int> idx;
            const float ix = x0 + w * 0.2f, iw = 150;
            for (int i = 0; i <= 24; i++) {
                const float u = float(i) / 24, x = ix + iw * u;
                const float top = horizon - 34 * std::pow(std::sin(u * 3.14159f), 1.6f) - 6 * std::sin(u * 9.0f) * std::sin(u * 3.14159f);
                vs.push_back({{x, top}, fcolor(0x0A1A2C, 255), {}});
                vs.push_back({{x, horizon + 2}, fcolor(0x0A1A2C, 255), {}});
                if (i) {
                    const int b = 2 * i;
                    idx.insert(idx.end(), {b - 2, b - 1, b, b - 1, b + 1, b});
                }
            }
            SDL_RenderGeometry(renderer_, nullptr, vs.data(), int(vs.size()), idx.data(), int(idx.size()));
        }
        // Waves: layers of a sine edge, darker and taller towards the viewer, slowly rolling.
        for (int layer = 0; layer < 5; layer++) {
            const float base = horizon + 8 + float(layer * layer) * 9 + float(layer) * 14, amp = 2.0f + float(layer) * 2.5f;
            const uint32_t col = layer < 2 ? 0x1B5A7E : layer < 4 ? 0x0F4061 : 0x082B44;
            std::vector<SDL_Vertex> vs;
            std::vector<int> idx;
            for (int i = 0; i <= 64; i++) {
                const float x = x0 + w * float(i) / 64;
                const float y = base + amp * std::sin(x * (0.025f - layer * 0.003f) + t * (0.6f + layer * 0.15f) + layer * 1.7f);
                vs.push_back({{x, y}, fcolor(col, 120 + layer * 30), {}});
                vs.push_back({{x, kH}, fcolor(0x061522, 255), {}});
                if (i) {
                    const int b = 2 * i;
                    idx.insert(idx.end(), {b - 2, b - 1, b, b - 1, b + 1, b});
                }
            }
            SDL_RenderGeometry(renderer_, nullptr, vs.data(), int(vs.size()), idx.data(), int(idx.size()));
        }
        // The sun's path on the water: soft streaks, shimmering.
        if (soft_) {
            SDL_SetTextureColorMod(soft_, 0xFF, 0xE3, 0xA0);
            for (int i = 0; i < 16; i++) {
                const float y = horizon + 6 + float(i * i) * 1.1f + float(i) * 4;
                const float half = 18 + float(i) * 7 + 10 * std::sin(t * 1.1f + float(i) * 1.9f);
                SDL_SetTextureAlphaMod(soft_, Uint8(std::max(0, 200 - i * 11)));
                const SDL_FRect d{x0 + w * 0.7f - half + 6 * std::sin(t * 0.7f + float(i)), y, 2 * half, 4 + float(i) * 0.5f};
                SDL_RenderTexture(renderer_, soft_, nullptr, &d);
            }
        }
    }
    gradient({x0, kH - 260, w, 260}, 0x000000, 0, 0x000000, 210);
}

void Window::draw(const View& v, const std::vector<Stop>& stops) {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    draw_background(v);

    // Header: the logo tile, the name, the versions.
    const SDL_FRect header{0, 0, kW, kHeader};
    SDL_SetRenderDrawColor(renderer_, R(kBlack), G(kBlack), B(kBlack), 255);
    SDL_RenderFillRect(renderer_, &header);
    round_rect({16, 10, 28, 28}, 7, v.accent);
    text(16 + (28 - text_width(v.short_name, kSmall)) / 2, 17, v.short_name, kSmall, kInk);
    float x = 56 + text(56, 15, v.app_name, kBody, kText) + 16;
    const SDL_FRect divider{x, 12, 1, 24};
    SDL_SetRenderDrawColor(renderer_, 0x2A, 0x2F, 0x3A, 255);
    SDL_RenderFillRect(renderer_, &divider);
    x += 14;
    text(x, 8, "launcher", kSmall, kDim);
    text(x + 62, 8, v.version, kSmall, kGrey);
    text(x, 25, "game", kSmall, kDim);
    text(x + 62, 25, v.can_play ? "built" : "not built", kSmall, v.can_play ? kGood : v.accent);

    // The rail.
    const SDL_FRect rail{0, kHeader, kRail, kH - kHeader};
    SDL_SetRenderDrawColor(renderer_, R(kBlack), G(kBlack), B(kBlack), 235);
    SDL_RenderFillRect(renderer_, &rail);

    for (size_t i = 0; i < stops.size(); i++) {
        const Stop& s = stops[i];
        const bool focus = keys_ && int(i) == focus_, hover = int(i) == hover_ || focus;
        if (s.rail) {
            const bool on = s.rail_page == page_;
            if (focus) round_rect({s.at.x - 3, s.at.y - 3, s.at.w + 6, s.at.h + 6}, 12, v.accent);
            if (hover || on || focus) round_rect(s.at, 10, focus ? kChip : kChip, on || focus ? 255 : 160);
            if (on) round_rect({2, s.at.y + 10, 4, s.at.h - 20}, 2, v.accent);
            const float cx = s.at.x + s.at.w / 2, cy = s.at.y + s.at.h / 2;
            const uint32_t ink = on || hover ? kText : kGrey;
            if (s.rail_page == 0) {  // the game's tile
                round_rect({cx - 15, cy - 15, 30, 30}, 7, on ? v.accent : 0x374151);
                text(cx - text_width(v.short_name, kSmall) / 2, cy - 8, v.short_name, kSmall, on ? kInk : kText);
            } else if (s.rail_page == 1) {  // a cog: a ring with teeth
                std::vector<SDL_Vertex> vs;
                std::vector<int> idx;
                const SDL_FColor c = fcolor(ink, 255);
                vs.push_back({{cx, cy}, c, {}});
                for (int k = 0; k <= 48; k++) {
                    const float a = float(k) / 48 * 6.2831853f;
                    const float rr = (k % 6 < 3) ? 12.0f : 9.0f;
                    vs.push_back({{cx + rr * std::cos(a), cy + rr * std::sin(a)}, c, {}});
                    if (k) idx.insert(idx.end(), {0, k, k + 1});
                }
                SDL_RenderGeometry(renderer_, nullptr, vs.data(), int(vs.size()), idx.data(), int(idx.size()));
                round_rect({cx - 4.5f, cy - 4.5f, 9, 9}, 4.5f, hover || on ? kChip : kBlack);
            } else {  // quit: an X
                for (const float sgn : {-1.0f, 1.0f}) {
                    const SDL_FColor c = fcolor(ink, 255);
                    const float d = 7, t = 1.3f;
                    const SDL_Vertex xv[4] = {{{cx - d - t * sgn, cy - d * sgn + t}, c, {}}, {{cx - d + t * sgn, cy - d * sgn - t}, c, {}},
                                              {{cx + d + t * sgn, cy + d * sgn - t}, c, {}}, {{cx + d - t * sgn, cy + d * sgn + t}, c, {}}};
                    const int xi[6] = {0, 1, 2, 0, 2, 3};
                    SDL_RenderGeometry(renderer_, nullptr, xv, 4, xi, 6);
                }
            }
        }
    }

    const float cx0 = kRail;
    if (page_ == 0) {
        // The title, bottom left; the buttons, bottom right.
        text(cx0 + 44, kH - 196, v.series, kNote, v.accent);
        text(cx0 + 42, kH - 176, v.title, kTitle, 0xFFFFFF);
        const std::string line = (v.rom_line.empty() ? std::string("No ROM yet") : "ROM  " + v.rom_line) + "   \xC2\xB7   " +
                                 (v.can_play ? "Game built" : "Game not built yet");
        text(cx0 + 44, kH - 98, line, kNote, 0xC8CCD4);
        if (v.can_play && !v.resume_time.empty())
            text(cx0 + 44, kH - 76, "Last played  " + v.resume_time, kNote, kGrey);
    } else {
        const SDL_FRect panel{cx0 + 28, kHeader + 24, kW - cx0 - 56, kH - kHeader - 48};
        round_rect(panel, 8, kPanel, 240);
        const SDL_FRect line{panel.x, panel.y + 52, panel.w, 1};
        SDL_SetRenderDrawColor(renderer_, R(kChip), G(kChip), B(kChip), 255);
        SDL_RenderFillRect(renderer_, &line);
        if (tab_ == 0)
            text(panel.x + 28, panel.y + panel.h - 34,
                 "Saved for the next start. In the game, F1 or Esc opens the menu with the rest.", kSmall, kGrey);
        else
            text(panel.x + 28, panel.y + panel.h - 34,
                 "Saves (saves\\) and save states (states\\) stay when you change the ROM or rebuild.", kSmall, kGrey);
    }

    for (size_t i = 0; i < stops.size(); i++) {
        const Stop& s = stops[i];
        if (s.rail) continue;
        const bool focus = keys_ && int(i) == focus_, hover = int(i) == hover_ || focus;
        if (s.tab >= 0) {
            const bool on = s.tab == tab_;
            text(s.at.x + (s.at.w - text_width(s.text, kBody)) / 2, s.at.y + 14, s.text, kBody, on ? v.accent : hover ? kText : kGrey);
            if (on) round_rect({s.at.x, s.at.y + s.at.h - 3, s.at.w, 3}, 1.5f, v.accent);
            if (focus) round_rect({s.at.x, s.at.y + 4, s.at.w, 2}, 1, kGrey);
            continue;
        }
        if (s.option >= 0) {
            const Option& o = v.options[size_t(s.option)];
            if (focus) round_rect({s.at.x - 12, s.at.y + 6, 3, s.at.h - 12}, 1.5f, v.accent);
            text(s.at.x, s.at.y + 9, o.label, kBody, focus ? 0xFFFFFF : kText);
            for (size_t c = 0; c < s.chips.size(); c++) {
                const bool on = int(c) == o.chosen, enabled = c >= o.enabled.size() || o.enabled[c];
                round_rect(s.chips[c], 6, on ? (enabled ? v.accent : kDim) : enabled ? kChip : kChipOff,
                           on || enabled ? 255 : 200);
                const std::string& t = o.choices[c];
                text(s.chips[c].x + (s.chips[c].w - text_width(t, kNote)) / 2, s.chips[c].y + 10, t, kNote,
                     on ? kInk : enabled ? kText : kDim);
            }
            continue;
        }
        if (page_ == 1) continue;  // (the Game files buttons: drawn over their cards, below)
        // The game page's buttons.
        if (s.primary) {
            if (focus) round_rect({s.at.x - 4, s.at.y - 4, s.at.w + 8, s.at.h + 8}, 12, 0xFFFFFF, 230);
            round_rect(s.at, 9, v.accent, 255);
            if (hover) round_rect(s.at, 9, 0xFFFFFF, 40);
            const float tw = text_width(s.text, kButton);
            const float ix = s.at.x + (s.at.w - tw - 22) / 2, iy = s.at.y + s.at.h / 2;
            const SDL_FColor c = fcolor(kInk, 255);
            const SDL_Vertex tri[3] = {{{ix, iy - 8}, c, {}}, {{ix + 13, iy}, c, {}}, {{ix, iy + 8}, c, {}}};
            SDL_RenderGeometry(renderer_, nullptr, tri, 3, nullptr, 0);
            text(ix + 22, s.at.y + (s.at.h - 24) / 2, s.text, kButton, kInk);
        } else {
            if (focus) round_rect({s.at.x - 4, s.at.y - 4, s.at.w + 8, s.at.h + 8}, 12, 0xFFFFFF, 230);
            round_rect(s.at, 9, kChip, hover ? 255 : 225);
            text(s.at.x + (s.at.w - text_width(s.text, kBody)) / 2, s.at.y + (s.at.h - 19) / 2, s.text, kBody, kText);
        }
    }
    if (page_ == 1 && tab_ == 1) {  // the Game files rows' titles and details
        const float px = kRail + 28 + 28;
        const std::array<std::pair<std::string, std::string>, 3> rows = {{
            {"Game ROM", v.rom_line.empty() ? "None kept yet" : v.rom_line},
            {"Game build", v.game_line},
            {"Data folder", v.data_dir},
        }};
        for (size_t r = 0; r < rows.size(); r++) {
            const float y = kHeader + 24 + 76 + float(r) * 92;
            const SDL_FRect card{px - 12, y - 10, kW - px - 56 - 4, 76};
            round_rect(card, 8, kChip, 110);
            text(px + 4, y + 4, rows[r].first, kBody, kText);
            std::string detail = rows[r].second;
            const float max_w = card.w - 230;
            if (text_width(detail, kNote) > max_w) {  // (a long folder: its end, which says the most)
                while (detail.size() > 4 && text_width("\xE2\x80\xA6" + detail, kNote) > max_w) detail.erase(0, 1);
                detail = "\xE2\x80\xA6" + detail;
            }
            text(px + 4, y + 30, detail, kNote, kGrey);
        }
        // (the buttons were drawn above, over the cards: draw them again on top)
        for (size_t i = 0; i < stops.size(); i++) {
            const Stop& s = stops[i];
            if (s.rail || s.tab >= 0 || s.option >= 0) continue;
            const bool focus = keys_ && int(i) == focus_, hover = int(i) == hover_ || focus;
            if (focus) round_rect({s.at.x - 3, s.at.y - 3, s.at.w + 6, s.at.h + 6}, 9, 0xFFFFFF);
            round_rect(s.at, 6, s.enabled ? v.accent : kDim, hover ? 255 : 225);
            text(s.at.x + (s.at.w - text_width(s.text, kNote)) / 2, s.at.y + 11, s.text, kNote, kInk);
        }
    }
    SDL_RenderPresent(renderer_);
}

// ---- layout and input ----

std::vector<Window::Stop> Window::layout(const View& v) const {
    std::vector<Stop> stops;
    const auto rail_item = [&](float y, int page, Action a) {
        Stop s;
        s.rail = true, s.rail_page = page, s.action = a;
        s.at = {12, y, kRail - 24, kRail - 24};
        stops.push_back(s);
    };
    rail_item(kHeader + 14, 0, Action::None);
    rail_item(kH - 2 * (kRail - 24) - 28, 1, Action::None);
    rail_item(kH - (kRail - 24) - 16, -1, Action::Quit);

    if (page_ == 0) {
        const float bh = 58, by = kH - 44 - bh;
        Stop play;
        play.primary = true;
        play.at = {kW - 44 - 240, by, 240, bh};
        if (!v.can_play) play.text = "BUILD GAME", play.action = Action::Rebuild;
        else if (!v.resume_time.empty()) play.text = "CONTINUE", play.action = Action::Continue;
        else play.text = "PLAY", play.action = Action::Play;
        if (v.can_play && !v.resume_time.empty()) {
            Stop fresh;
            fresh.at = {play.at.x - 14 - 180, by, 180, bh};
            fresh.text = "Start game", fresh.action = Action::Play;
            stops.push_back(fresh);
        }
        stops.push_back(play);
        return stops;
    }
    const SDL_FRect panel{kRail + 28, kHeader + 24, kW - kRail - 56, kH - kHeader - 48};
    const char* tabs[] = {"Display", "Game files"};
    float tx = panel.x + 20;
    for (int t = 0; t < 2; t++) {
        Stop s;
        s.tab = t, s.text = tabs[t];
        const float w = text_width(s.text, kBody) + 32;
        s.at = {tx, panel.y, w, 52};
        tx += w + 8;
        stops.push_back(s);
    }
    if (tab_ == 0) {
        float y = panel.y + 72;
        const float chips_w = 430, chips_x = panel.x + panel.w - 28 - chips_w;
        for (size_t o = 0; o < v.options.size(); o++) {
            Stop s;
            s.option = int(o);
            s.at = {panel.x + 28, y, panel.w - 56, 36};
            const size_t n = v.options[o].choices.size();
            const float cw = (chips_w - 6 * float(n - 1)) / float(n);
            for (size_t i = 0; i < n; i++) s.chips.push_back({chips_x + float(i) * (cw + 6), y, cw, 36});
            stops.push_back(s);
            y += 36 + 12;
        }
    } else {
        const std::pair<const char*, Action> rows[] = {
            {"Change ROM\xE2\x80\xA6", Action::ChangeRom}, {"Rebuild game", Action::Rebuild}, {"Open folder", Action::OpenFolder}};
        for (int r = 0; r < 3; r++) {
            Stop s;
            s.text = rows[r].first, s.action = rows[r].second;
            const float y = kHeader + 24 + 76 + float(r) * 92;
            s.at = {panel.x + panel.w - 28 - 6 - 180, y + 9, 180, 38};
            stops.push_back(s);
        }
    }
    return stops;
}

void Window::focus_default(const std::vector<Stop>& stops) {
    focus_ = -1;
    for (size_t i = 0; i < stops.size(); i++)
        if (!stops[i].rail && (stops[i].primary || (focus_ < 0 && stops[i].tab < 0))) focus_ = int(i);
    if (focus_ < 0) focus_ = 0;
}

Action Window::activate(View& v, const std::vector<Stop>& stops, int index) {
    if (index < 0 || index >= int(stops.size())) return Action::None;
    const Stop& s = stops[size_t(index)];
    if (!s.enabled) return Action::None;
    if (s.rail && s.rail_page >= 0) {
        if (page_ != s.rail_page) page_ = s.rail_page, tab_ = 0;
        focus_ = index;
        return Action::None;
    }
    if (s.tab >= 0) {
        tab_ = s.tab;
        return Action::None;
    }
    (void)v;
    return s.action;
}

// Up/down: the next item in the rail or the page; left/right: a setting's next choice, the tab or button beside, or
// between the rail and the page.
Action Window::move(View& v, const std::vector<Stop>& stops, int dy, int dx) {
    const int n = int(stops.size());
    if (focus_ < 0 || focus_ >= n) {
        focus_default(stops);
        return Action::None;
    }
    const Stop& s = stops[size_t(focus_)];
    if (dx != 0) {
        if (s.option >= 0) {
            Option& o = v.options[size_t(s.option)];
            for (int c = o.chosen + dx; c >= 0 && c < int(o.choices.size()); c += dx)
                if (c >= int(o.enabled.size()) || o.enabled[size_t(c)]) {
                    o.chosen = c;
                    changed_option = s.option;
                    return Action::Changed;
                }
            if (dx < 0 && o.chosen == 0) {  // (past the first choice: to the rail)
                for (int i = 0; i < n; i++)
                    if (stops[size_t(i)].rail && stops[size_t(i)].rail_page == page_) focus_ = i;
            }
            return Action::None;
        }
        if (s.rail) {
            if (dx > 0) {
                const int keep = focus_;
                focus_default(stops);
                if (stops[size_t(focus_)].rail) focus_ = keep;
            }
            return Action::None;
        }
        // The item beside, on the same line; else (going left) the rail.
        int best = -1;
        for (int i = 0; i < n; i++) {
            const Stop& o = stops[size_t(i)];
            if (o.rail || i == focus_ || std::fabs(o.at.y - s.at.y) > 4) continue;
            if ((dx > 0 && o.at.x > s.at.x && (best < 0 || o.at.x < stops[size_t(best)].at.x)) ||
                (dx < 0 && o.at.x < s.at.x && (best < 0 || o.at.x > stops[size_t(best)].at.x)))
                best = i;
        }
        if (best >= 0) {
            focus_ = best;
            if (stops[size_t(best)].tab >= 0) tab_ = stops[size_t(best)].tab;
        } else if (dx < 0) {
            for (int i = 0; i < n; i++)
                if (stops[size_t(i)].rail && stops[size_t(i)].rail_page == page_) focus_ = i;
        }
        return Action::None;
    }
    for (int i = focus_ + dy; i >= 0 && i < n; i += dy) {
        const Stop& o = stops[size_t(i)];
        if (o.rail != s.rail || !o.enabled) continue;
        if (!s.rail && o.tab >= 0 && s.tab >= 0) continue;  // (tabs: left/right)
        if (!s.rail && std::fabs(o.at.y - s.at.y) <= 4) continue;  // (same line: left/right)
        focus_ = i;
        if (o.rail && o.rail_page >= 0 && o.rail_page != page_) page_ = o.rail_page, tab_ = 0;
        break;
    }
    return Action::None;
}

Action Window::run(View& v) {
    if (!renderer_) return Action::Play;
    changed_option = -1;
    for (;;) {
        std::vector<Stop> stops = layout(v);
        if (focus_ < 0 || focus_ >= int(stops.size())) focus_default(stops);
        draw(v, stops);
        SDL_Event e;
        if (!SDL_WaitEventTimeout(&e, 16)) continue;  // (the background moves a little)
        do {
            SDL_ConvertEventToRenderCoordinates(renderer_, &e);
            Action a = Action::None;
            const int page = page_, tab = tab_;
            switch (e.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: return Action::Quit;
                case SDL_EVENT_KEY_DOWN:
                    keys_ = true;
                    switch (e.key.scancode) {
                        case SDL_SCANCODE_ESCAPE:
                            if (page_ != 0) page_ = 0, focus_ = -1;
                            else return Action::Quit;
                            break;
                        case SDL_SCANCODE_RETURN:
                        case SDL_SCANCODE_KP_ENTER:
                        case SDL_SCANCODE_SPACE: a = activate(v, stops, focus_); break;
                        case SDL_SCANCODE_UP: a = move(v, stops, -1, 0); break;
                        case SDL_SCANCODE_DOWN: a = move(v, stops, 1, 0); break;
                        case SDL_SCANCODE_LEFT: a = move(v, stops, 0, -1); break;
                        case SDL_SCANCODE_RIGHT: a = move(v, stops, 0, 1); break;
                        case SDL_SCANCODE_TAB:
                            if (page_ == 1) tab_ ^= 1, focus_ = -1;
                            break;
                        default: break;
                    }
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (SDL_Gamepad* pad = SDL_OpenGamepad(e.gdevice.which)) pads_.push_back(pad);
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    keys_ = true;
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_SOUTH:
                        case SDL_GAMEPAD_BUTTON_START: a = activate(v, stops, focus_); break;
                        case SDL_GAMEPAD_BUTTON_EAST:
                            if (page_ != 0) page_ = 0, focus_ = -1;
                            break;
                        case SDL_GAMEPAD_BUTTON_BACK: page_ = page_ == 0 ? 1 : 0, tab_ = 0, focus_ = -1; break;
                        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
                        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
                            if (page_ == 1) tab_ ^= 1, focus_ = -1;
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: a = move(v, stops, -1, 0); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: a = move(v, stops, 1, 0); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: a = move(v, stops, 0, -1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: a = move(v, stops, 0, 1); break;
                        default: break;
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION:
                    keys_ = false;
                    hover_ = -1;
                    for (size_t i = 0; i < stops.size(); i++)
                        if (stops[i].enabled && inside(stops[i].at, e.motion.x, e.motion.y)) hover_ = int(i);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (e.button.button != SDL_BUTTON_LEFT) break;
                    keys_ = false;
                    for (size_t i = 0; i < stops.size(); i++) {
                        const Stop& s = stops[i];
                        if (s.option < 0) {
                            if (inside(s.at, e.button.x, e.button.y)) focus_ = int(i), a = activate(v, stops, int(i));
                            continue;
                        }
                        Option& o = v.options[size_t(s.option)];
                        for (size_t c = 0; c < s.chips.size(); c++)
                            if (inside(s.chips[c], e.button.x, e.button.y) && (c >= o.enabled.size() || o.enabled[c]) &&
                                int(c) != o.chosen) {
                                focus_ = int(i);
                                o.chosen = int(c);
                                changed_option = s.option;
                                a = Action::Changed;
                            }
                    }
                    break;
                default: break;
            }
            if (page_ != page || tab_ != tab) {  // (another page: its items)
                hover_ = -1;
                const int keep = focus_;
                stops = layout(v);
                const bool rail_kept = keep >= 0 && keep < int(stops.size()) && stops[size_t(keep)].rail;
                if (!rail_kept) {
                    focus_default(stops);
                    if (tab_ != tab)  // (on the tabs' line)
                        for (size_t i = 0; i < stops.size(); i++)
                            if (stops[i].tab == tab_) focus_ = int(i);
                }
            }
            if (a != Action::None) return a;
        } while (SDL_PollEvent(&e));
    }
}

}  // namespace ph::launcher
