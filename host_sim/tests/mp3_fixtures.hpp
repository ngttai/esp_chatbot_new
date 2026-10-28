#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace mp3fixture {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

inline void append(Bytes &out, const Bytes &more) { out.insert(out.end(), more.begin(), more.end()); }

inline Bytes text(const std::string &s) { return Bytes(s.begin(), s.end()); }

inline Bytes synchsafe(uint32_t v)
{
    return {uint8_t((v >> 21) & 0x7F), uint8_t((v >> 14) & 0x7F), uint8_t((v >> 7) & 0x7F), uint8_t(v & 0x7F)};
}

inline Bytes be32(uint32_t v) { return {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; }

inline Bytes frame(int major, const std::string &id, const Bytes &payload)
{
    Bytes out = text(id);
    append(out, major == 4 ? synchsafe(uint32_t(payload.size())) : be32(uint32_t(payload.size())));
    out.push_back(0);
    out.push_back(0);
    append(out, payload);
    return out;
}

inline Bytes text_frame(int major, const std::string &id, uint8_t encoding, const Bytes &value)
{
    Bytes payload{encoding};
    append(payload, value);
    return frame(major, id, payload);
}

inline Bytes utf16_le(const std::u16string &s, bool bom = true)
{
    Bytes out;
    if (bom) {
        out.push_back(0xFF);
        out.push_back(0xFE);
    }
    for (char16_t c : s) {
        out.push_back(uint8_t(c & 0xFF));
        out.push_back(uint8_t(c >> 8));
    }
    return out;
}

inline Bytes id3v2(int major, const Bytes &frames, size_t padding = 0, uint8_t flags = 0)
{
    Bytes out{'I', 'D', '3', uint8_t(major), 0, flags};
    append(out, synchsafe(uint32_t(frames.size() + padding)));
    append(out, frames);
    out.insert(out.end(), padding, 0);
    return out;
}

// MPEG1 Layer III, 128 kbps, 44.1 kHz, stereo: 417 bytes per frame.
inline Bytes mpeg_frames(size_t count)
{
    Bytes out;
    for (size_t i = 0; i < count; i++) {
        Bytes f(417, 0);
        f[0] = 0xFF;
        f[1] = 0xFB;
        f[2] = 0x90;
        f[3] = 0x00;
        append(out, f);
    }
    return out;
}

inline Bytes xing_frames(uint32_t total_frames, size_t physical_frames)
{
    Bytes out = mpeg_frames(physical_frames);
    memcpy(out.data() + 4 + 32, "Xing", 4);
    out[4 + 32 + 7] = 0x01;
    const Bytes n = be32(total_frames);
    memcpy(out.data() + 4 + 32 + 8, n.data(), 4);
    return out;
}

inline Bytes id3v1(const std::string &title, const std::string &artist, const std::string &album)
{
    Bytes out(128, 0);
    memcpy(out.data(), "TAG", 3);
    memcpy(out.data() + 3, title.data(), std::min<size_t>(30, title.size()));
    memcpy(out.data() + 33, artist.data(), std::min<size_t>(30, artist.size()));
    memcpy(out.data() + 63, album.data(), std::min<size_t>(30, album.size()));
    return out;
}

inline void write_file(const fs::path &path, const Bytes &data)
{
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(data.data()), std::streamsize(data.size()));
}


} // namespace mp3fixture
