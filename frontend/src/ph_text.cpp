#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "ph_text.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ntr/recomp.h"

namespace ph {
namespace {

constexpr uint32_t kOamMgrEngineA = 0x02063DECu;
constexpr uint32_t kOamMgrEngineB = 0x020660C0u;
constexpr uint32_t kSingleEngineFlag = 0x027E0C54u;
constexpr uint32_t kDispCntA = 0x04000000u;
constexpr uint32_t kDispCntB = 0x04001000u;
constexpr uint32_t kMasterBrightA = 0x0400006Cu;
constexpr uint32_t kMasterBrightB = 0x0400106Cu;
constexpr uint32_t kPowCnt1 = 0x04000304u;
constexpr uint32_t kObjPalA = 0x05000200u;
constexpr uint32_t kObjPalB = 0x05000600u;

struct RecordedGlyph {
    uint16_t codepoint = 0;
    int16_t x = 0;          // local canvas X (after NFTR left bearing)
    int16_t y = 0;          // local canvas Y (cell top)
    uint8_t cell_h = 16;
    uint8_t baseline = 14;
    uint8_t glyph_w = 8;
    int8_t char_w = 8;
    uint8_t color_idx = 1;  // `cl` argument passed to NNS_G2dCharCanvasDrawChar
    uint8_t bpp = 2;
};

struct ObjCanvas {
    std::vector<RecordedGlyph> glyphs;
};

struct PendingEntry {
    HdGlyph glyph;
    uint8_t pal_idx = 15;
    uint8_t solid_color_idx = 3;
};

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;
std::unordered_map<uint32_t, ObjCanvas> g_canvases;
std::vector<PendingEntry> g_pending[2];  // indexed by 2D engine (0 = A, 1 = B)
std::vector<HdGlyph> g_visible[2];       // indexed by screen layer (0 = top, 1 = bottom)

stbtt_fontinfo g_font{};
std::atomic<int> g_font_state{0};  // 0 uninit, 1 ready, -1 failed
std::mutex g_font_init_mutex;

const stbtt_fontinfo* ensure_font() {
    int st = g_font_state.load(std::memory_order_acquire);
    if (st == 1) return &g_font;
    if (st == -1) return nullptr;
    std::lock_guard<std::mutex> lock(g_font_init_mutex);
    st = g_font_state.load(std::memory_order_relaxed);
    if (st == 1) return &g_font;
    if (st == -1) return nullptr;
    const int offset = stbtt_GetFontOffsetForIndex(kHdFontData, 0);
    if (offset >= 0 && stbtt_InitFont(&g_font, kHdFontData, offset)) {
        g_font_state.store(1, std::memory_order_release);
        return &g_font;
    }
    g_font_state.store(-1, std::memory_order_release);
    return nullptr;
}

bool valid_main_ram(uint32_t addr) {
    return addr >= 0x02000000u && addr < 0x02400000u;
}

struct NftrMetrics {
    int8_t left = 0;
    uint8_t glyph_w = 0;
    int8_t char_w = 0;
    uint8_t cell_h = 16;
    uint8_t baseline = 14;
    uint8_t bpp = 2;
    uint8_t flags = 0;
};

bool lookup_nftr_metrics(NtrCpu* c, uint32_t pFont, uint16_t ccode, NftrMetrics& out) {
    if (!valid_main_ram(pFont)) return false;
    const uint32_t finf = ntr_rd32(c, pFont);
    if (!valid_main_ram(finf)) return false;
    const uint32_t cglp = ntr_rd32(c, finf + 8u);
    const uint32_t cwdh = ntr_rd32(c, finf + 12u);
    const uint32_t cmap = ntr_rd32(c, finf + 16u);
    if (!valid_main_ram(cglp)) return false;

    out.cell_h = ntr_rd8(c, cglp + 1u);
    out.baseline = ntr_rd8(c, cglp + 4u);
    out.bpp = ntr_rd8(c, cglp + 6u);
    out.flags = ntr_rd8(c, cglp + 7u);

    // 1. Resolve glyph index via CMAP chain (matches NNS_G2dFontFindGlyphIndex at 0x02023EA4).
    uint16_t glyph_idx = 0xFFFFu;
    uint32_t p = cmap;
    for (int iter = 0; iter < 64 && valid_main_ram(p); ++iter) {
        const uint16_t c0 = ntr_rd16(c, p + 0u);
        const uint16_t c1 = ntr_rd16(c, p + 2u);
        if (ccode >= c0 && ccode <= c1) {
            const uint16_t map_type = ntr_rd16(c, p + 4u);
            if (map_type == 0) {
                glyph_idx = uint16_t(ntr_rd16(c, p + 12u) + (ccode - c0));
                break;
            } else if (map_type == 1) {
                glyph_idx = ntr_rd16(c, p + 12u + 2u * uint32_t(ccode - c0));
                break;
            } else if (map_type == 2) {
                const uint16_t count = ntr_rd16(c, p + 12u);
                int lo = 0, hi = int(count) - 1;
                while (lo <= hi) {
                    const int mid = (lo + hi) >> 1;
                    const uint32_t entry = p + 14u + 4u * uint32_t(mid);
                    const uint16_t code = ntr_rd16(c, entry);
                    if (code == ccode) {
                        glyph_idx = ntr_rd16(c, entry + 2u);
                        break;
                    } else if (code < ccode) {
                        lo = mid + 1;
                    } else {
                        hi = mid - 1;
                    }
                }
                break;
            }
        }
        p = ntr_rd32(c, p + 8u);
    }
    if (glyph_idx == 0xFFFFu) glyph_idx = ntr_rd16(c, finf + 2u);  // alterCharIndex

    // 2. Resolve widths via CWDH chain (matches NNS_G2dFontGetCharWidthsFromIndex at 0x02023EEC).
    bool found_w = false;
    p = cwdh;
    for (int iter = 0; iter < 64 && valid_main_ram(p); ++iter) {
        const uint16_t i0 = ntr_rd16(c, p + 0u);
        const uint16_t i1 = ntr_rd16(c, p + 2u);
        if (glyph_idx >= i0 && glyph_idx <= i1) {
            const uint32_t off = p + 8u + 3u * uint32_t(glyph_idx - i0);
            out.left = static_cast<int8_t>(ntr_rd8(c, off + 0u));
            out.glyph_w = ntr_rd8(c, off + 1u);
            out.char_w = static_cast<int8_t>(ntr_rd8(c, off + 2u));
            found_w = true;
            break;
        }
        p = ntr_rd32(c, p + 4u);
    }
    if (!found_w) {
        out.left = static_cast<int8_t>(ntr_rd8(c, finf + 4u));
        out.glyph_w = ntr_rd8(c, finf + 5u);
        out.char_w = static_cast<int8_t>(ntr_rd8(c, finf + 6u));
    }
    return true;
}

uint32_t rgb555_to_argb(uint16_t c15, uint8_t bright_mode, uint8_t bright_factor) {
    int r = int(c15 & 0x1Fu);
    int g = int((c15 >> 5) & 0x1Fu);
    int b = int((c15 >> 10) & 0x1Fu);
    r = (r << 3) | (r >> 2);
    g = (g << 3) | (g >> 2);
    b = (b << 3) | (b >> 2);
    const int f = std::min(int(bright_factor), 16);
    if (bright_mode == 1 && f > 0) {  // fade to white
        r += ((255 - r) * f) / 16;
        g += ((255 - g) * f) / 16;
        b += ((255 - b) * f) / 16;
    } else if (bright_mode == 2 && f > 0) {  // fade to black
        r -= (r * f) / 16;
        g -= (g * f) / 16;
        b -= (b * f) / 16;
    }
    return 0xFF000000u | (uint32_t(r) << 16) | (uint32_t(g) << 8) | uint32_t(b);
}

bool is_whitespace(uint16_t cp) {
    return cp == 0x0020u || cp == 0x00A0u || cp == 0x3000u;
}

}  // namespace

void set_hd_text_enabled(bool enabled) {
    g_enabled.store(enabled, std::memory_order_relaxed);
}

bool hd_text_enabled() {
    return g_enabled.load(std::memory_order_relaxed);
}

const stbtt_fontinfo* hd_font_info() {
    return ensure_font();
}

void hd_text_reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_canvases.clear();
    g_pending[0].clear();
    g_pending[1].clear();
    g_visible[0].clear();
    g_visible[1].clear();
}

void hd_text_draw_list(int layer, std::vector<HdGlyph>& out) {
    out.clear();
    if (layer < 0 || layer > 1) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    out = g_visible[layer];
}

}  // namespace ph

// Main 0x0203DCB8 (before): UnkStruct_0203dae0::Init(this, engine_raw, priority, clear_color, hSpace).
// Tracks every OBJ text canvas (NPC dialogue boxes, signs, prompts) while leaving BG canvases untouched.
extern "C" void ph_text_obj_init(NtrCpu* c) {
    std::lock_guard<std::mutex> lock(ph::g_mutex);
    ph::g_canvases[c->r[0]].glyphs.clear();
}

// Main 0x0203DDB8 (before): UnkStruct_0203dae0::Deinit(this).
extern "C" void ph_text_obj_deinit(NtrCpu* c) {
    std::lock_guard<std::mutex> lock(ph::g_mutex);
    ph::g_canvases.erase(c->r[0]);
}

// Main 0x020331C0 (before): UnkStruct_02032f0c::vfunc_10(this, clear_color, ...) -- clears the entire CharCanvas.
extern "C" void ph_text_clear(NtrCpu* c) {
    std::lock_guard<std::mutex> lock(ph::g_mutex);
    if (auto it = ph::g_canvases.find(c->r[0]); it != ph::g_canvases.end()) {
        it->second.glyphs.clear();
    }
}

// Main 0x020294C4 (before): NNS_G2dCharCanvasClearArea(pCC, cl, x, y, w, h).
// Removes recorded glyphs inside the cleared rectangle so partial box clears never leave stale text.
extern "C" void ph_text_clear_area(NtrCpu* c) {
    const uint32_t this_obj = c->r[0] - 0x10u;
    const int32_t x = static_cast<int32_t>(c->r[2]);
    const int32_t y = static_cast<int32_t>(c->r[3]);
    const int32_t w = static_cast<int32_t>(ntr_rd32(c, c->r[13] + 0u));
    const int32_t h = static_cast<int32_t>(ntr_rd32(c, c->r[13] + 4u));
    if (w <= 0 || h <= 0) return;
    std::lock_guard<std::mutex> lock(ph::g_mutex);
    auto it = ph::g_canvases.find(this_obj);
    if (it == ph::g_canvases.end() || it->second.glyphs.empty()) return;
    auto& glyphs = it->second.glyphs;
    glyphs.erase(std::remove_if(glyphs.begin(), glyphs.end(), [&](const ph::RecordedGlyph& g) {
        const int32_t gx = g.x + std::max<int32_t>(1, g.glyph_w) / 2;
        const int32_t gy = g.y + std::max<int32_t>(1, g.cell_h) / 2;
        return gx >= x && gx < x + w && gy >= y && gy < y + h;
    }), glyphs.end());
}

// Main 0x020296E0 (instead): NNS_G2dCharCanvasDrawChar(pCC, pFont, x, y, cl, ccode).
// When HD text is enabled and the target is a tracked OBJ canvas with a glyph supported by our TrueType font,
// records the glyph and skips 1x bitmap rasterization (returning the exact DS charWidth so line wrap and timing
// remain bit-identical to the original game).
extern "C" int ph_text_draw_char(NtrCpu* c) {
    if (!ph::g_enabled.load(std::memory_order_relaxed)) return 0;

    const uint32_t pCC = c->r[0];
    const uint32_t pFont = c->r[1];
    const int32_t x = static_cast<int32_t>(c->r[2]);
    const int32_t y = static_cast<int32_t>(c->r[3]);
    const uint32_t cl = ntr_rd32(c, c->r[13] + 0u);
    const uint16_t ccode = ntr_rd16(c, c->r[13] + 4u);
    const uint32_t this_obj = pCC - 0x10u;

    // Never intercept Private Use Area symbols (custom Nintendo/Zelda button & icon glyphs).
    if (ccode >= 0xE000u && ccode <= 0xF8FFu) return 0;

    const stbtt_fontinfo* font = ph::ensure_font();
    if (!font) return 0;

    const bool space = ph::is_whitespace(ccode);
    if (!space && stbtt_FindGlyphIndex(font, int(ccode)) == 0) return 0;

    ph::NftrMetrics m;
    if (!ph::lookup_nftr_metrics(c, pFont, ccode, m)) return 0;
    if (m.flags != 0) return 0;  // keep rotated/vertical glyphs on the DS rasterizer

    {
        std::lock_guard<std::mutex> lock(ph::g_mutex);
        auto it = ph::g_canvases.find(this_obj);
        if (it == ph::g_canvases.end()) return 0;  // BG canvas or untracked canvas
        if (!space) {
            ph::RecordedGlyph g;
            g.codepoint = ccode;
            g.x = static_cast<int16_t>(x + int32_t(m.left));
            g.y = static_cast<int16_t>(y);
            g.cell_h = m.cell_h > 0 ? m.cell_h : 16;
            g.baseline = m.baseline > 0 ? m.baseline : 14;
            g.glyph_w = m.glyph_w > 0 ? m.glyph_w : static_cast<uint8_t>(std::max<int>(1, m.char_w));
            g.char_w = m.char_w;
            g.color_idx = static_cast<uint8_t>(cl & 0xFu);
            g.bpp = m.bpp > 0 ? m.bpp : 2;
            it->second.glyphs.push_back(g);
        }
    }

    c->r[0] = static_cast<uint32_t>(static_cast<int32_t>(m.char_w));
    return 1;
}

// Main 0x0203E16C (before): UnkStruct_0203dae0::Draw(this, screen_x, screen_y, params).
// Called every render tick while an OBJ text box is on screen.
extern "C" void ph_text_obj_draw(NtrCpu* c) {
    const uint32_t this_obj = c->r[0];
    // Same visibility check as UnkStruct_0203dae0::Draw: VRAM slot == -1 or num_objs <= 0 means not drawn.
    if (ntr_rd32(c, this_obj + 280u) == 0xFFFFFFFFu) return;
    if (static_cast<int8_t>(ntr_rd8(c, this_obj + 287u)) <= 0) return;

    const int32_t screen_x = static_cast<int32_t>(c->r[1]);
    const int32_t screen_y = static_cast<int32_t>(c->r[2]);
    // Exact 2D engine resolution (matches fn_main_02032fa0 -> fn_main_020353e4).
    const int engine = (ntr_rd8(c, ph::kSingleEngineFlag) != 0) ? 0 : (ntr_rd8(c, this_obj + 80u) ? 1 : 0);
    // Exact 4-bit hardware OBJ palette from sprite 0's OAM attr2 at this + 88 (bits 12..15).
    const uint8_t pal_idx = static_cast<uint8_t>((ntr_rd16(c, this_obj + 88u) >> 12) & 0xFu);

    std::lock_guard<std::mutex> lock(ph::g_mutex);
    auto it = ph::g_canvases.find(this_obj);
    if (it == ph::g_canvases.end() || it->second.glyphs.empty()) return;

    auto& dst = ph::g_pending[engine];
    dst.reserve(dst.size() + it->second.glyphs.size());
    for (const ph::RecordedGlyph& g : it->second.glyphs) {
        ph::PendingEntry e;
        e.glyph.codepoint = g.codepoint;
        e.glyph.ds_x = float(screen_x + int32_t(g.x));
        e.glyph.ds_y = float(screen_y + int32_t(g.y));
        e.glyph.cell_h = float(g.cell_h);
        e.glyph.baseline = float(g.baseline);
        e.glyph.slot_w = float(g.glyph_w);
        e.glyph.char_w = float(g.char_w);
        e.pal_idx = pal_idx;
        // 2bpp NFTR: v=1..3 maps to palette cl + v - 1; solid fill (v=3) is (cl + 2) & 0xF.
        e.solid_color_idx = static_cast<uint8_t>((g.bpp >= 2 ? (g.color_idx + 2u) : g.color_idx) & 0xFu);
        dst.push_back(e);
    }
}

// Main 0x020249DC (before): NNS_G2dApplyOamManagerToProxy(oam_mgr).
// Commits each 2D engine's pending HD text list at exact OAM flush time and resolves live OBJ palette & brightness.
extern "C" void ph_text_oam_flush(NtrCpu* c) {
    int engine = -1;
    if (c->r[0] == ph::kOamMgrEngineA) engine = 0;
    else if (c->r[0] == ph::kOamMgrEngineB) engine = 1;
    else return;

    const uint32_t dispcnt = ntr_rd32(c, engine == 0 ? ph::kDispCntA : ph::kDispCntB);
    const bool obj_on = (dispcnt & (1u << 12)) != 0;
    const uint16_t mb = ntr_rd16(c, engine == 0 ? ph::kMasterBrightA : ph::kMasterBrightB);
    const uint8_t bright_mode = static_cast<uint8_t>((mb >> 14) & 3u);
    const uint8_t bright_factor = static_cast<uint8_t>(mb & 0x1Fu);
    const uint32_t pal_ram = (engine == 0) ? ph::kObjPalA : ph::kObjPalB;

    // POWCNT1 bit 15: 1 => Engine A on top screen (layer 0); 0 => Engine A on bottom screen (layer 1).
    const bool swap = ((ntr_rd32(c, ph::kPowCnt1) >> 15) & 1u) != 0;
    const int layer = (engine == 0) ? (swap ? 0 : 1) : (swap ? 1 : 0);

    std::lock_guard<std::mutex> lock(ph::g_mutex);
    auto& pending = ph::g_pending[engine];
    auto& visible = ph::g_visible[layer];
    visible.clear();

    if (obj_on && !pending.empty() && !(bright_mode != 0 && bright_factor >= 16)) {
        visible.reserve(pending.size());
        for (const ph::PendingEntry& e : pending) {
            ph::HdGlyph g = e.glyph;
            const uint32_t addr = pal_ram + uint32_t(e.pal_idx) * 32u + uint32_t(e.solid_color_idx) * 2u;
            const uint16_t c15 = ntr_rd16(c, addr);
            g.argb = ph::rgb555_to_argb(c15, bright_mode, bright_factor);
            visible.push_back(g);
        }
    }
    pending.clear();
}
