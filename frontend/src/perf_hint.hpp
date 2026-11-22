// perf_hint.hpp -- Android's performance hint (ADPF) for the game thread.
//
// The game thread works for part of each frame, then sleeps until the next one (pace). Android's CPU governor sees a
// thread that is only partly busy and keeps the cores' clocks low (on a Moto G Stylus 2025, mostly 0.7-1.3 GHz of
// 2.4), so the heavier frames miss the 60 Hz deadline. A hint session tells the system the deadline (one DS frame)
// and, every frame, how long the work really took; the system then raises the clocks just enough to meet it.
//
// APerformanceHint is in libandroid from Android 13 (API 33). It is looked up at run time, so the app still runs on
// Android 8+: without it (or anywhere but Android) every call here does nothing.
#pragma once

#include <cstdint>

#ifdef __ANDROID__
#include <dlfcn.h>
#include <unistd.h>
#include <cstdio>
#endif

namespace ph {

class PerfHint {
public:
    // Call on the thread to boost, once it is running. target_ns: the time one frame's work may take.
    void start(int64_t target_ns) {
#ifdef __ANDROID__
        if (session_ || tried_) return;
        tried_ = true;
        void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) return;
        using GetManager = void* (*)();
        using CreateSession = void* (*)(void*, const int32_t*, size_t, int64_t);
        auto get_manager = reinterpret_cast<GetManager>(dlsym(lib, "APerformanceHint_getManager"));
        auto create = reinterpret_cast<CreateSession>(dlsym(lib, "APerformanceHint_createSession"));
        report_ = reinterpret_cast<Report>(dlsym(lib, "APerformanceHint_reportActualWorkDuration"));
        if (!get_manager || !create || !report_) {
            std::fprintf(stderr, "perf hint: not available (Android 13+ only)\n");
            return;
        }
        void* manager = get_manager();
        const int32_t tid = int32_t(gettid());
        if (manager) session_ = create(manager, &tid, 1, target_ns);
        std::fprintf(stderr, session_ ? "perf hint: on for the game thread (target %.1f ms)\n"
                                      : "perf hint: the system refused a session (target %.1f ms)\n",
                     double(target_ns) / 1e6);
#else
        (void)target_ns;
#endif
    }

    // Once per frame: how long this frame's work took (not counting the wait for the next frame).
    void report(int64_t work_ns) {
#ifdef __ANDROID__
        if (session_ && work_ns > 0) report_(session_, work_ns);
#else
        (void)work_ns;
#endif
    }

private:
#ifdef __ANDROID__
    using Report = int (*)(void*, int64_t);
    void* session_ = nullptr;
    Report report_ = nullptr;
    bool tried_ = false;
#endif
};

}  // namespace ph
