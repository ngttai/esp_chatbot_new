#include "music_data.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "music_service.hpp"

#ifdef ESP_PLATFORM
#include "esp_pthread.h"
#endif

namespace {

std::unique_ptr<music::Service> service;
std::string root_override;

std::string default_root()
{
    if (!root_override.empty()) return root_override;
#ifdef ESP_PLATFORM
    return "/sdcard/Music";
#else
    const char *env = std::getenv("ESP_CHATBOT_MUSIC_DIR");
    return env != nullptr && env[0] != '\0' ? env : "sdcard/Music";
#endif
}

void setup_worker_thread()
{
#ifdef ESP_PLATFORM
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.thread_name = "music_meta";
    cfg.stack_size = 6144;
    cfg.prio = 3;
    cfg.inherit_cfg = false;
    esp_pthread_set_cfg(&cfg);
#endif
}

} // namespace

extern "C" {

void music_data_set_root(const char *path)
{
    root_override = path != nullptr ? path : "";
}

void music_data_start(void)
{
    if (service) return;
    music::Service::Config config;
    config.root = default_root();
    config.thread_setup = setup_worker_thread;
    service = std::make_unique<music::Service>(std::move(config));
    service->start();
}

size_t music_data_count(void)
{
    return service ? service->count() : 0;
}

bool music_data_scan_done(void)
{
    return service && service->scan_done();
}

uint32_t music_data_version(void)
{
    return service ? service->version() : 0;
}

bool music_data_get(size_t index, music_data_track_t *out)
{
    if (!service || out == nullptr) return false;
    music::TrackView view;
    if (!service->snapshot(index, view)) return false;
    memset(out, 0, sizeof(*out));
    memcpy(out->title, view.title, sizeof(out->title));
    memcpy(out->artist, view.artist, sizeof(out->artist));
    memcpy(out->album, view.album, sizeof(out->album));
    out->duration_ms = view.duration_ms;
    switch (view.state) {
    case music::MetaState::Ready: out->state = MUSIC_DATA_READY; break;
    case music::MetaState::Failed: out->state = MUSIC_DATA_FAILED; break;
    default: out->state = MUSIC_DATA_PENDING; break;
    }
    return true;
}

music_data_cover_t music_data_get_cover(size_t index)
{
    (void)index;
    return nullptr;
}

void music_data_set_ui_idle(bool idle)
{
    if (service) service->set_ui_idle(idle);
}

void music_data_set_pinned(size_t index)
{
    if (service) service->set_pinned(index);
}

void music_data_set_wanted(const size_t *indices, size_t count)
{
    if (service) service->set_wanted(indices, count);
}

} // extern "C"
