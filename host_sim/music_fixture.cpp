#include "music_fixture.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path fixture_root;

void remove_fixture()
{
    std::error_code ec;
    if (!fixture_root.empty()) fs::remove_all(fixture_root, ec);
}

} // namespace

namespace host_sim {

void prepare_music_fixture(bool force)
{
    const char *existing = std::getenv("ESP_CHATBOT_MUSIC_DIR");
    if (!force && existing != nullptr && existing[0] != '\0') return;

    std::error_code ec;
    std::string pattern = (fs::temp_directory_path(ec) / "esp_chatbot_music_XXXXXX").string();
    if (mkdtemp(pattern.data()) == nullptr) return;
    fixture_root = pattern;
    std::atexit(remove_fixture);

    static const char *const names[] = {
        "Morning Drive", "City Lights", "Night Walk", "Ocean Echoes",
        "Golden Hour", "Neon Rain", "Quiet Roads", "Afterglow",
    };
    // One MPEG1 Layer III frame (128 kbps, 44.1 kHz) so the file parses as MP3.
    std::vector<uint8_t> frame(417, 0);
    frame[0] = 0xFF;
    frame[1] = 0xFB;
    frame[2] = 0x90;

    const fs::path music_dir = fixture_root / "Music";
    fs::create_directories(music_dir, ec);
    int number = 1;
    for (const char *name : names) {
        std::ofstream file(music_dir / (std::to_string(number / 10) + std::to_string(number % 10) + " " + name + ".mp3"),
                           std::ios::binary);
        for (int i = 0; i < 60; i++) file.write(reinterpret_cast<const char *>(frame.data()), std::streamsize(frame.size()));
        number++;
    }
    setenv("ESP_CHATBOT_MUSIC_DIR", music_dir.string().c_str(), 1);
}

} // namespace host_sim
