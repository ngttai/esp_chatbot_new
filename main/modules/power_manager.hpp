/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>
#include <atomic>

#include "modules/power_policy.hpp"

struct PowerManagerInput {
    uint32_t now_ms = 0;
    uint32_t activity_revision = 0;
    bool agent_idle_allowed = false;
    bool blocked = false;
    bool display_ready = false;
};

class PowerManager {
public:
    static PowerManager &get_instance();

    void start(uint32_t display_output_id);
    void update(const PowerManagerInput &input);
    void notify_user_activity()
    {
        external_activity_revision_.fetch_add(1, std::memory_order_relaxed);
    }
    PowerPolicyState state() const { return policy_.state(); }

private:
    PowerManager();

    bool set_temporary_brightness(int brightness);

    PowerPolicy policy_;
    std::atomic_uint32_t external_activity_revision_{0};
    uint32_t display_output_id_ = 0;
    bool restore_pending_ = false;
    uint32_t restore_retry_ms_ = 0;
    bool started_ = false;
};
