/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>

#include "modules/power_policy.hpp"

struct PowerManagerInput {
    uint32_t now_ms = 0;
    uint32_t activity_revision = 0;
    bool agent_slept = false;
    bool blocked = false;
    bool display_ready = false;
};

class PowerManager {
public:
    static PowerManager &get_instance();

    void start(uint32_t display_output_id);
    void update(const PowerManagerInput &input);
    PowerPolicyState state() const { return policy_.state(); }

private:
    PowerManager();

    bool set_temporary_brightness(int brightness);

    PowerPolicy policy_;
    uint32_t display_output_id_ = 0;
    bool restore_pending_ = false;
    uint32_t restore_retry_ms_ = 0;
    bool started_ = false;
};
