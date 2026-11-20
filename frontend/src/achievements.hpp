// Local offline Achievements system for Phantom Hourglass Recomp.
// Tracks all 67 achievements from RetroAchievements set #14323 directly from DS RAM (AZEE / USA),
// persists unlocks locally in <data-dir>/saves/achievements.txt, and renders unlock toasts.
#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace ntr {
class Runtime;
}

namespace ph {

class GlPresenter;

enum class AchievementCategory : uint8_t {
    Story,
    Items,
    Bosses,
    Collectibles,
    Minigames,
    Endgame,
};

struct AchievementDef {
    int number = 0;               // 1..67
    const char* id = "";          // stable slug stored in achievements.txt
    const char* title = "";
    const char* description = "";
    int points = 0;
    AchievementCategory category = AchievementCategory::Story;
    int progress_max = 0;         // > 0 if achievement has countable progress (e.g. 20 Power Gems)
};

struct AchievementView {
    int number = 0;
    std::string id;
    std::string title;
    std::string description;
    int points = 0;
    AchievementCategory category = AchievementCategory::Story;
    bool unlocked = false;
    int64_t unlocked_at = 0;      // unix epoch seconds (0 if locked)
    int progress_cur = 0;
    int progress_max = 0;
};

struct AchievementsSnapshot {
    std::vector<AchievementView> items;
    int unlocked_count = 0;
    int total_count = 0;
    int unlocked_points = 0;
    int total_points = 0;
};

struct AchievementToast {
    std::string title;
    std::string description;
    int points = 0;
    uint64_t shown_ms = 0;
    uint64_t duration_ms = 4500;
};

class Achievements {
public:
    static constexpr int kCount = 67;

    Achievements();

    // Load persisted unlocks from `path` (e.g. <data-dir>/saves/achievements.txt).
    void load(const std::filesystem::path& path, bool read_only = false);

    // Save current unlocks atomically to disk (no-op if read_only or path is empty).
    bool save() const;

    // Reset all unlocked achievements and update the file on disk.
    void reset_all();

    // Called once per emulated DS frame (~60 Hz) from Host::on_frame().
    // Evaluates DS RAM safely and unlocks newly completed achievements.
    void tick(const ntr::Runtime& rt, uint64_t now_ms);

    // Notify when a save state is loaded so in-flight boss/minigame trackers reset cleanly.
    void on_state_loaded();

    // Thread-safe snapshot for PC menu & Android UI.
    AchievementsSnapshot snapshot() const;

    // Active toast banner (returns false if none is currently displayed).
    bool active_toast(uint64_t now_ms, AchievementToast& out) const;

    // Draw the active golden trophy toast banner at the top of the window (OpenGL / GLES).
    void draw_toast(GlPresenter& gl, int win_w, int win_h, float ui, uint64_t now_ms) const;

    // Static catalog of all 67 achievements.
    static const std::vector<AchievementDef>& catalog();
    static int total_points();
    static const char* category_name(AchievementCategory cat);

private:
    struct RuntimeTrackers {
        bool in_adventure = false;
        uint32_t prev_stage = 0xFFFFFFFFu;
        uint8_t prev_room = 0xFFu;
        uint32_t prev_scene = 0xFFFFFFFFu;

        // Previous frame flags/counters for edge-triggered checks
        bool have_prev = false;
        uint32_t prev_item_flags[4]{};
        uint8_t prev_adv_flags[52]{};
        uint32_t prev_salvage_flags = 0;
        uint16_t prev_health = 0;
        uint16_t prev_max_health = 0;
        int16_t prev_ship_health = 0;
        uint16_t prev_arrows = 0;
        bool prev_sword_in_use = false;
        uint32_t prev_hourglass_cur = 0;
        int32_t prev_char_id = 0;

        // Story helpers
        bool visited_astrid_basement = false;
        bool visited_sword_cave = false;

        // Blaaz (0x2B00): defeat without healing
        bool blaaz_active = false;
        bool blaaz_healed = false;

        // Cyclok (0x2A00): defeat within 45s (2700 frames)
        bool cyclok_active = false;
        uint32_t cyclok_frames = 0;

        // Crayk (0x2C00): defeat using at most 1 arrow
        bool crayk_active = false;
        int crayk_arrows_used = 0;

        // Cubus Sisters (0x3000): <= 36 sword swings
        bool cubus_active = false;
        int cubus_swings = 0;

        // Dongorongo (0x2E00): phase 1 <= 8 switches; phase 2 no sword
        bool dongo_active = false;
        int dongo_phase = 0;  // 1 or 2
        int dongo_switches = 0;
        bool dongo_p2_sword_used = false;
        bool dongo_p1_awarded = false;

        // Gleeok (0x2D00): defeat & take Sand of Hours without taking damage
        bool gleeok_active = false;
        bool gleeok_damaged = false;

        // Eox (0x2F00): defeat within 2:30 (9000 frames)
        bool eox_active = false;
        uint32_t eox_frames = 0;

        // Salvage (stage 3): salvage treasure without damaging Salvage Arm
        bool salvage_active = false;
        bool salvage_damaged = false;

        // Temple of the Ocean King (stage 37 / 0x25xx):
        // Eroding Will: B1 -> bottom within 5:30 (330*60 = 19800 frames) without replenishing sand
        bool totok_run_active = false;
        bool totok_sand_replenished = false;
        uint32_t totok_frames = 0;

        // Bellum 1 (0x3100): health never dips 3 hearts (12 quarters) below max
        bool bellum1_active = false;
        bool bellum1_failed = false;

        // Bellum 2 (0x3200): ship damaged <= 6 times
        bool bellum2_active = false;
        int bellum2_ship_hits = 0;

        // Bellum 3 / Bellumbeck (0x3300): < 3 hearts damage (<12 quarters) AND no spirit equipped
        bool bellum3_active = false;
        int bellum3_damage_taken = 0;
        bool bellum3_spirit_used = false;

        // Minigames
        // Goron Game (0x1B00): <= 30s (1800 frames)
        bool goron_active = false;
        uint32_t goron_frames = 0;

        // Prince of Red Lions Sword Training (0x0700): 90 streak no-hit & >200 hits
        bool train_active = false;
        int train_streak = 0;
        int train_max_streak = 0;
        int train_total_hits = 0;

        // Cannon Game (stage 1): score >= 2640
        bool cannon_active = false;
        uint32_t cannon_frames = 0;

        // Shooting Range (0x0C0B): combo >= 25
        bool shoot_active = false;
        int shoot_combo = 0;
    };

    void unlock_locked(int idx, uint64_t now_ms);
    void set_progress_locked(int idx, int cur);
    void reset_trackers_locked();

    mutable std::mutex mutex_;
    std::filesystem::path save_path_;
    bool read_only_ = false;
    std::vector<bool> unlocked_;
    std::vector<int64_t> unlocked_at_;
    std::vector<int> progress_cur_;
    mutable std::vector<AchievementToast> toast_queue_;
    RuntimeTrackers tr_;
};

}  // namespace ph
