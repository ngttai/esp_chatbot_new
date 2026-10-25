#pragma once

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define MUSIC_LOG(...) ESP_LOGI("music", __VA_ARGS__)
#else
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#define MUSIC_LOG(...) \
    do { \
        if (std::getenv("ESP_CHATBOT_MUSIC_TRACE") != nullptr) { \
            const auto ms_ = std::chrono::duration_cast<std::chrono::milliseconds>( \
                std::chrono::steady_clock::now().time_since_epoch()).count() % 1000000; \
            std::fprintf(stderr, "music [%06lld] ", static_cast<long long>(ms_)); \
            std::fprintf(stderr, __VA_ARGS__); \
            std::fputc('\n', stderr); \
        } \
    } while (false)
#endif
