// The Legend of Zelda: Phantom Hourglass -- native recomp, the program (PC and Android).
//
// One window showing one DS screen at a time (the touch screen by default), scaled to fit at the DS's aspect ratio
// (black bars, never cropped). The mouse or a finger is the stylus. Microphone: hold M (or click the right stick) for
// generated noise, or N / BLOW for the bundled blow sample. Lid: hold C to close the DS. F1 or Esc opens the menu (PC);
// the Android app has its own (PhMenu).
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
//                        [--fullscreen] [--unpaced] [--module M] [--read-only-save] [--renderer software|gpu|compute]
//                        [--scale 2|4|6|8] [--continue] [--wide] [--wide21] [--textures pixels|smooth|sharp]
//                        [--fixes 0-3] [--language auto|en|fr|de|it|es] [--hd-text] [--setup] [--no-launcher]
// PC release builds (PH_PC_SETUP) open the launcher first (pc_launcher.cpp), unless a test, --continue or --no-launcher.
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

#include "achievements.hpp"
#include "game_module.hpp"
#include "layout.hpp"
#include "ntr/rom.hpp"
#include "ntr/runtime.hpp"
#include "pad_input.hpp"
#include "ph_stick.hpp"
#include "perf_hint.hpp"
#ifdef PH_PC_CLANG
#include "pc_optimizer.hpp"
#endif
// The PC data folder's name (%APPDATA%\PH-Recomp\<name>): test builds get their own (-DPH_DATA_NAME=...).
#ifndef PH_DATA_NAME
#define PH_DATA_NAME "PhantomHourglass"
#endif
#include "ph_hooks.hpp"
#include "ph_temple.hpp"
#include "ph_text.hpp"
#include "png.hpp"
#include "gl_present.hpp"
#ifndef __ANDROID__
#include "menu.hpp"
#ifdef PH_PC_SETUP
#include <thread>

#include "embedded_toolchain.hpp"
#include "game_builder.hpp"
#include "pc_launcher.hpp"
#ifdef _WIN32
namespace ph { std::string win_pick_rom(std::string& error); std::string win_exe_path(); }  // win_pick.cpp
#endif
#endif
#endif

namespace fs = std::filesystem;
using ph::Layout;

namespace ph::pad {
std::atomic<uint32_t> held{0};
std::atomic<bool> blow{false};
std::atomic<bool> blow_sample{false};
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
std::atomic<int> textures{-1};
std::atomic<int> fixes{-1};
std::atomic<int> compute{-1};
std::atomic<bool> compute_failed{false};
std::atomic<bool> resume_done{false};
std::atomic<int> wide_mode{-1};
std::atomic<int> hd_text{-1};
std::atomic<int> map_x{0}, map_y{0}, map_w{0}, map_h{0};
std::atomic<bool> map_hidden{false};
std::atomic<int> map_dy{0};
std::atomic<ph::Achievements*> achievements{nullptr};
}  // namespace ph::pad

namespace {

#ifdef PH_BUILD_LABEL
constexpr char kTitle[] = "The Legend of Zelda: Phantom Hourglass - " PH_BUILD_LABEL;
#else
constexpr char kTitle[] = "The Legend of Zelda: Phantom Hourglass";
#endif
constexpr int kW = 256, kH = 192;
constexpr double kFrameSeconds = 560190.0 / 33513982.0;  // one DS frame (59.8261 Hz)
constexpr int kScales[] = {2, 4, 6, 8};
constexpr int kSlots = 12;  // save state slots
constexpr int kWideExtra = 43;  // widescreen (a test): 256 + 2 x 43 = 342 pixels across 192, about 16:9
constexpr int kUltraWideExtra = 96;  // 256 + 2 x 96 = 448 pixels across 192, 21:9
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

// ---- firmware language (DS UserData.Settings bits 0..2): -1 auto (system locale, else English) ---------------------
constexpr int kLangAuto = -1;
constexpr int kLangJa = 0, kLangEn = 1, kLangFr = 2, kLangDe = 3, kLangIt = 4, kLangEs = 5;

// Parses "auto", ISO-639 codes ("en", "fr", "es", "ja", "de", "it"), full names or DS codes 0-5; returns -2 if unknown.
int parse_language(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) start++;
    s.erase(0, start);
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (s == "auto") return kLangAuto;
    if (s == "ja" || s == "jp" || s == "japanese" || s == "0") return kLangJa;
    if (s == "en" || s == "english" || s == "1") return kLangEn;
    if (s == "fr" || s == "french" || s == "2") return kLangFr;
    if (s == "de" || s == "german" || s == "3") return kLangDe;
    if (s == "it" || s == "italian" || s == "4") return kLangIt;
    if (s == "es" || s == "spanish" || s == "5") return kLangEs;
    return -2;
}

const char* language_name(int lang) {
    switch (lang) {
        case kLangAuto: return "auto";
        case kLangJa: return "ja";
        case kLangEn: return "en";
        case kLangFr: return "fr";
        case kLangDe: return "de";
        case kLangIt: return "it";
        case kLangEs: return "es";
        default: return "en";
    }
}

uint8_t resolve_language(int lang, bool europe) {
    if (lang >= 0 && lang <= 5) return uint8_t(lang);
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
        int picked = -1;
        for (int i = 0; i < count && locales[i] && picked < 0; i++) {
            if (!locales[i]->language) continue;
            const int code = parse_language(locales[i]->language);
            // Pick the first preferred locale carried by this release: AZEE (USA) has EN/FR/ES; AZEP (Europe) adds DE/IT.
            if (code == kLangEn || code == kLangFr || code == kLangEs || (europe && (code == kLangDe || code == kLangIt)))
                picked = code;
        }
        SDL_free(locales);
        if (picked >= 0) return uint8_t(picked);
    }
    return uint8_t(kLangEn);
}

// ---- settings.ini: a few key=value lines --------------------------------------------------------------------------
struct Settings {
    Layout layout = Layout::Bottom;
    bool fullscreen = false;
    bool smooth = false;  // linear filtering instead of sharp pixels
    bool fps = false;
    bool gpu = true;      // the GPU renderer
    bool compute = true;  // with gpu: melonDS's compute-shader renderer (OpenGL 4.3 / GLES 3.1+)
    int scale = 4;        // its internal resolution
    int slot = 1;         // the save state slot chosen (1-12)
    int fps_corner = 1;   // the FPS counter's corner of the picture (0 top left, 1 top right, 2 bottom left, 3 bottom right)
    bool stretch = false; // the picture stretched over the whole window (else the DS's shape, with bars)
    bool stick = false;   // the analog stick moves Link (a gamepad's left stick, the phone's on-screen stick)
    bool wide = false;    // widescreen, 16:9 (a test; GPU and Compute renderers)
    int wide_mode = 1;    // 1: 16:9, 2: 21:9
    int textures = 0;     // the 3D's textures with the GPU renderer: 0 the DS's pixels, 1 smooth, 2 sharp
    int fixes = 0;        // 3D fixes, bits: 1 fewer seams (GPU renderer's better polygons), 2 stable textures (no warp)
    int language = kLangAuto;  // DS firmware language: -1 auto (system locale), 0-5 explicit DS code
    bool hd_text = false; // render OBJ dialogue text with an embedded vector TrueType font
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
        else if (k == "renderer") s.gpu = v == "gpu" || v == "compute", s.compute = v == "compute";
        else if (k == "scale") s.scale = std::atoi(v.c_str());
        else if (k == "slot") s.slot = std::clamp(std::atoi(v.c_str()), 1, kSlots);
        else if (k == "fps_corner") s.fps_corner = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "stretch") s.stretch = v == "1";
        else if (k == "stick") s.stick = v == "1";
        else if (k == "wide") s.wide = v == "1";
        else if (k == "wide_mode") s.wide_mode = std::clamp(std::atoi(v.c_str()), 1, 2);
        else if (k == "textures") s.textures = std::clamp(std::atoi(v.c_str()), 0, 2);
        else if (k == "fixes") s.fixes = std::clamp(std::atoi(v.c_str()), 0, 3);
        else if (k == "language") {
            const int parsed = parse_language(v);
            if (parsed >= kLangAuto) s.language = parsed;
        }
        else if (k == "hd_text") s.hd_text = v == "1";
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
    f << "renderer=" << (s.gpu ? (s.compute ? "compute" : "gpu") : "software") << "\n";
    f << "scale=" << s.scale << "\n";
    f << "slot=" << s.slot << "\n";
    f << "fps_corner=" << s.fps_corner << "\n";
    f << "stretch=" << (s.stretch ? 1 : 0) << "\n";
    f << "stick=" << (s.stick ? 1 : 0) << "\n";
    f << "wide=" << (s.wide ? 1 : 0) << "\n";
    f << "wide_mode=" << s.wide_mode << "\n";
    f << "textures=" << s.textures << "\n";
    f << "fixes=" << s.fixes << "\n";
    f << "language=" << language_name(s.language) << "\n";
    f << "hd_text=" << (s.hd_text ? 1 : 0) << "\n";
}

ntr::VideoSettings video_for(const Settings& s) {
    ntr::VideoSettings v;
    v.renderer = !s.gpu ? ntr::Renderer::Software : s.compute ? ntr::Renderer::Compute : ntr::Renderer::OpenGL;
    v.scale = s.gpu ? s.scale : 1;
#ifdef __ANDROID__
    if (s.compute) v.scale = std::min(v.scale, 4);  // (its tile memory at 6x: some 300 MB of a phone's graphics memory)
#endif
    v.wide_extra = s.gpu && s.wide ? (s.wide_mode == 2 ? kUltraWideExtra : kWideExtra) : 0;
    v.hud_edges = v.wide_extra > 0;  // the HUD at the wide picture's edges (touches on it are mapped back: to_ds)
    v.hud_sides = v.hud_edges;       // (the rupee count with its icon, the menu bar with the Menu button)
    v.texture_smoothing = s.textures == 1 ? ntr::TextureSmoothing::Smooth
                          : s.textures == 2 ? ntr::TextureSmoothing::Sharp : ntr::TextureSmoothing::Off;
    v.better_polygons = (s.fixes & 1) != 0;  // (seams between polygons in upscaled 3D)
    v.stable_textures = (s.fixes & 2) != 0;  // (textures the DS bends with the view, smeared)
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
    std::atomic<bool> blow_sample{false};
    std::atomic<bool> stick_on{false};       // the analog stick moves Link (ph_stick.hpp)
    std::atomic<int> stick_x{0}, stick_y{0};  // where it is, -1000..1000 (y down)
    // Widescreen as drawn: the extra pixels each side of the wide screen (0: none), and its layer (0 top, 1 bottom).
    std::atomic<int> wide_extra{0}, wide_layer{1};
    std::mutex hud_mutex;  // the HUD clusters moved to the edges in the frame shown (ph::to_ds)
    std::array<ph::HudMove, 32> hud{};
    int hud_count = 0;
    std::atomic<bool> quit{false};
    std::atomic<bool> paused{false};  // the app is in the background (Android): the game waits
    std::atomic<bool> resume_done{false};  // the resume state asked for at quit / going to the background is written (or failed)
    std::atomic<bool> background_event{false}, foreground_event{false};

    // The last finished frame from the software renderer, both screens stacked (top, then bottom).
    std::mutex frame_mutex;
    std::vector<uint32_t> frame = std::vector<uint32_t>(size_t(kW) * kH * 2, 0xFF000000u);
    bool frame_new = false;

    // What the game thread draws (PC): the screens shown, the filter, the menu; and the renderer asked for.
    std::mutex view_mutex;
    Settings view;
    bool menu_open = false;
    bool europe = false;
    int menu_selected = 0;
    int menu_page = 0;
    int menu_ach_filter = 0;
    int menu_ach_scroll = 0;
    bool menu_ach_reset_confirm = false;
    bool video_dirty = false;
    ph::Achievements achievements;
    std::atomic<bool> gpu_failed{false};  // the GPU renderer could not start
    std::atomic<bool> compute_failed{false};  // the compute renderer was asked for and cannot run (the OpenGL one does)
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

#ifdef __ANDROID__
// SDL's Android application lifecycle events are delivered to event watches, not the SDL_PollEvent queue.
bool SDLCALL watch_android_lifecycle(void* userdata, SDL_Event* event) {
    auto& shared = *static_cast<Shared*>(userdata);
    switch (event->type) {
        case SDL_EVENT_WILL_ENTER_BACKGROUND:
        case SDL_EVENT_DID_ENTER_BACKGROUND:
            if (!shared.paused.exchange(true)) shared.resume_done = false;
            shared.background_event = true;
            std::fprintf(stderr, "android: lifecycle background event %u\n", unsigned(event->type));
            break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
            shared.foreground_event = true;
            shared.paused = false;
            std::fprintf(stderr, "android: lifecycle foreground event\n");
            break;
        default:
            break;
    }
    return true;
}
#endif

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
        if (rt_) s_.achievements.tick(*rt_, SDL_GetTicks());
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
        bool blow_sample = live && s_.blow_sample.load();
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
        ntr::set_mic_sample(blow_sample);
        if (const uint32_t picker = ph::temple::frame([this](uint32_t a) { return rt_->peek32(a); }, live ? held | pressed : 0u))
            held &= ~picker, pressed &= ~picker;  // (the temple hall's floor picker takes L and R)
        rt_->set_buttons(held, pressed);
        if (touching)
            rt_->touch(std::clamp(tx, 0, kW - 1), std::clamp(ty, 0, kH - 1));
        else
            rt_->release_touch();

        // Benchmark (--unpaced --exit-after-frames N, e.g. with --continue): the time from frame 120 (the resume
        // state has loaded) to the last frame, in the log.
        if (exit_after_ && f == 120) bench_start_ns_ = SDL_GetTicksNS();
        if (exit_after_ && f >= exit_after_ && bench_start_ns_ && f > 120) {
            const double s = double(SDL_GetTicksNS() - bench_start_ns_) / 1e9;
            SDL_Log("bench: %llu frames in %.3f s = %.1f fps", (unsigned long long)(f - 120), s, double(f - 120) / s);
        }
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
            if (gl_ && !gl_->acquire()) {
                std::string error;
                if (gl_->recover(error)) {
                    std::fprintf(stderr, "video: Android GL context recovered; using software renderer for this run\n");
                    rt_->set_video(ntr::VideoSettings{});
                    s_.gpu_failed = true;
                } else {
                    std::fprintf(stderr, "video: could not recover Android GL context: %s\n", error.c_str());
                    s_.quit = true;
                }
            }
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
        if (ok) ph::hd_text_reset();
        if (ok) s_.achievements.on_state_loaded();
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
    void draw(const ntr::Frame& frame, bool count_game_frame = true) {
        Settings view;
        bool menu_open, video_dirty, ach_reset_confirm = false;
        int selected, page, ach_filter = 0, ach_scroll = 0;
        {
            std::lock_guard<std::mutex> lock(s_.view_mutex);
            view = s_.view;
            menu_open = s_.menu_open;
            selected = s_.menu_selected;
            page = s_.menu_page;
            ach_filter = s_.menu_ach_filter;
            ach_scroll = s_.menu_ach_scroll;
            ach_reset_confirm = s_.menu_ach_reset_confirm;
            (void)selected, (void)page, (void)ach_filter, (void)ach_scroll, (void)ach_reset_confirm;
            video_dirty = s_.video_dirty;
            s_.video_dirty = false;
        }
        if (video_dirty) {
            const ntr::VideoSettings vs = video_for(view);
            ph::set_wide_extra(vs.wide_extra);
            rt_->set_video(vs);
            video_asked_ = frames_;
        }
        if (!gl_->can_display(frame)) {
            if (!s_.gpu_failed.exchange(true)) {
                std::fprintf(stderr, "video: Android renderer resources were lost; using software renderer for this run\n");
                rt_->set_video(ntr::VideoSettings{});
            }
            return;
        }
        // A GPU renderer that could not start falls back to software: say so in the menu. (The runtime switches
        // between frames, so until a couple of frames after a change it still reports the renderer it had.)
        if (view.gpu && rt_->video().renderer == ntr::Renderer::Software && frames_ > video_asked_ + 2)
            s_.gpu_failed = true;
        if (frames_ > video_asked_ + 2)
            s_.compute_failed = view.gpu && view.compute && rt_->video().renderer == ntr::Renderer::OpenGL;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        gl_->begin(w, h);
        const int extra = view.wide ? frame.wide_extra : 0;  // (0 until the renderer has made the change)
        s_.wide_extra = extra;
        s_.wide_layer = frame.wide_layer;
        std::array<ph::HudMove, 32> frame_hud{};
        int frame_hud_count = 0;
        {
            std::lock_guard<std::mutex> lock(s_.hud_mutex);
            s_.hud_count = extra ? frame.hud_moves : 0;
            frame_hud_count = s_.hud_count;
            for (int i = 0; i < s_.hud_count; i++) {
                const auto& m = frame.hud_move[i];
                s_.hud[size_t(i)] = {m.x0, m.y0, m.x1, m.y1, m.dx};
                frame_hud[size_t(i)] = s_.hud[size_t(i)];
            }
        }
        const ph::Views v = ph::compute_views(float(w), float(h), view.layout, view.stretch, extra, frame.wide_layer);
        if (v.top_shown) {
            gl_->screen(frame, 0, v.top, view.smooth, extra > 0);
            const bool wide_top = extra > 0 && frame.wide_layer == 0;
            gl_->draw_hd_text(0, v.top, wide_top ? extra : 0, wide_top ? frame_hud.data() : nullptr,
                              wide_top ? frame_hud_count : 0);
        }
        if (v.bottom_shown) {
            gl_->screen(frame, 1, v.bottom, view.smooth, extra > 0);
            const bool wide_bot = extra > 0 && frame.wide_layer == 1;
            gl_->draw_hd_text(1, v.bottom, wide_bot ? extra : 0, wide_bot ? frame_hud.data() : nullptr,
                              wide_bot ? frame_hud_count : 0);
        }
        // The minimap: the screen not shown, small, where the phone's pad put it (ph::pad::map_*), like a game's
        // corner map. The same picture as that screen would show, kept to its own shape inside the rectangle.
        // (Hidden while the game shows no HUD for half a second: cutscenes. DsPad hides its frame too.)
        ph::pad::map_hidden.store(frame.hud_absent >= 30);
        if (v.top_shown != v.bottom_shown && ph::pad::map_w.load() > 0 && ph::pad::map_h.load() > 0 && frame.hud_absent < 30) {
            const int layer = v.top_shown ? 1 : 0;
            const int layer_extra = layer == frame.wide_layer ? extra : 0;
            const float aspect = (256.0f + 2.0f * float(layer_extra)) / 192.0f;
            const float bx = float(ph::pad::map_x.load()), by = float(ph::pad::map_y.load());
            const float bw = float(ph::pad::map_w.load()), bh = float(ph::pad::map_h.load());
            ph::Rect r{bx, by, bw, bh};
            if (bw / bh > aspect) r.w = bh * aspect, r.x = bx;  // (anchored to the corner it sits in: left and bottom)
            else r.h = bw / aspect, r.y = by + bh - r.h;
            // A text box under it (dialogue, a choice): the minimap goes up, just above the highest such box.
            {
                const ph::Rect& main_r = v.top_shown ? v.top : v.bottom;
                const int main_layer = v.top_shown ? 0 : 1;
                const int main_extra = main_layer == frame.wide_layer ? extra : 0;
                const float mx = main_r.w / (256.0f + 2.0f * float(main_extra)), my = main_r.h / 192.0f;
                std::vector<ph::TextBox> boxes;
                ph::text_boxes(main_layer, boxes);
                float bottom = r.y + r.h;
                for (const ph::TextBox& b : boxes) {
                    const float x0 = main_r.x + (b.x0 + float(main_extra)) * mx, x1 = main_r.x + (b.x1 + float(main_extra)) * mx;
                    const float y0 = main_r.y + b.y0 * my, y1 = main_r.y + b.y1 * my;
                    if (x0 < r.x + r.w && x1 > r.x && y0 < bottom && y1 > r.y) bottom = std::min(bottom, y0 - 6.0f);
                }
                const float dy = std::min(0.0f, std::max(bottom - r.h, 0.0f) - r.y);
                r.y += dy;
                ph::pad::map_dy.store(int(std::lround(dy)));
            }
            gl_->screen(frame, layer, r, view.smooth, extra > 0);
            gl_->draw_hd_text(layer, r, layer_extra, layer_extra ? frame_hud.data() : nullptr, layer_extra ? frame_hud_count : 0);
        }
        // Frames per second: the game's frames shown, counted over a second.
        if (count_game_frame) fps_frames_++;
        if (count_game_frame) if (const uint64_t now = SDL_GetTicks(); now - fps_since_ >= 1000) {
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
        if (const std::string pick = ph::temple::picker_text(); !pick.empty()) {  // the lobby's floor picker
            const ph::Rect pic = v.top_shown ? v.top : v.bottom;
            const float size = 14 * ui, tw = size * float(pick.size());
            const float x = pic.x + std::max(4 * ui, (pic.w - tw) / 2), y = pic.y + 28 * ui;
            gl_->fill({x - 8 * ui, y - 8 * ui, tw + 16 * ui, size + 16 * ui}, 0xC0000000);
            gl_->text(x, y, size, pick, 0xFFFFFFFF);
        }
        if (const std::string note = s_.current_note(); !note.empty()) {  // "Saved to slot 3"
            const float size = 12 * ui, tw = size * float(note.size());
            const float x = std::max(4 * ui, (float(w) - tw) / 2);
            gl_->fill({x - 8 * ui, float(h) - size - 28 * ui, tw + 16 * ui, size + 16 * ui}, 0xC0000000);
            gl_->text(x, float(h) - size - 20 * ui, size, note, 0xFFFFFFFF);
        }
        s_.achievements.draw_toast(*gl_, w, h, ui, SDL_GetTicks());
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
            menu.ach_filter = ach_filter;
            menu.ach_scroll = ach_scroll;
            menu.ach_reset_confirm = ach_reset_confirm;
            ph::MenuState ms{view.layout, view.gpu, view.scale, view.smooth, view.fps, view.fullscreen, s_.gpu_failed.load(),
                             view.slot, times_};
            ms.fps_corner = view.fps_corner;
            ms.stretch = view.stretch;
            ms.resume_time = resume_time_;
            ms.stick = view.stick;
            ms.wide = view.wide;
            ms.textures = view.textures;
            ms.fixes = view.fixes;
            ms.compute = view.compute;
            ms.language = view.language;
            ms.europe = s_.europe;
            ms.hd_text = view.hd_text;
            if (page == 2) ms.achievements = s_.achievements.snapshot();
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
        // This frame's work (emulating and drawing) ran from the end of the last wait until now: tell Android, so it
        // keeps the cores fast enough for the deadline (perf_hint.hpp). Not during fast-forward or a pause.
        perf_hint_.start(int64_t(kFrameSeconds * 1e9));
        if (work_start_.time_since_epoch().count() != 0 && !s_.paused.load() && !s_.fast_forward.load())
            perf_hint_.report(std::chrono::duration_cast<std::chrono::nanoseconds>(now - work_start_).count());
        if (unpaced_ || s_.fast_forward.load()) {
            next_ = now;
            work_start_ = clock::now();
            return;
        }
        const auto period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(kFrameSeconds));
        if (next_.time_since_epoch().count() == 0 || now - next_ > std::chrono::milliseconds(100)) next_ = now;
        next_ += period;
        if (next_ > now) SDL_DelayPrecise(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(next_ - now).count()));
        work_start_ = clock::now();
    }

    ph::PerfHint perf_hint_;
    std::chrono::steady_clock::time_point work_start_{};  // when this frame's work began (the end of the last wait)

    Shared& s_;
    SDL_Window* window_;  // (the window the game thread draws to)
    SDL_AudioStream* audio_;
    std::vector<ScriptStep> script_;
    uint64_t exit_after_;
    uint64_t bench_start_ns_ = 0;
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

#ifdef PH_PC_SETUP
// ---- first-run setup (PC): the player's ROM → ph_game.dll in the data folder, built here (game_builder.cpp) ----

// The system's open-file dialog; "" when cancelled.
fs::path pick_rom_file() {
#ifdef _WIN32
    std::string error;
    const std::string picked = ph::win_pick_rom(error);
    if (!error.empty()) error_box(error);
    return picked.empty() ? fs::path() : fs::u8path(picked);
#else
    struct Pick {
        std::atomic<int> done{0};
        std::string path;
    } pick;
    static const SDL_DialogFileFilter filters[] = {{"Nintendo DS ROM", "nds"}, {"All files", "*"}};
    SDL_ShowOpenFileDialog(
        [](void* user, const char* const* files, int) {
            auto* p = static_cast<Pick*>(user);
            if (files && files[0]) p->path = files[0];
            p->done = 1;
        },
        &pick, nullptr, filters, 2, nullptr, false);
    while (!pick.done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}
        SDL_Delay(15);
    }
    return pick.path.empty() ? fs::path() : fs::u8path(pick.path);
#endif
}

// The ROM kept in the data folder after setup: rom/azee.nds (USA) or rom/azep.nds (Europe); "" when none.
fs::path kept_rom(const fs::path& data_dir) {
    std::error_code ec;
    for (const char* name : {"azee.nds", "azep.nds"})
        if (fs::exists(data_dir / "rom" / name, ec)) return data_dir / "rom" / name;
    return {};
}

// Asks for the ROM (until a supported one or Quit), keeps a copy in the data folder, builds the game with a progress
// window. Returns false when the player quits or the build fails (after saying why).
bool first_run_setup(const fs::path& data_dir, fs::path& rom_path, const fs::path& module) {
    std::error_code ec;
    for (;;) {
        if (!rom_path.empty() && fs::exists(rom_path, ec)) {
            const std::vector<uint8_t> rom = read_file(rom_path);
            const ntr::KnownRom* known = ntr::find_known_rom(ntr::sha1(rom));
            if (known && known->supported && ph::select_release(known->game_code)) {
                std::string lower_code = known->game_code;
                for (char& ch : lower_code)
                    if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
                const fs::path kept = data_dir / "rom" / (lower_code + ".nds");
                if (rom_path != kept) {
                    fs::create_directories(kept.parent_path(), ec);
                    for (const char* name : {"azee.nds", "azep.nds"})  // (one kept ROM: a new region replaces the old)
                        fs::remove(data_dir / "rom" / name, ec);
                    std::ofstream(kept, std::ios::binary).write(reinterpret_cast<const char*>(rom.data()),
                                                                std::streamsize(rom.size()));
                    rom_path = kept;
                }
                break;
            }
            error_box(rom_path.string() + " is not a supported Phantom Hourglass dump (USA or Europe, clean or "
                      "patched).\nSHA-1 " + ntr::to_hex(ntr::sha1(rom)));
        }
        const SDL_MessageBoxButtonData buttons[] = {
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Select ROM..."},
            {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit"}};
        const SDL_MessageBoxData box = {SDL_MESSAGEBOX_INFORMATION, nullptr, kTitle,
            "Welcome! Select your own Phantom Hourglass dump: USA or Europe, the clean ROM or the D-pad / button "
            "controls patch.\n\nThe game is then built on this PC from it, once (about a minute). Nothing is downloaded.",
            2, buttons, nullptr};
        int choice = 0;
        if (!SDL_ShowMessageBox(&box, &choice) || choice != 1) return false;
        rom_path = pick_rom_file();
        if (rom_path.empty()) continue;
    }

    // ---- build, with a small progress window ----
    SDL_Window* window = SDL_CreateWindow(kTitle, 520, 120, 0);
    SDL_Renderer* renderer = window ? SDL_CreateRenderer(window, nullptr) : nullptr;
    std::atomic<int> progress{0};
    std::atomic<bool> finished{false}, clang_phase{false};
    std::string error;
    std::thread worker([&] {
        const auto u8 = [](const fs::path& p) { const auto s = p.u8string(); return std::string(s.begin(), s.end()); };
        // The compiler's files travel inside the exe: unpack them next to the work folder, remove them afterwards.
        const fs::path tools = data_dir / "build-tools";
        error = ph::unpack_toolchain(u8(tools));
#ifdef PH_PC_CLANG
        // clang first (pc_optimizer.cpp): recompile to C, then clang -O2 compiles it; TinyCC only when that fails.
        if (error.empty()) {
            const fs::path opt = ph::pc_opt_dir(data_dir);
            std::error_code opt_ec;
            std::atomic<int> step{0};
            std::string clang_error = ph::build_game(u8(rom_path), u8(tools), u8(module), u8(data_dir / "build-work"),
                                                     step, u8(opt / "gen"), false);
            if (clang_error.empty()) {
                fs::rename(tools / "tcc" / "include", opt / "inc", opt_ec);
                if (!opt_ec) fs::rename(tools / "rt", opt / "rt", opt_ec);
                if (opt_ec) clang_error = "cannot keep the compiler's headers: " + opt_ec.message();
            }
            if (clang_error.empty()) {
                clang_phase = true;
                clang_error = ph::pc_clang_build(data_dir, module, progress);
            }
            fs::remove_all(opt, opt_ec);
            if (!clang_error.empty()) {  // (TinyCC: a working, slower game)
                std::fprintf(stderr, "setup: clang failed, TinyCC instead: %s\n", clang_error.c_str());
                clang_phase = false;
                error = ph::build_game(u8(rom_path), u8(tools), u8(module), u8(data_dir / "build-work"), progress);
            }
        }
#else
        if (error.empty())
            error = ph::build_game(u8(rom_path), u8(tools), u8(module), u8(data_dir / "build-work"), progress);
#endif
        std::error_code rm_ec;
        fs::remove_all(tools, rm_ec);
        finished = true;
    });
    const uint64_t start = SDL_GetTicks();
    while (!finished) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {}  // (closing the window waits for the build: TinyCC can't be stopped midway)
        const int p = progress;
        if (window) {
            const uint64_t s = (SDL_GetTicks() - start) / 1000;
            char clock[16];
            std::snprintf(clock, sizeof clock, " (%u:%02u)", unsigned(s / 60), unsigned(s % 60));
            const std::string title = std::string(clang_phase ? "Compiling the game with clang (best speed, once)... "
                                                  : p < 20 ? "Recompiling your ROM... " : "Building the game... ") +
                                      std::to_string(p) + "%" + clock;
            SDL_SetWindowTitle(window, title.c_str());
        }
        if (renderer) {
            SDL_SetRenderDrawColor(renderer, 24, 26, 30, 255);
            SDL_RenderClear(renderer);
            const SDL_FRect back = {20, 50, 480, 20}, bar = {20, 50, 480.0f * float(p) / 100.0f, 20};
            SDL_SetRenderDrawColor(renderer, 60, 64, 72, 255);
            SDL_RenderFillRect(renderer, &back);
            SDL_SetRenderDrawColor(renderer, 47, 158, 110, 255);
            SDL_RenderFillRect(renderer, &bar);
            SDL_RenderPresent(renderer);
        }
        SDL_Delay(30);
    }
    worker.join();
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    if (!error.empty()) {
        error_box("The game could not be built:\n" + error);
        return false;
    }
    std::fprintf(stderr, "setup: game built in %.1f s\n", double(SDL_GetTicks() - start) / 1000.0);
    return true;
}

// ---- the launcher (PC): Continue / Start game, Change ROM, Rebuild game, Open data folder and the main settings ----

// Whether `path` is a Phantom Hourglass dump this program can build; else `why` says what it is.
bool supported_rom(const fs::path& path, std::string& why) {
    const std::vector<uint8_t> rom = read_file(path);
    if (rom.empty()) return why = "Cannot read " + path.string(), false;
    const ntr::KnownRom* known = ntr::find_known_rom(ntr::sha1(rom));
    if (known && known->supported && ph::select_release(known->game_code)) return true;
    why = path.string() + " is not a supported Phantom Hourglass dump (USA or Europe, clean or patched).\nSHA-1 " +
          ntr::to_hex(ntr::sha1(rom));
    return false;
}

std::string mib(uintmax_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; }

// 0: quit, 1: start the game, 2: continue from the resume state. `rom_path` becomes the ROM the game was built from
// when the player builds again here; game/ph_game.id is written after each build (removed when one fails, so the
// next start builds again). Saves, save states and settings stay.
int run_launcher(const fs::path& data_dir, fs::path& rom_path, const fs::path& module, const fs::path& id_path,
                 const std::string& exe_id) {
    const fs::path settings_path = data_dir / "settings.ini";
    Settings s = load_settings(settings_path);
    ph::launcher::Window win(kTitle);
    if (!win.ok()) return 1;  // (no window for it: straight into the game, as before)
    const auto build = [&](fs::path rom) {
        std::error_code ec;
        win.hide();
        if (first_run_setup(data_dir, rom, module)) {
            rom_path = rom;
            if (!exe_id.empty()) std::ofstream(id_path) << exe_id << '\n';
        } else {
            fs::remove(id_path, ec);
        }
        win.show();
    };
    for (;;) {
        std::error_code ec;
        ph::launcher::View v;
        v.title = "Phantom Hourglass";
        const fs::path rom = rom_path.empty() ? kept_rom(data_dir) : rom_path;
        const std::string name = rom.filename().string();
        if (rom.empty()) v.info = "ROM: none - choose one with Change ROM";
        else
            v.info = "ROM: " + name + (name == "azep.nds" ? " (Europe), " : name == "azee.nds" ? " (USA), " : ", ") +
                     mib(fs::file_size(rom, ec));
        std::string built_id;
        if (std::ifstream f(id_path); f) std::getline(f, built_id);
        v.can_play = fs::exists(module, ec) && (exe_id.empty() || built_id == exe_id);
        v.status = v.can_play ? "Game: built, " + mib(fs::file_size(module, ec)) : "Game: not built yet for this version";
        v.resume_time = resume_time(data_dir / "states");
        const bool gl = s.gpu && !s.compute;  // (texture filtering: the OpenGL renderer only)
        int res = 1;
        for (int i = 0; i < 4; i++)
            if (kScales[i] == s.scale) res = i;
        v.options = {
            {"Screen", {"Touch", "Top", "Both"}, int(s.layout), {true, true, true}},
            {"Renderer", {"Software", "GPU", "Compute"}, !s.gpu ? 0 : s.compute ? 2 : 1, {true, true, true}},
            {"Resolution", {"2x", "4x", "6x", "8x"}, res, {s.gpu, s.gpu, s.gpu, s.gpu}},
            {"3D textures", {"Pixels", "Smooth", "Sharp"}, gl ? s.textures : 0, {true, gl, gl}},
            {"Widescreen", {"4:3", "16:9", "21:9"}, s.gpu && s.wide ? s.wide_mode : 0, {true, s.gpu, s.gpu}},
            {"Fullscreen", {"Off", "On"}, s.fullscreen ? 1 : 0, {true, true}},
            {"HD text", {"Off", "On"}, s.hd_text ? 1 : 0, {true, true}},
        };
        switch (win.run(v)) {
            case ph::launcher::Action::Quit: return 0;
            case ph::launcher::Action::Play: return 1;
            case ph::launcher::Action::Continue: return 2;
            case ph::launcher::Action::Rebuild: {
                const fs::path from = kept_rom(data_dir);
                build(from.empty() ? rom_path : from);  // (none at all: the setup asks for one)
                break;
            }
            case ph::launcher::Action::ChangeRom: {
                const fs::path picked = pick_rom_file();
                std::string why;
                if (picked.empty()) break;
                if (!supported_rom(picked, why)) {
                    error_box(why);
                    break;
                }
                build(picked);
                break;
            }
            case ph::launcher::Action::OpenFolder: {
                fs::create_directories(data_dir, ec);
                const auto u8 = fs::absolute(data_dir, ec).generic_u8string();
                const std::string url = "file:///" + std::string(u8.begin(), u8.end());
                if (!SDL_OpenURL(url.c_str())) error_box("Cannot open " + url + ": " + SDL_GetError());
                break;
            }
            case ph::launcher::Action::Changed: {
                const int c = v.options[size_t(win.changed_option)].chosen;
                switch (win.changed_option) {
                    case 0: s.layout = Layout(c); break;
                    case 1: s.gpu = c >= 1, s.compute = c == 2; break;
                    case 2: s.scale = kScales[c]; break;
                    case 3: s.textures = c; break;
                    case 4:
                        s.wide = c != 0;
                        if (c) s.wide_mode = c;
                        break;
                    case 5: s.fullscreen = c == 1; break;
                    case 6: s.hd_text = c == 1; break;
                    default: break;
                }
                save_settings(settings_path, s);
                break;
            }
            default: break;
        }
    }
}
#endif

}  // namespace

int main(int argc, char** argv) {
    // ---- arguments ----
    fs::path rom_path, save_path, data_dir, script_path, module_path = ph::default_module_path();
    uint64_t exit_after = 0;
    bool fullscreen_arg = false, unpaced = false, read_only_save = false, continue_arg = false, wide_arg = false, wide21_arg = false;
    bool fps_probe_arg = false, hd_text_arg = false, setup_arg = false, no_launcher_arg = false;
    std::string renderer_arg, textures_arg;
    int scale_arg = 0, fixes_arg = -1, language_arg = -2;
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
        else if (a == "--wide21") wide_arg = wide21_arg = true;
        else if (a == "--fps-probe") fps_probe_arg = true;
        else if (a == "--textures" && has) textures_arg = argv[++i];
        else if (a == "--fixes" && has) fixes_arg = std::atoi(argv[++i]);
        else if (a == "--language" && has) {
            language_arg = parse_language(argv[++i]);
            if (language_arg < kLangAuto) {
                std::fprintf(stderr, "unknown language %s (expected auto, en, fr, es, ja, de or it)\n", argv[i]);
                return 2;
            }
        }
        else if (a == "--hd-text") hd_text_arg = true;
        else if (a == "--setup") setup_arg = true;
        else if (a == "--no-launcher") no_launcher_arg = true;  // (PC: build the game again from the kept ROM, or a new one)
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
        } else if (char* pref = SDL_GetPrefPath("PH-Recomp", PH_DATA_NAME)) {
            data_dir = pref;
            SDL_free(pref);
        }
    }
#ifdef __ANDROID__
    // A shell APK (built with -PphShell) carries no game: the game is built on the phone from the player's ROM into
    // game/libph_game.so in the app's files (LauncherActivity + GameBuilder), so a change to the program needs only the
    // small APK reinstalled. A module there is used in either APK.
    // (SDL_GetBasePath is not the files dir on Android: use the internal storage path.)
    if (const char* files = SDL_GetAndroidInternalStoragePath();
        files && module_path == ph::default_module_path() && fs::exists(fs::path(files) / "game" / "libph_game.so", ec))
        module_path = fs::path(files) / "game" / "libph_game.so";
#endif
#ifdef PH_PC_SETUP
    // No ph_game next to the program (a release download): the game lives in the data folder, built here from the
    // player's ROM on the first start (or with --setup); the ROM is kept there too.
    if (module_path == ph::default_module_path() && !fs::exists(module_path, ec)) {
        module_path = data_dir / "game" / "ph_game.dll";
        const bool rom_beside = fs::exists(base / "rom" / "azee.nds", ec) || fs::exists(base / "rom" / "azep.nds", ec);
        if (rom_path.empty() && !rom_beside) rom_path = kept_rom(data_dir);
        // The built game belongs to the exe that built it (its runtime, recompiler and hook lists): a new exe (an
        // update) rebuilds it from the kept ROM, without asking. game/ph_game.id holds the builder exe's SHA-1.
        const fs::path id_path = data_dir / "game" / "ph_game.id";
        std::string exe_id;
        if (const std::string exe = ph::win_exe_path(); !exe.empty())
            if (const std::vector<uint8_t> bytes = read_file(fs::u8path(exe)); !bytes.empty())
                exe_id = ntr::to_hex(ntr::sha1(bytes));
        std::string built_id;
        if (std::ifstream f(id_path); f) std::getline(f, built_id);
        const bool stale = !exe_id.empty() && built_id != exe_id;
        if (setup_arg || stale || !fs::exists(module_path, ec)) {
            if (rom_path.empty() && rom_beside)
                rom_path = fs::exists(base / "rom" / "azee.nds", ec) ? base / "rom" / "azee.nds" : base / "rom" / "azep.nds";
            if (!first_run_setup(data_dir, rom_path, module_path)) return 0;
            if (!exe_id.empty()) std::ofstream(id_path) << exe_id << '\n';
        }
        // The launcher (Change ROM, Rebuild game, settings) before the game; scripted tests and --continue skip it.
        if (!testing && !continue_arg && !no_launcher_arg) {
            const int chosen = run_launcher(data_dir, rom_path, module_path, id_path, exe_id);
            if (chosen == 0) return 0;
            continue_arg = chosen == 2;
        }
    }
#endif
    if (rom_path.empty()) {
        if (fs::exists(base / "rom" / "azep.nds", ec) && !fs::exists(base / "rom" / "azee.nds", ec))
            rom_path = base / "rom" / "azep.nds";
        else
            rom_path = base / "rom" / "azee.nds";
    }
    const fs::path settings_path = data_dir / "settings.ini";
    ph::temple::set_dir(data_dir);

    // ---- the ROM and the recompiled game ----
    std::vector<uint8_t> rom = read_file(rom_path);
    if (rom.empty()) return error_box("Cannot read the game ROM: " + rom_path.string()), 1;
    const ntr::KnownRom* known = ntr::find_known_rom(ntr::sha1(rom));
    if (!known || !known->supported || !ph::select_release(known->game_code))
        return error_box(rom_path.string() + " is not a supported Phantom Hourglass dump (USA or Europe, clean or patched)."), 1;
    if (save_path.empty()) {
        std::string lower_code = known->game_code;
        for (char& ch : lower_code)
            if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        save_path = data_dir / "saves" / (lower_code + ".sav");
    }
    ntr::GameCode code;
    std::string error;
    if (!ph::load_game_module(module_path, code, error)) return error_box(error), 1;
    if (!code.game_code.empty() && code.game_code != known->game_code)
        return error_box("The recompiled game module (" + module_path.string() + ") was built for " + code.game_code +
                         ", not " + known->game_code + ": rebuild it (2-BUILD.ps1) with the matching ROM."),
               1;
    std::vector<ScriptStep> script;
    if (!script_path.empty() && !load_script(script_path, script, error)) return error_box(error), 1;

    Settings settings = testing ? Settings{} : load_settings(settings_path);
    if (testing) settings.language = kLangEn;  // scripted tests stay deterministic unless --language overrides it
    if (fullscreen_arg) settings.fullscreen = true;
    if (wide_arg) settings.wide = true;
    if (wide21_arg) settings.wide_mode = 2;
    if (textures_arg == "pixels") settings.textures = 0;
    else if (textures_arg == "smooth") settings.textures = 1;
    else if (textures_arg == "sharp") settings.textures = 2;
    if (fixes_arg >= 0 && fixes_arg <= 3) settings.fixes = fixes_arg;
    if (language_arg >= kLangAuto) settings.language = language_arg;
    if (hd_text_arg) settings.hd_text = true;
    // "opengl" = "gpu" (melonDS's OpenGL renderer), the name ST and phoneperf use.
    if (renderer_arg == "gpu" || renderer_arg == "opengl") settings.gpu = true, settings.compute = false;
    else if (renderer_arg == "compute") settings.gpu = settings.compute = true;
    else if (renderer_arg == "software") settings.gpu = false;
    if (std::find(std::begin(kScales), std::end(kScales), scale_arg) != std::end(kScales)) settings.scale = scale_arg;
#ifdef __ANDROID__
    settings.layout = Layout::Bottom;  // (on the phone the on-screen pad chooses the screens: it starts on the touch screen)
#endif
    ph::pad::fps = settings.fps;
    ph::pad::fps_corner = settings.fps_corner;
    ph::set_hd_text_enabled(settings.hd_text);

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
    const bool is_europe = std::strcmp(known->game_code, "AZEP") == 0;
    Shared shared;
    shared.view = settings;
    shared.europe = is_europe;
    shared.shot_dir = data_dir / "screenshots";
    shared.state_dir = data_dir / "states";
    shared.achievements.load(data_dir / "saves" / "achievements.txt", read_only_save || testing);
    ph::pad::achievements.store(&shared.achievements);
    Host host(shared, window, audio, std::move(script), exit_after, unpaced);
    // (a benchmark, --exit-after-frames without a script, may start from the resume state: --continue)
    host.set_resume(continue_arg && script_path.empty(), !testing);
#ifdef __ANDROID__
    const bool lifecycle_watch_registered = SDL_AddEventWatch(watch_android_lifecycle, &shared);
    if (!lifecycle_watch_registered) std::fprintf(stderr, "android: could not register SDL lifecycle event watch: %s\n", SDL_GetError());
#endif
    GameThreadArgs args{&shared, window, &host, std::move(rom), code, {}};
    args.options.save_data = read_file(save_path);
    args.options.console_name = "Link";
    args.options.language = resolve_language(settings.language, is_europe);
    args.options.fps_probe = fps_probe_arg;
    args.options.video = video_for(settings);
    ph::set_wide_extra(args.options.video.wide_extra);
    std::fprintf(stderr, "save: %s (%zu bytes), language: %s\n", save_path.string().c_str(), args.options.save_data.size(),
                 language_name(args.options.language));
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
        shared.menu_ach_filter = menu.ach_filter;
        shared.menu_ach_scroll = menu.ach_scroll;
        shared.menu_ach_reset_confirm = menu.ach_reset_confirm;
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
        ms.textures = settings.textures;
        ms.fixes = settings.fixes;
        ms.compute = settings.compute;
        ms.language = settings.language;
        ms.europe = is_europe;
        ms.hd_text = settings.hd_text;
        if (menu.page == 2) ms.achievements = shared.achievements.snapshot();
        return ms;
    };
    auto menu_done = [&](const ph::MenuState& ms, ph::MenuResult r) {
        const bool video = ms.gpu != settings.gpu || ms.compute != settings.compute || ms.scale != settings.scale ||
                           ms.wide != settings.wide ||
                           ms.textures != settings.textures || ms.fixes != settings.fixes;
        const bool lang_changed = ms.language != settings.language;
        if (r == ph::MenuResult::Quit) request_quit();
        settings.layout = ms.layout, settings.gpu = ms.gpu, settings.scale = ms.scale;
        settings.smooth = ms.smooth, settings.fps = ms.fps, settings.slot = ms.slot;
        settings.fps_corner = ms.fps_corner, settings.stretch = ms.stretch;
        settings.stick = ms.stick;
        settings.wide = ms.wide;
        settings.textures = ms.textures;
        settings.fixes = ms.fixes;
        settings.compute = ms.compute;
        settings.language = ms.language;
        if (ms.hd_text != settings.hd_text) {
            settings.hd_text = ms.hd_text;
            ph::set_hd_text_enabled(settings.hd_text);
        }
        if (ms.fullscreen != settings.fullscreen) set_fullscreen(ms.fullscreen);
        ph::pad::fps = settings.fps;
        ph::pad::fps_corner = settings.fps_corner;
        if (lang_changed) {
            if (!testing) save_settings(settings_path, settings);
            shared.say("Language applies on next launch (not Continue)", 5000);
        }
        if (r == ph::MenuResult::SaveState) shared.state_save = settings.slot;
        if (r == ph::MenuResult::LoadState) {  // (back to the game to see it)
            shared.state_load = settings.slot;
            menu.open = false;
        }
        if (r == ph::MenuResult::Continue) {
            shared.state_load = kResumeSlot;
            menu.open = false;
        }
        if (r == ph::MenuResult::ResetAchievements) {
            shared.achievements.reset_all();
            shared.say("Achievements reset");
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
#ifdef __ANDROID__
        if (shared.background_event.exchange(false)) {
            if (audio) SDL_PauseAudioStreamDevice(audio);
            flush_save();
        }
        if (shared.foreground_event.exchange(false)) {
            if (audio) SDL_ResumeAudioStreamDevice(audio);
        }
#endif
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
                case SDL_EVENT_MOUSE_WHEEL:
                    if (testing) break;
                    if (menu.open && menu.page == 2) {
                        ph::MenuState ms = menu_state();
                        menu.wheel(e.wheel.y, ms);
                        publish(false);
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
        // The microphone: M / stick click provides generated noise for shouting; N plays the blow sample.
        shared.blow = (!menu_open && keys[SDL_SCANCODE_M]) ||
                      (pad && SDL_GetGamepadButton(pad, SDL_GAMEPAD_BUTTON_RIGHT_STICK)) || ph::pad::blow.load();
        shared.blow_sample = (!menu_open && keys[SDL_SCANCODE_N]) || ph::pad::blow_sample.load();
        // The lid: closed while C is held (or the pad's Lid button is on).
        ntr::set_lid_closed((!menu_open && keys[SDL_SCANCODE_C]) || ph::pad::lid.load());
        // Choices from the Android app's on-screen pad and menu.
        bool changed = false;
        if (const int want = ph::pad::show.exchange(-1); want >= 0 && want <= 2) settings.layout = Layout(want), changed = true;
        if (const int want = ph::pad::smooth.exchange(-1); want >= 0) settings.smooth = want != 0, changed = true;
        if (const int want = ph::pad::hd_text.exchange(-1); want >= 0) {
            settings.hd_text = want != 0;
            ph::set_hd_text_enabled(settings.hd_text);
            changed = true;
        }
        if (const int want = ph::pad::stretch.exchange(-1); want >= 0) settings.stretch = want != 0, changed = true;
        bool video = false;
        if (const int want = ph::pad::wide_mode.exchange(-1); want >= 0 && want <= 2) {
            const bool on = want != 0;
            const int mode = on ? want : settings.wide_mode;
            if (settings.wide != on || (on && settings.wide_mode != mode))
                settings.wide = on, settings.wide_mode = mode, changed = video = true;
        }
        if (const int want = ph::pad::textures.exchange(-1); want >= 0 && want <= 2 && want != settings.textures)
            settings.textures = want, changed = video = true;
        if (const int want = ph::pad::fixes.exchange(-1); want >= 0 && want <= 3 && want != settings.fixes)
            settings.fixes = want, changed = video = true;
        if (const int want = ph::pad::compute.exchange(-1); want >= 0 && (want != 0) != settings.compute)
            settings.compute = want != 0, changed = video = true;
        ph::pad::compute_failed = shared.compute_failed.load();
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
            std::array<ph::HudMove, 32> hud{};
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
        {
            ph::AchievementToast toast;
            if (shared.achievements.active_toast(SDL_GetTicks(), toast)) {
                const float k = std::max(1.0f, float(win_ph) / 360.0f);
                const std::string line = "TROPHY: " + toast.title + " (+" + std::to_string(toast.points) + " pts)";
                const float tw = 8.0f * float(line.size()), lw = float(win_pw) / k;
                SDL_SetRenderScale(renderer, k, k);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 22, 24, 34, 232);
                const SDL_FRect box{(lw - tw) / 2 - 8, 8, tw + 16, 18};
                SDL_RenderFillRect(renderer, &box);
                SDL_SetRenderDrawColor(renderer, 245, 197, 66, 255);
                SDL_RenderDebugText(renderer, (lw - tw) / 2, 13, line.c_str());
                SDL_SetRenderScale(renderer, 1, 1);
            }
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
    ph::pad::achievements.store(nullptr);
#ifdef __ANDROID__
    if (lifecycle_watch_registered) SDL_RemoveEventWatch(watch_android_lifecycle, &shared);
#endif
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
