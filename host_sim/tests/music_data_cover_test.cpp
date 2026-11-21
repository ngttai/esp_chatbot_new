#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <jpeglib.h>
#include "modules/music/music_data.h"
#include "modules/music/music_cover.hpp"
#include "modules/music/music_service.hpp"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (false)

std::vector<uint8_t> jpeg(unsigned width = 32, unsigned height = 16, bool blue = false)
{
    jpeg_compress_struct c{};
    jpeg_error_mgr err{};
    c.err = jpeg_std_error(&err);
    jpeg_create_compress(&c);
    unsigned char *out = nullptr;
    unsigned long length = 0;
    jpeg_mem_dest(&c, &out, &length);
    c.image_width = width; c.image_height = height;
    c.input_components = 3; c.in_color_space = JCS_RGB;
    jpeg_set_defaults(&c);
    jpeg_start_compress(&c, TRUE);
    std::vector<uint8_t> row(width * 3, 0);
    for (unsigned x = 0; x < width; ++x) row[x * 3 + (blue ? 2 : 0)] = 255;
    while (c.next_scanline < height) {
        JSAMPROW rows[] = {row.data()};
        jpeg_write_scanlines(&c, rows, 1);
    }
    jpeg_finish_compress(&c);
    std::vector<uint8_t> result(out, out + length);
    free(out); jpeg_destroy_compress(&c);
    return result;
}
void write(const fs::path &path, const std::vector<uint8_t> &bytes)
{
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}
void size32(std::vector<uint8_t> &v, size_t n, bool sync)
{
    for (int shift = 3; shift >= 0; --shift) v.push_back((n >> (shift * (sync ? 7 : 8))) & (sync ? 127 : 255));
}
std::vector<uint8_t> tag(unsigned version, const std::vector<uint8_t> &image, bool utf16 = false, uint8_t picture_type = 3)
{
    std::vector<uint8_t> body = {uint8_t(utf16 ? 1 : 0)};
    if (version == 2) body.insert(body.end(), {'J','P','G'});
    else body.insert(body.end(), {'i','m','a','g','e','/','j','p','e','g',0});
    body.push_back(picture_type);
    if (utf16) body.insert(body.end(), {255,254,'a',0,0,0});
    else body.push_back(0);
    body.insert(body.end(), image.begin(), image.end());
    std::vector<uint8_t> frame;
    if (version == 2) {
        frame = {'P','I','C',uint8_t(body.size() >> 16),uint8_t(body.size() >> 8),uint8_t(body.size())};
    } else {
        frame = {'A','P','I','C'}; size32(frame, body.size(), version == 4);
        frame.insert(frame.end(), {0,0});
    }
    frame.insert(frame.end(), body.begin(), body.end());
    std::vector<uint8_t> result = {'I','D','3',uint8_t(version),0,0};
    size32(result, frame.size(), true);
    result.insert(result.end(), frame.begin(), frame.end());
    return result;
}
bool red(const uint8_t *p)
{
    const uint16_t rgb = uint16_t(p[1]) << 8 | p[0];
    return (rgb & 0xF800) >= 0xF000 && (rgb & 0x7FF) < 0x40;
}
template<class Fn> bool wait_for(Fn fn)
{
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!fn()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}
int main()
{
    CHECK(music_data_get_cover(0) == nullptr);
    char pattern[] = "/tmp/music_data_cover_test_XXXXXX";
    char *base = mkdtemp(pattern);
    CHECK(base);
    const fs::path root(base);
    const auto image = jpeg();
    for (unsigned version : {2,3,4}) {
        for (bool utf16 : {false,true}) {
            write(root / "test.mp3", tag(version, image, utf16));
            const auto cover = music::load_cover((root / "test.mp3").c_str());
            CHECK(cover && red(cover->pixels.get()));
            CHECK(red(cover->pixels.get() + (128 * 128 - 1) * 2));
        }
    }
    auto back = tag(3, jpeg(32, 16, true), false, 4);
    const auto front = tag(3, image);
    std::vector<uint8_t> combined = {'I','D','3',3,0,0};
    size32(combined, back.size() + front.size() - 20, true);
    combined.insert(combined.end(), back.begin() + 10, back.end());
    combined.insert(combined.end(), front.begin() + 10, front.end());
    write(root / "test.mp3", combined);
    const auto preferred = music::load_cover((root / "test.mp3").c_str());
    CHECK(preferred && red(preferred->pixels.get()));
    const music::ParseOptions cancel{[](void *) { return true; }, nullptr};
    CHECK(!music::load_cover((root / "test.mp3").c_str(), cancel));
    write(root / "test.mp3", tag(3, {255,216,0,0}));
    CHECK(!music::load_cover((root / "test.mp3").c_str()));
    write(root / "test.mp3", tag(3, jpeg(1025,1)));
    CHECK(!music::load_cover((root / "test.mp3").c_str()));
    write(root / "test.mp3", {'I','D','3',3,0,0,127,127,127,127});
    CHECK(!music::load_cover((root / "test.mp3").c_str()));
    std::vector<uint8_t> oversized(513 * 1024, 0);
    oversized[0] = 255; oversized[1] = 216;
    write(root / "cover.jpg", oversized);
    CHECK(!music::load_cover((root / "test.mp3").c_str()));
    write(root / "cover.jpg", image);
    CHECK(music::load_cover((root / "test.mp3").c_str()));
    fs::remove(root / "cover.jpg");
    write(root / "folder.jpg", image);
    CHECK(music::load_cover((root / "test.mp3").c_str()));
    fs::remove(root / "folder.jpg");
    write(root / "test.mp3", tag(3, image));
    write(root / "z-empty.mp3", {0});

    // A decoder may finish after a selection changes; never publish that result.
    {
        std::atomic<bool> started{false}, release{false};
        std::atomic<unsigned> loads{0};
        music::Service::Config config;
        config.root = root.string();
        config.settle_ms = 0;
        config.parse = [](const char *, music::TrackMeta &meta, const music::ParseOptions &) {
            meta = {}; return music::ParseResult::Ok;
        };
        config.cover_loader = [&](const char *path, const music::ParseOptions &) {
            if (loads.fetch_add(1) == 0) {
                started = true;
                while (!release.load()) std::this_thread::sleep_for(1ms);
            }
            // Deliberately ignore cancellation to simulate an in-flight decoder.
            return music::load_cover(path);
        };
        music::Service worker(std::move(config));
        worker.start();
        const bool loading = wait_for([&] { return started.load(); });
        if (!loading) { release = true; CHECK(loading); }
        worker.set_pinned(1);
        release = true;
        CHECK(wait_for([&] { return loads.load() >= 2; }));
        CHECK(!worker.cover_of(0));
        worker.set_pinned(0);
        CHECK(wait_for([&] { return bool(worker.cover_of(0)); }));
    }

    music_data_set_root(root.c_str());
    music_data_start();
    CHECK(wait_for([] { return music_data_count() == 2 && music_data_get_cover(0); }));
    const auto *first = music_data_get_cover(0);
    CHECK(first && first->width == 128 && first->height == 128 && red(first->pixels));
    const uint8_t *retained = first->pixels;
    music_data_set_pinned(1);
    std::this_thread::sleep_for(200ms);
    CHECK(red(retained)); // Worker eviction must not free the displayed pixels.
    CHECK(music_data_get_cover(1) == nullptr);
    music_data_set_ui_idle(false);
    music_data_set_pinned(0);
    std::this_thread::sleep_for(200ms);
    CHECK(music_data_get_cover(0) == nullptr);
    music_data_set_ui_idle(true);
    CHECK(wait_for([] { return music_data_get_cover(0) != nullptr; }));
    CHECK(music_data_get_cover(99) == nullptr);
    fs::remove_all(root);
    std::puts("music_data_cover_test passed");
}
