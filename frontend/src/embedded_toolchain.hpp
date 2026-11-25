// The first-run build's support files (TinyCC headers + libs, runtime headers, game config), compiled into the exe
// by tools/embed_toolchain.cmake. unpack_toolchain writes them to a folder for build_game.
#pragma once
#include <cstddef>
#include <string>

namespace ph {

struct EmbeddedFile {
    const char* path;
    const unsigned char* data;
    size_t size;
};
extern const EmbeddedFile kToolchainFiles[];
extern const int kToolchainFileCount;

// Writes every embedded file under dir (which is emptied first). Returns "" or an error message.
std::string unpack_toolchain(const std::string& dir);

}  // namespace ph
