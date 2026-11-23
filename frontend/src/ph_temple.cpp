// Temple of the Ocean King floor checkpoints (ph_temple.hpp).
#include "ph_temple.hpp"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <vector>

#include "ntr/recomp.h"

namespace {

constexpr int32_t kTempleCourse = 37;  // dngn_main: the temple's floors (one map each)
constexpr int32_t kLobbyCourse = 38;   // dngn_main_f: the lobby
constexpr uint32_t kMapManager = 0x027E0E60u;

std::mutex g_mutex;
std::map<int, int> g_floors;  // map -> the entrance Link first arrived at (from the stairs above)
std::filesystem::path g_file;
int g_selected = -1;           // the map the lobby's stairs lead to; -1: the game's own (the first floor)
std::atomic<int> g_course{-1};  // the course Link is in (from the last frame)

void save_locked() {
    if (g_file.empty()) return;
    std::ofstream f(g_file, std::ios::trunc);
    for (const auto& [map, entrance] : g_floors) f << map << " " << entrance << "\n";
}

// The current course's index, or -1. The MapManager's first field is the Course (whose first field is its index).
int32_t course_index(uint32_t mm_field0, uint32_t deref) {
    // 0x027E0E60 holds the MapManager itself (its first field: the Course pointer) or a pointer to it.
    if (deref < 0x100u) return int32_t(deref);  // (it held the Course: deref is its index)
    return -1;
}

std::string floor_label(int map) {
    char text[16];
    if (map == 0) return "1F";
    std::snprintf(text, sizeof text, "B%d", map);
    return text;
}

}  // namespace

// Overlay 0 0x0208033C: MapBase::FindExit(this, id, Exit* out). The game's own search (exits at this + 0x118, count at
// + 0x11C, 0x18 bytes each, id at + 0x14), plus the floor records and the lobby's redirect.
extern "C" int ph_find_exit(NtrCpu* c) {
    const uint32_t self = c->r[0], id = c->r[1], out = c->r[2];
    c->r[0] = 0;
    if (int32_t(id) < 1) return 1;
    const uint32_t begin = ntr_rd32(c, self + 0x118u), count = ntr_rd32(c, self + 0x11Cu);
    uint32_t found = 0;
    for (uint32_t i = 0; i < count && i < 256; i++) {
        const uint32_t e = begin + i * 0x18u;
        if (ntr_rd8(c, e + 0x14u) == (id & 0xFFu)) {
            found = e;
            break;
        }
    }
    if (!found) return 1;
    for (uint32_t k = 0; k < 0x18u; k += 4) ntr_wr32(c, out + k, ntr_rd32(c, found + k));
    c->r[0] = 1;

    const int32_t from = g_course.load();
    const int32_t dest = int32_t(ntr_rd32(c, out + 0x00u));
    const int map = ntr_rd8(c, out + 0x12u), entrance = ntr_rd8(c, out + 0x13u);
    // (The game asks again every few frames while Link walks through: logged once per change.)
    static uint32_t s_last = 0;
    const uint32_t key = (uint32_t(from & 0xFF) << 24) | (uint32_t(dest & 0xFF) << 16) | (uint32_t(map) << 8) | uint32_t(entrance);
    if (key != s_last) {
        s_last = key;
        std::fprintf(stderr, "temple: exit %u from course %d -> course %d map %d entrance %d\n", id, from, dest, map,
                     entrance);
    }
    if (dest != kTempleCourse) return 1;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_floors.count(map)) {
        g_floors[map] = entrance;
        save_locked();
    }
    // The redirect is on 1F's stairs down (to B1, map 1, at the entrance Link first came down to), not the hall's: the
    // sand timer starts when Link enters 1F, and a floor reached without passing through 1F has none.
    const auto first_b1 = g_floors.find(1);
    if (from == kTempleCourse && map == 1 && first_b1 != g_floors.end() && entrance == first_b1->second &&
        g_selected > 1) {
        const auto it = g_floors.find(g_selected);
        if (it != g_floors.end()) {
            ntr_wr8(c, out + 0x12u, uint8_t(it->first));
            ntr_wr8(c, out + 0x13u, uint8_t(it->second));
            std::fprintf(stderr, "temple: 1F stairs -> map %d entrance %d\n", it->first, it->second);
        }
    }
    return 1;
}

namespace ph::temple {

void set_dir(const std::filesystem::path& dir) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_file = dir / "temple_floors.txt";
    g_floors.clear();
    std::ifstream f(g_file);
    int map = 0, entrance = 0;
    while (f >> map >> entrance)
        if (map >= 0 && map < 256 && entrance >= 0 && entrance < 256) g_floors[map] = entrance;
}

uint32_t frame(const std::function<uint32_t(uint32_t)>& peek32, uint32_t held) {
    static uint32_t s_prev = 0;
    constexpr uint32_t kKeyL = 1u << 9;
    const uint32_t pressed = held & ~s_prev;
    s_prev = held;
    const uint32_t field0 = peek32(kMapManager);
    int32_t course = -1;
    if (field0 >= 0x02000000u && field0 < 0x02400000u) {
        course = course_index(field0, peek32(field0));
        if (course < 0) {  // (a pointer to the MapManager: its first field is the Course)
            const uint32_t c2 = peek32(field0);
            if (c2 >= 0x02000000u && c2 < 0x02400000u) {
                const uint32_t idx = peek32(c2);
                if (idx < 0x100u) course = int32_t(idx);
            }
        }
    }
    g_course = course;
    if (course != kLobbyCourse) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_floors.empty() || g_floors.rbegin()->first < 2) return 0;  // (nothing past B1 yet)
    if (!(pressed & kKeyL)) return kKeyL;
    // The choices: B1 (the game's own, -1), then every floor reached past it, in order.
    std::vector<int> choices{-1};
    for (const auto& [map, entrance] : g_floors)
        if (map >= 2) choices.push_back(map);
    size_t at = 0;
    for (size_t i = 0; i < choices.size(); i++)
        if (choices[i] == g_selected) at = i;
    at = (at + 1) % choices.size();  // (L: the next floor, around to B1)
    g_selected = choices[at];
    return kKeyL;  // (L chooses the floor here: the game does not see it)
}

std::string picker_text() {
    if (g_course.load() != kLobbyCourse) return {};
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_floors.empty() || g_floors.rbegin()->first < 2) return {};
    const std::string where = g_selected < 2 ? std::string("B1") : floor_label(g_selected);
    return "1F Stairs Down: " + where + "   (L)";
}

}  // namespace ph::temple
