#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>

#include "modules/music/music_data.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; \
} } while (false)

int main()
{
    CHECK(music_data_get_cover(0) == nullptr);

    char pattern[] = "/tmp/music_data_cover_test_XXXXXX";
    char *base = mkdtemp(pattern);
    CHECK(base != nullptr);
    const fs::path root(base);
    const fs::path track = root / "Track.mp3";
    std::ofstream(track, std::ios::binary).put('\0');

    music_data_set_root(root.c_str());
    music_data_start();
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (music_data_count() != 1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }

    CHECK(music_data_count() == 1);
    CHECK(music_data_get_cover(0) == nullptr);
    CHECK(music_data_get_cover(1) == nullptr);

    std::error_code ec;
    fs::remove_all(root, ec);
    std::puts("music_data_cover_test passed");
    return 0;
}
