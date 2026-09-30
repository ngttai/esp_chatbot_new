/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <mutex>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "flip_clock/weather_source.h"
}

class WeatherService {
public:
    enum class State {
        NotConfigured,
        WaitingForNetwork,
        Loading,
        Live,
        Cached,
        Offline,
    };

    static WeatherService &get_instance()
    {
        static WeatherService instance;
        return instance;
    }

    bool init();
    bool start();
    weather_data_t snapshot() const;
    State state() const;

private:
    WeatherService() = default;
    WeatherService(const WeatherService &) = delete;
    WeatherService &operator=(const WeatherService &) = delete;

    static void task_entry(void *context);
    void run();
    bool refresh(std::string &error);
    void set_pending(State state, const char *message);
    void set_failure(const std::string &error);

    mutable std::mutex mutex_;
    weather_data_t data_{};
    State state_ = State::NotConfigured;
    bool has_live_data_ = false;
    TaskHandle_t task_ = nullptr;
};

extern "C" bool weather_service_get(weather_data_t *output);
