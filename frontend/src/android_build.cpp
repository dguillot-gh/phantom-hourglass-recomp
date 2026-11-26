// The shell APK's game builder, called from the launcher (GameBuilder.java) on a worker thread: see game_builder.hpp.
#include <jni.h>

#include <atomic>
#include <string>

#include "game_builder.hpp"

static std::atomic<int> g_progress{0};

static std::string str(JNIEnv* env, jstring s) {
    const char* c = env->GetStringUTFChars(s, nullptr);
    std::string r(c);
    env->ReleaseStringUTFChars(s, c);
    return r;
}

extern "C" JNIEXPORT jstring JNICALL Java_com_phrecomp_ph_GameBuilder_build(JNIEnv* env, jclass, jstring rom,
                                                                           jstring assets, jstring out, jstring work,
                                                                           jstring keep_c, jboolean compile) {
    const std::string err = ph::build_game(str(env, rom), str(env, assets), str(env, out), str(env, work), g_progress,
                                           keep_c ? str(env, keep_c) : std::string(), compile != JNI_FALSE);
    return err.empty() ? nullptr : env->NewStringUTF(err.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_com_phrecomp_ph_GameBuilder_progress(JNIEnv*, jclass) { return g_progress; }
