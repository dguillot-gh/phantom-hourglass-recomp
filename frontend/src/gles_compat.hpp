// The phone's GPU renderer: melonDS's OpenGL renderer is written for desktop OpenGL 3.2; on a phone it runs on
// OpenGL ES 3.2, through melonDS's own GL loader (glad), which loads the ES functions under the same names. Its
// shaders are ES copies made at build time (third_party/gles_shaders.cmake). What is left is done here, once the ES
// context is current and glad has loaded: the ES 3.0+ functions glad loads only for desktop GL 4.x, shims for the
// desktop-only calls melonDS makes (glDrawBuffer, glClearDepth, glDepthRange, glMapBuffer, glBindFragDataLocation),
// and the DS colour format (GL_UNSIGNED_SHORT_1_5_5_5_REV, desktop only) converted on upload and read back.
#pragma once

namespace ph {

// False if the context lacks something the GPU renderer needs (the software renderer still works).
bool install_gles_compat();

}  // namespace ph
