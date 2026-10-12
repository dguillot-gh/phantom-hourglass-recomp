# melonDS's OpenGL renderer shaders for OpenGL ES 3.2 (the phone's GPU renderer). They are written for desktop GLSL 1.40,
# which converts ints to floats by itself; GLSL ES does not, needs default precisions and locations for fragment
# outputs (desktop melonDS binds them with glBindFragDataLocation, which ES lacks). Each ES copy is the desktop
# shader (our patched one where third_party/CMakeLists.txt patches it) with the version line replaced by an ES header,
# `layout(location = N)` on the N-th fragment output, and the exact fixes below, which fail loudly if melonDS changes.
#
# Included by third_party/CMakeLists.txt for Android builds, or run on its own to check the result
# (tools/check_gles_shaders.py validates every shader with the NDK's glslc):
#   cmake -D GLES_SRC_DIR=<melonDS>/src/OpenGL_shaders -D GLES_PATCHED_DIR=<build>/melonds_patched/OpenGL_shaders
#         -D GLES_OUT_DIR=<out> -P gles_shaders.cmake

if(CMAKE_SCRIPT_MODE_FILE)
  cmake_policy(VERSION 3.20)
endif()

set(GLES_SHADERS 2DLayerPreVS 2DLayerPreFS 2DSpritePreVS 2DSpritePreFS 2DSpriteVS 2DSpriteFS 2DCompositorVS
  2DCompositorFS FinalPassVS FinalPassFS CaptureVS CaptureFS CaptureDownscaleVS CaptureDownscaleFS 3DClearVS 3DClearFS
  3DClearBitmapVS 3DClearBitmapFS 3DRenderVS 3DRenderFS 3DFinalPassVS 3DFinalPassEdgeFS 3DFinalPassFogFS)

set(GLES_HEAD "#version 320 es
precision highp float;
precision highp int;
precision highp sampler2D;
precision highp sampler2DArray;
precision highp isampler2D;
precision highp usampler2D;
precision highp usampler2DArray;
")

# Replaces every `from` with `to` in `text` (the shader `sh`); `from` must be there.
macro(gles_fix from to)
  string(FIND "${text}" "${from}" gles_at)
  if(gles_at EQUAL -1)
    message(FATAL_ERROR "gles_shaders.cmake: ${sh}.glsl changed upstream: cannot find: ${from}")
  endif()
  string(REPLACE "${from}" "${to}" text "${text}")
endmacro()

foreach(sh ${GLES_SHADERS})
  set(in ${GLES_SRC_DIR}/${sh}.glsl)
  if(EXISTS ${GLES_PATCHED_DIR}/${sh}.glsl)
    set(in ${GLES_PATCHED_DIR}/${sh}.glsl)
  endif()
  file(READ ${in} text)
  string(REPLACE "\r\n" "\n" text "${text}")
  gles_fix("#version 140\n" "${GLES_HEAD}")

  # Fragment outputs: locations in the order declared (as melonDS binds them: oColor 0, oAttr/oFlags/oBottomColor 1).
  if(sh MATCHES "FS$")
    set(n 0)
    set(done "")
    set(rest "${text}")
    string(FIND "${rest}" "\nout " at)
    while(NOT at EQUAL -1)
      math(EXPR cut "${at} + 1")
      string(SUBSTRING "${rest}" 0 ${cut} head)
      string(SUBSTRING "${rest}" ${cut} -1 rest)
      string(APPEND done "${head}layout(location = ${n}) ")
      math(EXPR n "${n} + 1")
      string(FIND "${rest}" "\nout " at)
    endwhile()
    set(text "${done}${rest}")
  endif()

  include(${CMAKE_CURRENT_LIST_DIR}/gles_shader_fixes.cmake)
  file(WRITE ${GLES_OUT_DIR}/${sh}.glsl.tmp "${text}")
  file(COPY_FILE ${GLES_OUT_DIR}/${sh}.glsl.tmp ${GLES_OUT_DIR}/${sh}.glsl ONLY_IF_DIFFERENT)  # (a rebuild only on change)
endforeach()
