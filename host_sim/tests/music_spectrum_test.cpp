#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
#include "modules/music/music_spectrum.h"
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)
std::vector<uint8_t> tone(unsigned hz, unsigned bits = 32, unsigned channels = 2)
{
    std::vector<uint8_t> out(512 * channels * bits / 8);
    for (unsigned i = 0; i < 512; ++i) {
        const int16_t pcm = hz ? int16_t(16000 * sin(2 * 3.141592653589793 * hz * i / 16000)) : 0;
        const uint32_t value = uint32_t(uint16_t(pcm)) << (bits - 16);
        for (unsigned ch = 0; ch < channels; ++ch) {
            for (unsigned byte = 0; byte < bits / 8; ++byte) out[(i * channels + ch) * (bits / 8) + byte] = value >> (byte * 8);
        }
    }
    return out;
}
int main()
{
    uint8_t levels[MUSIC_SPECTRUM_BANDS]{};
    CHECK(!music_spectrum_read(levels));
    CHECK(music_spectrum_set_enabled(true));
    music_spectrum_set_track("/sdcard/Music/test.mp3");
    auto pcm = tone(1000);
    music_spectrum_feed("file://sdcard/other.mp3", pcm.data(), pcm.size(), 16000, 2, 32);
    CHECK(!music_spectrum_read(levels));
    music_spectrum_feed("file://sdcard/Music/test.mp3", pcm.data(), pcm.size() / 2, 16000, 2, 32);
    CHECK(!music_spectrum_read(levels));
    music_spectrum_feed("file://sdcard/Music/test.mp3", pcm.data() + pcm.size() / 2, pcm.size() / 2, 16000, 2, 32);
    CHECK(music_spectrum_read(levels));
    CHECK(std::max_element(levels, levels + 16) - levels == 9 && levels[9] > 75);
    CHECK(!music_spectrum_read(levels));
    for (unsigned bits : {16,24,32}) {
        for (unsigned channels : {1,2}) {
            music_spectrum_set_track("/sdcard/Music/test.mp3");
            pcm = tone(3000, bits, channels);
            music_spectrum_feed("file://sdcard/Music/test.mp3", pcm.data(), pcm.size(), 16000, channels, bits);
            CHECK(music_spectrum_read(levels));
            CHECK(std::max_element(levels, levels + 16) - levels == 12 && levels[12] > 75);
        }
    }
    pcm = tone(0);
    music_spectrum_feed("file://sdcard/Music/test.mp3", pcm.data(), pcm.size(), 16000, 2, 32);
    CHECK(music_spectrum_read(levels));
    CHECK(std::all_of(levels, levels + 16, [](uint8_t x) { return x == 0; }));
    music_spectrum_set_enabled(false);
    music_spectrum_feed("file://sdcard/Music/test.mp3", pcm.data(), pcm.size(), 16000, 2, 32);
    CHECK(!music_spectrum_read(levels));
    CHECK(music_spectrum_set_enabled(true));
    CHECK(!music_spectrum_read(levels));
    pcm = tone(1000);
    std::atomic<bool> done{false};
    std::thread producer([&] {
        while (!done.load()) music_spectrum_feed("file://sdcard/Music/test.mp3", pcm.data(), pcm.size(), 16000, 2, 32);
    });
    for (unsigned i = 0; i < 1000; ++i) {
        music_spectrum_read(levels);
        if (i % 10 == 0) { music_spectrum_set_enabled(false); music_spectrum_set_enabled(true); }
    }
    done = true; producer.join();
    music_spectrum_set_enabled(false);
    std::puts("music_spectrum_test passed");
}
