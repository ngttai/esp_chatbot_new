/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

class TouchSensor {
public:
    static TouchSensor &get_instance()
    {
        static TouchSensor instance;
        return instance;
    }

    bool init();
    bool set_enabled(bool enabled);
    bool is_enabled() const
    {
        return enabled_.load();
    }

private:
    TouchSensor() = default;
    ~TouchSensor() = default;
    TouchSensor(const TouchSensor &) = delete;
    TouchSensor &operator=(const TouchSensor &) = delete;

    static void task_entry(void *arg);
    void run();
    uint32_t read_filtered_value() const;

    TaskHandle_t task_ = nullptr;
    std::atomic_bool enabled_{false};
};
