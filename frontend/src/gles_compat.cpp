// OpenGL ES 3.2 under melonDS's desktop OpenGL renderer (the phone's GPU renderer; see gles_compat.hpp).
#include "gles_compat.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <vector>

#include "frontend/glad/glad.h"

namespace ph {
namespace {

// The ES functions the shims call, and the originals they wrap.
PFNGLCLEARDEPTHFPROC es_clear_depthf;
PFNGLDEPTHRANGEFPROC es_depth_rangef;
PFNGLTEXIMAGE2DPROC real_tex_image_2d;
PFNGLTEXSUBIMAGE2DPROC real_tex_sub_image_2d;
PFNGLTEXIMAGE3DPROC real_tex_image_3d;
PFNGLTEXSUBIMAGE3DPROC real_tex_sub_image_3d;
PFNGLREADPIXELSPROC real_read_pixels;

// melonDS's DS colours are GL_UNSIGNED_SHORT_1_5_5_5_REV (red in bits 0-4, alpha in bit 15), which ES lacks; ES has
// GL_UNSIGNED_SHORT_5_5_5_1 (red in bits 11-15, alpha in bit 0).
std::vector<uint16_t> to_5551(const void* data, size_t count) {
    const auto* in = static_cast<const uint16_t*>(data);
    std::vector<uint16_t> out(count);
    for (size_t i = 0; i < count; i++) {
        const uint16_t v = in[i];
        out[i] = uint16_t((v & 0x1F) << 11 | ((v >> 5) & 0x1F) << 6 | ((v >> 10) & 0x1F) << 1 | (v >> 15));
    }
    return out;
}

void APIENTRY tex_image_2d(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border,
                               GLenum format, GLenum type, const void* data) {
    if (type != GL_UNSIGNED_SHORT_1_5_5_5_REV) return real_tex_image_2d(target, level, internal, w, h, border, format, type, data);
    if (!data) return real_tex_image_2d(target, level, internal, w, h, border, format, GL_UNSIGNED_SHORT_5_5_5_1, nullptr);
    const std::vector<uint16_t> px = to_5551(data, size_t(w) * size_t(h));
    real_tex_image_2d(target, level, internal, w, h, border, format, GL_UNSIGNED_SHORT_5_5_5_1, px.data());
}

void APIENTRY tex_sub_image_2d(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format,
                                   GLenum type, const void* data) {
    if (type != GL_UNSIGNED_SHORT_1_5_5_5_REV || !data) return real_tex_sub_image_2d(target, level, x, y, w, h, format, type, data);
    const std::vector<uint16_t> px = to_5551(data, size_t(w) * size_t(h));
    real_tex_sub_image_2d(target, level, x, y, w, h, format, GL_UNSIGNED_SHORT_5_5_5_1, px.data());
}

void APIENTRY tex_image_3d(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLsizei d, GLint border,
                               GLenum format, GLenum type, const void* data) {
    if (type != GL_UNSIGNED_SHORT_1_5_5_5_REV) return real_tex_image_3d(target, level, internal, w, h, d, border, format, type, data);
    if (!data) return real_tex_image_3d(target, level, internal, w, h, d, border, format, GL_UNSIGNED_SHORT_5_5_5_1, nullptr);
    const std::vector<uint16_t> px = to_5551(data, size_t(w) * size_t(h) * size_t(d));
    real_tex_image_3d(target, level, internal, w, h, d, border, format, GL_UNSIGNED_SHORT_5_5_5_1, px.data());
}

void APIENTRY tex_sub_image_3d(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w, GLsizei h, GLsizei d,
                                   GLenum format, GLenum type, const void* data) {
    if (type != GL_UNSIGNED_SHORT_1_5_5_5_REV || !data)
        return real_tex_sub_image_3d(target, level, x, y, z, w, h, d, format, type, data);
    const std::vector<uint16_t> px = to_5551(data, size_t(w) * size_t(h) * size_t(d));
    real_tex_sub_image_3d(target, level, x, y, z, w, h, d, format, GL_UNSIGNED_SHORT_5_5_5_1, px.data());
}

// The display capture's read back to VRAM: ES always reads RGBA bytes; melonDS wants 1_5_5_5_REV.
void APIENTRY read_pixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data) {
    if (type != GL_UNSIGNED_SHORT_1_5_5_5_REV || format != GL_RGBA) return real_read_pixels(x, y, w, h, format, type, data);
    std::vector<uint8_t> rgba(size_t(w) * size_t(h) * 4);
    real_read_pixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    auto* out = static_cast<uint16_t*>(data);
    for (size_t i = 0; i < size_t(w) * size_t(h); i++) {
        const uint8_t* p = &rgba[i * 4];
        out[i] = uint16_t((p[0] >> 3) | (p[1] >> 3) << 5 | (p[2] >> 3) << 10 | (p[3] >= 128 ? 0x8000 : 0));
    }
}

void APIENTRY draw_buffer(GLenum buf) { glad_glDrawBuffers(1, &buf); }
void APIENTRY clear_depth(GLdouble d) { es_clear_depthf(GLfloat(d)); }
void APIENTRY depth_range(GLdouble n, GLdouble f) { es_depth_rangef(GLfloat(n), GLfloat(f)); }
void* APIENTRY map_buffer(GLenum target, GLenum access) {
    GLint size = 0;
    glad_glGetBufferParameteriv(target, GL_BUFFER_SIZE, &size);
    const GLbitfield bits = access == GL_READ_ONLY ? GL_MAP_READ_BIT
                          : access == GL_WRITE_ONLY ? GL_MAP_WRITE_BIT : GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
    return glad_glMapBufferRange(target, 0, size, bits);
}
// (The fragment outputs have their locations in the ES shaders: third_party/gles_shaders.cmake.)
void APIENTRY bind_frag_data_location(GLuint, GLuint, const GLchar*) {}

template <class F>
void load(F& fn, const char* name) {
    if (!fn) fn = reinterpret_cast<F>(SDL_GL_GetProcAddress(name));
}

}  // namespace

bool install_gles_compat() {
    // ES 3.0-3.2 functions that glad loads only for desktop GL 4.x.
    load(glad_glTexStorage2D, "glTexStorage2D");
    load(glad_glTexStorage3D, "glTexStorage3D");
    load(glad_glProgramBinary, "glProgramBinary");
    load(glad_glGetProgramBinary, "glGetProgramBinary");
    load(glad_glProgramParameteri, "glProgramParameteri");
    load(es_clear_depthf, "glClearDepthf");
    load(es_depth_rangef, "glDepthRangef");
    // (Sampler objects, ES 3.0: the 3D's textures repeat / mirror / clamp through them, third_party/CMakeLists.txt.)
    load(glad_glGenSamplers, "glGenSamplers");
    load(glad_glBindSampler, "glBindSampler");
    load(glad_glSamplerParameteri, "glSamplerParameteri");
    if (!glad_glGenSamplers || !glad_glBindSampler || !glad_glSamplerParameteri ||!es_clear_depthf || !es_depth_rangef || !glad_glTexStorage3D || !glad_glDrawBuffers || !glad_glMapBufferRange ||
        !glad_glFramebufferTexture || !glad_glColorMaski) {
        std::fprintf(stderr, "video: this OpenGL ES lacks functions the GPU renderer needs\n");
        return false;
    }
    // Desktop-only functions melonDS calls, done with ES ones.
    glad_glDrawBuffer = draw_buffer;
    glad_glClearDepth = clear_depth;
    glad_glDepthRange = depth_range;
    glad_glMapBuffer = map_buffer;
    glad_glBindFragDataLocation = bind_frag_data_location;
    real_tex_image_2d = glad_glTexImage2D;
    real_tex_sub_image_2d = glad_glTexSubImage2D;
    real_tex_image_3d = glad_glTexImage3D;
    real_tex_sub_image_3d = glad_glTexSubImage3D;
    real_read_pixels = glad_glReadPixels;
    glad_glTexImage2D = tex_image_2d;
    glad_glTexSubImage2D = tex_sub_image_2d;
    glad_glTexImage3D = tex_image_3d;
    glad_glTexSubImage3D = tex_sub_image_3d;
    glad_glReadPixels = read_pixels;
    return true;
}

}  // namespace ph
