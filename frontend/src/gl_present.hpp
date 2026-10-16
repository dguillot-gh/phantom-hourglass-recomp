// The display: OpenGL on the game's thread (OpenGL ES on the phone). Shows the DS screens from either renderer (the software one's pictures,
// uploaded, or the GPU renderer's texture at 2x-8x), plus filled boxes and 8x8 text for the menu and the FPS counter.
// Every call is made on the thread that called init() (the game thread: the runtime's GPU renderer needs the context
// current there).
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

#include "layout.hpp"
#include "ntr/runtime.hpp"

namespace ph {

class GlPresenter {
public:
    // Creates the window's OpenGL context (4.3 core, else 3.3 core) on the calling thread and loads its functions.
    bool init(SDL_Window* window, std::string& error);
    void shutdown();
    // The OpenGL version in use, e.g. "4.6.0 NVIDIA ...".
    std::string version() const { return version_; }
    // The runtime's GPU renderer can run on this context (on a phone: OpenGL ES 3.2, see gles_compat.hpp).
    bool gpu_ok() const { return gpu_ok_; }
    // The phone in the background: its window's surface goes, so the context is let go meanwhile.
    void release();
    bool acquire();
    // Rebuild the presenter after Android replaces or loses its EGL context.
    bool recover(std::string& error);
    bool can_display(const ntr::Frame& frame) const;

    // One displayed frame: begin, draw, end (shows it).
    void begin(int width, int height);  // window pixels
    // Layer 0 top, 1 bottom. `wide`: a widescreen picture's wide screen is shown whole (else its middle 256 pixels).
    void screen(const ntr::Frame& frame, int layer, const Rect& where, bool smooth, bool wide = false);
    void fill(const Rect& where, uint32_t argb);
    // 8x8 characters at `size` pixels each; returns the width drawn.
    float text(float x, float y, float size, const std::string& s, uint32_t argb);
    void end();

    // Save state pictures: both screens (256 x 384, 0xAARRGGBB, top then bottom) of slot 0-11, kept in a texture;
    // thumbnail() draws one screen of one (layer 0 top, 1 bottom).
    void set_thumbnail(int slot, const uint32_t* pixels);
    void thumbnail(int slot, int layer, const Rect& where);

    // Both screens of `frame`, stacked (top then bottom), at the renderer's resolution, as 0xAARRGGBB rows.
    bool read_screens(const ntr::Frame& frame, std::vector<uint32_t>& out, int& width, int& height);

private:
    void quad(const Rect& r, float u0, float v0, float u1, float v1);

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::string version_;
    static constexpr int kThumbLayers = 24;
    unsigned program_ = 0, vao_ = 0, vbo_ = 0, soft_tex_ = 0, font_tex_ = 0, read_fbo_ = 0, thumb_tex_ = 0;
    int u_view_ = -1, u_mode_ = -1, u_layer_ = -1, u_color_ = -1, u_tex_ = -1, u_font_ = -1;
    int u_swap_ = -1;
    int width_ = 1, height_ = 1;
    bool gpu_ok_ = false;
    int errors_logged_ = 0;
};

// The GL context attributes, set before the window is made (Android picks the surface's format then).
void set_gl_attributes();

}  // namespace ph
