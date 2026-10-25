#pragma once

#include <cstddef>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_pthread.h"
#endif

namespace music {

// Applies a pthread config to the next thread created by the calling thread and
// puts back the config that was active before.
class ScopedThreadConfig {
public:
    ScopedThreadConfig(const char *name, size_t stack_size, int prio = -1)
    {
#ifdef ESP_PLATFORM
        had_saved_ = esp_pthread_get_cfg(&saved_) == ESP_OK;
        esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
        cfg.thread_name = name;
        cfg.stack_size = stack_size;
        if (prio >= 0) cfg.prio = size_t(prio);
        cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        cfg.inherit_cfg = false;
        esp_pthread_set_cfg(&cfg);
#else
        (void)name;
        (void)stack_size;
        (void)prio;
#endif
    }

    ~ScopedThreadConfig() { restore(); }

    void restore()
    {
#ifdef ESP_PLATFORM
        if (restored_) return;
        restored_ = true;
        esp_pthread_cfg_t defaults = esp_pthread_get_default_config();
        esp_pthread_set_cfg(had_saved_ ? &saved_ : &defaults);
#endif
    }

    ScopedThreadConfig(const ScopedThreadConfig &) = delete;
    ScopedThreadConfig &operator=(const ScopedThreadConfig &) = delete;

private:
#ifdef ESP_PLATFORM
    esp_pthread_cfg_t saved_{};
    bool had_saved_ = false;
    bool restored_ = false;
#endif
};

} // namespace music
