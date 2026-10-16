// Android only: the on-screen pad's calls into the game (com.phrecomp.ph.PhInput), and the program's output to logcat.
#ifdef __ANDROID__
#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <cstdio>
#include <thread>

#include "pad_input.hpp"

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetButtons(JNIEnv*, jclass, jint held) {
    ph::pad::held.store(uint32_t(held));
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetMic(JNIEnv*, jclass, jboolean on) {
    ph::pad::blow.store(on != JNI_FALSE);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetLid(JNIEnv*, jclass, jboolean closed) {
    ph::pad::lid.store(closed != JNI_FALSE);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeShowScreen(JNIEnv*, jclass, jint layout) {
    ph::pad::show.store(int(layout));
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetSmooth(JNIEnv*, jclass, jboolean on) {
    ph::pad::smooth.store(on != JNI_FALSE ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetFps(JNIEnv*, jclass, jboolean on) {
    ph::pad::fps.store(on != JNI_FALSE);
}

extern "C" JNIEXPORT jfloat JNICALL Java_com_phrecomp_ph_PhInput_nativeGameFps(JNIEnv*, jclass) {
    return jfloat(ph::pad::game_fps10.load()) / 10.0f;
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSaveState(JNIEnv*, jclass, jint slot) {
    ph::pad::state_save.store(int(slot));
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeLoadState(JNIEnv*, jclass, jint slot) {
    ph::pad::state_load.store(int(slot));
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetVideo(JNIEnv*, jclass, jboolean gpu, jint scale) {
    ph::pad::video.store((gpu != JNI_FALSE ? 1 << 8 : 0) | (int(scale) & 0xFF));
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_phrecomp_ph_PhInput_nativeGpuFailed(JNIEnv*, jclass) {
    return ph::pad::gpu_failed.load() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetFpsCorner(JNIEnv*, jclass, jint corner) {
    ph::pad::fps_corner.store(int(corner) & 3);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetStretch(JNIEnv*, jclass, jboolean on) {
    ph::pad::stretch.store(on != JNI_FALSE ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetStick(JNIEnv*, jclass, jfloat x, jfloat y) {
    ph::pad::stick_x.store(int(x * 1000.0f));
    ph::pad::stick_y.store(int(y * 1000.0f));
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetWide(JNIEnv*, jclass, jboolean on) {
    ph::pad::wide.store(on != JNI_FALSE ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeSetStickMode(JNIEnv*, jclass, jboolean on) {
    ph::pad::stick_mode.store(on != JNI_FALSE ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL Java_com_phrecomp_ph_PhInput_nativeRequestResume(JNIEnv*, jclass) {
    ph::pad::resume_done.store(false);
    ph::pad::resume_request.store(true);
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_phrecomp_ph_PhInput_nativeResumeDone(JNIEnv*, jclass) {
    return ph::pad::resume_done.load() && !ph::pad::resume_request.load() ? JNI_TRUE : JNI_FALSE;
}

namespace {

// Android drops a native program's stdout and stderr: send both, line by line, to logcat (tag "phrecomp").
struct LogcatPipe {
    LogcatPipe() {
        int fds[2];
        if (pipe(fds) != 0) return;
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[1]);
        const int in = fds[0];
        std::thread([in] {
            char buf[1024];
            size_t used = 0;
            for (;;) {
                const ssize_t n = read(in, buf + used, sizeof buf - 1 - used);
                if (n <= 0) break;
                used += size_t(n);
                size_t start = 0;
                for (size_t i = 0; i < used; i++) {
                    if (buf[i] != '\n') continue;
                    buf[i] = 0;
                    __android_log_write(ANDROID_LOG_INFO, "phrecomp", buf + start);
                    start = i + 1;
                }
                if (start == 0 && used == sizeof buf - 1) {  // a line longer than the buffer: write it as it is
                    buf[used] = 0;
                    __android_log_write(ANDROID_LOG_INFO, "phrecomp", buf);
                    used = 0;
                } else if (start > 0) {
                    for (size_t i = start; i < used; i++) buf[i - start] = buf[i];
                    used -= start;
                }
            }
        }).detach();
    }
} logcat_pipe;

}  // namespace
#endif
