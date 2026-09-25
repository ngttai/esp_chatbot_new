/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>
#include <cstdint>

#include "bq27220.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class BatteryMonitor {
public:
    struct Snapshot {
        bool valid = false;
        bool charging = false;
        uint8_t percentage = 0;
        uint16_t voltage_mv = 0;
        int16_t current_ma = 0;
        uint32_t revision = 0;
    };

    static BatteryMonitor &get_instance()
    {
        static BatteryMonitor instance;
        return instance;
    }

    bool init();
    Snapshot get_snapshot() const;

private:
    BatteryMonitor() = default;
    ~BatteryMonitor();
    BatteryMonitor(const BatteryMonitor &) = delete;
    BatteryMonitor &operator=(const BatteryMonitor &) = delete;

    static void timer_callback(TimerHandle_t timer);
    void sample();

    bq27220_handle_t gauge_ = nullptr;
    TimerHandle_t timer_ = nullptr;
    bool i2c_bus_referenced_ = false;
    std::atomic_bool valid_{false};
    std::atomic_bool charging_{false};
    std::atomic<uint8_t> percentage_{0};
    std::atomic<uint16_t> voltage_mv_{0};
    std::atomic<int16_t> current_ma_{0};
    std::atomic<uint32_t> revision_{0};
};
