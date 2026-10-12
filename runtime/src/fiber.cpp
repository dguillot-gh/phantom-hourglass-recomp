#include "fiber.hpp"

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>

namespace ntr {

struct Fiber {
    void* handle = nullptr;
    bool owns = false;
    FiberEntry entry = nullptr;
    void* arg = nullptr;
};

static void WINAPI fiber_trampoline(void* p) {
    Fiber* f = static_cast<Fiber*>(p);
    f->entry(f->arg);
    std::abort();  // fiber entries never return
}

Fiber* fiber_init_thread() {
    Fiber* f = new Fiber;
    f->handle = ConvertThreadToFiber(nullptr);
    if (!f->handle) f->handle = GetCurrentFiber();
    return f;
}

Fiber* fiber_create(std::size_t stack_size, FiberEntry entry, void* arg) {
    Fiber* f = new Fiber;
    f->owns = true;
    f->entry = entry;
    f->arg = arg;
    f->handle = CreateFiberEx(64 * 1024, stack_size, FIBER_FLAG_FLOAT_SWITCH, fiber_trampoline, f);
    if (!f->handle) std::abort();
    return f;
}

void fiber_switch(Fiber*, Fiber* to) { SwitchToFiber(to->handle); }

void fiber_destroy(Fiber* f) {
    if (f->owns) DeleteFiber(f->handle);
    delete f;
}

}  // namespace ntr

#elif defined(__ANDROID__) && defined(__aarch64__)
// Android's C library has no makecontext/swapcontext: a small context switch of our own. A suspended fiber's stack
// holds its callee-saved registers (AAPCS64: x19-x30, d8-d15); its stack pointer is all a Fiber keeps.
#include <cstdint>

extern "C" void ntr_fiber_swap(void** save_sp, void* new_sp);
extern "C" void ntr_fiber_start();
extern "C" void ntr_fiber_run(void* fiber);

__asm__(
    ".text\n"
    ".global ntr_fiber_swap\n"
    ".type ntr_fiber_swap, %function\n"
    "ntr_fiber_swap:\n"
    "  sub sp, sp, #160\n"
    "  stp x19, x20, [sp, #0]\n"
    "  stp x21, x22, [sp, #16]\n"
    "  stp x23, x24, [sp, #32]\n"
    "  stp x25, x26, [sp, #48]\n"
    "  stp x27, x28, [sp, #64]\n"
    "  stp x29, x30, [sp, #80]\n"
    "  stp d8, d9, [sp, #96]\n"
    "  stp d10, d11, [sp, #112]\n"
    "  stp d12, d13, [sp, #128]\n"
    "  stp d14, d15, [sp, #144]\n"
    "  mov x2, sp\n"
    "  str x2, [x0]\n"
    "  mov sp, x1\n"
    "  ldp x19, x20, [sp, #0]\n"
    "  ldp x21, x22, [sp, #16]\n"
    "  ldp x23, x24, [sp, #32]\n"
    "  ldp x25, x26, [sp, #48]\n"
    "  ldp x27, x28, [sp, #64]\n"
    "  ldp x29, x30, [sp, #80]\n"
    "  ldp d8, d9, [sp, #96]\n"
    "  ldp d10, d11, [sp, #112]\n"
    "  ldp d12, d13, [sp, #128]\n"
    "  ldp d14, d15, [sp, #144]\n"
    "  add sp, sp, #160\n"
    "  ret\n"
    ".size ntr_fiber_swap, .-ntr_fiber_swap\n"
    // A new fiber's first switch "returns" here with its Fiber in x19.
    ".global ntr_fiber_start\n"
    ".type ntr_fiber_start, %function\n"
    "ntr_fiber_start:\n"
    "  mov x0, x19\n"
    "  bl ntr_fiber_run\n"
    "  brk #0\n"
    ".size ntr_fiber_start, .-ntr_fiber_start\n");

namespace ntr {

struct Fiber {
    void* sp = nullptr;  // saved stack pointer while suspended
    void* stack = nullptr;
    FiberEntry entry = nullptr;
    void* arg = nullptr;
};

}  // namespace ntr

extern "C" void ntr_fiber_run(void* p) {
    ntr::Fiber* f = static_cast<ntr::Fiber*>(p);
    f->entry(f->arg);
    std::abort();  // fiber entries never return
}

namespace ntr {

Fiber* fiber_init_thread() { return new Fiber; }

Fiber* fiber_create(std::size_t stack_size, FiberEntry entry, void* arg) {
    Fiber* f = new Fiber;
    f->entry = entry;
    f->arg = arg;
    f->stack = std::malloc(stack_size);
    if (!f->stack) std::abort();
    // The first switch to it pops this frame: x19 = the Fiber, x29 = 0 (end of the frame chain), x30 = the start stub.
    uintptr_t top = (reinterpret_cast<uintptr_t>(f->stack) + stack_size) & ~uintptr_t(15);
    uint64_t* frame = reinterpret_cast<uint64_t*>(top - 160);
    for (int i = 0; i < 20; i++) frame[i] = 0;
    frame[0] = reinterpret_cast<uint64_t>(f);                       // x19
    frame[10] = 0;                                                   // x29
    frame[11] = reinterpret_cast<uint64_t>(&ntr_fiber_start);       // x30
    f->sp = frame;
    return f;
}

void fiber_switch(Fiber* from, Fiber* to) { ntr_fiber_swap(&from->sp, to->sp); }

void fiber_destroy(Fiber* f) {
    std::free(f->stack);
    delete f;
}

}  // namespace ntr

#else
#include <ucontext.h>

#include <cstdint>

namespace ntr {

struct Fiber {
    ucontext_t ctx{};
    void* stack = nullptr;
    FiberEntry entry = nullptr;
    void* arg = nullptr;
};

static void fiber_trampoline(unsigned lo, unsigned hi) {
    Fiber* f = reinterpret_cast<Fiber*>((uintptr_t(hi) << 32) | uintptr_t(lo));
    f->entry(f->arg);
    std::abort();
}

Fiber* fiber_init_thread() { return new Fiber; }

Fiber* fiber_create(std::size_t stack_size, FiberEntry entry, void* arg) {
    Fiber* f = new Fiber;
    f->entry = entry;
    f->arg = arg;
    f->stack = std::malloc(stack_size);
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = f->stack;
    f->ctx.uc_stack.ss_size = stack_size;
    f->ctx.uc_link = nullptr;
    uintptr_t p = reinterpret_cast<uintptr_t>(f);
    makecontext(&f->ctx, reinterpret_cast<void (*)()>(fiber_trampoline), 2, unsigned(p & 0xFFFFFFFFu), unsigned(p >> 32));
    return f;
}

void fiber_switch(Fiber* from, Fiber* to) { swapcontext(&from->ctx, &to->ctx); }

void fiber_destroy(Fiber* f) {
    std::free(f->stack);
    delete f;
}

}  // namespace ntr
#endif
