/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>

#include "led_indicator.h"

class HeadLed {
public:
    static HeadLed &get_instance()
    {
        static HeadLed instance;
        return instance;
    }

    bool init();
    void set_touch_pressed(bool pressed);
    void set_wifi_connected(bool connected);

private:
    HeadLed() = default;
    ~HeadLed() = default;
    HeadLed(const HeadLed &) = delete;
    HeadLed &operator=(const HeadLed &) = delete;

    led_indicator_handle_t handle_ = nullptr;
    std::atomic_bool initialized_{false};
};
