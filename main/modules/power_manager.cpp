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

using namespace esp_brookesia;

namespace {

constexpr bool HARDWARE_SLEEP_ENABLED = false;

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
    restore_brightness_ = -1;
    started_ = true;
    BROOKESIA_LOGI(
        "Power manager started: dim=%1% ms, brightness=%2%%%, hardware sleep backend=disabled",
        CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT
    );
}

void PowerManager::update(const PowerManagerInput &input)
{
    if (!started_) {
        return;
    }

    if ((policy_.state() == PowerPolicyState::Dimmed) &&
            (input.current_brightness >= 0) &&
            (input.current_brightness != CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT)) {
        restore_brightness_ = input.current_brightness;
        if (!set_temporary_brightness(CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT)) {
            restore_brightness_ = -1;
            policy_.recover_active(input.now_ms, input.activity_revision);
        }
    }

    const PowerPolicyState previous_state = policy_.state();
    const PowerPolicyAction action = policy_.update({
        .now_ms = input.now_ms,
        .activity_revision = input.activity_revision,
        .agent_slept = input.agent_slept,
        .blocked = input.blocked,
        .display_ready = input.display_ready,
    });

    switch (action) {
    case PowerPolicyAction::None:
        break;

    case PowerPolicyAction::EnterDim:
        restore_brightness_ = input.current_brightness;
        if (!set_temporary_brightness(CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT)) {
            restore_brightness_ = -1;
            policy_.recover_active(input.now_ms, input.activity_revision);
        }
        break;

    case PowerPolicyAction::ExitDim:
        if ((restore_brightness_ >= 0) && !set_temporary_brightness(restore_brightness_)) {
            policy_.recover_active(input.now_ms, input.activity_revision);
        }
        restore_brightness_ = -1;
        break;

    case PowerPolicyAction::EnterHardwareSleep:
    case PowerPolicyAction::WakeHardware:
        // Phase 5 supplies the hardware-specific actuator.
        policy_.recover_active(input.now_ms, input.activity_revision);
        break;
    }

    if (previous_state != policy_.state()) {
        BROOKESIA_LOGI(
            "Power state: %1% -> %2%", state_name(previous_state), state_name(policy_.state())
        );
    }
}

bool PowerManager::set_temporary_brightness(int brightness)
{
    brightness = std::clamp(brightness, 0, 100);
    auto result = service::Display::get_instance().set_backlight_brightness_temporary(
                      display_output_id_, static_cast<uint8_t>(brightness)
                  );
    if (!result) {
        BROOKESIA_LOGE("Failed to set temporary brightness to %1%%%: %2%", brightness, result.error());
        return false;
    }
    return true;
}
