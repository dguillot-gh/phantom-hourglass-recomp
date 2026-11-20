// Unit tests for the local offline Achievements system (RetroAchievements set #14323).
// Verifies catalog integrity (67 achievements), RAM evaluation across items, collectibles,
// bosses, challenges, TotOK, Bellum 1/2/3, minigames, toast queueing, persistence, and reset.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "achievements.hpp"
#include "gl_present.hpp"
#include "ntr/runtime.hpp"

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

// Minimal GlPresenter stubs so achievements.cpp links headlessly without SDL/GL.
namespace ph {
void GlPresenter::fill(const Rect&, uint32_t) {}
float GlPresenter::text(float, float, float, const std::string&, uint32_t) { return 0.0f; }
}  // namespace ph

namespace {

class MockRuntime final : public ntr::Runtime {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(4 * 1024 * 1024, 0);
    std::vector<uint8_t> dtcm = std::vector<uint8_t>(16 * 1024, 0);

    int run() override { return 0; }
    void set_buttons(uint32_t, uint32_t) override {}
    void touch(int, int) override {}
    void release_touch() override {}
    void set_video(const ntr::VideoSettings&) override {}
    ntr::VideoSettings video() const override { return {}; }
    void set_fast_cart(bool) override {}
    void set_cpu_speed(int) override {}
    unsigned render_between(double) override { return 0; }
    void request_save_state() override {}
    void request_load_state(std::vector<uint8_t>) override {}

    uint8_t* ptr(uint32_t addr) {
        if (addr >= 0x02000000u && addr < 0x02400000u) return ram.data() + (addr - 0x02000000u);
        if (addr >= 0x027E0000u && addr < 0x027E4000u) return dtcm.data() + (addr - 0x027E0000u);
        return nullptr;
    }
    const uint8_t* ptr(uint32_t addr) const {
        if (addr >= 0x02000000u && addr < 0x02400000u) return ram.data() + (addr - 0x02000000u);
        if (addr >= 0x027E0000u && addr < 0x027E4000u) return dtcm.data() + (addr - 0x027E0000u);
        return nullptr;
    }

    uint32_t peek32(uint32_t addr) const override {
        addr &= ~3u;
        const uint8_t* p = ptr(addr);
        if (!p) return 0;
        uint32_t v = 0;
        std::memcpy(&v, p, 4);
        return v;
    }

    void w8(uint32_t a, uint8_t v) { if (auto* p = ptr(a)) *p = v; }
    void w16(uint32_t a, uint16_t v) { if (auto* p = ptr(a & ~1u)) std::memcpy(p, &v, 2); }
    void w32(uint32_t a, uint32_t v) { if (auto* p = ptr(a & ~3u)) std::memcpy(p, &v, 4); }
};

constexpr uint32_t kImBase    = 0x021BA51Cu;
constexpr uint32_t kPmBase    = 0x021BA388u;
constexpr uint32_t kAfBase    = 0x021B557Cu;
constexpr uint32_t kAmmoBase  = 0x021BA6B8u;
constexpr uint32_t kEquipArr  = 0x021BA700u;
constexpr uint32_t kSwordObj  = 0x021BA740u;

void setup_adventure(MockRuntime& rt) {
    rt.w32(0x027E0618u, 2u);        // GameModeId_Adventure
    rt.w32(0x027E0FB4u, kImBase);   // gItemManager
    rt.w32(0x027E0FBCu, kPmBase);   // gPlayerManager
    rt.w32(0x027E0F74u, kAfBase);   // gAdventureFlags
    rt.w32(0x027E0F90u, 0x021B0000u);  // gPlayer (Link)
    rt.w32(kImBase + 0x0ACu, kEquipArr);
    rt.w32(kEquipArr, kSwordObj);
    rt.w32(kImBase + 0x0B0u, kAmmoBase);
    rt.w32(kImBase + 0x010u, 0xFFFFFFFFu);  // FairyId_None
    rt.w16(kPmBase + 0x00u, 12u);   // 3 hearts max
    rt.w16(kPmBase + 0x02u, 12u);   // 3 hearts cur
    rt.w16(kPmBase + 0x06u, 8u);    // ship health
    rt.w16(kPmBase + 0x08u, 5u);    // salvage arm health
    rt.w16(kAmmoBase + 5u * 2u, 20u);  // 20 arrows
}

bool is_unlocked(const ph::Achievements& ach, const char* id) {
    for (const auto& a : ach.snapshot().items)
        if (a.id == id) return a.unlocked;
    return false;
}

}  // namespace

int main() {
    // 1. Catalog sanity: 67 unique entries, valid titles/descriptions/points
    const auto& cat = ph::Achievements::catalog();
    CHECK(int(cat.size()) == ph::Achievements::kCount);
    CHECK(ph::Achievements::kCount == 67);
    std::set<std::string> ids;
    int pts = 0;
    for (int i = 0; i < 67; i++) {
        CHECK(cat[size_t(i)].number == i + 1);
        CHECK(ids.insert(cat[size_t(i)].id).second);
        CHECK(cat[size_t(i)].points > 0);
        pts += cat[size_t(i)].points;
    }
    CHECK(pts == ph::Achievements::total_points());
    CHECK(pts > 400);

    const auto tmp_dir = std::filesystem::temp_directory_path() / "ph_test_achievements";
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
    const auto save_file = tmp_dir / "achievements.txt";

    MockRuntime rt;
    ph::Achievements ach;
    ach.load(save_file, false);

    // 2. Title screen (game_mode == 1) must NEVER unlock anything
    rt.w32(0x027E0618u, 1u);
    ach.tick(rt, 100);
    CHECK(ach.snapshot().unlocked_count == 0);

    // 3. Enter Adventure mode & test collectibles + items + progress bars
    setup_adventure(rt);
    uint64_t t = 1000;
    ach.tick(rt, t);
    CHECK(ach.snapshot().unlocked_count == 0);

    // Collect 10 Power Gems, 6 Treasures, 3 Treasure Maps, 6 Golden Frogs, 5 Fish, 13 Heart Containers, 4 Uncharted Islands
    rt.w8(kImBase + 0x024u + 1u, 10u);  // Power Gems = 10
    for (int i = 0; i < 6; i++) rt.w8(kImBase + 0x090u + uint32_t(i), 1u);
    rt.w32(kImBase + 0x128u + 3u * 4u, 0x07u);  // 3 treasure maps
    rt.w8(kAfBase + 38u, 0x80u);        // Mercay Frog
    rt.w8(kAfBase + 39u, 0x1Fu);        // Remaining 5 Frogs
    for (int i = 0; i < 5; i++) rt.w8(kImBase + 0x098u + uint32_t(i), 1u);
    rt.w16(kPmBase + 0x00u, 64u);       // 16 hearts (13 Heart Containers)
    rt.w16(kPmBase + 0x02u, 64u);
    rt.w8(0x021B4CCCu, 1u);             // Uncharted
    rt.w8(0x021B4D44u, 1u);             // Harrow
    rt.w8(0x021B4D6Cu, 1u);             // Dee Ess
    rt.w8(0x021B4DE4u, 1u);             // Maze
    rt.w16(kImBase + 0x0B4u, 2u);       // Quiver max
    rt.w16(kImBase + 0x0B6u, 2u);       // Bomb bag max
    rt.w16(kImBase + 0x0B8u, 2u);       // Bombchu bag max
    rt.w32(kImBase + 0x00Cu, 90000u);   // 25:00 sand
    rt.w32(kImBase + 0x128u + 1u * 4u, (1u << 4));  // NE Sea Chart (without bombchu/grapple -> Two in a Row!)

    t += 16;
    ach.tick(rt, t);
    CHECK(is_unlocked(ach, "seeker_of_power"));
    CHECK(!is_unlocked(ach, "omnipotence"));
    CHECK(is_unlocked(ach, "treasure_hunter"));
    CHECK(is_unlocked(ach, "rookie_cartographer"));
    CHECK(!is_unlocked(ach, "regular_cartographer"));
    CHECK(is_unlocked(ach, "gale_treader"));
    CHECK(is_unlocked(ach, "renowned_angler"));
    CHECK(is_unlocked(ach, "heartthrob"));
    CHECK(is_unlocked(ach, "world_explorer"));
    CHECK(is_unlocked(ach, "armed_to_the_teeth"));
    CHECK(is_unlocked(ach, "the_sands_of_time"));
    CHECK(is_unlocked(ach, "caecias"));
    CHECK(is_unlocked(ach, "two_in_a_row"));

    // Verify toast queue is active
    ph::AchievementToast toast;
    CHECK(ach.active_toast(t, toast));
    CHECK(toast.points > 0);

    // 4. Boss challenge: Blaaz without healing (fail when healed, succeed on clean run)
    rt.w32(0x021B2E94u, 43u);  // Blaaz
    t += 16; ach.tick(rt, t);
    rt.w16(kPmBase + 0x02u, 60u);  // took damage
    t += 16; ach.tick(rt, t);
    rt.w16(kPmBase + 0x02u, 64u);  // healed!
    t += 16; ach.tick(rt, t);
    rt.w8(kAfBase + 3u, 0x20u);    // Blaaz defeated -> healed so Scorching Earth must NOT unlock
    t += 16; ach.tick(rt, t);
    CHECK(!is_unlocked(ach, "scorching_earth"));

    // Retry Blaaz without healing:
    rt.w32(0x021B2E94u, 13u);  // leave room
    rt.w8(kAfBase + 3u, 0x00u);
    t += 16; ach.tick(rt, t);
    rt.w32(0x021B2E94u, 43u);  // enter Blaaz
    t += 16; ach.tick(rt, t);
    rt.w16(kPmBase + 0x02u, 56u);  // took damage, did NOT heal
    t += 16; ach.tick(rt, t);
    rt.w8(kAfBase + 3u, 0x20u);    // Blaaz defeated!
    t += 16; ach.tick(rt, t);
    CHECK(is_unlocked(ach, "scorching_earth"));

    // 5. Boss challenge: Crayk with <= 1 arrow
    rt.w32(0x021B2E94u, 44u);  // Crayk
    t += 16; ach.tick(rt, t);
    rt.w16(kAmmoBase + 5u * 2u, 19u);  // 1 arrow shot
    t += 16; ach.tick(rt, t);
    rt.w8(kAfBase + 3u, 0x20u | 0x80u);  // Crayk defeated!
    t += 16; ach.tick(rt, t);
    CHECK(is_unlocked(ach, "minimalist"));

    // 6. TotOK: Pristine Path (25:00 remaining) & Eroding Will (<= 5:30, no sand refill)
    rt.w32(0x021B2E94u, 37u);  // TotOK
    rt.w8(0x021B2EA6u, 0x00u); // 1F (0x2500)
    rt.w32(0x021E2A48u, 90000u);
    t += 16; ach.tick(rt, t);
    rt.w8(0x021B2EA6u, 0x01u); // B1 (0x2501) -> starts Eroding Will timer
    t += 16; ach.tick(rt, t);
    rt.w8(0x021B2EA6u, 0x11u); // B13 (0x2511) with 25:00 left and no refill!
    t += 16; ach.tick(rt, t);
    CHECK(is_unlocked(ach, "pristine_path"));
    CHECK(is_unlocked(ach, "eroding_will"));

    // 7. Bellum 1, 2, 3 + Credits (Leech Killer, Green Baron, Defender of the Seas, A Wish for Freedom)
    rt.w32(0x021B2E94u, 49u);  // Bellum 1
    rt.w16(kPmBase + 0x02u, 60u);  // 1 heart below max (< 3 hearts)
    t += 16; ach.tick(rt, t);
    rt.w32(0x021B2E94u, 50u);  // Bellum 2
    rt.w8(0x021FA036u, 8u);
    t += 16; ach.tick(rt, t);
    CHECK(is_unlocked(ach, "leech_killer"));
    rt.w8(0x021FA036u, 6u);    // 1 hit (<= 6)
    t += 16; ach.tick(rt, t);
    rt.w32(0x021B2E94u, 51u);  // Bellum 3 (Bellumbeck), FairyId_None (-1)
    t += 16; ach.tick(rt, t);
    CHECK(is_unlocked(ach, "the_green_baron"));
    rt.w16(kPmBase + 0x02u, 52u);  // took 8 quarters = 2 hearts (< 3 hearts)
    t += 16; ach.tick(rt, t);
    rt.w8(0x021B5774u, 1u);    // defeated_bellum!
    t += 16; ach.tick(rt, t);
    CHECK(is_unlocked(ach, "defender_of_the_seas"));
    CHECK(is_unlocked(ach, "a_wish_for_freedom"));

    // 8. Persistence round-trip & reset
    const int count_before = ach.snapshot().unlocked_count;
    CHECK(count_before >= 18);
    ph::Achievements ach2;
    ach2.load(save_file, false);
    CHECK(ach2.snapshot().unlocked_count == count_before);
    CHECK(is_unlocked(ach2, "defender_of_the_seas"));

    ach2.reset_all();
    CHECK(ach2.snapshot().unlocked_count == 0);
    ph::Achievements ach3;
    ach3.load(save_file, false);
    CHECK(ach3.snapshot().unlocked_count == 0);

    std::filesystem::remove_all(tmp_dir, ec);
    std::printf("achievements: all tests passed (%d achievements, %d total pts)\n",
                ph::Achievements::kCount, ph::Achievements::total_points());
    return 0;
}
