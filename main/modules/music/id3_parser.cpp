#include "id3_parser.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace music {
namespace {

constexpr size_t kFrameTextMax = 384;
constexpr size_t kSyncWindow = 4096;

class Reader {
public:
    Reader(FILE *file, const ParseOptions &options): file_(file), options_(options) {}

    bool poll()
    {
        if (!cancelled_ && options_.cancelled != nullptr && options_.cancelled(options_.ctx)) {
            cancelled_ = true;
        }
        return !cancelled_;
    }

    bool read_at(long offset, void *buf, size_t size)
    {
        if (!poll()) return false;
        if (fseek(file_, offset, SEEK_SET) != 0) return false;
        return fread(buf, 1, size, file_) == size;
    }

    size_t read_some_at(long offset, void *buf, size_t size)
    {
        if (!poll()) return 0;
        if (fseek(file_, offset, SEEK_SET) != 0) return 0;
        return fread(buf, 1, size, file_);
    }

    bool cancelled() const { return cancelled_; }

private:
    FILE *file_;
    const ParseOptions &options_;
    bool cancelled_ = false;
};

uint32_t synchsafe(const uint8_t *p)
{
    return (uint32_t(p[0] & 0x7F) << 21) | (uint32_t(p[1] & 0x7F) << 14) | (uint32_t(p[2] & 0x7F) << 7) |
           uint32_t(p[3] & 0x7F);
}

uint32_t be32(const uint8_t *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

class Utf8Writer {
public:
    Utf8Writer(char *buf, size_t cap): buf_(buf), cap_(cap) { buf_[0] = '\0'; }

    void put(uint32_t cp)
    {
        if (cp == ' ' && len_ == 0 && !pending_separator_) return;
        char tmp[4];
        size_t n;
        if (cp < 0x80) {
            tmp[0] = char(cp);
            n = 1;
        } else if (cp < 0x800) {
            tmp[0] = char(0xC0 | (cp >> 6));
            tmp[1] = char(0x80 | (cp & 0x3F));
            n = 2;
        } else if (cp < 0x10000) {
            tmp[0] = char(0xE0 | (cp >> 12));
            tmp[1] = char(0x80 | ((cp >> 6) & 0x3F));
            tmp[2] = char(0x80 | (cp & 0x3F));
            n = 3;
        } else {
            tmp[0] = char(0xF0 | (cp >> 18));
            tmp[1] = char(0x80 | ((cp >> 12) & 0x3F));
            tmp[2] = char(0x80 | ((cp >> 6) & 0x3F));
            tmp[3] = char(0x80 | (cp & 0x3F));
            n = 4;
        }
        append(tmp, n);
    }

    // Starts a new value; values are joined with ", ".
    void separator()
    {
        if (len_ > 0) pending_separator_ = true;
    }

    void finish()
    {
        while (len_ > 0 && buf_[len_ - 1] == ' ') buf_[--len_] = '\0';
    }

private:
    void append(const char *s, size_t n)
    {
        if (full_) return;
        if (pending_separator_) {
            pending_separator_ = false;
            if (len_ + 2 + n >= cap_) {
                full_ = true;
                return;
            }
            buf_[len_++] = ',';
            buf_[len_++] = ' ';
        }
        if (len_ + n >= cap_) {
            full_ = true;
            return;
        }
        memcpy(buf_ + len_, s, n);
        len_ += n;
        buf_[len_] = '\0';
    }

    char *buf_;
    size_t cap_;
    size_t len_ = 0;
    bool full_ = false;
    bool pending_separator_ = false;
};

void decode_text(uint8_t encoding, const uint8_t *data, size_t size, char *out, size_t cap)
{
    Utf8Writer writer(out, cap);
    if (encoding == 0 || encoding == 3) {
        for (size_t i = 0; i < size; i++) {
            if (data[i] == 0) {
                writer.separator();
            } else if (encoding == 0) {
                writer.put(data[i]);
            } else if (data[i] < 0x80) {
                writer.put(data[i]);
            } else {
                const size_t need = data[i] >= 0xF0 ? 4 : data[i] >= 0xE0 ? 3 : data[i] >= 0xC0 ? 2 : 1;
                if (need == 1 || i + need > size) continue;
                uint32_t cp = need == 2 ? (data[i] & 0x1F) : need == 3 ? (data[i] & 0x0F) : (data[i] & 0x07);
                for (size_t k = 1; k < need; k++) cp = (cp << 6) | (data[i + k] & 0x3F);
                writer.put(cp);
                i += need - 1;
            }
        }
    } else {
        bool big_endian = encoding == 2;
        bool segment_start = true;
        for (size_t i = 0; i + 1 < size; i += 2) {
            if (segment_start && encoding == 1) {
                if (data[i] == 0xFF && data[i + 1] == 0xFE) {
                    big_endian = false;
                    segment_start = false;
                    continue;
                }
                if (data[i] == 0xFE && data[i + 1] == 0xFF) {
                    big_endian = true;
                    segment_start = false;
                    continue;
                }
            }
            uint32_t unit = big_endian ? (uint32_t(data[i]) << 8 | data[i + 1]) : (uint32_t(data[i + 1]) << 8 | data[i]);
            if (unit == 0) {
                writer.separator();
                segment_start = true;
                continue;
            }
            segment_start = false;
            if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < size) {
                uint32_t low = big_endian ? (uint32_t(data[i + 2]) << 8 | data[i + 3])
                                          : (uint32_t(data[i + 3]) << 8 | data[i + 2]);
                if (low >= 0xDC00 && low < 0xE000) {
                    writer.put(0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
                    i += 2;
                    continue;
                }
            }
            writer.put(unit);
        }
    }
    writer.finish();
}

size_t remove_unsynchronisation(uint8_t *data, size_t size)
{
    size_t w = 0;
    for (size_t r = 0; r < size; r++) {
        data[w++] = data[r];
        if (data[r] == 0xFF && r + 1 < size && data[r + 1] == 0x00) r++;
    }
    return w;
}

struct V2Result {
    bool found = false;
    uint32_t total_size = 0;
    uint32_t tlen_ms = 0;
};

void decode_frame_text(const uint8_t *buf, size_t size, char *out, size_t cap)
{
    if (size < 2) return;
    decode_text(buf[0], buf + 1, size - 1, out, cap);
}

V2Result parse_id3v2(Reader &reader, TrackMeta &meta)
{
    V2Result result;
    uint8_t header[10];
    if (!reader.read_at(0, header, sizeof(header))) return result;
    if (memcmp(header, "ID3", 3) != 0 || header[3] < 2 || header[3] > 4) return result;
    for (int i = 6; i < 10; i++) {
        if (header[i] & 0x80) return result;
    }

    const uint8_t major = header[3];
    const uint8_t flags = header[5];
    const uint32_t body = synchsafe(header + 6);
    result.found = true;
    result.total_size = 10 + body + ((major == 4 && (flags & 0x10)) ? 10 : 0);
    if (major == 2) return result;

    const bool tag_unsync = (flags & 0x80) != 0;
    uint32_t pos = 10;
    const uint32_t end = 10 + body;
    if (flags & 0x40) {
        uint8_t ext[4];
        if (!reader.read_at(10, ext, sizeof(ext))) return result;
        pos += major == 4 ? synchsafe(ext) : be32(ext) + 4;
    }

    bool have_title = false, have_artist = false, have_album = false, have_tlen = false;
    while (pos + 10 <= end && !(have_title && have_artist && have_album && have_tlen)) {
        uint8_t fh[10];
        if (!reader.read_at(long(pos), fh, sizeof(fh))) return result;
        if (fh[0] == 0) break;
        bool valid_id = true;
        for (int i = 0; i < 4; i++) {
            if (!((fh[i] >= 'A' && fh[i] <= 'Z') || (fh[i] >= '0' && fh[i] <= '9'))) valid_id = false;
        }
        if (!valid_id) break;
        const uint32_t size = major == 4 ? synchsafe(fh + 4) : be32(fh + 4);
        if (size == 0 || pos + 10 + size > end) break;

        const uint8_t fl = fh[9];
        const bool skip_flags = major == 4 ? (fl & 0x0C) != 0 : (fl & 0xC0) != 0;
        const bool length_indicator = major == 4 && (fl & 0x01);
        const bool frame_unsync = major == 4 && (fl & 0x02);

        enum { None, Title, Artist, Album, Tlen } kind = None;
        if (memcmp(fh, "TIT2", 4) == 0) kind = Title;
        else if (memcmp(fh, "TPE1", 4) == 0) kind = Artist;
        else if (memcmp(fh, "TALB", 4) == 0) kind = Album;
        else if (memcmp(fh, "TLEN", 4) == 0) kind = Tlen;

        if (kind != None && !skip_flags) {
            uint8_t buf[kFrameTextMax];
            size_t skip = length_indicator ? 4 : 0;
            size_t want = size > skip ? size - skip : 0;
            if (want > sizeof(buf)) want = sizeof(buf);
            if (want > 0 && reader.read_at(long(pos + 10 + skip), buf, want)) {
                if (tag_unsync || frame_unsync) want = remove_unsynchronisation(buf, want);
                switch (kind) {
                case Title:
                    decode_frame_text(buf, want, meta.title, sizeof(meta.title));
                    have_title = true;
                    break;
                case Artist:
                    decode_frame_text(buf, want, meta.artist, sizeof(meta.artist));
                    have_artist = true;
                    break;
                case Album:
                    decode_frame_text(buf, want, meta.album, sizeof(meta.album));
                    have_album = true;
                    break;
                default: {
                    char text[16] = {};
                    decode_frame_text(buf, want, text, sizeof(text));
                    result.tlen_ms = uint32_t(strtoul(text, nullptr, 10));
                    have_tlen = result.tlen_ms > 0;
                    break;
                }
                }
            } else if (reader.cancelled()) {
                return result;
            }
        }
        pos += 10 + size;
    }
    return result;
}

void copy_latin1_field(const uint8_t *src, size_t size, char *out, size_t cap)
{
    while (size > 0 && (src[size - 1] == 0 || src[size - 1] == ' ')) size--;
    Utf8Writer writer(out, cap);
    for (size_t i = 0; i < size; i++) {
        if (src[i] == 0) break;
        writer.put(src[i]);
    }
    writer.finish();
}

bool parse_id3v1(Reader &reader, long file_size, TrackMeta &meta)
{
    if (file_size < 128) return false;
    uint8_t tag[128];
    if (!reader.read_at(file_size - 128, tag, sizeof(tag))) return false;
    if (memcmp(tag, "TAG", 3) != 0) return false;
    if (meta.title[0] == '\0') copy_latin1_field(tag + 3, 30, meta.title, sizeof(meta.title));
    if (meta.artist[0] == '\0') copy_latin1_field(tag + 33, 30, meta.artist, sizeof(meta.artist));
    if (meta.album[0] == '\0') copy_latin1_field(tag + 63, 30, meta.album, sizeof(meta.album));
    return true;
}

struct MpegFrame {
    uint32_t bitrate;
    uint32_t sample_rate;
    uint32_t samples;
    uint32_t length;
    uint32_t side_info;
    bool mpeg1;
    bool crc;
};

bool parse_frame_header(const uint8_t *h, MpegFrame &frame)
{
    if (h[0] != 0xFF || (h[1] & 0xE0) != 0xE0) return false;
    const int version = (h[1] >> 3) & 0x03;
    const int layer = (h[1] >> 1) & 0x03;
    if (version == 1 || layer != 1) return false;
    const int bitrate_index = h[2] >> 4;
    const int rate_index = (h[2] >> 2) & 0x03;
    if (bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) return false;

    static const uint16_t kBitrateV1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const uint16_t kBitrateV2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const uint32_t kRateV1[] = {44100, 48000, 32000};
    const bool mpeg1 = version == 3;
    frame.mpeg1 = mpeg1;
    frame.bitrate = uint32_t(mpeg1 ? kBitrateV1[bitrate_index] : kBitrateV2[bitrate_index]) * 1000;
    frame.sample_rate = kRateV1[rate_index] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
    frame.samples = mpeg1 ? 1152 : 576;
    const uint32_t padding = (h[2] >> 1) & 1;
    frame.length = (mpeg1 ? 144 : 72) * frame.bitrate / frame.sample_rate + padding;
    const bool mono = (h[3] >> 6) == 3;
    frame.side_info = mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17);
    frame.crc = (h[1] & 1) == 0;
    return frame.length > 4;
}

uint32_t estimate_duration_ms(Reader &reader, long audio_start, long file_size, bool has_v1)
{
    if (audio_start >= file_size) return 0;
    uint8_t *window = static_cast<uint8_t *>(malloc(kSyncWindow));
    if (window == nullptr) return 0;

    uint32_t duration_ms = 0;
    const size_t got = reader.read_some_at(audio_start, window, kSyncWindow);
    for (size_t i = 0; i + 4 <= got; i++) {
        MpegFrame frame;
        if (!parse_frame_header(window + i, frame)) continue;
        const long frame_pos = audio_start + long(i);
        uint8_t next[4];
        const long next_pos = frame_pos + long(frame.length);
        if (next_pos + 4 <= file_size) {
            MpegFrame next_frame;
            if (!reader.read_at(next_pos, next, sizeof(next)) || !parse_frame_header(next, next_frame)) continue;
        }

        uint8_t probe[24];
        const long xing_pos = frame_pos + 4 + (frame.crc ? 2 : 0) + long(frame.side_info);
        uint32_t frames = 0;
        if (reader.read_at(xing_pos, probe, sizeof(probe)) &&
            (memcmp(probe, "Xing", 4) == 0 || memcmp(probe, "Info", 4) == 0) && (probe[7] & 0x01)) {
            frames = be32(probe + 8);
        } else if (reader.read_at(frame_pos + 4 + 32, probe, sizeof(probe)) && memcmp(probe, "VBRI", 4) == 0) {
            frames = be32(probe + 14);
        }
        if (frames > 0) {
            duration_ms = uint32_t(uint64_t(frames) * frame.samples * 1000 / frame.sample_rate);
        } else if (frame.bitrate > 0) {
            long audio_bytes = file_size - frame_pos - (has_v1 ? 128 : 0);
            if (audio_bytes > 0) duration_ms = uint32_t(uint64_t(audio_bytes) * 8000 / frame.bitrate);
        }
        break;
    }
    free(window);
    return duration_ms;
}

} // namespace

ParseResult parse_mp3_file(const char *path, TrackMeta &out, const ParseOptions &options)
{
    memset(&out, 0, sizeof(out));
    FILE *file = fopen(path, "rb");
    if (file == nullptr) return ParseResult::IoError;

    Reader reader(file, options);
    ParseResult result = ParseResult::Ok;
    do {
        if (fseek(file, 0, SEEK_END) != 0) {
            result = ParseResult::IoError;
            break;
        }
        const long file_size = ftell(file);
        if (file_size < 0) {
            result = ParseResult::IoError;
            break;
        }

        const V2Result v2 = parse_id3v2(reader, out);
        if (reader.cancelled()) {
            result = ParseResult::Cancelled;
            break;
        }
        const bool v1 = parse_id3v1(reader, file_size, out);
        if (reader.cancelled()) {
            result = ParseResult::Cancelled;
            break;
        }
        out.has_tag = v2.found || v1;

        if (v2.tlen_ms > 0) {
            out.duration_ms = v2.tlen_ms;
        } else {
            out.duration_ms = estimate_duration_ms(reader, long(v2.total_size), file_size, v1);
            if (reader.cancelled()) result = ParseResult::Cancelled;
        }
    } while (false);

    fclose(file);
    if (result != ParseResult::Ok) memset(&out, 0, sizeof(out));
    return result;
}

} // namespace music
