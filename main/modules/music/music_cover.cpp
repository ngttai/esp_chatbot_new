#include "music_cover.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#else
#include <csetjmp>
#include <jpeglib.h>
#endif

namespace music {
namespace {
constexpr size_t kMaxEncoded = 512 * 1024;
constexpr unsigned kMaxDimension = 1024;
using Buffer = std::shared_ptr<uint8_t>;
Buffer allocate(size_t size)
{
#ifdef ESP_PLATFORM
    return Buffer(static_cast<uint8_t *>(heap_caps_aligned_alloc(16, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)), free);
#else
    return Buffer(static_cast<uint8_t *>(malloc(size)), free);
#endif
}
bool cancelled(const ParseOptions &o) { return o.cancelled && o.cancelled(o.ctx); }
uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint32_t sync32(const uint8_t *p) { return uint32_t(p[0] & 127) << 21 | uint32_t(p[1] & 127) << 14 | uint32_t(p[2] & 127) << 7 | (p[3] & 127); }
struct File {
    FILE *f;
    explicit File(const char *p): f(fopen(p, "rb")) {}
    ~File() { if (f) fclose(f); }
};
bool read(FILE *f, long offset, void *out, size_t size, const ParseOptions &o)
{
    return !cancelled(o) && fseek(f, offset, SEEK_SET) == 0 && fread(out, 1, size, f) == size;
}
Buffer read_image(FILE *f, long offset, size_t size, const ParseOptions &o)
{
    if (size < 4 || size > kMaxEncoded) return {};
    uint8_t magic[2];
    if (!read(f, offset, magic, 2, o) || magic[0] != 255 || magic[1] != 216) return {};
    auto buf = allocate(size);
    if (!buf) return {};
    for (size_t pos = 0; pos < size; pos += 4096) {
        if (!read(f, offset + pos, buf.get() + pos, std::min(size_t(4096), size - pos), o)) return {};
    }
    return buf;
}
// Walk bounded frame headers without reading the entire ID3 tag into RAM.
Buffer embedded(FILE *f, size_t &image_size, const ParseOptions &o)
{
    uint8_t h[10];
    if (!read(f, 0, h, 10, o) || memcmp(h, "ID3", 3) || h[3] < 2 || h[3] > 4) return {};
    for (unsigned i = 6; i < 10; ++i) if (h[i] & 128) return {};
    const unsigned version = h[3];
    // Unsynchronised, compressed or extended tags are left to the fallback.
    if (h[5] & 0xC0) return {};
    const uint32_t end = 10 + sync32(h + 6);
    if (end > 16 * 1024 * 1024 || fseek(f, 0, SEEK_END) || ftell(f) < long(end)) return {};
    const unsigned header_size = version == 2 ? 6 : 10;
    Buffer fallback;
    size_t fallback_size = 0;
    for (uint32_t pos = 10, frames = 0; pos + header_size <= end && frames < 1024; ++frames) {
        if (!read(f, pos, h, header_size, o) || !h[0]) break;
        const uint32_t size = version == 2 ? (uint32_t(h[3]) << 16 | uint32_t(h[4]) << 8 | h[5]) : version == 4 ? sync32(h + 4) : be32(h + 4);
        if (!size || size > end - pos - header_size) break;
        const bool picture = version == 2 ? !memcmp(h, "PIC", 3) : !memcmp(h, "APIC", 4);
        const bool flags_ok = version == 2 || h[9] == 0;
        if (picture && flags_ok) {
            uint8_t prefix[512];
            const size_t count = std::min(size_t(size), sizeof(prefix));
            if (!read(f, pos + header_size, prefix, count, o)) return {};
            const unsigned encoding = prefix[0];
            size_t at = version == 2 ? 4 : 1;
            if (version != 2) {
                while (at < count && prefix[at]) ++at;
                ++at;
            }
            if (at >= count) return {};
            const unsigned picture_type = prefix[at++];
            if (encoding == 0 || encoding == 3) {
                while (at < count && prefix[at]) ++at;
                ++at;
            } else if (encoding == 1 || encoding == 2) {
                while (at + 1 < count && (prefix[at] || prefix[at + 1])) at += 2;
                at += 2;
            } else return {};
            if (at < count && at < size) {
                const size_t bytes = size - at;
                if (picture_type == 3 || !fallback) {
                    if (auto buf = read_image(f, pos + header_size + at, bytes, o)) {
                        if (picture_type == 3) { image_size = bytes; return buf; }
                        fallback = std::move(buf);
                        fallback_size = bytes;
                    }
                }
            }
        }
        pos += header_size + size;
    }
    image_size = fallback_size;
    return fallback;
}
#ifndef ESP_PLATFORM
struct JpegError { jpeg_error_mgr base; jmp_buf jump; };
void jpeg_failed(j_common_ptr ptr) { longjmp(reinterpret_cast<JpegError *>(ptr->err)->jump, 1); }
#endif
Buffer decode(const Buffer &encoded, size_t size, unsigned &width, unsigned &height)
{
#ifdef ESP_PLATFORM
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    jpeg_dec_handle_t decoder = nullptr;
    if (jpeg_dec_open(&config, &decoder) != JPEG_ERR_OK) return {};
    jpeg_dec_io_t io{};
    io.inbuf = encoded.get();
    io.inbuf_len = static_cast<int>(size);
    jpeg_dec_header_info_t header{};
    Buffer pixels;
    int length = 0;
    if (jpeg_dec_parse_header(decoder, &io, &header) == JPEG_ERR_OK && header.width && header.height &&
        header.width <= kMaxDimension && header.height <= kMaxDimension &&
        jpeg_dec_get_outbuf_len(decoder, &length) == JPEG_ERR_OK && length > 0 &&
        size_t(length) <= size_t(kMaxDimension) * kMaxDimension * 2) {
        pixels = allocate(length);
        io.outbuf = pixels.get();
        if (!pixels || jpeg_dec_process(decoder, &io) != JPEG_ERR_OK) pixels.reset();
        width = header.width;
        height = header.height;
    }
    jpeg_dec_close(decoder);
    return pixels;
#else
    // All state changed by libjpeg lives on the heap across its error longjmp.
    struct State {
        jpeg_decompress_struct decoder{};
        JpegError error{};
        Buffer pixels;
        Buffer row;
    };
    auto state = std::make_unique<State>();
    auto *decoder = &state->decoder;
    state->row = allocate(kMaxDimension * 3);
    if (!state->row) return {};
    decoder->err = jpeg_std_error(&state->error.base);
    state->error.base.error_exit = jpeg_failed;
    if (setjmp(state->error.jump)) { jpeg_destroy_decompress(decoder); return {}; }
    jpeg_create_decompress(decoder);
    jpeg_mem_src(decoder, encoded.get(), size);
    jpeg_read_header(decoder, TRUE);
    if (decoder->progressive_mode || !decoder->image_width || !decoder->image_height ||
        decoder->image_width > kMaxDimension || decoder->image_height > kMaxDimension) {
        jpeg_destroy_decompress(decoder); return {};
    }
    decoder->out_color_space = JCS_RGB;
    jpeg_start_decompress(decoder);
    width = decoder->output_width;
    height = decoder->output_height;
    state->pixels = allocate(size_t(width) * height * 2);
    if (!state->pixels) { jpeg_destroy_decompress(decoder); return {}; }
    while (decoder->output_scanline < height) {
        const unsigned y = decoder->output_scanline;
        JSAMPROW rows[] = {state->row.get()};
        jpeg_read_scanlines(decoder, rows, 1);
        for (unsigned x = 0; x < width; ++x) {
            const uint8_t *rgb = state->row.get() + x * 3;
            const uint16_t pixel = uint16_t(rgb[0] >> 3) << 11 | uint16_t(rgb[1] >> 2) << 5 | (rgb[2] >> 3);
            state->pixels.get()[(y * width + x) * 2] = pixel & 255;
            state->pixels.get()[(y * width + x) * 2 + 1] = pixel >> 8;
        }
    }
    jpeg_finish_decompress(decoder);
    jpeg_destroy_decompress(decoder);
    return state->pixels;
#endif
}
} // namespace
CoverPtr load_cover(const char *path, const ParseOptions &options)
{
    if (!path || cancelled(options)) return {};
    size_t size = 0;
    Buffer encoded;
    {
        File file(path);
        if (!file.f) return {};
        encoded = embedded(file.f, size, options);
    }
    if (!encoded) {
        const std::string source(path);
        const auto slash = source.find_last_of('/');
        const std::string parent = slash == std::string::npos ? "" : source.substr(0, slash + 1);
        for (const char *name : {"cover.jpg", "folder.jpg"}) {
            if (cancelled(options)) return {};
            File file((parent + name).c_str());
            if (!file.f || fseek(file.f, 0, SEEK_END)) continue;
            const long length = ftell(file.f);
            if (length <= 0 || length > long(kMaxEncoded)) continue;
            size = size_t(length);
            encoded = read_image(file.f, 0, size, options);
            if (encoded) break;
        }
    }
    if (!encoded || cancelled(options)) return {};
    unsigned width = 0, height = 0;
    const auto decoded = decode(encoded, size, width, height);
    if (!decoded || cancelled(options)) return {};
    auto result = std::make_shared<CoverImage>();
    constexpr unsigned target = CoverImage::kSize;
    result->pixels = allocate(target * target * 2);
    if (!result->pixels) return {};
    const unsigned side = std::min(width, height);
    const unsigned left = (width - side) / 2, top = (height - side) / 2;
    for (unsigned y = 0; y < target; ++y) {
        if (cancelled(options)) return {};
        for (unsigned x = 0; x < target; ++x) {
            const size_t src = ((top + y * side / target) * width + left + x * side / target) * 2;
            memcpy(result->pixels.get() + (y * target + x) * 2, decoded.get() + src, 2);
        }
    }
    return result;
}
} // namespace music
