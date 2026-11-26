#!/usr/bin/env bash
# clang + lld that run ON an Android phone (arm64), for the optimised on-device game build (GameBuilder.java).
# One multicall binary, AArch64 target only ("llvm clang ...", "llvm ld.lld ..."), packed into the APK as
# jniLibs/arm64-v8a/libllvm.so: Android only lets an app run programs from its native library folder.
# No LLVM headers are needed on the phone: the game's C includes only tools/ondevice + TinyCC's include/.
#
# Linux host (CI: .github/workflows/windows-exe.yml, job device-llvm). Needs cmake, ninja, a host C++ compiler, and
# the NDK in $ANDROID_NDK (or $ANDROID_HOME/ndk/<NDK_VER>). About 1.5-2 hours on a 4-core runner; done once per
# LLVM version (CI keeps the result as a GitHub Release asset, see LLVM_TAG).
#   bash tools/ondevice/build_device_llvm.sh <out dir>      -> <out dir>/llvm
set -euo pipefail
LLVM_VER=20.1.8
NDK_VER=28.2.13676358
OUT="$(mkdir -p "${1:?usage: build_device_llvm.sh <out dir>}" && cd "$1" && pwd)"
WORK="${WORK:-$PWD/build/device-llvm}"
NDK="${ANDROID_NDK:-$ANDROID_HOME/ndk/$NDK_VER}"
JOBS="${JOBS:-$(nproc)}"
SRC="$WORK/llvm-project-$LLVM_VER.src"
mkdir -p "$WORK"

if [ ! -f "$SRC/llvm/CMakeLists.txt" ]; then
  curl -sfL -o "$WORK/llvm.tar.xz" \
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-$LLVM_VER/llvm-project-$LLVM_VER.src.tar.xz"
  tar -C "$WORK" -xJf "$WORK/llvm.tar.xz" "llvm-project-$LLVM_VER.src/"{llvm,clang,lld,cmake,third-party,libunwind/include}
  rm "$WORK/llvm.tar.xz"
fi

COMMON=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DLLVM_TARGETS_TO_BUILD=AArch64
  -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF
  -DCLANG_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_DOCS=OFF -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF
  -DLLVM_ENABLE_LIBXML2=OFF -DLLVM_ENABLE_BINDINGS=OFF -DCLANG_ENABLE_ARCMT=OFF
  -DCLANG_ENABLE_STATIC_ANALYZER=OFF -DCLANG_PLUGIN_SUPPORT=OFF -DLLVM_ENABLE_TERMINFO=OFF -DLLVM_ENABLE_LIBEDIT=OFF
  -DLLVM_APPEND_VC_REV=OFF)

# 1. tablegen for the build machine
NATIVE="$WORK/native"
if [ ! -x "$NATIVE/bin/clang-tblgen" ]; then
  cmake -S "$SRC/llvm" -B "$NATIVE" "${COMMON[@]}" -DLLVM_ENABLE_PROJECTS=clang
  cmake --build "$NATIVE" -j "$JOBS" --target llvm-tblgen llvm-min-tblgen clang-tblgen
fi

# 2. the phone build: one `llvm` multicall binary with clang and lld (no shared libraries: c++_static)
CROSS="$WORK/cross"
cmake -S "$SRC/llvm" -B "$CROSS" "${COMMON[@]}" -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=26 \
  -DANDROID_STL=c++_static -DLLVM_HOST_TRIPLE=aarch64-linux-android26 -DLLVM_DEFAULT_TARGET_TRIPLE=aarch64-linux-android26 \
  -DLLVM_NATIVE_TOOL_DIR="$NATIVE/bin" -DLLVM_TABLEGEN="$NATIVE/bin/llvm-tblgen" -DCLANG_TABLEGEN="$NATIVE/bin/clang-tblgen" \
  -DLLVM_TOOL_LLVM_DRIVER_BUILD=ON -DLLVM_BUILD_TOOLS=OFF -DLLVM_BUILD_UTILS=OFF \
  -DLLVM_ENABLE_PIC=ON -DLLVM_ENABLE_THREADS=ON -DCLANG_ENABLE_OBJC_REWRITER=OFF \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -s"
cmake --build "$CROSS" -j "$JOBS" --target llvm-driver
cp "$CROSS/bin/llvm" "$OUT/llvm"
"$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "$OUT/llvm"
echo "LLVM $LLVM_VER, AArch64, built by tools/ondevice/build_device_llvm.sh" > "$OUT/VERSION.txt"
ls -la "$OUT/llvm"
