/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>

enum class UiIdleScreenRole : uint8_t {
    Home,
    Navigation,
    Clock,
    Application,
    Critical,
};

enum class UiIdleDecision : uint8_t {
    Hold,
    NavigateHome,
};

class UiIdleCoordinator {
public:
    explicit UiIdleCoordinator(uint32_t timeout_ms): timeout_ms_(timeout_ms) {}

    UiIdleDecision update(
        uint32_t now_ms,
        uint32_t activity_revision,
        uint8_t screen_key,
        UiIdleScreenRole screen_role,
        bool critical_operation
    )
    {
        const bool activity_changed = initialized_ && (activity_revision != activity_revision_);
        const bool screen_changed = initialized_ && (screen_key != screen_key_);
        if (!initialized_ || activity_changed || screen_changed) {
            initialized_ = true;
            activity_revision_ = activity_revision;
            screen_key_ = screen_key;
            last_activity_ms_ = now_ms;
            action_sent_ = false;
            return UiIdleDecision::Hold;
        }

        if (static_cast<uint32_t>(now_ms - last_activity_ms_) < timeout_ms_) {
            return UiIdleDecision::Hold;
        }
        if (critical_operation || screen_role != UiIdleScreenRole::Navigation) {
            return UiIdleDecision::Hold;
        }
        if (action_sent_) {
            return UiIdleDecision::Hold;
        }

        action_sent_ = true;
        return UiIdleDecision::NavigateHome;
    }

private:
    uint32_t timeout_ms_;
    uint32_t last_activity_ms_ = 0;
    uint32_t activity_revision_ = 0;
    uint8_t screen_key_ = 0;
    bool initialized_ = false;
    bool action_sent_ = false;
};
