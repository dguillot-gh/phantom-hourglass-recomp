#include <array>
#include <cstdio>

#include "ntr/rom.hpp"

namespace ntr {

uint32_t crc32(std::span<const uint8_t> data) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    uint32_t c = 0xFFFFFFFFu;
    for (uint8_t b : data) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return ~c;
}

std::string crc32_hex(uint32_t crc) {
    char buf[9];
    std::snprintf(buf, sizeof buf, "%08x", crc);
    return buf;
}

// Phantom Hourglass (USA). Both dumps share one config (frontend/config/azee): the D-pad patch changes code inside the
// game's functions but moves none of them (every function start of the zeldaret/ph decomp's USA config checks out).
static const KnownRom kKnownRoms[] = {
    {"4c8f52dd719918bbcd46e73a8bae8628139c1b85", "", "AZEE", "USA",
     "The Legend of Zelda - Phantom Hourglass (USA) (En,Fr,Es)", true},
    {"7e0d26d143992f1eee182b24f1abb9e7f038eacf", "404e6312", "AZEE", "USA (D-pad patch)",
     "The Legend of Zelda - Phantom Hourglass (USA) (D-pad patch)", true},
};

std::span<const KnownRom> known_roms() { return kKnownRoms; }

const KnownRom* find_known_rom(const Sha1Digest& digest) {
    std::string hex = to_hex(digest);
    for (const KnownRom& r : kKnownRoms)
        if (hex == r.sha1_hex) return &r;
    return nullptr;
}

}  // namespace ntr
