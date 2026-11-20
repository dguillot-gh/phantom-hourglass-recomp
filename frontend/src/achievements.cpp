#include "achievements.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>

#include "gl_present.hpp"
#include "ntr/runtime.hpp"

namespace ph {
namespace {

// ---- Phantom Hourglass (USA / AZEE) verified RAM & DTCM layout ----
// DTCM singleton pointers (zeldaret/ph config/usa/arm9/dtcm/symbols.txt):
constexpr uint32_t kGameModeId       = 0x027E0618u;  // Game::mModeId (2 = Adventure)
constexpr uint32_t kPtrAdvFlags      = 0x027E0F74u;  // AdventureFlags*
constexpr uint32_t kPtrPlayer        = 0x027E0F90u;  // PlayerBase*
constexpr uint32_t kPtrItemManager   = 0x027E0FB4u;  // ItemManager*
constexpr uint32_t kPtrPlayerManager = 0x027E0FBCu;  // PlayerManager*

// Verified static AZEE addresses (MultiworldGG tloz_ph Addresses.py):
constexpr uint32_t kAddrStage             = 0x021B2E94u;  // u32 current stage ID
constexpr uint32_t kAddrRoom              = 0x021B2EA6u;  // u8 current room ID
constexpr uint32_t kAddrIslandUncharted   = 0x021B4CCCu;  // u8 != 0 when discovered
constexpr uint32_t kAddrIslandHarrow      = 0x021B4D44u;  // u8 != 0 when discovered
constexpr uint32_t kAddrIslandDeeEss      = 0x021B4D6Cu;  // u8 != 0 when discovered
constexpr uint32_t kAddrIslandMaze        = 0x021B4DE4u;  // u8 != 0 when discovered
constexpr uint32_t kAddrAdvFlagsFallback  = 0x021B557Cu;  // 52 bytes
constexpr uint32_t kAddrDefeatedBellum    = 0x021B5774u;  // u8: 1 or 0xBB when Bellumbeck beaten
constexpr uint32_t kAddrInCutscene        = 0x021BBCF4u;  // u8
constexpr uint32_t kAddrHourglassCurrent  = 0x021E2A48u;  // u32 current remaining sand frames in TotOK
constexpr uint32_t kAddrSalvageHealthLive = 0x021F5720u;  // u8 live salvage arm health during dive
constexpr uint32_t kAddrBoatHealthLive    = 0x021FA036u;  // u8 live ship health at sea / Bellum 2
constexpr uint32_t kAddrInMinigame        = 0x02307D77u;  // u8

// ItemManager field offsets (zeldaret/ph include/Item/ItemManager.hpp):
constexpr uint32_t kImHourglassFrames  = 0x00Cu;  // u32 (max 90000 = 25:00)
constexpr uint32_t kImEquippedFairy    = 0x010u;  // s32 (-1 None, 0 Courage, 1 Power, 2 Wisdom)
constexpr uint32_t kImNumGems          = 0x024u;  // u8[3]: [0]=Courage, [1]=Power, [2]=Wisdom
constexpr uint32_t kImTreasure         = 0x090u;  // s8[8]
constexpr uint32_t kImFishCount        = 0x098u;  // u8[6]: [0..4] major species, [5] Stowfish
constexpr uint32_t kImEquipItemsPtr    = 0x0ACu;  // EquipItem* (*)[11]
constexpr uint32_t kImAmmoPtr          = 0x0B0u;  // u16 (*)[11]
constexpr uint32_t kImQuiverSize       = 0x0B4u;  // u16 (0..2)
constexpr uint32_t kImBombBagSize      = 0x0B6u;  // u16 (0..2)
constexpr uint32_t kImBombchuBagSize   = 0x0B8u;  // u16 (0..2)
constexpr uint32_t kImItemFlags        = 0x128u;  // u32[4]
constexpr uint32_t kImSalvageFlags     = 0x138u;  // u32

// PlayerManager field offsets (zeldaret/ph include/Player/PlayerManager.hpp):
constexpr uint32_t kPmMaxHealth        = 0x00u;   // u16 (quarter-hearts: 12 = 3 hearts, 64 = 16 hearts)
constexpr uint32_t kPmHealth           = 0x02u;   // u16 (quarter-hearts)
constexpr uint32_t kPmShipHealth       = 0x06u;   // s16
constexpr uint32_t kPmSalvageArmHealth = 0x08u;   // u16 (max 5)

// Stage IDs (MultiworldGG tloz_ph Constants.py):
constexpr uint32_t kStageCannonGame    = 1u;
constexpr uint32_t kStageSalvage       = 3u;
constexpr uint32_t kStagePrinceRedLion = 7u;
constexpr uint32_t kStageMercay        = 11u;
constexpr uint32_t kStageGust          = 14u;
constexpr uint32_t kStageDeeEss        = 27u;
constexpr uint32_t kStageTotOK         = 37u;
constexpr uint32_t kStageCyclok        = 42u;
constexpr uint32_t kStageBlaaz         = 43u;
constexpr uint32_t kStageCrayk         = 44u;
constexpr uint32_t kStageGleeok        = 45u;
constexpr uint32_t kStageDongorongo    = 46u;
constexpr uint32_t kStageEox           = 47u;
constexpr uint32_t kStageCubus         = 48u;
constexpr uint32_t kStageBellum1       = 49u;
constexpr uint32_t kStageBellum2       = 50u;
constexpr uint32_t kStageBellum3       = 51u;
constexpr uint32_t kStageCredits       = 54u;

constexpr uint32_t kMaxHourglassFrames = 1500u * 60u;  // 25:00 at 60 Hz

// Achievement indices matching RetroAchievements set #14323 (0..66)
enum AchIdx {
    kPermissionShmermission = 0,
    kTreasureHunter,
    kSeekerOfPower,
    kOmnipotence,
    kSeekerOfWisdom,
    kOmniscience,
    kSeekerOfCourage,
    kOmnivalor,
    kAfricus,
    kCorus,
    kVulturnus,
    kCaecias,
    kScorchingEarth,
    kHiddenInFlame,
    kAVisionOfDarkness,
    kBackAtYa,
    kTempusFugit,
    kOpenFire,
    kDigToTheCenter,
    kWhereTheWindBlows,
    kAndKablooey,
    kAShortFuse,
    kHiddenInWind,
    kTheSandsOfTime,
    kMentalReflection,
    kAnArmAndALeg,
    kABowyersPride,
    kMinimalist,
    kHiddenInOblivion,
    kQuadrupleBreakUp,
    kPetraphobia,
    kTwoInARow,
    kPrecisionDemolition,
    kSeamlessRelay,
    kWhyDontYaJustShootHim,
    kSanguineOre,
    kTheHooks,
    kOneHeadIsBetterThanTwo,
    kLacrimalOre,
    kRookieCartographer,
    kRegularCartographer,
    kVeteranCartographer,
    kLegendaryCartographer,
    kStop,
    kHammerTime,
    kSudorificOre,
    kBeTheCrane,
    kWorldExplorer,
    kRollinRollinRollin,
    kArmedToTheTeeth,
    kHeartthrob,
    kSecondIntention,
    kActionPort,
    kRenownedAngler,
    kGaleTreader,
    kDoubleHero,
    kSharpshooter,
    kThoroughExorcism,
    kArsBellica,
    kThePhantombuster,
    kTheVirtueOfBalance,
    kPristinePath,
    kErodingWill,
    kLeechKiller,
    kTheGreenBaron,
    kDefenderOfTheSeas,
    kAWishForFreedom,
};

bool in_main_ram(uint32_t addr) { return addr >= 0x02000000u && addr < 0x02400000u; }

uint8_t peek8(const ntr::Runtime& rt, uint32_t addr) {
    const uint32_t w = rt.peek32(addr & ~3u);
    return uint8_t((w >> (8u * (addr & 3u))) & 0xFFu);
}

uint16_t peek16(const ntr::Runtime& rt, uint32_t addr) {
    addr &= ~1u;
    const uint32_t w = rt.peek32(addr & ~3u);
    return uint16_t((w >> (8u * (addr & 3u))) & 0xFFFFu);
}

int popcount32(uint32_t v) {
    int c = 0;
    while (v) {
        v &= (v - 1u);
        c++;
    }
    return c;
}

bool has_item_flag(const uint32_t flags[4], int word, int bit) {
    return (flags[word] & (1u << bit)) != 0;
}

bool has_adv_bit(const uint8_t adv[52], int byte_idx, uint8_t mask) {
    return (adv[byte_idx] & mask) != 0;
}

bool adv_rose(const uint8_t prev[52], const uint8_t cur[52], int byte_idx, uint8_t mask) {
    return !has_adv_bit(prev, byte_idx, mask) && has_adv_bit(cur, byte_idx, mask);
}

bool item_rose(const uint32_t prev[4], const uint32_t cur[4], int word, int bit) {
    return !has_item_flag(prev, word, bit) && has_item_flag(cur, word, bit);
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
    return s.substr(a, b - a);
}

bool write_text_atomic(const std::filesystem::path& path, const std::string& content) {
    if (path.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), std::streamsize(content.size()));
        if (!f) return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

const std::vector<AchievementDef>& build_catalog() {
    using C = AchievementCategory;
    static const std::vector<AchievementDef> kList = {
        {1,  "permission_shmermission",    "Permission Shmermission",          "Learn swordplay from Oshus after raiding his storage.",                                                1,  C::Story,        0},
        {2,  "treasure_hunter",            "Treasure Hunter",                  "Obtain six types of treasure.",                                                                        5,  C::Collectibles, 6},
        {3,  "seeker_of_power",            "Seeker of Power",                  "Collect 10 Power Gems.",                                                                               5,  C::Collectibles, 10},
        {4,  "omnipotence",                "Omnipotence",                      "Collect all 20 Power Gems.",                                                                           10, C::Collectibles, 20},
        {5,  "seeker_of_wisdom",           "Seeker of Wisdom",                 "Collect 10 Wisdom Gems.",                                                                              5,  C::Collectibles, 10},
        {6,  "omniscience",                "Omniscience",                      "Collect all 20 Wisdom Gems.",                                                                          10, C::Collectibles, 20},
        {7,  "seeker_of_courage",          "Seeker of Courage",                "Collect 10 Courage Gems.",                                                                             5,  C::Collectibles, 10},
        {8,  "omnivalor",                  "Omnivalor",                        "Collect all 20 Courage Gems.",                                                                         10, C::Collectibles, 20},
        {9,  "africus",                    "Africus",                          "Acquire the Southwestern Sea chart.",                                                                  3,  C::Story,        0},
        {10, "corus",                      "Corus",                            "Acquire the Northwestern Sea chart.",                                                                  3,  C::Story,        0},
        {11, "vulturnus",                  "Vulturnus",                        "Acquire the Southeastern Sea chart.",                                                                  3,  C::Story,        0},
        {12, "caecias",                    "Caecias",                          "Acquire the Northeastern Sea chart.",                                                                  3,  C::Story,        0},
        {13, "scorching_earth",            "Scorching Earth",                  "Defeat Blaaz without healing.",                                                                        5,  C::Bosses,       0},
        {14, "hidden_in_flame",            "Hidden in Flame",                  "Free the Spirit of Power.",                                                                            5,  C::Story,        0},
        {15, "a_vision_of_darkness",       "A Vision of Darkness",             "Rescue Astrid, the fortuneteller on the Isle of Ember.",                                               1,  C::Story,        0},
        {16, "back_at_ya",                 "Back at Ya!",                      "Acquire the boomerang.",                                                                               3,  C::Items,        0},
        {17, "tempus_fugit",               "Tempus Fugit",                     "Acquire the Phantom Hourglass.",                                                                       5,  C::Story,        0},
        {18, "open_fire",                  "Open Fire!",                       "Buy Eddo's cannon.",                                                                                   3,  C::Items,        0},
        {19, "dig_to_the_center",          "Dig to the Center of the Earth!",  "Obtain the shovel.",                                                                                   3,  C::Items,        0},
        {20, "where_the_wind_blows",       "Where the Wind Blows",             "Reach the Isle of Gust for the first time.",                                                           1,  C::Story,        0},
        {21, "and_kablooey",               "And Kablooey!",                    "Obtain bombs.",                                                                                        3,  C::Items,        0},
        {22, "a_short_fuse",               "A Short Fuse",                     "Defeat Cyclok within 45 seconds.",                                                                     10, C::Bosses,       0},
        {23, "hidden_in_wind",             "Hidden in Wind",                   "Free the Spirit of Wisdom.",                                                                           5,  C::Story,        0},
        {24, "the_sands_of_time",          "The Sands of Time",                "Fill the Phantom Hourglass with 25 minutes' worth of Sand of Hours.",                                  10, C::Collectibles, 25},
        {25, "mental_reflection",          "Mental Reflection",                "Solve the crest puzzle in the second section of the Temple of the Ocean King.",                        3,  C::Story,        0},
        {26, "an_arm_and_a_leg",           "An Arm and a Leg",                 "Buy the Salvage Arm.",                                                                                 1,  C::Items,        0},
        {27, "a_bowyers_pride",            "A Bowyer's Pride",                 "Obtain the bow.",                                                                                      3,  C::Items,        0},
        {28, "minimalist",                 "Minimalist",                       "Defeat Crayk using at most one arrow.",                                                                10, C::Bosses,       0},
        {29, "hidden_in_oblivion",         "Hidden in Oblivion",               "Free the Spirit of Courage.",                                                                          5,  C::Story,        0},
        {30, "quadruple_break_up",         "Quadruple Break Up",               "Win at Dead Man's Volley and earn the Ghost Key using thirty-six sword swings or fewer.",              25, C::Bosses,       0},
        {31, "petraphobia",                "Petraphobia",                      "Find Tetra.",                                                                                          5,  C::Story,        0},
        {32, "two_in_a_row",               "Two in a Row",                     "Obtain the Northeastern Sea chart while having obtained neither the Bombchu bag nor the grappling hook.", 5, C::Endgame,    0},
        {33, "precision_demolition",       "Precision Demolition",             "Obtain the Bombchu bag.",                                                                              3,  C::Items,        0},
        {34, "seamless_relay",             "Seamless Relay",                   "Complete Dongorongo's first phase without switching between Link and Gongoron more than eight times.", 5,  C::Bosses,       0},
        {35, "why_dont_ya_just_shoot_him", "Why Don't Ya Just Shoot Him?",     "Complete Dongorongo's second phase without using your sword on it.",                                   10, C::Bosses,       0},
        {36, "sanguine_ore",               "Sanguine Ore",                     "Obtain Crimsonine.",                                                                                   5,  C::Story,        0},
        {37, "the_hooks",                  "The Hooks!",                       "Obtain the grappling hook.",                                                                           3,  C::Items,        0},
        {38, "one_head_is_better_than_two","One Head Is Better than Two",      "Defeat Gleeok and take its Sand of Hours without taking damage.",                                      5,  C::Bosses,       0},
        {39, "lacrimal_ore",               "Lacrimal Ore",                     "Obtain Azurine.",                                                                                      5,  C::Story,        0},
        {40, "rookie_cartographer",        "Rookie Cartographer",              "Collect three treasure maps.",                                                                         3,  C::Collectibles, 3},
        {41, "regular_cartographer",       "Regular Cartographer",             "Collect seven treasure maps.",                                                                         5,  C::Collectibles, 7},
        {42, "veteran_cartographer",       "Veteran Cartographer",             "Collect fifteen treasure maps.",                                                                       10, C::Collectibles, 15},
        {43, "legendary_cartographer",     "Legendary Cartographer",           "Collect all thirty-one treasure maps.",                                                                25, C::Collectibles, 31},
        {44, "stop",                       "Stop!",                            "Obtain the hammer.",                                                                                   3,  C::Items,        0},
        {45, "hammer_time",                "Hammer Time!",                     "Defeat Eox within 2:30.",                                                                              5,  C::Bosses,       0},
        {46, "sudorific_ore",              "Sudorific Ore",                    "Obtain Aquanine.",                                                                                     5,  C::Story,        0},
        {47, "be_the_crane",               "Be the Crane",                     "Salvage treasure without damaging the Salvage Arm.",                                                   5,  C::Minigames,    0},
        {48, "world_explorer",             "World Explorer",                   "Discover all uncharted islands.",                                                                      10, C::Collectibles, 4},
        {49, "rollin_rollin_rollin",       "Rollin', Rollin', Rollin'",        "Finish the Goron Game in thirty seconds or less.",                                                     25, C::Minigames,    0},
        {50, "armed_to_the_teeth",         "Armed to the Teeth",               "Get all bomb, Bombchu, and quiver upgrades.",                                                          10, C::Collectibles, 6},
        {51, "heartthrob",                 "Heartthrob",                       "Collect all thirteen Heart Containers.",                                                               10, C::Collectibles, 13},
        {52, "second_intention",           "Second Intention",                 "Score a total of 90 hits in a row during training without getting hit yourself.",                      5,  C::Minigames,    0},
        {53, "action_port",                "Action Port",                      "Get a score of 2640 in the cannon minigame.",                                                          10, C::Minigames,    0},
        {54, "renowned_angler",            "Renowned Angler",                  "Catch all five major species of fish.",                                                                10, C::Collectibles, 5},
        {55, "gale_treader",               "Gale Treader",                     "Find all six Golden Frogs.",                                                                           10, C::Collectibles, 6},
        {56, "double_hero",                "Double Hero",                      "Score more than 200 hits during training.",                                                            3,  C::Minigames,    0},
        {57, "sharpshooter",               "Sharpshooter",                     "Get a combo of 25 or more at the Shooting Range.",                                                     5,  C::Minigames,    0},
        {58, "thorough_exorcism",          "Thorough Exorcism",                "Get a score of 2000 or more at the Shooting Range.",                                                   10, C::Minigames,    0},
        {59, "ars_bellica",                "Ars Bellica",                      "Obtain the Swordsman's Scroll.",                                                                       10, C::Items,        0},
        {60, "the_phantombuster",          "The Phantombuster",                "Forge the Phantom Sword.",                                                                             5,  C::Story,        0},
        {61, "the_virtue_of_balance",      "The Virtue of Balance",            "Upgrade all spirits' abilities to Level 2.",                                                           25, C::Collectibles, 3},
        {62, "pristine_path",              "Pristine Path",                    "Complete the Temple of the Ocean King with 25:00 remaining in the Phantom Hourglass.",                 25, C::Endgame,      0},
        {63, "eroding_will",               "Eroding Will",                     "Complete the Temple of the Ocean King from beginning to end within 5:30 without replenishing sand.",   25, C::Endgame,      0},
        {64, "leech_killer",               "Leech Killer",                     "Defeat Bellum's first phase without your life energy dipping three hearts below its maximum.",         5,  C::Endgame,      0},
        {65, "the_green_baron",            "The Green Baron",                  "Defeat Bellum's second phase without your ship being damaged more than six times.",                    10, C::Endgame,      0},
        {66, "defender_of_the_seas",       "Defender of the Seas",             "Defeat Bellum's final phase while taking less than three hearts of damage and without any spirit abilities equipped.", 25, C::Endgame, 0},
        {67, "a_wish_for_freedom",         "A Wish for Freedom",               "Save the World of the Ocean King from Bellum's influence.",                                            25, C::Endgame,      0},
    };
    return kList;
}

}  // namespace

const std::vector<AchievementDef>& Achievements::catalog() { return build_catalog(); }

int Achievements::total_points() {
    static const int kTotal = [] {
        int sum = 0;
        for (const auto& d : catalog()) sum += d.points;
        return sum;
    }();
    return kTotal;
}

const char* Achievements::category_name(AchievementCategory cat) {
    switch (cat) {
        case AchievementCategory::Story:        return "Story";
        case AchievementCategory::Items:        return "Items";
        case AchievementCategory::Bosses:       return "Bosses";
        case AchievementCategory::Collectibles: return "Collectibles";
        case AchievementCategory::Minigames:    return "Minigames";
        case AchievementCategory::Endgame:      return "Endgame";
    }
    return "Other";
}

Achievements::Achievements()
    : unlocked_(size_t(kCount), false),
      unlocked_at_(size_t(kCount), 0),
      progress_cur_(size_t(kCount), 0) {}

void Achievements::load(const std::filesystem::path& path, bool read_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    save_path_ = path;
    read_only_ = read_only;
    std::fill(unlocked_.begin(), unlocked_.end(), false);
    std::fill(unlocked_at_.begin(), unlocked_at_.end(), 0);
    std::fill(progress_cur_.begin(), progress_cur_.end(), 0);
    toast_queue_.clear();
    reset_trackers_locked();

    std::ifstream f(path);
    if (!f) return;
    const auto& cat = catalog();
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));
        for (size_t i = 0; i < cat.size(); i++) {
            if (key == cat[i].id) {
                unlocked_[i] = true;
                unlocked_at_[i] = std::strtoll(val.c_str(), nullptr, 10);
                if (cat[i].progress_max > 0) progress_cur_[i] = cat[i].progress_max;
                break;
            }
        }
    }
}

bool Achievements::save() const {
    std::string out;
    std::filesystem::path path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (read_only_ || save_path_.empty()) return true;
        path = save_path_;
        const auto& cat = catalog();
        std::ostringstream ss;
        ss << "# Phantom Hourglass Recomp local achievements (RetroAchievements set #14323)\n";
        for (size_t i = 0; i < cat.size(); i++) {
            if (unlocked_[i]) ss << cat[i].id << "=" << unlocked_at_[i] << "\n";
        }
        out = ss.str();
    }
    return write_text_atomic(path, out);
}

void Achievements::reset_all() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::fill(unlocked_.begin(), unlocked_.end(), false);
        std::fill(unlocked_at_.begin(), unlocked_at_.end(), 0);
        std::fill(progress_cur_.begin(), progress_cur_.end(), 0);
        toast_queue_.clear();
        reset_trackers_locked();
    }
    save();
}

void Achievements::on_state_loaded() {
    std::lock_guard<std::mutex> lock(mutex_);
    reset_trackers_locked();
}

void Achievements::reset_trackers_locked() {
    tr_ = RuntimeTrackers{};
}

void Achievements::unlock_locked(int idx, uint64_t now_ms) {
    if (idx < 0 || idx >= kCount || unlocked_[size_t(idx)]) return;
    const auto& def = catalog()[size_t(idx)];
    unlocked_[size_t(idx)] = true;
    unlocked_at_[size_t(idx)] = int64_t(std::time(nullptr));
    if (def.progress_max > 0) progress_cur_[size_t(idx)] = def.progress_max;
    std::fprintf(stderr, "achievement: unlocked #%02d '%s' (+%d pts)\n", def.number, def.title, def.points);

    AchievementToast t;
    t.title = def.title;
    t.description = def.description;
    t.points = def.points;
    t.shown_ms = toast_queue_.empty() ? now_ms : 0;
    toast_queue_.push_back(std::move(t));
}

void Achievements::set_progress_locked(int idx, int cur) {
    if (idx < 0 || idx >= kCount) return;
    const int mx = catalog()[size_t(idx)].progress_max;
    if (mx <= 0) return;
    const int clamped = std::clamp(cur, 0, mx);
    if (unlocked_[size_t(idx)]) {
        progress_cur_[size_t(idx)] = mx;
    } else if (clamped > progress_cur_[size_t(idx)]) {
        progress_cur_[size_t(idx)] = clamped;
    }
}

void Achievements::tick(const ntr::Runtime& rt, uint64_t now_ms) {
    bool newly_unlocked = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto prev_unlocked_count = [&] {
            int n = 0;
            for (bool u : unlocked_) if (u) n++;
            return n;
        }();

        // Only evaluate during active Adventure gameplay (GameModeId_Adventure == 2).
        const uint32_t game_mode = rt.peek32(kGameModeId);
        const uint32_t im_ptr = rt.peek32(kPtrItemManager);
        const uint32_t pm_ptr = rt.peek32(kPtrPlayerManager);
        const uint32_t af_ptr = rt.peek32(kPtrAdvFlags);
        const bool valid_ptrs = (game_mode == 2u) && in_main_ram(im_ptr) && in_main_ram(pm_ptr);
        if (!valid_ptrs) {
            if (tr_.in_adventure) reset_trackers_locked();
            return;
        }
        tr_.in_adventure = true;

        // Read ItemManager fields
        uint32_t item_flags[4]{};
        for (int i = 0; i < 4; i++) item_flags[i] = rt.peek32(im_ptr + kImItemFlags + uint32_t(i * 4));
        const uint32_t salvage_flags = rt.peek32(im_ptr + kImSalvageFlags);
        const uint32_t hourglass_max_frames = rt.peek32(im_ptr + kImHourglassFrames);
        const int32_t equipped_fairy = int32_t(rt.peek32(im_ptr + kImEquippedFairy));
        const uint8_t courage_gems = peek8(rt, im_ptr + kImNumGems + 0u);
        const uint8_t power_gems   = peek8(rt, im_ptr + kImNumGems + 1u);
        const uint8_t wisdom_gems  = peek8(rt, im_ptr + kImNumGems + 2u);
        const uint16_t quiver_up   = peek16(rt, im_ptr + kImQuiverSize);
        const uint16_t bomb_up     = peek16(rt, im_ptr + kImBombBagSize);
        const uint16_t chu_up      = peek16(rt, im_ptr + kImBombchuBagSize);

        int treasure_types = 0;
        for (int i = 0; i < 8; i++) {
            const int8_t cnt = int8_t(peek8(rt, im_ptr + kImTreasure + uint32_t(i)));
            if (cnt > 0) treasure_types++;
        }
        int major_fish = 0;
        for (int i = 0; i < 5; i++) {
            if (peek8(rt, im_ptr + kImFishCount + uint32_t(i)) > 0) major_fish++;
        }

        // Read arrows & EquipSword::mInUse safely via ItemManager pointers
        uint16_t arrows = tr_.prev_arrows;
        const uint32_t ammo_ptr = rt.peek32(im_ptr + kImAmmoPtr);
        if (in_main_ram(ammo_ptr)) arrows = peek16(rt, ammo_ptr + 5u * 2u);  // ItemFlag_Bow == 5

        bool sword_in_use = false;
        const uint32_t equip_arr_ptr = rt.peek32(im_ptr + kImEquipItemsPtr);
        if (in_main_ram(equip_arr_ptr)) {
            const uint32_t sword_ptr = rt.peek32(equip_arr_ptr + 0u);  // ItemFlag_OshusSword == 0
            if (in_main_ram(sword_ptr)) sword_in_use = peek8(rt, sword_ptr + 8u) != 0;
        }

        // Read AdventureFlags (52 bytes)
        const uint32_t adv_base = in_main_ram(af_ptr) ? af_ptr : kAddrAdvFlagsFallback;
        uint8_t adv[52]{};
        for (uint32_t i = 0; i < 52u; i += 4u) {
            const uint32_t w = rt.peek32(adv_base + i);
            adv[i + 0] = uint8_t(w & 0xFFu);
            adv[i + 1] = uint8_t((w >> 8) & 0xFFu);
            adv[i + 2] = uint8_t((w >> 16) & 0xFFu);
            adv[i + 3] = uint8_t((w >> 24) & 0xFFu);
        }

        // Read PlayerManager health / ship / salvage
        const uint16_t max_health = peek16(rt, pm_ptr + kPmMaxHealth);
        const uint16_t health = peek16(rt, pm_ptr + kPmHealth);
        const int16_t ship_health_pm = int16_t(peek16(rt, pm_ptr + kPmShipHealth));
        const uint16_t salvage_arm_pm = peek16(rt, pm_ptr + kPmSalvageArmHealth);

        // Sanity filter: ignore uninitialized frames right after boot/file load (Link always has >= 12 max health)
        if (max_health < 12u || max_health > 64u) {
            return;
        }

        // Current stage / room / scene
        const uint32_t stage = rt.peek32(kAddrStage);
        const uint8_t room = peek8(rt, kAddrRoom);
        const uint32_t scene = (stage << 8) | uint32_t(room);
        const uint8_t defeated_bellum = peek8(rt, kAddrDefeatedBellum);
        const uint32_t hourglass_cur = rt.peek32(kAddrHourglassCurrent);
        const uint8_t salvage_live = peek8(rt, kAddrSalvageHealthLive);
        const uint8_t boat_live = peek8(rt, kAddrBoatHealthLive);
        const int16_t ship_health = (stage == kStageBellum2 && boat_live > 0 && boat_live <= 20)
                                        ? int16_t(boat_live)
                                        : ship_health_pm;
        const bool in_cutscene = peek8(rt, kAddrInCutscene) != 0;

        // Read active player pointer / Gongoron indicator for Dongorongo
        const uint32_t player_ptr = rt.peek32(kPtrPlayer);
        const int32_t char_id = int32_t(player_ptr);  // changes when switching Link <-> Gongoron

        // -----------------------------------------------------------------------------------------
        // 1. Direct Item, Story Flag, and Collectible Achievements (always evaluated cleanly)
        // -----------------------------------------------------------------------------------------

        // #2 Treasure Hunter: 6 types of treasure
        set_progress_locked(kTreasureHunter, treasure_types);
        if (treasure_types >= 6) unlock_locked(kTreasureHunter, now_ms);

        // #3..#8 Spirit Gems (Power, Wisdom, Courage)
        set_progress_locked(kSeekerOfPower, power_gems);
        set_progress_locked(kOmnipotence, power_gems);
        if (power_gems >= 10) unlock_locked(kSeekerOfPower, now_ms);
        if (power_gems >= 20) unlock_locked(kOmnipotence, now_ms);

        set_progress_locked(kSeekerOfWisdom, wisdom_gems);
        set_progress_locked(kOmniscience, wisdom_gems);
        if (wisdom_gems >= 10) unlock_locked(kSeekerOfWisdom, now_ms);
        if (wisdom_gems >= 20) unlock_locked(kOmniscience, now_ms);

        set_progress_locked(kSeekerOfCourage, courage_gems);
        set_progress_locked(kOmnivalor, courage_gems);
        if (courage_gems >= 10) unlock_locked(kSeekerOfCourage, now_ms);
        if (courage_gems >= 20) unlock_locked(kOmnivalor, now_ms);

        // #9..#12 Sea Charts (ItemFlags word 1, bits 1..4)
        if (has_item_flag(item_flags, 1, 1)) unlock_locked(kAfricus, now_ms);
        if (has_item_flag(item_flags, 1, 2)) unlock_locked(kCorus, now_ms);
        if (has_item_flag(item_flags, 1, 3)) unlock_locked(kVulturnus, now_ms);
        if (has_item_flag(item_flags, 1, 4)) unlock_locked(kCaecias, now_ms);

        // #14, #23, #29 Spirits freed (ItemFlags word 0, bits 21=Power, 22=Wisdom, 20=Courage)
        if (has_item_flag(item_flags, 0, 21)) unlock_locked(kHiddenInFlame, now_ms);
        if (has_item_flag(item_flags, 0, 22)) unlock_locked(kHiddenInWind, now_ms);
        if (has_item_flag(item_flags, 0, 20)) unlock_locked(kHiddenInOblivion, now_ms);

        // #16, #17, #18, #19, #21, #26, #27, #33, #37, #44, #59, #60 Key Equipment & Items
        if (has_item_flag(item_flags, 0, 2))  unlock_locked(kBackAtYa, now_ms);
        if (has_item_flag(item_flags, 1, 0))  unlock_locked(kTempusFugit, now_ms);
        if (has_item_flag(item_flags, 1, 9))  unlock_locked(kOpenFire, now_ms);
        if (has_item_flag(item_flags, 0, 3))  unlock_locked(kDigToTheCenter, now_ms);
        if (has_item_flag(item_flags, 0, 4))  unlock_locked(kAndKablooey, now_ms);
        if (has_item_flag(item_flags, 1, 12)) unlock_locked(kAnArmAndALeg, now_ms);
        if (has_item_flag(item_flags, 0, 5))  unlock_locked(kABowyersPride, now_ms);
        if (has_item_flag(item_flags, 0, 7))  unlock_locked(kPrecisionDemolition, now_ms);
        if (has_item_flag(item_flags, 0, 6))  unlock_locked(kTheHooks, now_ms);
        if (has_item_flag(item_flags, 0, 8))  unlock_locked(kStop, now_ms);
        if (has_item_flag(item_flags, 1, 13)) unlock_locked(kArsBellica, now_ms);
        if (has_item_flag(item_flags, 1, 5))  unlock_locked(kThePhantombuster, now_ms);

        // #24 The Sands of Time: 25 minutes (1500s * 60 = 90000 frames)
        const int sand_mins = int(std::min<uint32_t>(hourglass_max_frames, kMaxHourglassFrames) / 3600u);
        set_progress_locked(kTheSandsOfTime, sand_mins);
        if (hourglass_max_frames >= kMaxHourglassFrames) unlock_locked(kTheSandsOfTime, now_ms);

        // #25 Mental Reflection: Courage Crest puzzle in TotOK B6 (adv_flags[16] & 0x04)
        if (has_adv_bit(adv, 16, 0x04u)) unlock_locked(kMentalReflection, now_ms);

        // #31 Petraphobia: Find Tetra (adv_flags[31] & 0x02)
        if (has_adv_bit(adv, 31, 0x02u)) unlock_locked(kPetraphobia, now_ms);

        // #36, #39, #46 Three Pure Metals (AdventureFlags byte 15: bits 5=Azurine, 6=Crimsonine, 7=Aquanine)
        if (has_adv_bit(adv, 15, 0x40u)) unlock_locked(kSanguineOre, now_ms);
        if (has_adv_bit(adv, 15, 0x20u)) unlock_locked(kLacrimalOre, now_ms);
        if (has_adv_bit(adv, 15, 0x80u)) unlock_locked(kSudorificOre, now_ms);

        // #40..#43 Treasure Maps (ItemFlags word 3, bits 0..30 = 31 maps)
        const int map_count = popcount32(item_flags[3] & 0x7FFFFFFFu);
        set_progress_locked(kRookieCartographer, map_count);
        set_progress_locked(kRegularCartographer, map_count);
        set_progress_locked(kVeteranCartographer, map_count);
        set_progress_locked(kLegendaryCartographer, map_count);
        if (map_count >= 3)  unlock_locked(kRookieCartographer, now_ms);
        if (map_count >= 7)  unlock_locked(kRegularCartographer, now_ms);
        if (map_count >= 15) unlock_locked(kVeteranCartographer, now_ms);
        if (map_count >= 31) unlock_locked(kLegendaryCartographer, now_ms);

        // #48 World Explorer: Discover all 4 uncharted islands (Uncharted, Harrow, Dee Ess, Maze)
        const int uncharted_found = (peek8(rt, kAddrIslandUncharted) != 0 ? 1 : 0) +
                                    (peek8(rt, kAddrIslandHarrow) != 0 ? 1 : 0) +
                                    (peek8(rt, kAddrIslandDeeEss) != 0 ? 1 : 0) +
                                    (peek8(rt, kAddrIslandMaze) != 0 ? 1 : 0);
        set_progress_locked(kWorldExplorer, uncharted_found);
        if (uncharted_found >= 4) unlock_locked(kWorldExplorer, now_ms);

        // #50 Armed to the Teeth: all 6 ammo upgrades (quiver 2 + bomb bag 2 + bombchu bag 2)
        const int ammo_upgrades = std::min<int>(quiver_up, 2) + std::min<int>(bomb_up, 2) + std::min<int>(chu_up, 2);
        set_progress_locked(kArmedToTheTeeth, ammo_upgrades);
        if (quiver_up >= 2u && bomb_up >= 2u && chu_up >= 2u) unlock_locked(kArmedToTheTeeth, now_ms);

        // #51 Heartthrob: 13 Heart Containers (start 3 hearts = 12 quarters -> 16 hearts = 64 quarters)
        const int heart_containers = std::max(0, int(max_health) / 4 - 3);
        set_progress_locked(kHeartthrob, heart_containers);
        if (max_health >= 64u) unlock_locked(kHeartthrob, now_ms);

        // #54 Renowned Angler: all 5 major species of fish
        set_progress_locked(kRenownedAngler, major_fish);
        if (major_fish >= 5) unlock_locked(kRenownedAngler, now_ms);

        // #55 Gale Treader: all 6 Golden Frogs (AdventureFlags FLAG(9,23)..FLAG(9,28) -> byte 38 bit 7 + byte 39 bits 0..4)
        const int frogs = (has_adv_bit(adv, 38, 0x80u) ? 1 : 0) + popcount32(adv[39] & 0x1Fu);
        set_progress_locked(kGaleTreader, frogs);
        if (frogs >= 6) unlock_locked(kGaleTreader, now_ms);

        // #58 Thorough Exorcism: score >= 2000 at Shooting Range (adv_flags[42] & 0x10)
        if (has_adv_bit(adv, 42, 0x10u)) unlock_locked(kThoroughExorcism, now_ms);

        // #61 The Virtue of Balance: all 3 spirits upgraded to Level 2 (ItemFlags word 0, bits 26..28)
        const int lv2_spirits = (has_item_flag(item_flags, 0, 26) ? 1 : 0) +
                                (has_item_flag(item_flags, 0, 27) ? 1 : 0) +
                                (has_item_flag(item_flags, 0, 28) ? 1 : 0);
        set_progress_locked(kTheVirtueOfBalance, lv2_spirits);
        if (lv2_spirits >= 3) unlock_locked(kTheVirtueOfBalance, now_ms);

        // #20 Where the Wind Blows: reach Isle of Gust (stage 14)
        if (stage == kStageGust) unlock_locked(kWhereTheWindBlows, now_ms);

        // #67 A Wish for Freedom: defeat Bellumbeck / reach credits
        if (stage == kStageCredits || defeated_bellum == 1u || defeated_bellum == 0xBBu) {
            unlock_locked(kAWishForFreedom, now_ms);
        }

        // -----------------------------------------------------------------------------------------
        // 2. Edge-Triggered, Story, Boss Challenge, TotOK, and Minigame State Machines
        // -----------------------------------------------------------------------------------------
        const bool scene_changed = tr_.have_prev && (scene != tr_.prev_scene);
        const bool stage_changed = tr_.have_prev && (stage != tr_.prev_stage);

        if (tr_.have_prev) {
            // #1 Permission Shmermission: raid Oshus's sword cave (0x0B13), get Oshus's Sword, learn swordplay
            if (scene == 0x0B13u) tr_.visited_sword_cave = true;
            if (has_item_flag(item_flags, 0, 0) &&
                (item_rose(tr_.prev_item_flags, item_flags, 0, 0) ||
                 (tr_.visited_sword_cave && stage == kStageMercay && room != 0x13u && room != 0x0Au) ||
                 has_item_flag(item_flags, 1, 0))) {
                unlock_locked(kPermissionShmermission, now_ms);
            }

            // #15 A Vision of Darkness: rescue Astrid on Isle of Ember (0x0D14 basement -> 0x0D0A house, or Blaaz beaten)
            if (scene == 0x0D14u) tr_.visited_astrid_basement = true;
            if ((tr_.visited_astrid_basement && scene == 0x0D0Au) || has_adv_bit(adv, 3, 0x20u)) {
                unlock_locked(kAVisionOfDarkness, now_ms);
            }

            // #32 Two in a Row: acquire NE Sea Chart with neither Bombchu bag nor Grappling Hook
            if (item_rose(tr_.prev_item_flags, item_flags, 1, 4) &&
                !has_item_flag(item_flags, 0, 7) && !has_item_flag(item_flags, 0, 6)) {
                unlock_locked(kTwoInARow, now_ms);
            }

            // ---- Boss #13: Scorching Earth (Blaaz, 0x2B00, defeat without healing) ----
            if (stage == kStageBlaaz) {
                if (!tr_.blaaz_active && !has_adv_bit(adv, 3, 0x20u)) {
                    tr_.blaaz_active = true;
                    tr_.blaaz_healed = false;
                }
                if (tr_.blaaz_active) {
                    if (health > tr_.prev_health && max_health == tr_.prev_max_health) tr_.blaaz_healed = true;
                    if (adv_rose(tr_.prev_adv_flags, adv, 3, 0x20u)) {
                        if (!tr_.blaaz_healed) unlock_locked(kScorchingEarth, now_ms);
                        tr_.blaaz_active = false;
                    }
                }
            } else {
                tr_.blaaz_active = false;
            }

            // ---- Boss #22: A Short Fuse (Cyclok, 0x2A00, defeat within 45s = 2700 frames) ----
            if (stage == kStageCyclok) {
                if (!tr_.cyclok_active && !has_adv_bit(adv, 3, 0x40u)) {
                    tr_.cyclok_active = true;
                    tr_.cyclok_frames = 0;
                }
                if (tr_.cyclok_active) {
                    if (!in_cutscene) tr_.cyclok_frames++;
                    if (adv_rose(tr_.prev_adv_flags, adv, 3, 0x40u)) {
                        if (tr_.cyclok_frames <= 45u * 60u) unlock_locked(kAShortFuse, now_ms);
                        tr_.cyclok_active = false;
                    }
                }
            } else {
                tr_.cyclok_active = false;
            }

            // ---- Boss #28: Minimalist (Crayk, 0x2C00, defeat using at most 1 arrow) ----
            if (stage == kStageCrayk) {
                if (!tr_.crayk_active && !has_adv_bit(adv, 3, 0x80u)) {
                    tr_.crayk_active = true;
                    tr_.crayk_arrows_used = 0;
                }
                if (tr_.crayk_active) {
                    if (arrows < tr_.prev_arrows) tr_.crayk_arrows_used += int(tr_.prev_arrows - arrows);
                    if (adv_rose(tr_.prev_adv_flags, adv, 3, 0x80u)) {
                        if (tr_.crayk_arrows_used <= 1) unlock_locked(kMinimalist, now_ms);
                        tr_.crayk_active = false;
                    }
                }
            } else {
                tr_.crayk_active = false;
            }

            // ---- Boss #30: Quadruple Break Up (Cubus Sisters, 0x3000, <= 36 sword swings) ----
            if (stage == kStageCubus) {
                if (!tr_.cubus_active && !has_adv_bit(adv, 47, 0x08u)) {
                    tr_.cubus_active = true;
                    tr_.cubus_swings = 0;
                }
                if (tr_.cubus_active) {
                    if (sword_in_use && !tr_.prev_sword_in_use) tr_.cubus_swings++;
                    if (adv_rose(tr_.prev_adv_flags, adv, 47, 0x08u) ||
                        item_rose(tr_.prev_item_flags, item_flags, 1, 11)) {  // ItemFlag_GhostKey
                        if (tr_.cubus_swings <= 36) unlock_locked(kQuadrupleBreakUp, now_ms);
                        tr_.cubus_active = false;
                    }
                }
            } else {
                tr_.cubus_active = false;
            }

            // ---- Boss #34 & #35: Dongorongo (0x2E00, P1 <= 8 switches, P2 no sword) ----
            if (stage == kStageDongorongo) {
                if (!tr_.dongo_active && !has_adv_bit(adv, 31, 0x04u)) {
                    tr_.dongo_active = true;
                    tr_.dongo_phase = 1;
                    tr_.dongo_switches = 0;
                    tr_.dongo_p2_sword_used = false;
                    tr_.dongo_p1_awarded = false;
                }
                if (tr_.dongo_active) {
                    if (tr_.dongo_phase == 1) {
                        if (char_id != tr_.prev_char_id && tr_.prev_char_id != 0) tr_.dongo_switches++;
                        // Transition into phase 2 (mid-fight room/cutscene shift)
                        if (scene_changed && room != 0) {
                            if (!tr_.dongo_p1_awarded && tr_.dongo_switches <= 8) {
                                unlock_locked(kSeamlessRelay, now_ms);
                                tr_.dongo_p1_awarded = true;
                            }
                            tr_.dongo_phase = 2;
                        }
                    } else if (tr_.dongo_phase == 2) {
                        if (sword_in_use && !tr_.prev_sword_in_use) tr_.dongo_p2_sword_used = true;
                    }
                    if (adv_rose(tr_.prev_adv_flags, adv, 31, 0x04u)) {
                        if (!tr_.dongo_p1_awarded && tr_.dongo_switches <= 8) unlock_locked(kSeamlessRelay, now_ms);
                        if (!tr_.dongo_p2_sword_used) unlock_locked(kWhyDontYaJustShootHim, now_ms);
                        tr_.dongo_active = false;
                    }
                }
            } else {
                tr_.dongo_active = false;
            }

            // ---- Boss #38: One Head Is Better than Two (Gleeok, 0x2D00, no damage taken) ----
            if (stage == kStageGleeok) {
                if (!tr_.gleeok_active && !has_adv_bit(adv, 31, 0x08u)) {
                    tr_.gleeok_active = true;
                    tr_.gleeok_damaged = false;
                }
                if (tr_.gleeok_active) {
                    if (health < tr_.prev_health) tr_.gleeok_damaged = true;
                    if (adv_rose(tr_.prev_adv_flags, adv, 31, 0x08u)) {
                        if (!tr_.gleeok_damaged) unlock_locked(kOneHeadIsBetterThanTwo, now_ms);
                        tr_.gleeok_active = false;
                    }
                }
            } else {
                tr_.gleeok_active = false;
            }

            // ---- Boss #45: Hammer Time! (Eox, 0x2F00, defeat within 2:30 = 9000 frames) ----
            if (stage == kStageEox) {
                if (!tr_.eox_active && !has_adv_bit(adv, 31, 0x10u)) {
                    tr_.eox_active = true;
                    tr_.eox_frames = 0;
                }
                if (tr_.eox_active) {
                    if (!in_cutscene) tr_.eox_frames++;
                    if (adv_rose(tr_.prev_adv_flags, adv, 31, 0x10u)) {
                        if (tr_.eox_frames <= 150u * 60u) unlock_locked(kHammerTime, now_ms);
                        tr_.eox_active = false;
                    }
                }
            } else {
                tr_.eox_active = false;
            }

            // ---- Minigame #47: Be the Crane (Salvage treasure without damaging Salvage Arm) ----
            if (stage == kStageSalvage) {
                if (!tr_.salvage_active) {
                    tr_.salvage_active = true;
                    tr_.salvage_damaged = false;
                }
                const int live_hp = (salvage_live > 0 && salvage_live <= 5) ? int(salvage_live) : int(salvage_arm_pm);
                if (live_hp < 5) tr_.salvage_damaged = true;
            } else {
                if (tr_.salvage_active && !tr_.salvage_damaged && (salvage_flags & ~tr_.prev_salvage_flags) != 0) {
                    unlock_locked(kBeTheCrane, now_ms);
                }
                tr_.salvage_active = false;
            }
            // Also handle immediate flag rise while still in stage 3
            if (tr_.salvage_active && !tr_.salvage_damaged && (salvage_flags & ~tr_.prev_salvage_flags) != 0) {
                unlock_locked(kBeTheCrane, now_ms);
                tr_.salvage_active = false;
            }

            // ---- TotOK #62 Pristine Path & #63 Eroding Will ----
            if (stage == kStageTotOK) {
                // Starting from B1 (0x2501) directly after 1F (0x2500) begins a full-run timer!
                if (scene == 0x2501u && tr_.prev_scene == 0x2500u) {
                    tr_.totok_run_active = true;
                    tr_.totok_sand_replenished = false;
                    tr_.totok_frames = 0;
                }
                if (tr_.totok_run_active) {
                    if (!in_cutscene) tr_.totok_frames++;
                    // Sand replenished if current sand increases (by > 30 frames to ignore pause jitter)
                    if (hourglass_cur > tr_.prev_hourglass_cur + 30u && tr_.prev_hourglass_cur > 0u) {
                        tr_.totok_sand_replenished = true;
                    }
                }
                // Reached B13 (0x2511) or Bellum staircase (0x2512)
                if (scene == 0x2511u || scene == 0x2512u) {
                    if (hourglass_cur >= kMaxHourglassFrames) {
                        unlock_locked(kPristinePath, now_ms);
                    }
                    if (tr_.totok_run_active && !tr_.totok_sand_replenished && tr_.totok_frames <= 330u * 60u) {
                        unlock_locked(kErodingWill, now_ms);
                        tr_.totok_run_active = false;
                    }
                }
            } else if (stage == kStageBellum1) {
                // Entering Bellum 1 directly from TotOK bottom also counts as completing TotOK!
                if (tr_.prev_stage == kStageTotOK) {
                    if (hourglass_cur >= kMaxHourglassFrames) unlock_locked(kPristinePath, now_ms);
                    if (tr_.totok_run_active && !tr_.totok_sand_replenished && tr_.totok_frames <= 330u * 60u) {
                        unlock_locked(kErodingWill, now_ms);
                    }
                }
                tr_.totok_run_active = false;
            } else {
                tr_.totok_run_active = false;
            }

            // ---- Endgame #64: Leech Killer (Bellum Phase 1, 0x3100, never dip 3 hearts = 12 quarters below max) ----
            if (stage == kStageBellum1) {
                if (!tr_.bellum1_active) {
                    tr_.bellum1_active = true;
                    tr_.bellum1_failed = false;
                }
                if (int(max_health) - int(health) >= 12) tr_.bellum1_failed = true;
            } else if (tr_.bellum1_active) {
                if (stage == kStageBellum2 && !tr_.bellum1_failed) unlock_locked(kLeechKiller, now_ms);
                tr_.bellum1_active = false;
            }

            // ---- Endgame #65: The Green Baron (Bellum Phase 2, 0x3200, ship damaged <= 6 times) ----
            if (stage == kStageBellum2) {
                if (!tr_.bellum2_active) {
                    tr_.bellum2_active = true;
                    tr_.bellum2_ship_hits = 0;
                }
                if (ship_health < tr_.prev_ship_health && tr_.prev_ship_health > 0) tr_.bellum2_ship_hits++;
            } else if (tr_.bellum2_active) {
                if (stage == kStageBellum3 && tr_.bellum2_ship_hits <= 6) unlock_locked(kTheGreenBaron, now_ms);
                tr_.bellum2_active = false;
            }

            // ---- Endgame #66: Defender of the Seas (Bellumbeck Phase 3, 0x3300, < 3 hearts dmg + no spirit) ----
            if (stage == kStageBellum3) {
                if (!tr_.bellum3_active) {
                    tr_.bellum3_active = true;
                    tr_.bellum3_damage_taken = 0;
                    tr_.bellum3_spirit_used = false;
                }
                if (health < tr_.prev_health) tr_.bellum3_damage_taken += int(tr_.prev_health - health);
                if (equipped_fairy != -1) tr_.bellum3_spirit_used = true;
                if (defeated_bellum == 1u || defeated_bellum == 0xBBu) {
                    if (tr_.bellum3_damage_taken < 12 && !tr_.bellum3_spirit_used) {
                        unlock_locked(kDefenderOfTheSeas, now_ms);
                    }
                    tr_.bellum3_active = false;
                }
            } else if (tr_.bellum3_active) {
                if (stage == kStageCredits && tr_.bellum3_damage_taken < 12 && !tr_.bellum3_spirit_used) {
                    unlock_locked(kDefenderOfTheSeas, now_ms);
                }
                tr_.bellum3_active = false;
            }

            // ---- Minigame #49: Rollin', Rollin', Rollin' (Goron Game on Dee Ess 0x1B00, <= 30s) ----
            if (stage == kStageDeeEss) {
                const bool mini_on = peek8(rt, kAddrInMinigame) != 0;
                if (mini_on && !tr_.goron_active) {
                    tr_.goron_active = true;
                    tr_.goron_frames = 0;
                }
                if (tr_.goron_active) {
                    if (!in_cutscene) tr_.goron_frames++;
                    if (adv_rose(tr_.prev_adv_flags, adv, 35, 0x20u)) {
                        if (tr_.goron_frames <= 30u * 60u) unlock_locked(kRollinRollinRollin, now_ms);
                        tr_.goron_active = false;
                    } else if (!mini_on) {
                        tr_.goron_active = false;
                    }
                }
            } else {
                tr_.goron_active = false;
            }

            // ---- Minigame #52 & #56: Prince of Red Lions Sword Training (stage 7, 0x0700) ----
            if (stage == kStagePrinceRedLion) {
                const bool mini_on = peek8(rt, kAddrInMinigame) != 0;
                if (mini_on && !tr_.train_active) {
                    tr_.train_active = true;
                    tr_.train_streak = 0;
                    tr_.train_max_streak = 0;
                    tr_.train_total_hits = 0;
                }
                if (tr_.train_active) {
                    if (health < tr_.prev_health) tr_.train_streak = 0;
                    if (sword_in_use && !tr_.prev_sword_in_use) {
                        tr_.train_streak++;
                        tr_.train_total_hits++;
                        tr_.train_max_streak = std::max(tr_.train_max_streak, tr_.train_streak);
                    }
                    if (tr_.train_max_streak >= 90 && has_adv_bit(adv, 40, 0x40u)) {
                        unlock_locked(kSecondIntention, now_ms);
                    }
                    if (tr_.train_total_hits > 200 && has_adv_bit(adv, 40, 0x40u)) {
                        unlock_locked(kDoubleHero, now_ms);
                    }
                    if (!mini_on) tr_.train_active = false;
                }
            } else {
                tr_.train_active = false;
            }

            // ---- Minigame #53: Action Port (Cannon Minigame, stage 1 -> Bannan reward adv_flags[46] & 0x40) ----
            if (stage == kStageCannonGame) {
                if (!tr_.cannon_active) {
                    tr_.cannon_active = true;
                    tr_.cannon_frames = 0;
                }
                tr_.cannon_frames++;
            } else if (tr_.cannon_active) {
                if (adv_rose(tr_.prev_adv_flags, adv, 46, 0x40u)) unlock_locked(kActionPort, now_ms);
                tr_.cannon_active = false;
            }

            // ---- Minigame #57: Sharpshooter (Shooting Range, Molida 0x0C0B, combo >= 25) ----
            if (scene == 0x0C0Bu) {
                const bool mini_on = peek8(rt, kAddrInMinigame) != 0;
                if (mini_on && !tr_.shoot_active) {
                    tr_.shoot_active = true;
                    tr_.shoot_combo = 0;
                }
                if (tr_.shoot_active) {
                    if (arrows < tr_.prev_arrows) tr_.shoot_combo++;
                    if (tr_.shoot_combo >= 25 && has_adv_bit(adv, 42, 0x10u)) {
                        unlock_locked(kSharpshooter, now_ms);
                    }
                    if (!mini_on) tr_.shoot_active = false;
                }
            } else {
                tr_.shoot_active = false;
            }

            (void)stage_changed;
        }

        // Store previous frame snapshot
        tr_.have_prev = true;
        tr_.prev_stage = stage;
        tr_.prev_room = room;
        tr_.prev_scene = scene;
        std::memcpy(tr_.prev_item_flags, item_flags, sizeof item_flags);
        std::memcpy(tr_.prev_adv_flags, adv, sizeof adv);
        tr_.prev_salvage_flags = salvage_flags;
        tr_.prev_health = health;
        tr_.prev_max_health = max_health;
        tr_.prev_ship_health = ship_health;
        tr_.prev_arrows = arrows;
        tr_.prev_sword_in_use = sword_in_use;
        tr_.prev_hourglass_cur = hourglass_cur;
        tr_.prev_char_id = char_id;

        int after_unlocked_count = 0;
        for (bool u : unlocked_) if (u) after_unlocked_count++;
        newly_unlocked = after_unlocked_count > prev_unlocked_count;
    }
    if (newly_unlocked) save();
}

AchievementsSnapshot Achievements::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto& cat = catalog();
    AchievementsSnapshot snap;
    snap.items.reserve(cat.size());
    snap.total_count = int(cat.size());
    snap.total_points = total_points();
    for (size_t i = 0; i < cat.size(); i++) {
        AchievementView v;
        v.number = cat[i].number;
        v.id = cat[i].id;
        v.title = cat[i].title;
        v.description = cat[i].description;
        v.points = cat[i].points;
        v.category = cat[i].category;
        v.unlocked = unlocked_[i];
        v.unlocked_at = unlocked_at_[i];
        v.progress_cur = unlocked_[i] && cat[i].progress_max > 0 ? cat[i].progress_max : progress_cur_[i];
        v.progress_max = cat[i].progress_max;
        if (v.unlocked) {
            snap.unlocked_count++;
            snap.unlocked_points += v.points;
        }
        snap.items.push_back(std::move(v));
    }
    return snap;
}

bool Achievements::active_toast(uint64_t now_ms, AchievementToast& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!toast_queue_.empty()) {
        AchievementToast& front = toast_queue_.front();
        if (front.shown_ms == 0) front.shown_ms = now_ms;
        if (now_ms >= front.shown_ms + front.duration_ms) {
            toast_queue_.erase(toast_queue_.begin());
            continue;
        }
        out = front;
        return true;
    }
    return false;
}

void Achievements::draw_toast(GlPresenter& gl, int win_w, int win_h, float ui, uint64_t now_ms) const {
    AchievementToast t;
    if (!active_toast(now_ms, t)) return;

    const float elapsed = float(now_ms - t.shown_ms);
    const float dur = float(t.duration_ms);
    float slide = 1.0f;
    if (elapsed < 280.0f) slide = elapsed / 280.0f;
    else if (elapsed > dur - 350.0f) slide = std::max(0.0f, (dur - elapsed) / 350.0f);
    // Ease out cubic
    slide = 1.0f - (1.0f - slide) * (1.0f - slide) * (1.0f - slide);

    const float pad = 10.0f * ui;
    const float hdr_sz = 10.0f * ui;
    const float title_sz = 14.0f * ui;
    const float desc_sz = 10.0f * ui;
    const float badge_w = 34.0f * ui;

    const std::string hdr = "ACHIEVEMENT UNLOCKED  +" + std::to_string(t.points) + " PTS";
    const float max_w = std::min(float(win_w) - 24.0f * ui, 460.0f * ui);
    const float text_max_w = std::max(120.0f * ui, max_w - badge_w - pad * 3.0f);

    const float hdr_draw = std::min(hdr_sz, text_max_w / float(std::max<size_t>(1, hdr.size())));
    const float title_draw = std::min(title_sz, text_max_w / float(std::max<size_t>(1, t.title.size())));
    std::string desc = t.description;
    const size_t max_chars = size_t(std::max(16.0f, text_max_w / (desc_sz * 0.85f)));
    if (desc.size() > max_chars && max_chars > 4) desc = desc.substr(0, max_chars - 3) + "...";
    const float desc_draw = std::min(desc_sz, text_max_w / float(std::max<size_t>(1, desc.size())));

    const float content_w = std::max({hdr_draw * float(hdr.size()),
                                      title_draw * float(t.title.size()),
                                      desc_draw * float(desc.size())});
    const float bw = std::min(max_w, badge_w + pad * 3.0f + content_w);
    const float bh = pad * 2.0f + hdr_draw + title_draw + desc_draw + 10.0f * ui;
    const float bx = (float(win_w) - bw) * 0.5f;
    const float by = -bh + (bh + 14.0f * ui) * slide;

    // Golden-framed dark trophy card
    gl.fill({bx - 2.0f * ui, by - 2.0f * ui, bw + 4.0f * ui, bh + 4.0f * ui}, 0xF0F5C542u);
    gl.fill({bx, by, bw, bh}, 0xEC161822u);
    // Trophy emblem box on the left
    const float icon_y = by + (bh - badge_w) * 0.5f;
    gl.fill({bx + pad, icon_y, badge_w, badge_w}, 0xFF2B2312u);
    gl.fill({bx + pad + 2.0f * ui, icon_y + 2.0f * ui, badge_w - 4.0f * ui, badge_w - 4.0f * ui}, 0xFF3E3114u);
    // Simple crisp pixel trophy silhouette
    const float ix = bx + pad + badge_w * 0.22f, iy = icon_y + badge_w * 0.18f, iw = badge_w * 0.56f;
    gl.fill({ix, iy, iw, badge_w * 0.34f}, 0xFFF5C542u);
    gl.fill({ix + iw * 0.32f, iy + badge_w * 0.34f, iw * 0.36f, badge_w * 0.18f}, 0xFFD9A326u);
    gl.fill({ix + iw * 0.15f, iy + badge_w * 0.52f, iw * 0.70f, badge_w * 0.12f}, 0xFFF5C542u);

    const float tx = bx + pad * 2.0f + badge_w;
    float ty = by + pad;
    gl.text(tx, ty, hdr_draw, hdr, 0xFFF5C542u);
    ty += hdr_draw + 5.0f * ui;
    gl.text(tx, ty, title_draw, t.title, 0xFFFFFFFFu);
    ty += title_draw + 5.0f * ui;
    gl.text(tx, ty, desc_draw, desc, 0xFFB8C0D0u);
    (void)win_h;
}

}  // namespace ph
