/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>
#include <mutex>
#include "modules/power_policy.hpp"

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
    void set_wifi_enabled(bool enabled);
    void set_developer_mode();
    void set_power_state(PowerPolicyState state);

private:
    HeadLed() = default;
    ~HeadLed() = default;
    HeadLed(const HeadLed &) = delete;
    HeadLed &operator=(const HeadLed &) = delete;

    led_indicator_handle_t handle_ = nullptr;
    void apply_pattern_locked(int transition = -1);
    std::mutex mutex_;
    PowerPolicyState power_state_ = PowerPolicyState::Active;
    bool wifi_connected_ = false;
    bool wifi_enabled_ = true;
    bool touch_pressed_ = false;
    bool developer_mode_ = false;
    std::atomic_bool initialized_{false};
};
