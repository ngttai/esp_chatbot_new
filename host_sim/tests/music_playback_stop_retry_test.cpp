#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include "modules/music/music_audio_backend.hpp"
#include "modules/music/music_playback.h"
#include "modules/music/music_data.h"

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "failed line %d: %s\n", __LINE__, #value); return 1; } } while (false)
namespace {
struct Backend final : music::PlayerBackend {
    std::function<void(music::BackendState)> event;
    bool reject_stop = false;
    int stops = 0;
    bool play(const std::string &, uint64_t) override { return true; }
    bool pause(uint64_t) override { return true; }
    bool resume(uint64_t) override { return true; }
    bool stop(uint64_t) override { ++stops; return !reject_stop; }
};
Backend *backend;
music_playback_state_t state() {
    music_playback_info_t info{};
    music_playback_get_info(&info);
    return info.state;
}
}
namespace music {
std::unique_ptr<PlayerBackend> make_audio_backend(std::function<void(BackendState)> on_state,
                                                 std::function<void(BackendCommand, uint64_t)>) {
    auto result = std::make_unique<Backend>();
    backend = result.get();
    result->event = std::move(on_state);
    return result;
}
}
extern "C" bool music_data_get(size_t, music_data_track_t *out) {
    *out = {};
    out->state = MUSIC_DATA_READY;
    out->duration_ms = 60000;
    return true;
}
extern "C" bool music_data_get_path(size_t, char *out, size_t size) {
    const char path[] = "/sdcard/Music/test.mp3";
    if (size < sizeof(path)) return false;
    std::memcpy(out, path, sizeof(path));
    return true;
}
int main() {
    music_playback_start();
    CHECK(music_playback_play(0));
    backend->event(music::BackendState::Playing);
    music_playback_pump();
    CHECK(state() == MUSIC_PLAYBACK_PLAYING);
    backend->reject_stop = true;
    CHECK(!music_playback_stop());
    const int first = backend->stops;
    music_playback_pump();
    CHECK(backend->stops == first + 1);
    CHECK(state() == MUSIC_PLAYBACK_PLAYING);
    backend->reject_stop = false;
    music_playback_pump();
    CHECK(state() == MUSIC_PLAYBACK_STOPPED);
    backend->event(music::BackendState::Idle);
    music_playback_pump();
    const int accepted = backend->stops;
    music_playback_pump();
    CHECK(backend->stops == accepted);
    // A new explicit play supersedes an earlier rejected Stop.
    CHECK(music_playback_play(1));
    backend->event(music::BackendState::Playing);
    music_playback_pump();
    backend->reject_stop = true;
    CHECK(!music_playback_stop());
    backend->reject_stop = false;
    CHECK(music_playback_play(2));
    const int superseded = backend->stops;
    music_playback_pump();
    CHECK(backend->stops == superseded);
    std::puts("PASS: rejected app-exit Stop is retried, accepted Stop settles, explicit Play supersedes old intent");
}
