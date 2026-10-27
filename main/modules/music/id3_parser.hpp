#pragma once

#include <cstddef>
#include <cstdint>

namespace music {

struct TrackMeta {
    char title[96];
    char artist[64];
    char album[64];
    uint32_t duration_ms;
    bool has_tag;
};

enum class ParseResult : uint8_t {
    Ok,
    Cancelled,
    IoError,
};

struct ParseOptions {
    // Polled between file reads; returning true aborts the parse.
    bool (*cancelled)(void *ctx) = nullptr;
    void *ctx = nullptr;
};

// Reads ID3v2/ID3v1 text fields and the duration of an MP3 file.
ParseResult parse_mp3_file(const char *path, TrackMeta &out, const ParseOptions &options = {});

} // namespace music
