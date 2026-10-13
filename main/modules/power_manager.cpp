/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <algorithm>

#include "sdkconfig.h"
#include "brookesia/service_display.hpp"
#include "private/utils.hpp"
#include "modules/power_manager.hpp"
#include "modules/head_led.hpp"

using namespace esp_brookesia;

namespace {

constexpr bool HARDWARE_SLEEP_ENABLED = true;

const char *state_name(PowerPolicyState state)
{
    switch (state) {
    case PowerPolicyState::Boot: return "Boot";
    case PowerPolicyState::Active: return "Active";
    case PowerPolicyState::WaitAgentSleep: return "WaitAgentSleep";
    case PowerPolicyState::Dimmed: return "Dimmed";
    case PowerPolicyState::HardwareSleep: return "HardwareSleep";
    case PowerPolicyState::Waking: return "Waking";
    }
    return "Unknown";
}

} // namespace

PowerManager &PowerManager::get_instance()
{
    static PowerManager instance;
    return instance;
}

PowerManager::PowerManager():
    policy_(CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_HW_SLEEP_TIMEOUT_MS,
            HARDWARE_SLEEP_ENABLED)
{}

void PowerManager::start(uint32_t display_output_id)
{
    display_output_id_ = display_output_id;
    restore_pending_ = false;
    started_ = true;
    BROOKESIA_LOGI(
        "Power manager started: dim=%1% ms, brightness=%2%%%, hardware sleep backend=backlight-only",
        CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT
    );
}

void PowerManager::update(const PowerManagerInput &source_input)
{
    if (!started_) {
        return;
    }

    PowerManagerInput input = source_input;
    input.activity_revision += external_activity_revision_.load(std::memory_order_relaxed);

    if (restore_pending_) {
        if (static_cast<uint32_t>(input.now_ms - restore_retry_ms_) < 1000) {
            return;
        }
        restore_retry_ms_ = input.now_ms;
        auto result = service::Display::get_instance().restore_backlight(display_output_id_);
        if (!result) {
            BROOKESIA_LOGE("Backlight restore retry failed: %1%", result.error());
            return;
        }
        restore_pending_ = false;
        policy_.recover_active(input.now_ms, input.activity_revision);
        HeadLed::get_instance().set_power_state(policy_.state());
    }

    const PowerPolicyState previous_state = policy_.state();
    const PowerPolicyAction action = policy_.update({
        .now_ms = input.now_ms,
        .activity_revision = input.activity_revision,
        .agent_idle_allowed = input.agent_idle_allowed,
        .blocked = input.blocked,
        .display_ready = input.display_ready,
    });

    switch (action) {
    case PowerPolicyAction::None:
        break;

    case PowerPolicyAction::EnterDim:
        if (!set_temporary_brightness(CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT)) {
            policy_.recover_active(input.now_ms, input.activity_revision);
            restore_pending_ = true;
            restore_retry_ms_ = input.now_ms;
        }
        break;

    case PowerPolicyAction::ExitDim:
    case PowerPolicyAction::WakeHardware: {
        auto result = service::Display::get_instance().restore_backlight(display_output_id_);
        if (!result) {
            BROOKESIA_LOGE("Backlight restore failed: %1%", result.error());
            restore_pending_ = true;
            restore_retry_ms_ = input.now_ms;
        }
        policy_.recover_active(input.now_ms, input.activity_revision);
        break;
    }

    case PowerPolicyAction::EnterHardwareSleep:
        if (!set_temporary_brightness(0)) {
            policy_.recover_active(input.now_ms, input.activity_revision);
            restore_pending_ = true;
            restore_retry_ms_ = input.now_ms;
        }
        break;
    }

    if (previous_state != policy_.state()) {
        HeadLed::get_instance().set_power_state(policy_.state());
        BROOKESIA_LOGI(
            "Power state: %1% -> %2%", state_name(previous_state), state_name(policy_.state())
        );
    }
}

bool PowerManager::set_temporary_brightness(int brightness)
{
    brightness = std::clamp(brightness, 0, 100);
    auto result = service::Display::get_instance().set_backlight_pwm_temporary(
                      display_output_id_, static_cast<uint8_t>(brightness)
                  );
    if (!result) {
        BROOKESIA_LOGE("Failed to set temporary brightness to %1%%%: %2%", brightness, result.error());
        return false;
    }
    return true;
}
