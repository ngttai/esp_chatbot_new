#include "music_playback.h"

#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "music_audio_backend.hpp"
#include "music_data.h"
#include "music_log.hpp"
#include "music_player.hpp"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

namespace {

constexpr size_t kMaxQueuedEvents = 64;
constexpr size_t kPathSize = 512;

struct QueuedEvent {
    bool failure;
    music::BackendState state;
    music::BackendCommand command;
    uint64_t seq;
};

struct Context {
    std::mutex queue_mutex;
    std::deque<QueuedEvent> queue;
    std::unique_ptr<music::PlayerBackend> backend;
    std::unique_ptr<music::Player> player;
    uint32_t logged_version = 0;
};

std::unique_ptr<Context> ctx;

const char *state_name(music::PlayerState state)
{
    switch (state) {
    case music::PlayerState::Stopped: return "Stopped";
    case music::PlayerState::Preparing: return "Preparing";
    case music::PlayerState::Playing: return "Playing";
    case music::PlayerState::Paused: return "Paused";
    case music::PlayerState::Ended: return "Ended";
    case music::PlayerState::Error: return "Error";
    }
    return "?";
}

const char *backend_name(music::BackendState state)
{
    switch (state) {
    case music::BackendState::Idle: return "Idle";
    case music::BackendState::Playing: return "Playing";
    case music::BackendState::Paused: return "Paused";
    }
    return "?";
}

#ifdef ESP_PLATFORM
void log_heap()
{
    MUSIC_LOG("heap internal free=%u min=%u largest=%u dma_largest=%u psram_free=%u",
              unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
              unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
              unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
              unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_DMA)),
              unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
#else
void log_heap() {}
#endif

void log_state()
{
    const music::PlayerSnapshot snap = ctx->player->snapshot();
    if (snap.version == ctx->logged_version) return;
    ctx->logged_version = snap.version;
    MUSIC_LOG("state %s index=%u", state_name(snap.state), unsigned(snap.index));
    log_heap();
}

} // namespace

extern "C" {

void music_playback_start(void)
{
    if (ctx) return;
    ctx = std::make_unique<Context>();
    Context *c = ctx.get();
    auto push = [c](QueuedEvent event) {
        std::lock_guard<std::mutex> lock(c->queue_mutex);
        if (c->queue.size() >= kMaxQueuedEvents) c->queue.pop_front();
        c->queue.push_back(event);
    };
    c->backend = music::make_audio_backend(
        [push](music::BackendState state) { push({false, state, music::BackendCommand::Play, 0}); },
        [push](music::BackendCommand command, uint64_t seq) {
            push({true, music::BackendState::Idle, command, seq});
        });
    c->player = std::make_unique<music::Player>(*c->backend, music::Player::Config{});
}

bool music_playback_play(size_t index)
{
    if (!ctx) return false;
    music_data_track_t track;
    char path[kPathSize];
    if (!music_data_get(index, &track) || !music_data_get_path(index, path, sizeof(path))) return false;
    const uint32_t duration = track.state == MUSIC_DATA_READY ? track.duration_ms : 0;
    const bool ok = ctx->player->play(index, path, duration);
    log_state();
    return ok;
}

bool music_playback_pause(void)
{
    if (!ctx) return false;
    const bool ok = ctx->player->pause();
    log_state();
    return ok;
}

bool music_playback_resume(void)
{
    if (!ctx) return false;
    const bool ok = ctx->player->resume();
    log_state();
    return ok;
}

bool music_playback_stop(void)
{
    if (!ctx) return false;
    const bool ok = ctx->player->stop();
    log_state();
    return ok;
}

void music_playback_pump(void)
{
    if (!ctx) return;
    std::vector<QueuedEvent> events;
    {
        std::lock_guard<std::mutex> lock(ctx->queue_mutex);
        events.assign(ctx->queue.begin(), ctx->queue.end());
        ctx->queue.clear();
    }
    for (const QueuedEvent &event : events) {
        if (event.failure) {
            MUSIC_LOG("apply failure %d seq=%u", int(event.command), unsigned(event.seq));
            ctx->player->on_command_failed(event.command, event.seq);
        } else {
            MUSIC_LOG("apply %s", backend_name(event.state));
            ctx->player->on_backend_state(event.state);
        }
        log_state();
    }
    ctx->player->tick();
    // Metadata may arrive after Play started (lazy loading).
    const music::PlayerSnapshot snap = ctx->player->snapshot();
    if (snap.duration_ms == 0 && (snap.state == music::PlayerState::Preparing ||
                                  snap.state == music::PlayerState::Playing ||
                                  snap.state == music::PlayerState::Paused)) {
        music_data_track_t track;
        if (music_data_get(snap.index, &track) && track.state == MUSIC_DATA_READY && track.duration_ms > 0) {
            ctx->player->set_duration(snap.index, track.duration_ms);
        }
    }
    log_state();
}

void music_playback_test_report_open_failure(void)
{
    if (!ctx) return;
    std::lock_guard<std::mutex> lock(ctx->queue_mutex);
    ctx->queue.push_front({false, music::BackendState::Idle, music::BackendCommand::Play, 0});
}

void music_playback_get_info(music_playback_info_t *out)
{
    if (out == nullptr) return;
    *out = {};
    if (!ctx) return;
    const music::PlayerSnapshot snap = ctx->player->snapshot();
    out->state = static_cast<music_playback_state_t>(snap.state);
    out->index = snap.index;
    out->elapsed_ms = snap.elapsed_ms;
    out->duration_ms = snap.duration_ms;
    out->version = snap.version;
}

} // extern "C"
