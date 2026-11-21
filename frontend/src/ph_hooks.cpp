// Phantom Hourglass's hooks (frontend/config/<code>/hooks.txt): the NitroSDK routines the runtime replaces.
#include "ph_hooks.hpp"
#include "ph_text.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

#include "ntr/nitro.h"
#include "ntr/recomp.h"

namespace {
// AZEE (USA) and AZEP (Europe). The decomp (zeldaret/ph) leaves these unnamed; each was matched by its instructions.
struct Addresses {
    uint32_t os_irq_table;                  // OS_IRQTable (DTCM): the literal OS_IrqHandler loads
    uint32_t os_irq_handler_thread_switch;  // OS_IrqHandler_ThreadSwitch (ITCM): the LR OS_IrqHandler sets
};
constexpr Addresses kUsa = {0x027E0014, 0x01FFA4E4};
constexpr Addresses kEur = {0x027E0014, 0x01FFA4E4};
const Addresses* A = &kUsa;
}  // namespace

// Main 0x02005BFC: G3i_PerspectiveW_ (r0, r1 = sin and cos of half the field of view, r2 = the aspect ratio, 4.12).
// The game always passes the DS's 4:3 (0x1555): widescreen gives the wide one instead, and tells the geometry engine
// (third_party/CMakeLists.txt patch) that this scene is wide. A scene of orthographic geometry only (ffcc_wide_block)
// stays 4:3.
extern "C" int ffcc_wide_scene;
extern "C" int ffcc_wide_block;
namespace {
std::atomic<uint32_t> g_aspect{0x1555};
}
extern "C" void ph_perspective_aspect(NtrCpu* c) {
    const uint32_t aspect = g_aspect.load(std::memory_order_relaxed);
    if (c->r[2] == 0x1555 && aspect != 0x1555 && ffcc_wide_block == 0) {
        c->r[2] = aspect;
        ffcc_wide_scene = 8;
    }
}

// ITCM 0x01FFA48C: OS_IrqHandler.
extern "C" void ph_os_irq_handler(NtrCpu* c) { ntr::nitro::irq_handler(c, A->os_irq_table, A->os_irq_handler_thread_switch); }

namespace ph {

bool select_release(const std::string& game_code) {
    if (game_code == "AZEE") {
        A = &kUsa;
        return true;
    }
    if (game_code == "AZEP") {
        A = &kEur;
        return true;
    }
    return false;
}

void* find_hook(const char* name) {
    static const struct {
        const char* name;
        void* fn;
    } kHooks[] = {
        {"ph_os_irq_handler", reinterpret_cast<void*>(&ph_os_irq_handler)},
        {"ph_perspective_aspect", reinterpret_cast<void*>(&ph_perspective_aspect)},
        {"ntr_nitro_save_context", reinterpret_cast<void*>(&ntr_nitro_save_context)},
        {"ntr_nitro_load_context", reinterpret_cast<void*>(&ntr_nitro_load_context)},
        {"ntr_nitro_init_context", reinterpret_cast<void*>(&ntr_nitro_init_context)},
        {"ph_text_obj_init", reinterpret_cast<void*>(&ph_text_obj_init)},
        {"ph_text_obj_deinit", reinterpret_cast<void*>(&ph_text_obj_deinit)},
        {"ph_text_clear", reinterpret_cast<void*>(&ph_text_clear)},
        {"ph_text_clear_area", reinterpret_cast<void*>(&ph_text_clear_area)},
        {"ph_text_draw_char", reinterpret_cast<void*>(&ph_text_draw_char)},
        {"ph_text_obj_draw", reinterpret_cast<void*>(&ph_text_obj_draw)},
        {"ph_text_oam_flush", reinterpret_cast<void*>(&ph_text_oam_flush)},
    };
    for (const auto& h : kHooks)
        if (std::strcmp(h.name, name) == 0) return h.fn;
    return nullptr;
}

void set_wide_extra(int extra) {
    g_aspect = extra > 0 ? uint32_t(((256 + 2 * extra) * 4096 + 96) / 192) : 0x1555u;
}

}  // namespace ph
