#include "png.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ph {
namespace {

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            t[i] = v;
        }
        return t;
    }();
    c = ~c;
    for (size_t i = 0; i < n; i++) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return ~c;
}

void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}

void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, uint32_t(data.size()));
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    put32(out, crc32(body.data(), body.size()));
}

}  // namespace

bool write_png(const std::string& path, const uint32_t* pixels, int width, int height) {
    // Raw scanlines: filter byte 0, then RGB.
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (1 + size_t(width) * 3));
    for (int y = 0; y < height; y++) {
        raw.push_back(0);
        for (int x = 0; x < width; x++) {
            const uint32_t p = pixels[size_t(y) * width + x];
            raw.push_back(uint8_t(p >> 16));
            raw.push_back(uint8_t(p >> 8));
            raw.push_back(uint8_t(p));
        }
    }
    // zlib stream of stored (uncompressed) deflate blocks, at most 65535 bytes each.
    std::vector<uint8_t> z = {0x78, 0x01};
    for (size_t pos = 0; pos < raw.size() || pos == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n));
        z.push_back(uint8_t(n >> 8));
        z.push_back(uint8_t(~n));
        z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
        if (last) break;
    }
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    put32(z, (b << 16) | a);

    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    put32(ihdr, uint32_t(width));
    put32(ihdr, uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});  // 8 bits, RGB, deflate, no filter method, no interlace
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    return std::fclose(f) == 0 && ok;
}

bool read_png(const std::string& path, std::vector<uint32_t>& pixels, int& width, int& height) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::vector<uint8_t> in;
    uint8_t buf[65536];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) in.insert(in.end(), buf, buf + n);
    std::fclose(f);
    auto get32 = [&](size_t at) {
        return uint32_t(in[at]) << 24 | uint32_t(in[at + 1]) << 16 | uint32_t(in[at + 2]) << 8 | in[at + 3];
    };
    if (in.size() < 8 + 25 || in[0] != 0x89 || in[1] != 'P') return false;
    std::vector<uint8_t> z;
    width = height = 0;
    for (size_t at = 8; at + 12 <= in.size();) {
        const uint32_t len = get32(at);
        if (at + 12 + len > in.size()) return false;
        const char* type = reinterpret_cast<const char*>(&in[at + 4]);
        const uint8_t* body = &in[at + 8];
        if (std::memcmp(type, "IHDR", 4) == 0) {
            width = int(get32(at + 8));
            height = int(get32(at + 12));
            if (body[8] != 8 || body[9] != 2 || body[12] != 0) return false;  // 8-bit RGB, not interlaced
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            z.insert(z.end(), body, body + len);
        }
        at += 12 + len;
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 || z.size() < 2) return false;
    // Stored deflate blocks only.
    std::vector<uint8_t> raw;
    for (size_t at = 2; at + 5 <= z.size();) {
        const uint8_t head = z[at];
        if ((head >> 1) & 3) return false;  // a compressed block: not one of ours
        const size_t n = size_t(z[at + 1]) | size_t(z[at + 2]) << 8;
        if (at + 5 + n > z.size()) return false;
        raw.insert(raw.end(), z.begin() + long(at + 5), z.begin() + long(at + 5 + n));
        at += 5 + n;
        if (head & 1) break;
    }
    const size_t stride = 1 + size_t(width) * 3;
    if (raw.size() < stride * size_t(height)) return false;
    pixels.resize(size_t(width) * size_t(height));
    for (int y = 0; y < height; y++) {
        const uint8_t* row = &raw[size_t(y) * stride];
        if (row[0] != 0) return false;  // a filtered row: not one of ours
        for (int x = 0; x < width; x++) {
            const uint8_t* p = row + 1 + x * 3;
            pixels[size_t(y) * width + x] = 0xFF000000u | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
        }
    }
    return true;
}

}  // namespace ph
