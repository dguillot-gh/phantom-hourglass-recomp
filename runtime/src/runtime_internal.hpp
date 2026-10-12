// Internal runtime state shared by the service implementations (runtime.cpp, swi.cpp, nitro.cpp).
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "NDS.h"
#include "fiber.hpp"
#include "gl_between.hpp"
#include "input_latch.hpp"
#include "gx_stream.hpp"
#include "ntr/recomp.h"
#include "ntr/runtime.hpp"

namespace ntr {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;

// Fake guest addresses. A suspended guest thread's saved PC is the token of the host fiber holding its
// recompiled call stack; the BIOS IRQ and HLE call returns are recognised by their entry LR.
constexpr u32 kFiberTokenBase = 0xEE000000u;
constexpr u32 kFiberTokenMask = 0xFF000000u;
constexpr u32 kBiosIrqReturn = 0xFFFF0290u;
constexpr u32 kHleCallReturn = 0xFFFF0300u;

// melonDS's NDS with the scheduler internals we drive ourselves exposed.
class RecompNDS final : public melonDS::NDS {
public:
    RecompNDS(melonDS::NDSArgs&& args, void* userdata) : NDS(std::move(args), userdata) {}
    void run_system(u64 ts) { RunSystem(ts); }
    u64 next_target() { return NextTarget(); }
    melonDS::DMA& dma(int i) { return DMAs[i]; }
};

struct GuestFiber {
    Fiber* fiber = nullptr;
    u32 start_pc = 0;     // entry for a fresh thread (bit 0 = Thumb)
    bool alive = false;
    bool saved = false;   // its context was saved since it last resumed (so it can be resumed again)
    // Saved by OS_SaveContext (not by an IRQ's thread switch): the OSContext at `ctx` describes it, its real PC being
    // `resume_lr`, the return address of that call (save states, savestate.cpp).
    bool voluntary = false;
    u32 ctx = 0, resume_lr = 0;
    int hle_depth = 0;  // calls_guest() under way: the BIOS emulation's own state is on this fiber's host stack
    // Interrupted by an IRQ taken as OS_Halt woke up (the idle thread): its PC was OS_Halt's return address, so it
    // can be described by guest data too (its OSContext, written by the IRQ's thread switch, with this PC).
    u32 irq_pc = 0;
};

[[noreturn]] void fatal(const char* fmt, ...);
void log(const char* fmt, ...);

class RuntimeImpl final : public Runtime {
public:
    RuntimeImpl(std::vector<uint8_t> rom, const GameCode& code, Host& host, RuntimeOptions options);
    ~RuntimeImpl() override;

    int run() override;
    void set_buttons(uint32_t held, uint32_t pressed) override;
    void touch(int x, int y) override;
    void release_touch() override;
    void set_video(const VideoSettings& settings) override;
    VideoSettings video() const override { return video_; }
    void set_fast_cart(bool on) override;
    void set_cpu_speed(int multiplier) override { pending_cpu_speed_ = multiplier; }
    unsigned render_between(double x) override;
    void request_save_state() override;
    void request_load_state(std::vector<uint8_t> state) override;

    // ---- save states (savestate.cpp) ----
    bool save_requested() const { return save_requested_; }
    void note_saved_context(u32 ctx, u32 lr);  // OS_SaveContext
    void try_save_state(u32 next_ctx);         // OS_LoadContext, while a save is requested
    bool loading_state = false;                // melonDS's writes of the save memory are not the game's

    // ---- services for recompiled code ----
    u32 bus_read32(u32 a);
    u32 bus_read16(u32 a);
    u32 bus_read8(u32 a);
    void bus_write32(u32 a, u32 v);
    void bus_write16(u32 a, u32 v);
    void bus_write8(u32 a, u32 v);
    void call(u32 target);
    void jump(u32 target, u32 from, u32 entry_lr);
    // Runs guest code without a recompiled function on the melonDS interpreter until it reaches `stop`.
    bool can_interpret(u32 target) const;
    void interpret(u32 target, u32 stop);
    void dump_state();
    // Function-entry profile (FFCC_TRACE_ENTRY builds).
    void note_entry(u32 addr) {
        if (options.profile_path.empty()) return;
        FuncProfile& p = profile_[addr];
        if (!p.count++) p.first = frames_;
        p.last = frames_;
        // Cycles since the last function entry go to the function entered then (returns are not seen, so a caller's
        // work after a call is counted to its callee: a rough guide to where the time goes).
        if (profile_last_) profile_last_->cycles += u64(cpu.cycles - profile_last_cycles_);
        profile_last_ = &p;
        profile_last_cycles_ = cpu.cycles;
    }
    void poll();
    // CP15 wait for interrupt (OS_Halt) or the BIOS's Halt; `from_os_halt`: the mcr in OS_Halt, after which the IRQ
    // that wakes the CPU is taken at once, at a PC known to be OS_Halt's return.
    void halt(bool from_os_halt = false);
    u32 os_halt_return_pc();
    void swi(u32 number, u32 pc);
    u32 mrc(u32 cp, u32 op1, u32 crn, u32 crm, u32 op2);
    void mcr(u32 cp, u32 op1, u32 crn, u32 crm, u32 op2, u32 v);

    // CPU modes and banked registers.
    u32 cpsr() const { return ntr_get_cpsr(const_cast<NtrCpu*>(&cpu)); }
    void set_cpsr(u32 value, u32 mask);
    u32* spsr_slot();
    u32 user_reg(u32 r);
    void set_user_reg(u32 r, u32 v);
    void exception_return(u32 target);

    // Calls guest code at `target` and returns when it returns (LR = kHleCallReturn).
    void call_guest(u32 target);

    // Guest thread (fiber) management, used by the NitroSDK context hooks.
    u32 current_token() const { return kFiberTokenBase + current_fiber_ * 4; }
    void mark_saved() { fibers_[current_fiber_].saved = true; }
    // Continues guest execution at `pc` (bit 0 = Thumb) with the registers already in `cpu`.
    // Returns immediately if `pc` is the current fiber's own token.
    void resume_at(u32 pc);
    // Forgets the fiber whose token is `pc` (its guest thread context was reinitialised).
    void discard_token(u32 pc);

    NtrCpu cpu{};
    std::unique_ptr<RecompNDS> nds;
    Host& host;
    RuntimeOptions options;

private:
    void build_function_index(const GameCode& code);
    const NtrFuncEntry* lookup(u32 target);
    bool overlay_resident(const NtrOverlayInfo& ov);
    void map_pages();
    void sync_after_io(u32 addr);
    void advance();
    void end_frame();
    void apply_video(VideoSettings settings);
    void report_perf();
    void trace_cart_write(u32 addr, u32 value, u32 size);
    bool irq_pending() const;
    void take_irq();
    void swap_banks(u32 old_mode, u32 new_mode);
    void cleanup_dead_fibers();
    static void fiber_main(void* arg);
    static void boot_main(void* arg);
    u32 cart_id() const;
    u32 find_token_context(u32 token);
    void age_save_request();
    bool load_state_now(std::string& error);
    void interpret_resume(u32 pc);

    u32 halt_pc_ = 0;         // OS_Halt's return address while its waking IRQ is taken (take_irq)
    int halt_known_ = 0;      // OS_Halt recognised (1) or not (-1), logged once
    bool save_requested_ = false;
    int save_wait_frames_ = 0;
    bool load_requested_ = false;   // at the end of this frame
    bool load_pending_ = false;     // on the root fiber (run())
    std::vector<uint8_t> load_data_;
    u32 boot_ctx_ = 0;              // the OSContext a loaded state's OS_LoadContext loads
    std::unordered_set<u32> resume_pcs_;  // where loaded states' threads resume (on the interpreter)

    std::vector<uint8_t*> read_pages_, write_pages_;
    std::unordered_map<u32, std::vector<const NtrFuncEntry*>> functions_;
    std::vector<const NtrOverlayInfo*> overlays_by_id_;
    std::vector<const NtrOverlayInfo*> interpreted_;
    std::unordered_map<u32, const NtrFuncEntry*> call_cache_;

    // Banked registers for modes other than the current one (melonDS layout: r13, r14, spsr).
    u32 bank_usr_[2] = {};
    u32 bank_fiq_[8] = {};  // r8-r14, spsr
    u32 bank_svc_[3] = {}, bank_abt_[3] = {}, bank_irq_[3] = {}, bank_und_[3] = {};
    u32 fiq_usr_[5] = {};   // user r8-r12 while in FIQ mode

    std::vector<GuestFiber> fibers_;
    u32 current_fiber_ = 0;
    Fiber* root_fiber_ = nullptr;
    bool quit_ = false;
    int exit_code_ = 0;

    VideoSettings video_;            // in use
    VideoSettings pending_video_;    // requested by set_video()
    bool video_dirty_ = false;
    bool video_applied_ = false;
    int pending_cpu_speed_ = 0;  // requested by set_cpu_speed() (0 = no change)
    void apply_cpu_speed(int multiplier);

    uint32_t buttons_ = 0, presses_ = 0;  // held, and gone down since the last frame (from set_buttons)
    InputLatch latch_;                     // so that the game's 30 Hz reads see every press and release
    bool keys_read_ = false;               // the game read KEYINPUT since the last frame
    int frames_unread_ = 0;
    int touch_x_ = -1, touch_y_ = -1;
    std::vector<int16_t> audio_buf_;
    uint64_t frames_ = 0;
    uint64_t interp_calls_ = 0, interp_steps_ = 0;
    struct InterpStat { u64 calls = 0, steps = 0; };
    std::unordered_map<u32, InterpStat> interp_stats_;
    struct FuncProfile { u64 first = 0, last = 0, count = 0, cycles = 0; };
    std::unordered_map<u32, FuncProfile> profile_;  // (node-based: profile_last_ stays valid)
    FuncProfile* profile_last_ = nullptr;
    int64_t profile_last_cycles_ = 0;
    // --trace-files: ROM layout (offset, end, name) and the cartridge command being written.
    struct RomSpan { u32 begin, end; std::string name; };
    std::vector<RomSpan> rom_spans_;
    u8 cart_cmd_[8] = {};
    std::string last_traced_file_;  // per entry address (coverage report)
    // --perf: host time spent emulating (excluding the frontend's frame pacing) since the last report.
    std::chrono::steady_clock::time_point perf_mark_{};
    u64 frame_steps_mark_ = 0;  // interp_steps_ at the last frame (Frame::interp_steps)

    // --fps-probe: per frame, whether a new 3D scene was swapped in, display capture ran, and the 3D engine (engine A)
    // was on the top screen; summarised every 60 frames.
    void probe_frame();
    bool probe_capture_ = false;
    std::string probe_pattern_, probe_busy_, probe_wide_;
    u64 halted_cycles_ = 0, probe_cycles_mark_ = 0, probe_halted_mark_ = 0;  // ARM9 cycles spent halted (all time)
    int probe_new3d_ = 0, probe_capture_frames_ = 0, probe_top_ = 0, probe_swaps_ = 0;
    bool probe_last_top_ = false;

    // GX command streams: frame interpolation, --gx-probe, --gx-check.
    std::unique_ptr<gx::Streams> gx_;
    std::unique_ptr<GlBetween> between_;  // frames between emulated frames (display_rate)
    double perf_host_s_ = 0, perf_max_s_ = 0;
    u64 perf_steps_mark_ = 0, perf_calls_mark_ = 0;
};

inline RuntimeImpl& rt(NtrCpu* c) { return *static_cast<RuntimeImpl*>(c->runtime); }

}  // namespace ntr
