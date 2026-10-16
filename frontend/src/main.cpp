// The Legend of Zelda: Phantom Hourglass -- native recomp, the program (PC and Android).
//
// One window showing one DS screen at a time (the touch screen by default), scaled to fit at the DS's aspect ratio
// (black bars, never cropped). The mouse or a finger is the stylus. Microphone: hold M (or click the right stick) to
// blow into it. Lid: hold C to close the DS. F1 or Esc opens the menu (PC); the Android app has its own (PhMenu).
// Save states: 12 slots in <data>/states (slot-NN.state, .png both screens when taken, .txt when); F5 saves to the slot
// chosen in the menu, F8 loads it. The cartridge save (the game's own saving) is never touched by them.
// Continue: quitting (and on the phone, going to the background) first writes a "resume" state (resume.state, .png,
// .txt in the same folder); --continue, the PC menu's Continue or F9 loads it. When the game saves itself later in a
// session the resume state is older than the save, so it is deleted (and written again at the next quit).
//
// The game runs on a thread of its own, which also draws, with OpenGL (gl_present.cpp; OpenGL ES on the phone): the
// runtime's GPU renderer (2x-8x) needs the context current on the thread that runs the game. The main thread handles
// the window, input, the menu's input and the save file. A phone without OpenGL ES 3.0 falls back to SDL's renderer
// on the main thread (software renderer only).
//
//   PhantomHourglass.exe <rom.nds> [--save-file F] [--data-dir D] [--input-script S] [--exit-after-frames N]
//                        [--fullscreen] [--unpaced] [--module M] [--read-only-save] [--renderer software|gpu]
//                        [--scale 2|4|6|8] [--continue] [--wide]
#include <SDL3/SDL.h>
#ifdef __ANDROID__
#include <SDL3/SDL_main.h>  // (SDL's Android activity calls SDL_main in libmain.so)
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "game_module.hpp"
#include "layout.hpp"
#include "ntr/rom.hpp"
#include "ntr/runtime.hpp"
#include "pad_input.hpp"
#include "ph_stick.hpp"
#include "ph_hooks.hpp"
#include "png.hpp"
#include "gl_present.hpp"
#ifndef __ANDROID__
#include "menu.hpp"
#endif

namespace fs = std::filesystem;
using ph::Layout;

namespace ph::pad {
std::atomic<uint32_t> held{0};
std::atomic<bool> blow{false};
std::atomic<bool> lid{false};
std::atomic<int> show{-1};
std::atomic<int> smooth{-1};
std::atomic<bool> fps{false};
std::atomic<int> fps_corner{1};
std::atomic<int> stretch{-1};
std::atomic<int> game_fps10{0};
std::atomic<int> state_save{0};
std::atomic<int> state_load{0};
std::atomic<int> video{-1};
std::atomic<bool> gpu_failed{false};
std::atomic<bool> resume_request{false};
std::atomic<int> stick_x{0}, stick_y{0};
std::atomic<int> stick_mode{-1};
std::atomic<int> wide{-1};
std::atomic<bool> resume_done{false};
}  // namespace ph::pad

namespace {

constexpr char kTitle[] = "The Legend of Zelda: Phantom Hourglass";
constexpr int kW = 256, kH = 192;
constexpr double kFrameSeconds = 560190.0 / 33513982.0;  // one DS frame (59.8261 Hz)
constexpr int kScales[] = {2, 4, 6, 8};
constexpr int kSlots = 12;  // save state slots
constexpr int kWideExtra = 43;  // widescreen (a test): 256 + 2 x 43 = 342 pixels across 192, about 16:9
constexpr int kResumeSlot = 99;  // the state written at quit, for Continue (resume.*)
constexpr uint64_t kResumeStaleFrames = 1800;  // a cartridge save after this much play makes the resume state old

// ---- save state slots: <dir>/slot-NN.state, .png (both screens, 256 x 384) and .txt (when, "MM-DD HH:MM") ----------
fs::path slot_file(const fs::path& dir, int slot, const char* ext) {
    char name[32];
    if (slot == kResumeSlot) std::snprintf(name, sizeof name, "resume.%s", ext);
    else std::snprintf(name, sizeof name, "slot-%02d.%s", slot, ext);
    return dir / name;
}

// When the resume state was written ("" = there is none).
std::string resume_time(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(slot_file(dir, kResumeSlot, "state"), ec)) return {};
    std::ifstream f(slot_file(dir, kResumeSlot, "txt"));
    std::string t;
    if (!std::getline(f, t) || t.empty()) t = "saved";
    return t;
}

// When each slot was saved ("" = empty).
[[maybe_unused]] std::array<std::string, kSlots> slot_times(const fs::path& dir) {  // (the PC menu)
    std::array<std::string, kSlots> t;
    for (int i = 0; i < kSlots; i++) {
        std::error_code ec;
        if (!fs::exists(slot_file(dir, i + 1, "state"), ec)) continue;
        std::ifstream f(slot_file(dir, i + 1, "txt"));
        if (!std::getline(f, t[size_t(i)]) || t[size_t(i)].empty()) t[size_t(i)] = "saved";
    }
    return t;
}

std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Writes through a temporary file, so a crash mid-write never leaves a half-written save.
bool write_file_atomic(const fs::path& p, const std::vector<uint8_t>& data) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    const fs::path tmp = p.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) return false;
    }
    fs::rename(tmp, p, ec);
    return !ec;
}

// ---- settings.ini: a few key=value lines --------------------------------------------------------------------------
struct Settings {
    Layout layout = Layout::Bottom;
    bool fullscreen = false;
    bool smooth = false;  // linear filtering instead of sharp pixels
    bool fps = false;
    bool gpu = false;     // the GPU renderer (PC)
    int scale = 4;        // its internal resolution
    int slot = 1;         // the save state slot chosen (1-12)
    int fps_corner = 1;   // the FPS counter's corner of the picture (0 top left, 1 top right, 2 bottom left, 3 bottom right)
    bool stretch = false; // the picture stretched over the whole window (else the DS's shape, with bars)
    bool stick = false;   // the analog stick moves Link (a gamepad's left stick, the phone's on-screen stick)
    bool wide = false;    // widescreen, 16:9 (a test; the GPU renderer only)
};

Settings load_settings(const fs::path& p) {
    Settings s;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "layout") s.layout = v == "top" ? Layout::Top : v == "both" ? Layout::Both : Layout::Bottom;
        else if (k == "fullscreen") s.fullscreen = v == "1";
        else if (k == "smooth") s.smooth = v == "1";
        else if (k == "fps") s.fps = v == "1";
        else if (k == "renderer") s.gpu = v == "gpu";
        else if (k == "scale") s.scale = std::atoi(v.c_str());
        else if (k == "slot") s.slot = std::clamp(std::atoi(v.c_str()), 1, kSlots);
        else if (k == "fps_corner") s.fps_corner = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "stretch") s.stretch = v == "1";
        else if (k == "stick") s.stick = v == "1";
        else if (k == "wide") s.wide = v == "1";
    }
    if (std::find(std::begin(kScales), std::end(kScales), s.scale) == std::end(kScales)) s.scale = 4;
    return s;
}

void save_settings(const fs::path& p, const Settings& s) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream f(p, std::ios::trunc);
    f << "# Phantom Hourglass recomp settings\n";
    f << "layout=" << (s.layout == Layout::Top ? "top" : s.layout == Layout::Both ? "both" : "bottom") << "\n";
    f << "fullscreen=" << (s.fullscreen ? 1 : 0) << "\n";
    f << "smooth=" << (s.smooth ? 1 : 0) << "\n";
    f << "fps=" << (s.fps ? 1 : 0) << "\n";
    f << "renderer=" << (s.gpu ? "gpu" : "software") << "\n";
    f << "scale=" << s.scale << "\n";
    f << "slot=" << s.slot << "\n";
    f << "fps_corner=" << s.fps_corner << "\n";
    f << "stretch=" << (s.stretch ? 1 : 0) << "\n";
    f << "stick=" << (s.stick ? 1 : 0) << "\n";
    f << "wide=" << (s.wide ? 1 : 0) << "\n";
}

ntr::VideoSettings video_for(const Settings& s) {
    ntr::VideoSettings v;
    v.renderer = s.gpu ? ntr::Renderer::OpenGL : ntr::Renderer::Software;
    v.scale = s.gpu ? s.scale : 1;
    v.wide_extra = s.gpu && s.wide ? kWideExtra : 0;  // (the game's projection follows: ph::set_wide_extra)
    v.hud_edges = v.wide_extra > 0;  // the HUD at the wide picture's edges (touches on it are mapped back: to_ds)
    return v;
}

// ---- --input-script: scripted input and screenshots for unattended tests --------------------------------------------
//   <frame> press A+Up [frames]   <frame> touch x,y [frames]   <frame> blow [frames]   <frame> shot file.png
//   <frame> savestate N   <frame> loadstate N   <frame> quit
struct ScriptStep {
    uint64_t frame = 0, frames = 4;
    uint32_t buttons = 0;
    int tx = -1, ty = -1;
    std::string shot;
    bool quit = false;
    bool blow = false;  // the microphone hears blowing
    int save_slot = 0, load_slot = 0;  // savestate N / loadstate N
};

uint32_t button_bit(std::string name) {
    for (char& c : name) c = char(std::tolower(static_cast<unsigned char>(c)));
    static const std::map<std::string, uint32_t> kBits = {
        {"a", ntr::KeyA}, {"b", ntr::KeyB}, {"x", ntr::KeyX}, {"y", ntr::KeyY}, {"l", ntr::KeyL}, {"r", ntr::KeyR},
        {"start", ntr::KeyStart}, {"select", ntr::KeySelect}, {"up", ntr::KeyUp}, {"down", ntr::KeyDown},
        {"left", ntr::KeyLeft}, {"right", ntr::KeyRight}};
    const auto it = kBits.find(name);
    return it == kBits.end() ? 0 : it->second;
}

bool load_script(const fs::path& p, std::vector<ScriptStep>& out, std::string& error) {
    std::ifstream f(p);
    if (!f) return error = "cannot read " + p.string(), false;
    std::string line;
    int n = 0;
    while (std::getline(f, line)) {
        n++;
        if (const auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::istringstream ss(line);
        ScriptStep s;
        std::string cmd, arg;
        if (!(ss >> s.frame)) continue;
        ss >> cmd;
        if (cmd == "quit") {
            s.quit = true;
        } else if (cmd == "blow") {
            s.blow = true;
            uint64_t frames;
            if (ss >> frames) s.frames = frames;
        } else if (cmd == "savestate" || cmd == "loadstate") {
            int slot = 0;
            if (!(ss >> slot) || slot < 1 || slot > kSlots)
                return error = p.string() + ":" + std::to_string(n) + ": " + cmd + " needs a slot 1-12", false;
            (cmd == "savestate" ? s.save_slot : s.load_slot) = slot;
        } else if (cmd == "shot") {
            std::getline(ss >> std::ws, s.shot);
        } else if (cmd == "press" || cmd == "touch") {
            ss >> arg;
            if (cmd == "press") {
                std::stringstream parts(arg);
                std::string b;
                while (std::getline(parts, b, '+')) {
                    const uint32_t bit = button_bit(b);
                    if (!bit) return error = p.string() + ":" + std::to_string(n) + ": unknown button " + b, false;
                    s.buttons |= bit;
                }
            } else if (std::sscanf(arg.c_str(), "%d,%d", &s.tx, &s.ty) != 2) {
                return error = p.string() + ":" + std::to_string(n) + ": touch needs x,y", false;
            }
            uint64_t frames;
            if (ss >> frames) s.frames = frames;
        } else {
            return error = p.string() + ":" + std::to_string(n) + ": unknown step " + cmd, false;
        }
        out.push_back(s);
    }
    return true;
}

// ---- what the main thread and the game thread share ---------------------------------------------------------------
struct Shared {
    // Input, written by the main thread.
    std::atomic<uint32_t> held{0};
    std::atomic<uint32_t> pressed{0};  // went down since the game thread last took them (taps shorter than a frame)
    std::atomic<bool> touching{false};
    std::atomic<int> touch_x{0}, touch_y{0};
    std::atomic<bool> fast_forward{false};
    std::atomic<bool> blow{false};  // the player's "blow into the mic" input is held
    std::atomic<bool> stick_on{false};       // the analog stick moves Link (ph_stick.hpp)
    std::atomic<int> stick_x{0}, stick_y{0};  // where it is, -1000..1000 (y down)
    // Widescreen as drawn: the extra pixels each side of the wide screen (0: none), and its layer (0 top, 1 bottom).
    std::atomic<int> wide_extra{0}, wide_layer{1};
    std::mutex hud_mutex;  // the HUD clusters moved to the edges in the frame shown (ph::to_ds)
    std::array<ph::HudMove, 16> hud{};
    int hud_count = 0;
    std::atomic<bool> quit{false};
    std::atomic<bool> paused{false};  // the app is in the background (Android): the game waits
    std::atomic<bool> resume_done{false};  // the resume state asked for at quit / going to the background is written (or failed)

    // The last finished frame from the software renderer, both screens stacked (top, then bottom).
    std::mutex frame_mutex;
    std::vector<uint32_t> frame = std::vector<uint32_t>(size_t(kW) * kH * 2, 0xFF000000u);
    bool frame_new = false;

    // What the game thread draws (PC): the screens shown, the filter, the menu; and the renderer asked for.
    std::mutex view_mutex;
    Settings view;
    bool menu_open = false;
    int menu_selected = 0;
    int menu_page = 0;
    bool video_dirty = false;
    std::atomic<bool> gpu_failed{false};  // the GPU renderer could not start
    std::atomic<int> gl_display{0};       // 1: the game thread draws with OpenGL; -1: it cannot (phone fallback)
    std::atomic<bool> shot_request{false};
    fs::path shot_dir;

    // Save states: a slot to save to or load (1-12, 0 none), asked for by the main thread or the phone's menu; a line
    // of text shown over the game for a moment ("Saved to slot 3"); a count of changes to the slots' files.
    std::atomic<int> state_save{0}, state_load{0};
    fs::path state_dir;
    std::mutex note_mutex;
    std::string note;
    uint64_t note_until = 0;
    std::atomic<int> states_version{0};
    void say(const std::string& text, uint64_t ms = 2500) {
        std::fprintf(stderr, "state: %s\n", text.c_str());
        std::lock_guard<std::mutex> lock(note_mutex);
        note = text;
        note_until = SDL_GetTicks() + ms;
    }
    std::string current_note() {
        std::lock_guard<std::mutex> lock(note_mutex);
        return SDL_GetTicks() < note_until ? note : std::string();
    }

    // The cartridge save memory as the game last wrote it.
    std::mutex save_mutex;
    std::vector<uint8_t> save;
    bool save_dirty = false;

    std::atomic<int> started{0};  // 1: the game is running; -1: it could not start (start_error)
    std::string start_error;
    std::atomic<bool> game_done{false};
    int exit_code = 0;
};

class Host final : public ntr::Host {
public:
    Host(Shared& s, SDL_Window* window, SDL_AudioStream* audio, std::vector<ScriptStep> script, uint64_t exit_after,
         bool unpaced)
        : s_(s), window_(window), audio_(audio), script_(std::move(script)), exit_after_(exit_after), unpaced_(unpaced) {}

    void attach(ntr::Runtime* rt) { rt_ = rt; }
    // Continue: load the resume state as soon as the game runs. `resume`: write one when the app quits or goes to the
    // background (not in scripted test runs).
    void set_resume(bool continue_at_start, bool resume) { continue_ = continue_at_start, resume_ = resume; }
    void set_gl(ph::GlPresenter* gl) { gl_ = gl; }

    bool on_frame(const ntr::Frame& frame) override {
        const uint64_t f = ++frames_;
        last_ = frame;
        have_last_ = true;
        if (frame.top && frame.bottom) {
            std::lock_guard<std::mutex> lock(s_.frame_mutex);
            std::memcpy(s_.frame.data(), frame.top, sizeof(uint32_t) * kW * kH);
            std::memcpy(s_.frame.data() + kW * kH, frame.bottom, sizeof(uint32_t) * kW * kH);
            s_.frame_new = true;
        }
        if (gl_ && !s_.paused.load()) draw(frame);  // (in the background the window may be gone)

        // Input for the next frame: the player's, plus any script steps active then.
        // (A scripted run takes only the script's input, so it does the same thing whatever happens in the window.)
        const bool live = script_.empty();
        uint32_t held = live ? s_.held.load() : 0;
        uint32_t pressed = s_.pressed.exchange(0);
        if (!live) pressed = 0;
        bool touching = live && s_.touching.load();
        int tx = s_.touch_x.load(), ty = s_.touch_y.load();
        bool blow = live && s_.blow.load();
        if (live && s_.stick_on.load()) {  // (a finger on the touch screen comes first)
            const float sx = touching ? 0.0f : float(s_.stick_x.load()) / 1000.0f;
            const float sy = touching ? 0.0f : float(s_.stick_y.load()) / 1000.0f;
            int x = 0, y = 0;
            if (stick_.touch_for(*rt_, sx, sy, x, y)) touching = true, tx = x, ty = y;
        }
        bool quit = false;
        for (const ScriptStep& st : script_) {
            if (st.frame == f) {
                if (st.quit) quit = true;
                if (!st.shot.empty()) shot(st.shot);
                if (st.save_slot) s_.state_save = st.save_slot;
                if (st.load_slot) s_.state_load = st.load_slot;
            }
            if (f >= st.frame && f < st.frame + st.frames) {
                held |= st.buttons;
                if (f == st.frame) pressed |= st.buttons;
                if (st.tx >= 0) touching = true, tx = st.tx, ty = st.ty;
                if (st.blow) blow = true;
            }
        }
        if (s_.shot_request.exchange(false)) {
            std::error_code ec;
            fs::create_directories(s_.shot_dir, ec);
            shot((s_.shot_dir / ("shot-" + std::to_string(SDL_GetTicks()) + ".png")).string());
        }
        if (continue_ && f == 2) continue_ = false, start_load(kResumeSlot);
        if (const int slot = s_.state_save.exchange(0)) start_save(slot);
        if (const int slot = s_.state_load.exchange(0)) start_load(slot);
        ntr::set_mic_blow(blow);
        rt_->set_buttons(held, pressed);
        if (touching)
            rt_->touch(std::clamp(tx, 0, kW - 1), std::clamp(ty, 0, kH - 1));
        else
            rt_->release_touch();

        if (quit || (exit_after_ && f >= exit_after_)) return false;
        if (s_.paused.load() && resume_ && !s_.resume_done.load()) {
            // Android, going to the background (where the app may be killed): first the resume state, a frame or
            // two more of the game (not drawn, without input), then the wait below.
            if (!save_slot_) start_save(kResumeSlot);
            if (save_slot_ == kResumeSlot) {
                pace();
                return !s_.quit.load();
            }
        }
        if (s_.paused.load()) {  // (Android: the window's surface goes in the background)
            if (gl_) gl_->release();
            while (s_.paused.load() && !s_.quit.load()) SDL_Delay(50);
            if (gl_) gl_->acquire();
        }
        pace();
        return !s_.quit.load();
    }

    void on_audio(const int16_t* samples, size_t frames) override {
        if (!audio_ || unpaced_ || s_.fast_forward.load()) return;
        // Keep at most ~100 ms queued: if the output falls behind (a hitch), drop rather than lag.
        if (SDL_GetAudioStreamQueued(audio_) > 48000 * 4 / 10) return;
        SDL_PutAudioStreamData(audio_, samples, int(frames * 2 * sizeof(int16_t)));
    }

    void on_save(const uint8_t* data, size_t size) override {
        {
            std::lock_guard<std::mutex> lock(s_.save_mutex);
            s_.save.assign(data, data + size);
            s_.save_dirty = true;
        }
        // The game saved itself after a while of play: a resume state from before is older than the save now, and
        // continuing from it would go back before that save. (A save right at the start is not that.)
        if (frames_ > kResumeStaleFrames && !resume_cleared_) {
            resume_cleared_ = true;
            std::error_code ec;
            for (const char* ext : {"state", "png", "txt"}) fs::remove(slot_file(s_.state_dir, kResumeSlot, ext), ec);
            s_.states_version++;
        }
    }

    void on_state_saved(std::vector<uint8_t> state) override {
        const int slot = save_slot_;
        save_slot_ = 0;
        if (!slot) return;
        if (slot == kResumeSlot) {
            // Quitting goes on, written or not. (The main thread ends the game only after this returns.)
            s_.resume_done = true;
            if (state.empty()) return (void)std::fprintf(stderr, "state: no moment for the resume state\n");
        }
        if (resume_pending_) resume_pending_ = false, s_.state_save = kResumeSlot;  // (asked for during this one)
        if (state.empty()) return s_.say("Could not save now: try again in a moment");
        if (!write_file_atomic(slot_file(s_.state_dir, slot, "state"), state))
            return s_.say("Could not write slot " + std::to_string(slot));
        if (!thumb_.empty()) ph::write_png(slot_file(s_.state_dir, slot, "png").string(), thumb_.data(), kW, kH * 2);
        const std::time_t now = std::time(nullptr);
        char when[32] = "saved";
        if (const std::tm* t = std::localtime(&now)) std::strftime(when, sizeof when, "%m-%d %H:%M", t);
        std::ofstream(slot_file(s_.state_dir, slot, "txt"), std::ios::trunc) << when << "\n";
        s_.states_version++;
        if (slot == kResumeSlot) {
            resume_cleared_ = false;  // (a new one: the next cartridge save makes it old again)
            return (void)std::fprintf(stderr, "state: resume state written (for Continue)\n");
        }
        s_.say("Saved to slot " + std::to_string(slot));
    }

    void on_state_loaded(bool ok, const std::string& error) override {
        const int slot = load_slot_;
        load_slot_ = 0;
        if (slot == kResumeSlot) return s_.say(ok ? "Continued from where you left off" : "Could not continue: " + error);
        s_.say(ok ? "Loaded slot " + std::to_string(slot) : "Slot " + std::to_string(slot) + " not loaded: " + error);
    }

private:
    // A save state of the frame being shown: its picture now, the state itself when the game gives a moment.
    void start_save(int slot) {
        if (save_slot_) {  // (one at a time; the resume state right after)
            if (slot == kResumeSlot) resume_pending_ = true;
            return;
        }
        std::vector<uint32_t> pixels;
        int w = kW, h = kH * 2;
        screens(pixels, w, h);
        thumb_.assign(size_t(kW) * kH * 2, 0xFF000000u);  // at the DS's resolution whatever the renderer's
        if (w > 0 && h > 0 && pixels.size() >= size_t(w) * size_t(h))
            for (int y = 0; y < kH * 2; y++)
                for (int x = 0; x < kW; x++)
                    thumb_[size_t(y) * kW + size_t(x)] = pixels[size_t(y * h / (kH * 2)) * size_t(w) + size_t(x * w / kW)];
        save_slot_ = slot;
        std::fprintf(stderr, "state: saving to slot %d (at the game's next moment)\n", slot);
        rt_->request_save_state();
    }

    void start_load(int slot) {
        if (load_slot_) return;
        std::vector<uint8_t> data = read_file(slot_file(s_.state_dir, slot, "state"));
        if (data.empty())
            return s_.say(slot == kResumeSlot ? std::string("Nothing to continue from")
                                              : "Slot " + std::to_string(slot) + " is empty");
        load_slot_ = slot;
        std::fprintf(stderr, "state: loading slot %d (%zu KB)\n", slot, data.size() / 1024);
        rt_->request_load_state(std::move(data));
    }

    // Both screens of the frame being shown, stacked (at the renderer's resolution).
    void screens(std::vector<uint32_t>& pixels, int& w, int& h) {
        if (gl_ && have_last_ && gl_->read_screens(last_, pixels, w, h)) return;
        std::lock_guard<std::mutex> lock(s_.frame_mutex);
        pixels = s_.frame;
        w = kW, h = kH * 2;
    }

    // Both screens of the frame being shown, stacked, as a PNG (at the renderer's resolution).
    void shot(const std::string& path) {
        std::vector<uint32_t> pixels;
        int w = kW, h = kH * 2;
        screens(pixels, w, h);
        if (!ph::write_png(path, pixels.data(), w, h)) std::fprintf(stderr, "screenshot: cannot write %s\n", path.c_str());
    }

    // Shows the frame (and the FPS counter, a note, the PC's menu) and applies a renderer change asked for in the menu.
    void draw(const ntr::Frame& frame) {
        Settings view;
        bool menu_open, video_dirty;
        int selected, page;
        {
            std::lock_guard<std::mutex> lock(s_.view_mutex);
            view = s_.view;
            menu_open = s_.menu_open;
            selected = s_.menu_selected;
            page = s_.menu_page;
            (void)selected, (void)page;
            video_dirty = s_.video_dirty;
            s_.video_dirty = false;
        }
        if (video_dirty) {
            const ntr::VideoSettings vs = video_for(view);
            ph::set_wide_extra(vs.wide_extra);
            rt_->set_video(vs);
            video_asked_ = frames_;
        }
        // A GPU renderer that could not start falls back to software: say so in the menu. (The runtime switches
        // between frames, so until a couple of frames after a change it still reports the renderer it had.)
        if (view.gpu && rt_->video().renderer == ntr::Renderer::Software && frames_ > video_asked_ + 2)
            s_.gpu_failed = true;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        gl_->begin(w, h);
        const int extra = view.wide ? frame.wide_extra : 0;  // (0 until the renderer has made the change)
        s_.wide_extra = extra;
        s_.wide_layer = frame.wide_layer;
        {
            std::lock_guard<std::mutex> lock(s_.hud_mutex);
            s_.hud_count = extra ? frame.hud_moves : 0;
            for (int i = 0; i < s_.hud_count; i++) {
                const auto& m = frame.hud_move[i];
                s_.hud[size_t(i)] = {m.x0, m.y0, m.x1, m.y1, m.dx};
            }
        }
        const ph::Views v = ph::compute_views(float(w), float(h), view.layout, view.stretch, extra, frame.wide_layer);
        if (v.top_shown) gl_->screen(frame, 0, v.top, view.smooth, extra > 0);
        if (v.bottom_shown) gl_->screen(frame, 1, v.bottom, view.smooth, extra > 0);
        // Frames per second: the game's frames shown, counted over a second.
        fps_frames_++;
        if (const uint64_t now = SDL_GetTicks(); now - fps_since_ >= 1000) {
            ph::pad::game_fps10 = int(fps_frames_ * 10000 / (now - fps_since_));
            fps_frames_ = 0;
            fps_since_ = now;
        }
        const float ui = std::max(1.0f, SDL_GetWindowDisplayScale(window_));
        if (ph::pad::fps.load()) {  // (the PC menu, F4 and the phone menu all set it)
            char text[16];
            std::snprintf(text, sizeof text, "%d.%d", ph::pad::game_fps10.load() / 10, ph::pad::game_fps10.load() % 10);
            // In a corner of the picture (not of the window: on the phone its bars hold the controls).
            ph::Rect pic = v.top_shown ? v.top : v.bottom;
            if (v.top_shown && v.bottom_shown) {
                const float x1 = std::max(v.top.x + v.top.w, v.bottom.x + v.bottom.w);
                const float y1 = std::max(v.top.y + v.top.h, v.bottom.y + v.bottom.h);
                pic.x = std::min(v.top.x, v.bottom.x), pic.y = std::min(v.top.y, v.bottom.y);
                pic.w = x1 - pic.x, pic.h = y1 - pic.y;
            }
            const float size = 12 * ui, bw = size * float(std::strlen(text)) + 8 * ui, bh = size + 8 * ui;
            const int corner = ph::pad::fps_corner.load();
            const float bx = (corner & 1) ? pic.x + pic.w - bw - 4 * ui : pic.x + 4 * ui;
            const float by = (corner & 2) ? pic.y + pic.h - bh - 4 * ui : pic.y + 4 * ui;
            gl_->fill({bx, by, bw, bh}, 0xA0000000);
            gl_->text(bx + 4 * ui, by + 4 * ui, size, text, 0xFFFFFFFF);
        }
        if (const std::string note = s_.current_note(); !note.empty()) {  // "Saved to slot 3"
            const float size = 12 * ui, tw = size * float(note.size());
            const float x = std::max(4 * ui, (float(w) - tw) / 2);
            gl_->fill({x - 8 * ui, float(h) - size - 28 * ui, tw + 16 * ui, size + 16 * ui}, 0xC0000000);
            gl_->text(x, float(h) - size - 20 * ui, size, note, 0xFFFFFFFF);
        }
#ifndef __ANDROID__
        if (menu_open) {
            // The slots' pictures, read again when a slot changed.
            if (const int version = s_.states_version.load(); version != thumbs_version_) {
                thumbs_version_ = version;
                times_ = slot_times(s_.state_dir);
                resume_time_ = resume_time(s_.state_dir);
                for (int i = 0; i < kSlots; i++) {
                    std::vector<uint32_t> px;
                    int tw = 0, th = 0;
                    if (!times_[size_t(i)].empty() && ph::read_png(slot_file(s_.state_dir, i + 1, "png").string(), px, tw, th) &&
                        tw == kW && th == kH * 2)
                        gl_->set_thumbnail(i, px.data());
                }
            }
            ph::Menu menu;
            menu.open = true;
            menu.page = page;
            menu.selected = selected;
            ph::MenuState ms{view.layout, view.gpu, view.scale, view.smooth, view.fps, view.fullscreen, s_.gpu_failed.load(),
                             view.slot, times_};
            ms.fps_corner = view.fps_corner;
            ms.stretch = view.stretch;
            ms.resume_time = resume_time_;
            ms.stick = view.stick;
            ms.wide = view.wide;
            const int layer = view.layout == Layout::Top ? 0 : 1;  // the screen the player looks at
            menu.draw(*gl_, ms, float(w), float(h), ui, float(ph::pad::game_fps10.load()) / 10.0f,
                      [&](int slot, const ph::Rect& r) { gl_->thumbnail(slot, layer, r); });
        }
#else
        (void)menu_open;
#endif
        gl_->end();
    }

    // Runs at the DS's 59.83 frames a second (unless unpaced or fast-forwarding).
    void pace() {
        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        if (unpaced_ || s_.fast_forward.load()) {
            next_ = now;
            return;
        }
        const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(kFrameSeconds));
        if (next_.time_since_epoch().count() == 0 || now - next_ > std::chrono::milliseconds(100)) next_ = now;
        next_ += period;
        if (next_ > now) SDL_DelayPrecise(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(next_ - now).count()));
    }

    Shared& s_;
    SDL_Window* window_;  // (the window the game thread draws to)
    SDL_AudioStream* audio_;
    std::vector<ScriptStep> script_;
    uint64_t exit_after_;
    bool unpaced_;
    ntr::Runtime* rt_ = nullptr;
    ntr::Frame last_{};  // (a copy: the runtime's goes when on_frame returns)
    bool have_last_ = false;
    int save_slot_ = 0, load_slot_ = 0;  // a save or load under way
    bool continue_ = false, resume_ = false;  // (set_resume)
    bool resume_cleared_ = false;  // the resume state was deleted as older than a cartridge save
    bool resume_pending_ = false;  // the resume state was asked for while another state was being saved
    std::vector<uint32_t> thumb_;        // the picture of the state being saved
    uint64_t frames_ = 0;
    uint64_t video_asked_ = 0;  // the frame of the last renderer change asked for
    uint64_t fps_since_ = 0, fps_frames_ = 0;  // (the FPS counter)
    std::chrono::steady_clock::time_point next_{};
    ph::GlPresenter* gl_ = nullptr;  // (null: the phone's fallback, the main thread draws)
    ph::Stick stick_;
#ifndef __ANDROID__
    int thumbs_version_ = -1;  // the slot pictures in gl_ (states_version)
    std::array<std::string, kSlots> times_{};
    std::string resume_time_;  // when the resume state was written ("" none)
#endif
};

// Everything the game thread needs to create and run the game.
struct GameThreadArgs {
    Shared* shared;
    SDL_Window* window;
    Host* host;
    std::vector<uint8_t> rom;
    ntr::GameCode code;
    ntr::RuntimeOptions options;
};

int SDLCALL game_thread(void* p) {
    auto* a = static_cast<GameThreadArgs*>(p);
    // The display's OpenGL context lives on this thread: the runtime's GPU renderer draws with it here.
    ph::GlPresenter gl;
    std::string error;
    const bool have_gl = gl.init(a->window, error);
    if (have_gl) {
        std::fprintf(stderr, "video: OpenGL %s%s\n", gl.version().c_str(), gl.gpu_ok() ? "" : " (no GPU renderer)");
        a->host->set_gl(&gl);
        if (!gl.gpu_ok()) a->options.video = ntr::VideoSettings{}, a->shared->gpu_failed = true;
        a->shared->gl_display = 1;
    } else {
#ifdef __ANDROID__
        // The main thread draws the software renderer's frames with SDL's renderer instead.
        std::fprintf(stderr, "video: %s: the phone's fallback display, software renderer\n", error.c_str());
        a->options.video = ntr::VideoSettings{};
        a->shared->gpu_failed = true;
        a->shared->gl_display = -1;
#else
        a->shared->start_error = "Cannot start OpenGL 3.3: " + error;
        a->shared->started = -1;
        a->shared->game_done = true;
        return 0;
#endif
    }
    {
        std::unique_ptr<ntr::Runtime> rt = ntr::Runtime::create(std::move(a->rom), a->code, *a->host, std::move(a->options));
        a->host->attach(rt.get());
        a->shared->started = 1;
        a->shared->exit_code = rt->run();
        // (rt is destroyed here, on this thread: the GPU renderer's resources need the context)
    }
    if (have_gl) gl.shutdown();
    a->shared->game_done = true;
    return 0;
}

// Keyboard: arrows = D-pad, X = A, Z = B, S = X, A = Y, Q = L, W = R, Enter = Start, Right Shift / Backspace = Select.
uint32_t keyboard_buttons() {
    const bool* k = SDL_GetKeyboardState(nullptr);
    uint32_t b = 0;
    if (k[SDL_SCANCODE_X]) b |= ntr::KeyA;
    if (k[SDL_SCANCODE_Z]) b |= ntr::KeyB;
    if (k[SDL_SCANCODE_S]) b |= ntr::KeyX;
    if (k[SDL_SCANCODE_A]) b |= ntr::KeyY;
    if (k[SDL_SCANCODE_Q]) b |= ntr::KeyL;
    if (k[SDL_SCANCODE_W]) b |= ntr::KeyR;
    if (k[SDL_SCANCODE_RETURN]) b |= ntr::KeyStart;
    if (k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_BACKSPACE]) b |= ntr::KeySelect;
    if (k[SDL_SCANCODE_UP]) b |= ntr::KeyUp;
    if (k[SDL_SCANCODE_DOWN]) b |= ntr::KeyDown;
    if (k[SDL_SCANCODE_LEFT]) b |= ntr::KeyLeft;
    if (k[SDL_SCANCODE_RIGHT]) b |= ntr::KeyRight;
    return b;
}

uint32_t key_bit(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_X: return ntr::KeyA;
        case SDL_SCANCODE_Z: return ntr::KeyB;
        case SDL_SCANCODE_S: return ntr::KeyX;
        case SDL_SCANCODE_A: return ntr::KeyY;
        case SDL_SCANCODE_Q: return ntr::KeyL;
        case SDL_SCANCODE_W: return ntr::KeyR;
        case SDL_SCANCODE_RETURN: return ntr::KeyStart;
        case SDL_SCANCODE_RSHIFT:
        case SDL_SCANCODE_BACKSPACE: return ntr::KeySelect;
        case SDL_SCANCODE_UP: return ntr::KeyUp;
        case SDL_SCANCODE_DOWN: return ntr::KeyDown;
        case SDL_SCANCODE_LEFT: return ntr::KeyLeft;
        case SDL_SCANCODE_RIGHT: return ntr::KeyRight;
        default: return 0;
    }
}

// Gamepad, by position (the DS's A is the right face button, as on a Nintendo pad): D-pad or left stick = D-pad.
uint32_t pad_button_bit(int button) {
    switch (button) {
        case SDL_GAMEPAD_BUTTON_EAST: return ntr::KeyA;
        case SDL_GAMEPAD_BUTTON_SOUTH: return ntr::KeyB;
        case SDL_GAMEPAD_BUTTON_NORTH: return ntr::KeyX;
        case SDL_GAMEPAD_BUTTON_WEST: return ntr::KeyY;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ntr::KeyL;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ntr::KeyR;
        case SDL_GAMEPAD_BUTTON_START: return ntr::KeyStart;
        case SDL_GAMEPAD_BUTTON_BACK: return ntr::KeySelect;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: return ntr::KeyUp;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return ntr::KeyDown;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return ntr::KeyLeft;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return ntr::KeyRight;
        default: return 0;
    }
}

// `stick`: the left stick is the analog stick (ph_stick.hpp), not the D-pad.
uint32_t gamepad_buttons(SDL_Gamepad* pad, bool stick) {
    if (!pad) return 0;
    uint32_t b = 0;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        if (SDL_GetGamepadButton(pad, SDL_GamepadButton(i))) b |= pad_button_bit(i);
    if (stick) return b;
    const float x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
    const float y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
    if (std::hypot(x, y) > 0.35f) {  // the nearest of eight directions
        const float d = 0.38268343f * std::hypot(x, y);  // sin(22.5 degrees)
        if (x < -d) b |= ntr::KeyLeft;
        if (x > d) b |= ntr::KeyRight;
        if (y < -d) b |= ntr::KeyUp;
        if (y > d) b |= ntr::KeyDown;
    }
    return b;
}

void error_box(const std::string& text, SDL_Window* window = nullptr) {
    std::fprintf(stderr, "%s\n", text.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kTitle, text.c_str(), window);
}

}  // namespace

int main(int argc, char** argv) {
    // ---- arguments ----
    fs::path rom_path, save_path, data_dir, script_path, module_path = ph::default_module_path();
    uint64_t exit_after = 0;
    bool fullscreen_arg = false, unpaced = false, read_only_save = false, continue_arg = false, wide_arg = false;
    std::string renderer_arg;
    int scale_arg = 0;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool has = i + 1 < argc;
        if (a == "--save-file" && has) save_path = argv[++i];
        else if (a == "--data-dir" && has) data_dir = argv[++i];
        else if (a == "--input-script" && has) script_path = argv[++i];
        else if (a == "--exit-after-frames" && has) exit_after = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--module" && has) module_path = argv[++i];
        else if (a == "--renderer" && has) renderer_arg = argv[++i];
        else if (a == "--scale" && has) scale_arg = std::atoi(argv[++i]);
        else if (a == "--fullscreen") fullscreen_arg = true;
        else if (a == "--unpaced") unpaced = true;
        else if (a == "--read-only-save") read_only_save = true;
        else if (a == "--continue") continue_arg = true;
        else if (a == "--wide") wide_arg = true;
        else if (a == "--no-settings") {}  // (accepted: tests never write settings anyway, see below)
        else if (!a.empty() && a[0] != '-' && rom_path.empty()) rom_path = a;
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    const bool testing = !script_path.empty() || exit_after != 0;

    // Fingers are handled as fingers (the touch screen), not as a pretend mouse.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const fs::path base = SDL_GetBasePath() ? fs::path(SDL_GetBasePath()) : fs::path(".");

    // ---- where settings and saves live: --data-dir, else data\ next to the program (portable.txt), else per user ----
    std::error_code ec;
    if (data_dir.empty()) {
        if (fs::exists(base / "portable.txt", ec)) {
            data_dir = base / "data";
        } else if (char* pref = SDL_GetPrefPath("PH-Recomp", "PhantomHourglass")) {
            data_dir = pref;
            SDL_free(pref);
        }
    }
    if (rom_path.empty()) rom_path = base / "rom" / "azee.nds";
    if (save_path.empty()) save_path = data_dir / "saves" / "azee.sav";
    const fs::path settings_path = data_dir / "settings.ini";

    // ---- the ROM and the recompiled game ----
    std::vector<uint8_t> rom = read_file(rom_path);
    if (rom.empty()) return error_box("Cannot read the game ROM: " + rom_path.string()), 1;
    const ntr::KnownRom* known = ntr::find_known_rom(ntr::sha1(rom));
    if (!known || !known->supported || !ph::select_release(known->game_code))
        return error_box(rom_path.string() + " is not a supported Phantom Hourglass dump (USA, clean or D-pad patch)."), 1;
    ntr::GameCode code;
    std::string error;
    if (!ph::load_game_module(module_path, code, error)) return error_box(error), 1;
    std::vector<ScriptStep> script;
    if (!script_path.empty() && !load_script(script_path, script, error)) return error_box(error), 1;

    Settings settings = testing ? Settings{} : load_settings(settings_path);
    if (fullscreen_arg) settings.fullscreen = true;
    if (wide_arg) settings.wide = true;
    if (renderer_arg == "gpu") settings.gpu = true;
    else if (renderer_arg == "software") settings.gpu = false;
    if (std::find(std::begin(kScales), std::end(kScales), scale_arg) != std::end(kScales)) settings.scale = scale_arg;
#ifdef __ANDROID__
    settings.layout = Layout::Bottom;  // (on the phone the on-screen pad chooses the screens: it starts on the touch screen)
#endif
    ph::pad::fps = settings.fps;
    ph::pad::fps_corner = settings.fps_corner;

    // ---- window: the largest whole multiple of the DS screen that fits the desktop with room to spare ----
    SDL_Rect usable{0, 0, 1280, 720};
    SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable);
    const int scale = std::max(1, std::min(int(usable.w * 0.9) / kW, int(usable.h * 0.85) / kH));
    ph::set_gl_attributes();
#ifdef __ANDROID__
    const SDL_WindowFlags window_flags = SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL;
#else
    const SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_OPENGL;
#endif
    SDL_Window* window = SDL_CreateWindow(kTitle, kW * scale, kH * scale, window_flags);
    if (!window) return error_box(std::string("Cannot open a window: ") + SDL_GetError()), 1;
    SDL_SetWindowMinimumSize(window, kW, kH);
    if (settings.fullscreen) SDL_SetWindowFullscreen(window, true);


    // ---- sound: 48 kHz stereo, as the runtime makes it ----
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
    SDL_AudioStream* audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (audio) SDL_ResumeAudioStreamDevice(audio);
    else std::fprintf(stderr, "audio: %s (playing without sound)\n", SDL_GetError());

    // ---- the game, on its own thread with a large stack (the recompiled code nests deeply) ----
    Shared shared;
    shared.view = settings;
    shared.shot_dir = data_dir / "screenshots";
    shared.state_dir = data_dir / "states";
    Host host(shared, window, audio, std::move(script), exit_after, unpaced);
    host.set_resume(continue_arg && !testing, !testing);
    GameThreadArgs args{&shared, window, &host, std::move(rom), code, {}};
    args.options.save_data = read_file(save_path);
    args.options.console_name = "Link";
    args.options.video = video_for(settings);
    ph::set_wide_extra(args.options.video.wide_extra);
    std::fprintf(stderr, "save: %s (%zu bytes)\n", save_path.string().c_str(), args.options.save_data.size());
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_ENTRY_FUNCTION_POINTER, reinterpret_cast<void*>(&game_thread));
    SDL_SetStringProperty(props, SDL_PROP_THREAD_CREATE_NAME_STRING, "game");
    SDL_SetNumberProperty(props, SDL_PROP_THREAD_CREATE_STACKSIZE_NUMBER, 64 * 1024 * 1024);
    SDL_SetPointerProperty(props, SDL_PROP_THREAD_CREATE_USERDATA_POINTER, &args);
    SDL_Thread* thread = SDL_CreateThreadWithProperties(props);
    SDL_DestroyProperties(props);
    if (!thread) return error_box(std::string("Cannot start the game: ") + SDL_GetError(), window), 1;

#ifdef __ANDROID__
    // The phone's fallback when the game thread has no OpenGL ES: this thread draws with SDL's renderer.
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    while (shared.gl_display.load() == 0 && !shared.game_done) SDL_Delay(5);
    if (shared.gl_display.load() < 0) {
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) return error_box(std::string("Cannot draw to the window: ") + SDL_GetError(), window), 1;
        SDL_SetRenderVSync(renderer, 1);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kW, kH * 2);
        if (!texture) return error_box(std::string("Cannot create the screen texture: ") + SDL_GetError(), window), 1;
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);  // (the DS pictures' alpha byte is not opacity)
    }
#endif

    // ---- main loop: events, input, the menu, saving (and on Android, drawing) ----
    SDL_Gamepad* pad = nullptr;
    auto flush_save = [&] {
        std::vector<uint8_t> data;
        {
            std::lock_guard<std::mutex> lock(shared.save_mutex);
            if (!shared.save_dirty) return;
            shared.save_dirty = false;
            data = shared.save;
        }
        if (read_only_save) return;
        if (!write_file_atomic(save_path, data)) std::fprintf(stderr, "save: cannot write %s\n", save_path.string().c_str());
    };
    // Quitting: first the resume state (for Continue), at the game's next moment; quit without it after a while.
    uint64_t quit_deadline = 0;
    auto request_quit = [&] {
        if (testing || shared.started.load() != 1 || quit_deadline || shared.resume_done.load()) {
            shared.quit = true;
            return;
        }
        quit_deadline = SDL_GetTicks() + 7000;  // (the runtime gives up after about five seconds)
        shared.state_save = kResumeSlot;
    };
#ifndef __ANDROID__
    // (On the phone the start screen has a Continue button.)
    if (!testing && !continue_arg && !resume_time(shared.state_dir).empty())
        shared.say("F9: continue from where you quit", 8000);
#endif
#ifndef __ANDROID__
    ph::Menu menu;
#endif
    bool menu_open = false;
    // Sends the settings (and the menu) to the game thread; `video`: the renderer changed too.
    auto publish = [&](bool video) {
        std::lock_guard<std::mutex> lock(shared.view_mutex);
        shared.view = settings;
#ifndef __ANDROID__
        shared.menu_open = menu.open;
        shared.menu_selected = menu.selected;
        shared.menu_page = menu.page;
        menu_open = menu.open;
#endif
        if (video) shared.video_dirty = true;
    };
    auto set_fullscreen = [&](bool on) {
        settings.fullscreen = on;
        SDL_SetWindowFullscreen(window, on);
    };
#ifndef __ANDROID__
    // The menu: what it shows, and what a key or click in it did.
    auto menu_state = [&] {
        ph::MenuState ms{settings.layout, settings.gpu, settings.scale, settings.smooth, settings.fps,
                         settings.fullscreen, shared.gpu_failed.load(), settings.slot, slot_times(shared.state_dir)};
        ms.fps_corner = settings.fps_corner;
        ms.stretch = settings.stretch;
        ms.resume_time = resume_time(shared.state_dir);
        ms.stick = settings.stick;
        ms.wide = settings.wide;
        return ms;
    };
    auto menu_done = [&](const ph::MenuState& ms, ph::MenuResult r) {
        const bool video = ms.gpu != settings.gpu || ms.scale != settings.scale || ms.wide != settings.wide;
        if (r == ph::MenuResult::Quit) request_quit();
        settings.layout = ms.layout, settings.gpu = ms.gpu, settings.scale = ms.scale;
        settings.smooth = ms.smooth, settings.fps = ms.fps, settings.slot = ms.slot;
        settings.fps_corner = ms.fps_corner, settings.stretch = ms.stretch;
        settings.stick = ms.stick;
        settings.wide = ms.wide;
        if (ms.fullscreen != settings.fullscreen) set_fullscreen(ms.fullscreen);
        ph::pad::fps = settings.fps;
        ph::pad::fps_corner = settings.fps_corner;
        if (r == ph::MenuResult::SaveState) shared.state_save = settings.slot;
        if (r == ph::MenuResult::LoadState) {  // (back to the game to see it)
            shared.state_load = settings.slot;
            menu.open = false;
        }
        if (r == ph::MenuResult::Continue) {
            shared.state_load = kResumeSlot;
            menu.open = false;
        }
        publish(video);
    };
#endif
    uint64_t last_flush = SDL_GetTicks();
    uint64_t fps_since = SDL_GetTicks(), fps_frames = 0;
    // The finger on the touch screen (phones, touch PCs): the first one down is the stylus.
    SDL_FingerID finger = 0;
    bool finger_down = false;
    float finger_x = 0, finger_y = 0;  // window pixels
    std::vector<uint32_t> pixels(size_t(kW) * kH * 2);
    while (!shared.game_done) {
        SDL_Event e;
        int win_pw = 1, win_ph = 1;
        SDL_GetWindowSizeInPixels(window, &win_pw, &win_ph);
        const float density = std::max(0.1f, SDL_GetWindowPixelDensity(window));
#ifndef __ANDROID__
        const float ui = std::max(1.0f, SDL_GetWindowDisplayScale(window));
#endif
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT: request_quit(); break;
                case SDL_EVENT_TERMINATING: shared.quit = true; break;  // (no time left for anything)
                // Android: in the background the game writes the resume state, then waits; the save is written at
                // once (the app may be killed).
                case SDL_EVENT_WILL_ENTER_BACKGROUND:
                case SDL_EVENT_DID_ENTER_BACKGROUND:
                    if (!shared.paused.load()) shared.resume_done = false;
                    shared.paused = true;
                    if (audio) SDL_PauseAudioStreamDevice(audio);
                    flush_save();
                    break;
                case SDL_EVENT_DID_ENTER_FOREGROUND:
                    shared.paused = false;
                    if (audio) SDL_ResumeAudioStreamDevice(audio);
                    break;
                case SDL_EVENT_FINGER_DOWN:
                case SDL_EVENT_FINGER_MOTION:
                case SDL_EVENT_FINGER_UP:
                case SDL_EVENT_FINGER_CANCELED: {
                    if (e.type == SDL_EVENT_FINGER_DOWN && !finger_down) finger = e.tfinger.fingerID, finger_down = true;
                    if (!finger_down || e.tfinger.fingerID != finger) break;
                    finger_x = e.tfinger.x * float(win_pw);
                    finger_y = e.tfinger.y * float(win_ph);
                    if (e.type == SDL_EVENT_FINGER_UP || e.type == SDL_EVENT_FINGER_CANCELED) finger_down = false;
                    break;
                }
                case SDL_EVENT_GAMEPAD_ADDED:
                    if (!pad) pad = SDL_OpenGamepad(e.gdevice.which);
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED:
                    if (pad && SDL_GetGamepadID(pad) == e.gdevice.which) SDL_CloseGamepad(pad), pad = nullptr;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN: shared.pressed |= pad_button_bit(e.gbutton.button); break;
#ifndef __ANDROID__
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (testing) break;  // (a test run's input is its script's alone)
                    if (menu.open && e.button.button == SDL_BUTTON_LEFT) {
                        ph::MenuState ms = menu_state();
                        menu_done(ms, menu.click(e.button.x * density, e.button.y * density, float(win_pw), float(win_ph),
                                                 ui, ms));
                    }
                    break;
#endif
                case SDL_EVENT_KEY_DOWN: {
                    if (e.key.repeat || testing) break;  // (a test run's input is its script's alone)
                    const SDL_Scancode k = e.key.scancode;
#ifndef __ANDROID__
                    if (menu.open) {  // the menu has the keyboard
                        ph::MenuState ms = menu_state();
                        menu_done(ms, menu.key(k, ms));
                        break;
                    }
                    if (k == SDL_SCANCODE_F1 || k == SDL_SCANCODE_ESCAPE) {
                        menu.open = true;
                        publish(false);
                        break;
                    }
#endif
                    shared.pressed |= key_bit(k);
                    if (k == SDL_SCANCODE_F11 || (k == SDL_SCANCODE_RETURN && (e.key.mod & SDL_KMOD_ALT))) {
                        set_fullscreen(!settings.fullscreen);
                    } else if (k == SDL_SCANCODE_TAB) {  // swap the screen shown
                        settings.layout = settings.layout == Layout::Bottom ? Layout::Top : Layout::Bottom;
                    } else if (k == SDL_SCANCODE_F2) {  // both screens, stacked
                        settings.layout = settings.layout == Layout::Both ? Layout::Bottom : Layout::Both;
                    } else if (k == SDL_SCANCODE_F3) {  // sharp pixels / smooth
                        settings.smooth = !settings.smooth;
                    } else if (k == SDL_SCANCODE_F4) {  // FPS counter
                        settings.fps = !settings.fps;
                        ph::pad::fps = settings.fps;
                    } else if (k == SDL_SCANCODE_F12) {  // screenshot of both screens
                        shared.shot_request = true;
                    } else if (k == SDL_SCANCODE_F5) {  // save state, to the slot chosen in the menu
                        shared.state_save = settings.slot;
                    } else if (k == SDL_SCANCODE_F8) {  // load it
                        shared.state_load = settings.slot;
                    } else if (k == SDL_SCANCODE_F9) {  // continue from where the game was quit
                        shared.state_load = kResumeSlot;
                    }
                    publish(false);
                    break;
                }
                default: break;
            }
        }

        // Buttons held (the keyboard's only while the menu is closed), fast-forward on the ` key.
        const bool* keys = SDL_GetKeyboardState(nullptr);
        shared.held = (menu_open ? 0u : keyboard_buttons()) | gamepad_buttons(pad, settings.stick) | ph::pad::held.load();
        // The analog stick: the gamepad's left stick or the phone's on-screen one, whichever is pushed farther.
        if (const int want = ph::pad::stick_mode.exchange(-1); want >= 0) settings.stick = want != 0;
        shared.stick_on = settings.stick && !menu_open;
        {
            float sx = float(ph::pad::stick_x.load()) / 1000.0f, sy = float(ph::pad::stick_y.load()) / 1000.0f;
            if (pad && settings.stick) {
                const float gx = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                const float gy = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                if (std::hypot(gx, gy) > std::hypot(sx, sy)) sx = gx, sy = gy;
            }
            shared.stick_x = int(std::clamp(sx, -1.0f, 1.0f) * 1000.0f);
            shared.stick_y = int(std::clamp(sy, -1.0f, 1.0f) * 1000.0f);
        }
        shared.fast_forward = !menu_open && keys[SDL_SCANCODE_GRAVE];
        // The microphone: M, or the right stick's click, stands for blowing into it (or shouting).
        shared.blow = (!menu_open && keys[SDL_SCANCODE_M]) ||
                      (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) || ph::pad::blow.load();
        // The lid: closed while C is held (or the pad's Lid button is on).
        ntr::set_lid_closed((!menu_open && keys[SDL_SCANCODE_C]) || ph::pad::lid.load());
        // Choices from the Android app's on-screen pad and menu.
        bool changed = false;
        if (const int want = ph::pad::show.exchange(-1); want >= 0 && want <= 2) settings.layout = Layout(want), changed = true;
        if (const int want = ph::pad::smooth.exchange(-1); want >= 0) settings.smooth = want != 0, changed = true;
        if (const int want = ph::pad::stretch.exchange(-1); want >= 0) settings.stretch = want != 0, changed = true;
        bool video = false;
        if (const int want = ph::pad::wide.exchange(-1); want >= 0 && (want != 0) != settings.wide)
            settings.wide = want != 0, changed = video = true;
        if (const int want = ph::pad::video.exchange(-1); want >= 0) {  // (gpu << 8) | scale
            settings.gpu = (want >> 8) != 0;
            if (std::find(std::begin(kScales), std::end(kScales), want & 0xFF) != std::end(kScales)) settings.scale = want & 0xFF;
            changed = video = true;
        }
        ph::pad::gpu_failed = shared.gpu_failed.load();
        if (ph::pad::resume_request.exchange(false)) {  // (the phone's Quit, before it ends the app)
            shared.resume_done = false;
            if (testing || shared.started.load() != 1) shared.resume_done = true;
            else shared.state_save = kResumeSlot;
        }
        ph::pad::resume_done = shared.resume_done.load();
        if (const int slot = ph::pad::state_save.exchange(0); slot >= 1 && slot <= kSlots) shared.state_save = slot;
        if (const int slot = ph::pad::state_load.exchange(0); slot >= 1 && slot <= kSlots) shared.state_load = slot;
        if (changed) publish(video);

        // The stylus: the mouse's left button or the finger, where the touch screen is shown.
        float mx = 0, my = 0;
        bool down = !menu_open && (SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK) && SDL_GetMouseFocus() == window;
        mx *= density, my *= density;  // (window coordinates to pixels)
        if (finger_down && !menu_open) down = true, mx = finger_x, my = finger_y;
        const int wide_extra = settings.wide ? shared.wide_extra.load() : 0, wide_layer = shared.wide_layer.load();
        const ph::Views views =
            ph::compute_views(float(win_pw), float(win_ph), settings.layout, settings.stretch, wide_extra, wide_layer);
        bool touching = false;
        if (down && views.bottom_shown && views.bottom.contains(mx, my) && views.bottom.w > 0) {
            touching = true;
            int tx = 0, ty = 0;
            std::array<ph::HudMove, 16> hud{};
            int hud_count = 0;
            if (wide_layer == 1 && wide_extra) {
                std::lock_guard<std::mutex> lock(shared.hud_mutex);
                hud = shared.hud;
                hud_count = shared.hud_count;
            }
            ph::to_ds(views.bottom, wide_layer == 1 ? wide_extra : 0, mx, my, tx, ty, hud.data(), hud_count);
            shared.touch_x = tx;
            shared.touch_y = ty;
        }
        shared.touching = touching;

#ifdef __ANDROID__
        if (!renderer) {  // (the game thread draws)
            SDL_Delay(2);
        } else {
        // The fallback: draw the newest frame (vsync paces this loop to the display).
        bool fresh = false;
        {
            std::lock_guard<std::mutex> lock(shared.frame_mutex);
            if (shared.frame_new) {
                pixels = shared.frame;
                shared.frame_new = false;
                fresh = true;
            }
        }
        if (fresh) SDL_UpdateTexture(texture, nullptr, pixels.data(), kW * int(sizeof(uint32_t)));
        SDL_SetTextureScaleMode(texture, settings.smooth ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect top{0, 0, float(kW), float(kH)}, bottom{0, float(kH), float(kW), float(kH)};
        const SDL_FRect top_at{views.top.x, views.top.y, views.top.w, views.top.h};
        const SDL_FRect bottom_at{views.bottom.x, views.bottom.y, views.bottom.w, views.bottom.h};
        if (views.top_shown) SDL_RenderTexture(renderer, texture, &top, &top_at);
        if (views.bottom_shown) SDL_RenderTexture(renderer, texture, &bottom, &bottom_at);
        // Frames per second: the game's frames that reached the screen, counted over a second.
        if (fresh) fps_frames++;
        if (const uint64_t now = SDL_GetTicks(); now - fps_since >= 1000) {
            ph::pad::game_fps10 = int(fps_frames * 10000 / (now - fps_since));
            fps_frames = 0;
            fps_since = now;
        }
        if (ph::pad::fps.load()) {
            char text[16];
            std::snprintf(text, sizeof text, "%d.%d", ph::pad::game_fps10.load() / 10, ph::pad::game_fps10.load() % 10);
            const float k = std::max(1.0f, float(win_ph) / 360.0f);  // (8x8 text, scaled to the screen)
            SDL_SetRenderScale(renderer, k, k);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const SDL_FRect box{2, 2, 8.0f * float(std::strlen(text)) + 4, 12};
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, 4, 4, text);
            SDL_SetRenderScale(renderer, 1, 1);
        }
        if (const std::string note = shared.current_note(); !note.empty()) {  // "Saved to slot 3"
            const float k = std::max(1.0f, float(win_ph) / 360.0f);
            const float tw = 8.0f * float(note.size()), lw = float(win_pw) / k, lh = float(win_ph) / k;
            SDL_SetRenderScale(renderer, k, k);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 192);
            const SDL_FRect box{(lw - tw) / 2 - 6, lh - 26, tw + 12, 16};
            SDL_RenderFillRect(renderer, &box);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(renderer, (lw - tw) / 2, lh - 22, note.c_str());
            SDL_SetRenderScale(renderer, 1, 1);
        }
        SDL_RenderPresent(renderer);
        if (!fresh) SDL_Delay(1);
        }
#else
        (void)fps_since, (void)fps_frames;
        SDL_Delay(2);  // (the game thread draws; this loop only reads input)
#endif

        if (SDL_GetTicks() - last_flush > 2000) {
            flush_save();
            last_flush = SDL_GetTicks();
        }
        if (quit_deadline && (shared.resume_done.load() || SDL_GetTicks() > quit_deadline)) shared.quit = true;
    }

    shared.quit = true;
    SDL_WaitThread(thread, nullptr);
    flush_save();
    if (shared.started == -1) error_box(shared.start_error, window);
    if (!testing) save_settings(settings_path, settings);
    const int rc = shared.started == -1 ? 1 : shared.exit_code;
    if (rc != 0 && ntr::last_fatal()[0]) error_box(std::string("The game stopped: ") + ntr::last_fatal(), window);
    if (pad) SDL_CloseGamepad(pad);
    if (audio) SDL_DestroyAudioStream(audio);
#ifdef __ANDROID__
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
#endif
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rc;
}
