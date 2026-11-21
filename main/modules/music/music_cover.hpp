#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include "id3_parser.hpp"

namespace music {
struct CoverImage {
    static constexpr unsigned kSize = 128;
    // RGB565 in native little-endian order. Large allocations use PSRAM on ESP.
    std::shared_ptr<uint8_t> pixels;
};
using CoverPtr = std::shared_ptr<const CoverImage>;
// JPEG APIC/PIC in ID3v2, then cover.jpg/folder.jpg beside the MP3.
// Unsupported, corrupt, oversized or cancelled artwork returns an empty pointer.
CoverPtr load_cover(const char *path, const ParseOptions &options = {});
} // namespace music
