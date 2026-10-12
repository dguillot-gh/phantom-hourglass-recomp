// Minimal PNG writer for screenshots (uncompressed deflate: no image library needed).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ph {

// Writes `pixels` (0xAARRGGBB, `width` x `height`, rows top to bottom) as an RGB PNG. False if the file cannot be written.
bool write_png(const std::string& path, const uint32_t* pixels, int width, int height);

// Reads a PNG that write_png wrote (RGB, uncompressed deflate, no filters) into 0xFFRRGGBB rows. False for any other
// PNG, or no file.
bool read_png(const std::string& path, std::vector<uint32_t>& pixels, int& width, int& height);

}  // namespace ph
