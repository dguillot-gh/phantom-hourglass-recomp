#include "embedded_toolchain.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace ph {

std::string unpack_toolchain(const std::string& dir) {
    std::error_code ec;
    const fs::path root = fs::u8path(dir);
    fs::remove_all(root, ec);
    for (int i = 0; i < kToolchainFileCount; i++) {
        const EmbeddedFile& f = kToolchainFiles[i];
        const fs::path p = root / fs::u8path(f.path);
        fs::create_directories(p.parent_path(), ec);
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char*>(f.data), std::streamsize(f.size));
        if (!out) return "Cannot write the build files to " + dir;
    }
    return "";
}

}  // namespace ph
