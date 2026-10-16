#include "gl_present.hpp"

#include <cstdio>
#include <cstring>

#include "font8x8_basic.h"
#include "frontend/glad/glad.h"  // (melonDS's GL loader, which the runtime's GPU renderer uses too)
#if defined(__ANDROID__) || defined(PH_GLES_TEST)
#include "gles_compat.hpp"
#endif

namespace ph {
namespace {

// The shaders' first lines: desktop GLSL 3.30, or GLSL ES 3.00 on the phone.
#ifdef __ANDROID__
constexpr bool kEs = true;
const char* kHeader = "#version 300 es\nprecision highp float;\nprecision highp sampler2D;\nprecision highp sampler2DArray;\n";
#else
constexpr bool kEs = false;
const char* kHeader = "#version 330 core\n";
#endif

const char* kVertex = R"(
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform vec2 uView;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos.x / uView.x * 2.0 - 1.0, 1.0 - aPos.y / uView.y * 2.0, 0.0, 1.0);
}
)";

// uMode 0: a DS screen (a layer of the screens' array texture); 1: a filled box; 2: text (the font's coverage).
// uSwap: the texture holds the software renderer's BGRA bytes uploaded as RGBA (ES has no GL_BGRA uploads).
const char* kFragment = R"(
in vec2 vUV;
out vec4 oColor;
uniform int uMode;
uniform sampler2DArray uTex;
uniform sampler2D uFont;
uniform float uLayer;
uniform vec4 uColor;
uniform int uSwap;
void main() {
    if (uMode == 0) {
        vec3 c = texture(uTex, vec3(vUV, uLayer)).rgb;
        oColor = vec4(uSwap == 1 ? c.bgr : c, 1.0);
    }
    else if (uMode == 1) oColor = uColor;
    else oColor = vec4(uColor.rgb, uColor.a * texture(uFont, vUV).r);
}
)";

unsigned compile(unsigned type, const char* body, std::string& error) {
    const unsigned s = glCreateShader(type);
    const char* src[2] = {kHeader, body};
    glShaderSource(s, 2, src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {};
        glGetShaderInfoLog(s, sizeof log, nullptr, log);
        error = std::string("shader: ") + log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

void set_color(int location, uint32_t argb) {
    glUniform4f(location, float((argb >> 16) & 0xFF) / 255.0f, float((argb >> 8) & 0xFF) / 255.0f,
                float(argb & 0xFF) / 255.0f, float(argb >> 24) / 255.0f);
}

}  // namespace

bool GlPresenter::init(SDL_Window* window, std::string& error) {
    window_ = window;
#ifdef __ANDROID__
    // OpenGL ES (the attributes were set before the window was made, see set_gl_attributes): 3.2 for the GPU
    // renderer, else 3.0 for the software renderer's picture.
    const int versions[][2] = {{3, 2}, {3, 0}};
#else
    set_gl_attributes();
    const int versions[][2] = {{4, 3}, {3, 3}};
#endif
    for (const auto& v : versions) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, v[0]);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, v[1]);
        if ((context_ = SDL_GL_CreateContext(window))) break;
    }
    if (!context_) {
        error = std::string(kEs ? "OpenGL ES 3.0" : "OpenGL 3.3") + " is not available: " + SDL_GetError();
        return false;
    }
    SDL_GL_MakeCurrent(window, context_);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) {
        error = "cannot load the OpenGL functions";
        return false;
    }
    SDL_GL_SetSwapInterval(0);  // (the game paces itself to the DS's 59.83 Hz)
    if (const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION))) version_ = v;
    if (const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER))) version_ += std::string(" (") + r + ")";
#if defined(__ANDROID__)
    gpu_ok_ = GLAD_GL_VERSION_3_2 && install_gles_compat();
#elif defined(PH_GLES_TEST)
    gpu_ok_ = install_gles_compat();  // (the phone's shims, on desktop GL: the gles-test build)
    std::fprintf(stderr, "video: gles-test build: the phone's ES shaders and shims\n");
#else
    gpu_ok_ = true;
#endif

    const unsigned vs = compile(GL_VERTEX_SHADER, kVertex, error);
    const unsigned fs = vs ? compile(GL_FRAGMENT_SHADER, kFragment, error) : 0;
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        error = "cannot link the display shader";
        return false;
    }
    u_view_ = glGetUniformLocation(program_, "uView");
    u_mode_ = glGetUniformLocation(program_, "uMode");
    u_layer_ = glGetUniformLocation(program_, "uLayer");
    u_color_ = glGetUniformLocation(program_, "uColor");
    u_tex_ = glGetUniformLocation(program_, "uTex");
    u_font_ = glGetUniformLocation(program_, "uFont");
    u_swap_ = glGetUniformLocation(program_, "uSwap");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 24, nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 4, reinterpret_cast<void*>(sizeof(float) * 2));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // The software renderer's two screens.
    glGenTextures(1, &soft_tex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, soft_tex_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 256, 192, 2, 0, kEs ? GL_RGBA : GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // The font: 128 characters of 8x8 in a 16 x 8 grid.
    std::vector<uint8_t> atlas(128 * 64, 0);
    for (int c = 0; c < 128; c++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++)
                if (font8x8_basic[c][y] & (1u << x)) atlas[size_t((c / 16) * 8 + y) * 128 + size_t((c % 16) * 8 + x)] = 255;
    glGenTextures(1, &font_tex_);
    glBindTexture(GL_TEXTURE_2D, font_tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 64, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // The save state slots' pictures: both screens of each, two layers a slot.
    glGenTextures(1, &thumb_tex_);
    glBindTexture(GL_TEXTURE_2D_ARRAY, thumb_tex_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 256, 192, kThumbLayers, 0, kEs ? GL_RGBA : GL_BGRA, GL_UNSIGNED_BYTE,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &read_fbo_);
    return true;
}

void GlPresenter::shutdown() {
    if (!context_) return;
    glDeleteFramebuffers(1, &read_fbo_);
    glDeleteTextures(1, &soft_tex_);
    glDeleteTextures(1, &thumb_tex_);
    glDeleteTextures(1, &font_tex_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteProgram(program_);
    SDL_GL_MakeCurrent(window_, nullptr);
    SDL_GL_DestroyContext(context_);
    context_ = nullptr;
}


void GlPresenter::begin(int width, int height) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    // The runtime's GPU renderer leaves its own state behind: set everything this draw relies on.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glUniform2f(u_view_, float(width_), float(height_));
    glUniform1i(u_tex_, 0);
    glUniform1i(u_font_, 1);
    glUniform1i(u_swap_, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void GlPresenter::quad(const Rect& r, float u0, float v0, float u1, float v1) {
    const float x0 = r.x, y0 = r.y, x1 = r.x + r.w, y1 = r.y + r.h;
    const float v[24] = {x0, y0, u0, v0, x1, y0, u1, v0, x1, y1, u1, v1,
                         x0, y0, u0, v0, x1, y1, u1, v1, x0, y1, u0, v1};
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof v, v);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void GlPresenter::screen(const ntr::Frame& frame, int layer, const Rect& where, bool smooth, bool wide) {
    glActiveTexture(GL_TEXTURE0);
    if (frame.top && frame.bottom) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, soft_tex_);
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, layer, 256, 192, 1, kEs ? GL_RGBA : GL_BGRA, GL_UNSIGNED_BYTE,
                        layer == 0 ? frame.top : frame.bottom);
    } else if (frame.gl_texture) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, frame.gl_texture);
    } else {
        return;
    }
    const int f = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, f);
    // A widened picture (the GPU renderer's wide_extra) has the screen's 256 pixels in the middle; the wide screen
    // itself is shown whole when asked for (widescreen).
    float u0 = 0, u1 = 1;
    if (frame.wide_extra > 0 && !(wide && layer == frame.wide_layer)) {
        const float total = 256.0f + 2.0f * float(frame.wide_extra);
        u0 = float(frame.wide_extra) / total;
        u1 = 1 - u0;
    }
    glUniform1i(u_mode_, 0);
    glUniform1f(u_layer_, float(layer));
    glUniform1i(u_swap_, kEs && frame.top && frame.bottom ? 1 : 0);
    glDisable(GL_BLEND);
    quad(where, u0, 0, u1, 1);
    glEnable(GL_BLEND);
    glUniform1i(u_swap_, 0);
}

void GlPresenter::fill(const Rect& where, uint32_t argb) {
    glUniform1i(u_mode_, 1);
    set_color(u_color_, argb);
    quad(where, 0, 0, 0, 0);
}

float GlPresenter::text(float x, float y, float size, const std::string& s, uint32_t argb) {
    glUniform1i(u_mode_, 2);
    set_color(u_color_, argb);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, font_tex_);
    glActiveTexture(GL_TEXTURE0);
    float cx = x;
    for (unsigned char c : s) {
        if (c >= 128) c = '?';
        const float u0 = float(c % 16) * 8.0f / 128.0f, v0 = float(c / 16) * 8.0f / 64.0f;
        quad({cx, y, size, size}, u0, v0, u0 + 8.0f / 128.0f, v0 + 8.0f / 64.0f);
        cx += size;
    }
    return cx - x;
}

void GlPresenter::set_thumbnail(int slot, const uint32_t* pixels) {
    if (slot < 0 || slot * 2 + 1 >= kThumbLayers || !pixels) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, thumb_tex_);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, slot * 2, 256, 192, 2, kEs ? GL_RGBA : GL_BGRA, GL_UNSIGNED_BYTE, pixels);
}

void GlPresenter::thumbnail(int slot, int layer, const Rect& where) {
    if (slot < 0 || slot * 2 + 1 >= kThumbLayers) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, thumb_tex_);
    glUniform1i(u_mode_, 0);
    glUniform1f(u_layer_, float(slot * 2 + layer));
    glUniform1i(u_swap_, kEs ? 1 : 0);
    quad(where, 0, 0, 1, 1);
    glUniform1i(u_swap_, 0);
}

void GlPresenter::end() {
    // OpenGL errors from the frame (the GPU renderer's included): the first few, in the log (on a phone, logcat).
    for (GLenum e; errors_logged_ < 40 && (e = glGetError()) != GL_NO_ERROR;)
        std::fprintf(stderr, "video: OpenGL error 0x%04x (%d logged)\n", unsigned(e), ++errors_logged_);
    glBindVertexArray(0);
    glUseProgram(0);
    if (!SDL_GL_SwapWindow(window_)) {
        if (errors_logged_ < 40) {
            std::fprintf(stderr, "video: SDL_GL_SwapWindow failed: %s\n", SDL_GetError());
            ++errors_logged_;
        }
    }
}

bool GlPresenter::read_screens(const ntr::Frame& frame, std::vector<uint32_t>& out, int& width, int& height) {
    if (frame.top && frame.bottom) {
        width = 256;
        height = 384;
        out.resize(size_t(width) * height);
        std::memcpy(out.data(), frame.top, sizeof(uint32_t) * 256 * 192);
        std::memcpy(out.data() + 256 * 192, frame.bottom, sizeof(uint32_t) * 256 * 192);
        return true;
    }
    if (!frame.gl_texture) return false;
    int w = 0, h = 0;
    glBindTexture(GL_TEXTURE_2D_ARRAY, frame.gl_texture);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D_ARRAY, 0, GL_TEXTURE_HEIGHT, &h);
    if (w <= 0 || h <= 0) return false;
    width = w;
    height = h * 2;
    out.resize(size_t(w) * h * 2);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fbo_);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    for (int layer = 0; layer < 2; layer++) {
        glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, frame.gl_texture, 0, layer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(0, 0, w, h, kEs ? GL_RGBA : GL_BGRA, GL_UNSIGNED_BYTE, out.data() + size_t(layer) * w * h);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (kEs)  // (RGBA bytes: to 0xAARRGGBB)
        for (uint32_t& p : out) p = (p & 0xFF00FF00u) | (p & 0xFFu) << 16 | (p >> 16 & 0xFFu);
    return true;
}

void set_gl_attributes() {
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __ANDROID__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
}

void GlPresenter::release() { SDL_GL_MakeCurrent(window_, nullptr); }

bool GlPresenter::acquire() {
    if (!SDL_GL_MakeCurrent(window_, context_)) return false;
    return SDL_GL_GetCurrentContext() == context_ && program_ && glIsProgram(program_) && vao_ && glIsVertexArray(vao_);
}

bool GlPresenter::recover(std::string& error) {
    // A context lost by Android invalidates all GL names. Abandon those names instead of
    // deleting them in the replacement context, then create a fresh presenter context.
    SDL_GL_MakeCurrent(window_, nullptr);
    context_ = nullptr;
    program_ = vao_ = vbo_ = soft_tex_ = font_tex_ = read_fbo_ = thumb_tex_ = 0;
    u_view_ = u_mode_ = u_layer_ = u_color_ = u_tex_ = u_font_ = u_swap_ = -1;
    gpu_ok_ = false;
    errors_logged_ = 0;
    version_.clear();
    return init(window_, error);
}

bool GlPresenter::can_display(const ntr::Frame& frame) const {
    if (SDL_GL_GetCurrentContext() != context_ || !program_ || !glIsProgram(program_) || !vao_ || !glIsVertexArray(vao_))
        return false;
    return !frame.gl_texture || glIsTexture(frame.gl_texture);
}

}  // namespace ph
