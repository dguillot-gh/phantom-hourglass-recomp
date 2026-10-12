// Save states: the whole console (melonDS's state) plus what the runtime adds to it -- the ARM9's registers and the
// game's threads.
//
// The recompiled code keeps each guest thread's call stack on a host fiber, which cannot be written to a file. So a
// state is taken only where every thread can be described by guest data alone: when the game switches threads
// (OS_LoadContext) and every other thread is parked by OS_SaveContext, as threads waiting for something are. Each
// parked thread's OSContext then holds its registers and, in place of a PC, its fiber's token; the state records the
// real PC instead -- the return address of its OS_SaveContext call. Loading discards every fiber, puts those PCs
// back into the OSContexts, and does the OS_LoadContext the state was taken in. A thread resumed at such a PC runs
// on the interpreter (it is in the middle of a function) until the functions on its stack return; every call it
// makes meanwhile runs recompiled, so in practice only the few instructions between calls are interpreted.
#include <cstring>

#include "NDSCart.h"
#include "Savestate.h"
#include "ntr/nitro.h"
#include "runtime_internal.hpp"

namespace ntr {

namespace {

constexpr u32 kStateMagic = 0x5354524Eu;  // "NTRS"
constexpr u32 kStateVersion = 2;         // 2: melonDS's part packed (1: as it is)
constexpr u32 kCtxPcPlus4 = 0x40;         // OSContext: the PC + 4 (nitro.cpp)
constexpr int kSaveGiveUpFrames = 300;     // about five seconds without a moment to take the state

struct Writer {
    std::vector<u8>& out;
    void u32v(u32 v) {
        for (int i = 0; i < 4; i++) out.push_back(u8(v >> (8 * i)));
    }
    void u64v(u64 v) {
        u32v(u32(v));
        u32v(u32(v >> 32));
    }
};

struct Reader {
    const u8* p;
    size_t n, at = 0;
    bool ok = true;
    u32 u32v() {
        if (at + 4 > n) return ok = false, 0;
        u32 v = 0;
        for (int i = 0; i < 4; i++) v |= u32(p[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    u64 u64v() {
        const u64 lo = u32v();
        return lo | (u64(u32v()) << 32);
    }
};

// melonDS's state is mostly zeros (it keeps 16 MB of main RAM, the DSi's size, of which the DS uses 4): runs of
// them are packed. Records: u32 count of literal bytes, those bytes, u32 count of zero bytes; to the end.
std::vector<u8> pack(const u8* p, size_t n) {
    std::vector<u8> out;
    out.reserve(n / 4);
    Writer w{out};
    size_t i = 0;
    while (i < n) {
        size_t lit = i;
        while (lit < n) {  // the literal bytes go up to the next run of at least 32 zeros
            if (p[lit] == 0) {
                size_t z = lit;
                while (z < n && z - lit < 32 && p[z] == 0) z++;
                if (z - lit >= 32 || z == n) break;
                lit = z;
            } else {
                lit++;
            }
        }
        size_t zero = lit;
        while (zero < n && p[zero] == 0) zero++;
        w.u32v(u32(lit - i));
        out.insert(out.end(), p + i, p + lit);
        w.u32v(u32(zero - lit));
        i = zero;
    }
    return out;
}

bool unpack(const u8* p, size_t n, std::vector<u8>& out) {
    Reader r{p, n};
    while (r.at < n) {
        const u32 lit = r.u32v();
        if (!r.ok || r.at + lit > n) return false;
        out.insert(out.end(), p + r.at, p + r.at + lit);
        r.at += lit;
        const u32 zero = r.u32v();
        if (!r.ok || out.size() + zero > (64u << 20)) return false;
        out.resize(out.size() + zero, 0);
    }
    return true;
}

}  // namespace

// The state's cartridge: its game code and header checksum (a state from another game, or another release of this
// one, is refused).
u32 RuntimeImpl::cart_id() const {
    const auto& h = nds->NDSCartSlot.GetCart()->GetHeader();
    u32 code = 0;
    std::memcpy(&code, h.GameCode, 4);
    return code ^ (u32(h.HeaderCRC16) << 16);
}

void RuntimeImpl::request_save_state() { save_requested_ = true; }

void RuntimeImpl::request_load_state(std::vector<uint8_t> state) {
    load_data_ = std::move(state);
    load_requested_ = true;
}

// The OSContext whose PC slot holds `token` (+ 4): the one word in main RAM with that value, else 0 (none, or more
// than one: not sure which).
u32 RuntimeImpl::find_token_context(u32 token) {
    const u32 want = token + 4;
    const u32* ram = reinterpret_cast<const u32*>(nds->MainRAM);
    const size_t words = (size_t(nds->MainRAMMask) + 1) / 4;
    u32 found = 0;
    for (size_t i = 0; i < words; i++) {
        if (ram[i] != want) continue;
        if (found) return 0;
        found = 0x02000000u + u32(i * 4) - kCtxPcPlus4;
    }
    return found;
}

// OS_SaveContext: the thread's state is all in its OSContext, the real PC being `lr`.
void RuntimeImpl::note_saved_context(u32 ctx, u32 lr) {
    GuestFiber& f = fibers_[current_fiber_];
    f.voluntary = true;
    f.ctx = ctx;
    f.resume_lr = lr;
}

// Called when save_requested_ is set, from end_frame: the request waits for a moment to take the state, or ends.
void RuntimeImpl::age_save_request() {
    if (!save_requested_) return;
    if (++save_wait_frames_ < kSaveGiveUpFrames) return;
    save_requested_ = false;
    save_wait_frames_ = 0;
    log("state: no moment to save in %d frames (a thread never parked)\n", kSaveGiveUpFrames);
    host.on_state_saved({});
}

// At OS_LoadContext(next_ctx), before anything of it has run.
void RuntimeImpl::try_save_state(u32 next_ctx) {
    if ((cpu.cpsr_ctrl & 0x1F) == 0x12) return;  // inside an IRQ handler (its return is on a host stack)
    struct Fixup { u32 ctx, slot, fiber; };
    std::vector<Fixup> fixups;
    for (u32 k = 0; k < fibers_.size(); k++) {
        const GuestFiber& f = fibers_[k];
        if (!f.alive || !f.fiber) continue;
        if (k == current_fiber_ && !f.saved) continue;  // the thread ending (OS_ExitThread)
        if (!f.saved || f.hle_depth) return;  // (inside a BIOS call's callback: that call's state is on a host stack)
        const u32 token = kFiberTokenBase + k * 4;
        if (!f.voluntary) {
            // Interrupted by an IRQ that switched threads. Its PC is somewhere in recompiled code -- not now -- unless
            // the IRQ woke OS_Halt (the idle thread): then its OSContext, which the IRQ's thread switch wrote, holds
            // the token; that context is found by the token, and the PC is OS_Halt's return.
            if (!f.irq_pc) return;
            const u32 ctx = find_token_context(token);
            if (!ctx) return;
            fixups.push_back({ctx, f.irq_pc + 4, k});
            continue;
        }
        if (ntr_rd32(&cpu, f.ctx + kCtxPcPlus4) != token + 4) continue;  // its OSContext is gone: nothing resumes it
        fixups.push_back({f.ctx, f.resume_lr + 4, k});
    }
    // The thread about to run must be one of those, or one that has not started yet.
    const u32 next_slot = ntr_rd32(&cpu, next_ctx + kCtxPcPlus4);
    if (((next_slot - 4) & kFiberTokenMask) == kFiberTokenBase) {
        bool found = false;
        for (const Fixup& x : fixups) found |= x.ctx == next_ctx;
        if (!found) return;
    }

    save_requested_ = false;
    save_wait_frames_ = 0;
    nds->ARM9Timestamp = u64(cpu.cycles);
    melonDS::Savestate st(24u << 20);  // (melonDS keeps 16 MB of main RAM: room for all of it)
    if (!nds->DoSavestate(&st) || st.Error) {
        log("state: melonDS could not save its state\n");
        host.on_state_saved({});
        return;
    }

    std::vector<u8> rt;
    Writer w{rt};
    for (u32 v : cpu.r) w.u32v(v);
    w.u32v(cpu.n), w.u32v(cpu.z), w.u32v(cpu.c), w.u32v(cpu.v), w.u32v(cpu.q), w.u32v(cpu.cpsr_ctrl);
    w.u64v(u64(cpu.cycles));
    for (u32 v : bank_usr_) w.u32v(v);
    for (u32 v : bank_fiq_) w.u32v(v);
    for (u32 v : bank_svc_) w.u32v(v);
    for (u32 v : bank_abt_) w.u32v(v);
    for (u32 v : bank_irq_) w.u32v(v);
    for (u32 v : bank_und_) w.u32v(v);
    for (u32 v : fiq_usr_) w.u32v(v);
    w.u32v(nds->ARM9ClockShift);
    w.u32v(next_ctx);
    w.u32v(u32(fixups.size()));
    for (const Fixup& x : fixups) w.u32v(x.ctx), w.u32v(x.slot);

    std::vector<u8> out;
    Writer o{out};
    o.u32v(kStateMagic);
    o.u32v(kStateVersion);
    o.u32v(cart_id());
    o.u32v(u32(rt.size()));
    const std::vector<u8> melon = pack(static_cast<const u8*>(st.Buffer()), st.Length());
    o.u32v(u32(melon.size()));
    out.insert(out.end(), rt.begin(), rt.end());
    out.insert(out.end(), melon.begin(), melon.end());
    log("state: saved at frame %llu (%zu threads parked, %zu KB)\n", (unsigned long long)frames_, fixups.size(),
        out.size() / 1024);
    host.on_state_saved(std::move(out));
}

// On the root fiber (run()): replaces the console's state and every guest thread with the requested state's. On
// failure nothing has changed (the console is put back as it was) and `error` says why.
bool RuntimeImpl::load_state_now(std::string& error) {
    const std::vector<u8> data = std::move(load_data_);
    load_data_.clear();
    Reader h{data.data(), data.size()};
    const u32 magic = h.u32v(), version = h.u32v(), id = h.u32v(), rt_len = h.u32v();
    u32 melon_len = h.u32v();
    if (!h.ok || magic != kStateMagic) return error = "not a save state", false;
    if (version != 1 && version != kStateVersion) return error = "a save state from another version of the program", false;
    if (id != cart_id()) return error = "a save state of another game or release", false;
    if (h.at + size_t(rt_len) + melon_len != data.size()) return error = "the save state file is incomplete", false;
    Reader r{data.data() + h.at, rt_len};
    const u8* melon = data.data() + h.at + rt_len;
    std::vector<u8> unpacked;
    if (version >= 2) {
        if (!unpack(melon, melon_len, unpacked)) return error = "the save state file is damaged", false;
        melon = unpacked.data();
        melon_len = u32(unpacked.size());
    }

    // The cartridge's save memory stays as it is: the state's copy would roll back the player's own save.
    auto& slot = nds->NDSCartSlot;
    std::vector<u8> sram;
    if (slot.GetSaveMemory()) sram.assign(slot.GetSaveMemory(), slot.GetSaveMemory() + slot.GetSaveMemoryLength());
    auto keep_sram = [&] {
        if (slot.GetSaveMemory() && slot.GetSaveMemoryLength() == sram.size())
            std::memcpy(slot.GetSaveMemory(), sram.data(), sram.size());
    };

    const u32 shift_now = nds->ARM9ClockShift;
    melonDS::Savestate backup(24u << 20);
    if (!nds->DoSavestate(&backup) || backup.Error) return error = "the console's state could not be kept", false;
    loading_state = true;  // (melonDS writes the state's save memory out as it loads: not to the player's file)
    melonDS::Savestate st(const_cast<u8*>(melon), melon_len, false);
    const bool ok = !st.Error && nds->DoSavestate(&st) && !st.Error;
    if (!ok) {
        melonDS::Savestate back(backup.Buffer(), backup.Length(), false);
        nds->DoSavestate(&back);
        keep_sram();
        loading_state = false;
        nds->ARM9ClockShift = shift_now;
        return error = "melonDS could not load the save state", false;
    }
    keep_sram();
    loading_state = false;

    for (u32& v : cpu.r) v = r.u32v();
    cpu.n = r.u32v(), cpu.z = r.u32v(), cpu.c = r.u32v(), cpu.v = r.u32v(), cpu.q = r.u32v(), cpu.cpsr_ctrl = r.u32v();
    r.u64v();  // (the clock: melonDS's ARM9 timestamp, below)
    for (u32& v : bank_usr_) v = r.u32v();
    for (u32& v : bank_fiq_) v = r.u32v();
    for (u32& v : bank_svc_) v = r.u32v();
    for (u32& v : bank_abt_) v = r.u32v();
    for (u32& v : bank_irq_) v = r.u32v();
    for (u32& v : bank_und_) v = r.u32v();
    for (u32& v : fiq_usr_) v = r.u32v();
    const u32 shift = r.u32v();
    boot_ctx_ = r.u32v();
    const u32 count = r.u32v();
    std::vector<std::pair<u32, u32>> fixups;
    for (u32 i = 0; i < count && r.ok; i++) {
        const u32 ctx = r.u32v();
        fixups.emplace_back(ctx, r.u32v());
    }
    if (!r.ok) fatal("save state: the runtime's part is cut short (the console was loaded already)");

    // melonDS's clock is in the state's ARM9 speed: convert it to the one in use.
    nds->ARM9ClockShift = shift;
    apply_cpu_speed(1 << (shift_now - 1));
    cpu.cycles = int64_t(nds->ARM9Timestamp);
    cpu.next_event = 0;
    map_pages();
    for (const auto& [ctx, slot_value] : fixups) {
        ntr_wr32(&cpu, ctx + kCtxPcPlus4, slot_value);
        resume_pcs_.insert(slot_value - 4);
    }

    // New fibers. The old ones' tokens are not reused (a stale one must not name a new thread).
    for (GuestFiber& f : fibers_) {
        if (f.fiber) fiber_destroy(f.fiber);
        f = GuestFiber{};
    }
    current_fiber_ = u32(fibers_.size());
    fibers_.push_back({fiber_create(1u << 20, &RuntimeImpl::boot_main, this), 0, true, false});

    // The renderer starts afresh from the loaded VRAM and 3D state; the input latch too.
    video_applied_ = false;
    apply_video(video_);
    latch_ = InputLatch{};
    keys_read_ = false;
    frames_unread_ = 0;
    save_requested_ = false;
    save_wait_frames_ = 0;
    log("state: loaded (%u threads to resume)\n", count);
    return true;
}

// The first fiber after a load: does the OS_LoadContext the state was taken in.
void RuntimeImpl::boot_main(void* arg) {
    RuntimeImpl* self = static_cast<RuntimeImpl*>(arg);
    self->cleanup_dead_fibers();
    self->cpu.r[0] = self->boot_ctx_;
    ntr_nitro_load_context(&self->cpu);
    fatal("OS_LoadContext returned to a loaded state's first fiber");
}

// A thread of a loaded state, resumed in the middle of a function at `pc`: the interpreter runs it, handing every
// call it makes to the recompiled code. Returns never (the thread's outermost function ends in OS_ExitThread).
void RuntimeImpl::interpret_resume(u32 pc) {
    melonDS::ARMv5& arm = nds->ARM9;
    if (options.trace_threads) log("thread %u resumes at %08x on the interpreter\n", current_fiber_, pc);
    auto load = [&](u32 at) {
        for (int i = 0; i < 15; i++) arm.R[i] = cpu.r[i];
        arm.CPSR = (cpsr() & ~0x20u) | 0x80;  // (IRQs come through poll(), as in interpret())
        nds->ARM9Timestamp = u64(cpu.cycles);
        arm.Cycles = 0;
        arm.JumpTo(at);
    };
    // (The mode and IRQ mask stay cpu.cpsr_ctrl's: calls made from here change them through the runtime.)
    auto store = [&] {
        for (int i = 0; i < 15; i++) cpu.r[i] = arm.R[i];
        cpu.n = arm.CPSR >> 31 & 1;
        cpu.z = arm.CPSR >> 30 & 1;
        cpu.c = arm.CPSR >> 29 & 1;
        cpu.v = arm.CPSR >> 28 & 1;
        cpu.q = arm.CPSR >> 27 & 1;
        cpu.cycles = int64_t(nds->ARM9Timestamp);
    };
    auto pc_of = [&] { return arm.R[15] - ((arm.CPSR & 0x20) ? 2 : 4); };

    load(pc);
    const u32 stall = melonDS::CPUStop_DMA9 | melonDS::CPUStop_GXStall;
    for (;;) {
        const u32 at = pc_of();
        const u32 size = (arm.CPSR & 0x20) ? 2 : 4;
        if ((arm.CPSR & 0x1F) != (cpu.cpsr_ctrl & 0x1F) || at >= 0xFFFF0000u) {
            store();
            dump_state();
            fatal("resumed thread left guest code at %08x (cpsr %08x), resumed at %08x", at, arm.CPSR, pc);
        }
        const u64 saved_target = nds->ARM9Target;
        nds->ARM9Target = nds->ARM9Timestamp + 1;
        arm.Execute<melonDS::CPUExecuteMode::Interpreter>();
        nds->ARM9Target = saved_target;
        interp_steps_++;
        if (arm.Halted) fatal("resumed thread halted the CPU at %08x", at);
        const u32 next = pc_of() | ((arm.CPSR >> 5) & 1);
        // A call (BL, BLX, or MOV LR, PC before BX): the link register holds the return address. The callee runs
        // recompiled and returns here.
        if ((arm.R[14] & ~1u) == at + size && (next & ~1u) != at + size && lookup(next)) {
            store();
            const u32 ret = cpu.r[14];
            call(next);
            load(ret);
            continue;
        }
        if (int64_t(nds->ARM9Timestamp) >= cpu.next_event || (nds->CPUStop & stall)) {
            store();
            poll();  // may run IRQ handlers and switch guest threads; `cpu` holds this thread's state again after
            load(next);
        }
    }
}

}  // namespace ntr
