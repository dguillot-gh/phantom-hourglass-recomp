// Phantom Hourglass's hooks (frontend/config/azee/hooks.txt): the NitroSDK routines the runtime replaces.
#include "ph_hooks.hpp"

#include <cstdint>
#include <cstring>

#include "ntr/nitro.h"
#include "ntr/recomp.h"

namespace {
// AZEE (USA). The decomp (zeldaret/ph) leaves these unnamed; each was matched by its instructions.
struct Addresses {
    uint32_t os_irq_table;                  // OS_IRQTable (DTCM): the literal OS_IrqHandler loads
    uint32_t os_irq_handler_thread_switch;  // OS_IrqHandler_ThreadSwitch (ITCM): the LR OS_IrqHandler sets
};
constexpr Addresses kUsa = {0x027E0014, 0x01FFA4E4};
const Addresses* A = &kUsa;
}  // namespace

// ITCM 0x01FFA48C: OS_IrqHandler.
extern "C" void ph_os_irq_handler(NtrCpu* c) { ntr::nitro::irq_handler(c, A->os_irq_table, A->os_irq_handler_thread_switch); }

namespace ph {

bool select_release(const std::string& game_code) {
    if (game_code != "AZEE") return false;
    A = &kUsa;
    return true;
}

void* find_hook(const char* name) {
    static const struct {
        const char* name;
        void* fn;
    } kHooks[] = {
        {"ph_os_irq_handler", reinterpret_cast<void*>(&ph_os_irq_handler)},
        {"ntr_nitro_save_context", reinterpret_cast<void*>(&ntr_nitro_save_context)},
        {"ntr_nitro_load_context", reinterpret_cast<void*>(&ntr_nitro_load_context)},
        {"ntr_nitro_init_context", reinterpret_cast<void*>(&ntr_nitro_init_context)},
    };
    for (const auto& h : kHooks)
        if (std::strcmp(h.name, name) == 0) return h.fn;
    return nullptr;
}

}  // namespace ph
