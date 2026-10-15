/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <algorithm>
#include <cmath>
#include <chrono>

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"
#include "brookesia/service_display.hpp"
#include "modules/head_led.hpp"
#include "modules/ai_agents.hpp"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "modules/power_manager.hpp"
#include "brookesia/lib_utils.hpp"

using namespace esp_brookesia;

namespace {

constexpr bool HARDWARE_SLEEP_ENABLED = true;
constexpr int UPDATE_PERIOD_MS = 50;

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
    return *get_shared_instance();
}

std::shared_ptr<PowerManager> PowerManager::get_shared_instance()
{
    static const std::shared_ptr<PowerManager> instance(new PowerManager);
    return instance;
}

PowerManager::PowerManager():
    ServiceBase({.name = SERVICE_NAME,
                 .description = "Coordinate display idle power and user activity",
                 .version = "1.0.0"}),
    policy_(CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_HW_SLEEP_TIMEOUT_MS,
            HARDWARE_SLEEP_ENABLED)
{}

bool PowerManager::on_start()
{
    std::lock_guard lock(lifecycle_mutex_);
    policy_ = PowerPolicy(CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_HW_SLEEP_TIMEOUT_MS,
                          HARDWARE_SLEEP_ENABLED);
    published_state_ = PowerPolicyState::Boot;
    restore_pending_ = false;
    running_ = true;
    const uint32_t generation = ++generation_;
    if (!get_task_scheduler()->post_periodic([this, generation]() { return tick(generation); }, UPDATE_PERIOD_MS,
                                            &tick_task_, get_request_task_group())) {
        running_ = false;
        return false;
    }
    BROOKESIA_LOGI(
        "Pwr service started: shared scheduler, period=50 ms, dim=%1% ms, brightness=%2%%%, backend=backlight-only",
        CONFIG_EXAMPLE_PWR_DIM_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_DIM_BRIGHTNESS_PERCENT
    );
    return true;
}

void PowerManager::on_stop()
{
    std::lock_guard lock(lifecycle_mutex_);
    running_ = false;
    ++generation_;
    if (tick_task_ != 0) get_task_scheduler()->cancel(tick_task_);
    tick_task_ = 0;
    const auto input = inputs_.snapshot();
    if (input.configured) {
        display_output_id_ = input.output_id;
        (void)restore_backlight();
    }
}

bool PowerManager::tick(uint32_t generation)
{
    std::lock_guard lock(lifecycle_mutex_);
    if (!running_ || generation != generation_) return false;
    const auto input = inputs_.snapshot();
    if (!input.configured) return true;
    display_output_id_ = input.output_id;
    update({
#if defined(ESP_PLATFORM)
        .now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000),
#else
        .now_ms = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()),
#endif
        .activity_revision = input.activity_revision,
#if defined(ESP_PLATFORM)
        .agent_idle_allowed = !AI_Agents::get_instance().blocks_power_idle(),
#else
        .agent_idle_allowed = host_agent_idle_.load(),
#endif
        .blocked = input.blocked,
        .display_ready = input.display_ready,
    });
    publish_state();
    return true;
}

void PowerManager::publish_state()
{
    const auto current = policy_.state();
    const auto previous = published_state_.exchange(current);
    if (current == previous) return;
#if defined(ESP_PLATFORM)
    HeadLed::get_instance().set_power_state(current);
#endif
    (void)publish_event("StateChanged", std::vector<service::EventItem>{
        std::string(state_name(previous)), std::string(state_name(current))});
    BROOKESIA_LOGI("Power state: %1% -> %2%", state_name(previous), state_name(current));
#if defined(ESP_PLATFORM)
    BROOKESIA_LOGI("Pwr heap: internal free=%1%, min=%2%, largest=%3%; PSRAM free=%4%, min=%5%, largest=%6%",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    BROOKESIA_LOGI("Pwr worker stack minimum remaining: %1% bytes", uxTaskGetStackHighWaterMark(nullptr));
#endif
}

std::vector<service::FunctionSchema> PowerManager::get_function_schemas()
{
    return {
        {"Configure", "Select the display output", {{"OutputId", "Display output id", service::FunctionValueType::Number}}},
        {"NotifyActivity", "Record user activity", {}},
        {"SetIdleConditions", "Update UI activity and blockers", {
            {"ActivityRevision", "UI activity revision", service::FunctionValueType::Number},
            {"Blocked", "Critical UI operation", service::FunctionValueType::Boolean},
            {"DisplayReady", "Backlight is initialized", service::FunctionValueType::Boolean}}},
        {.name = "GetState", .description = "Read current power state",
         .return_value = service::FunctionReturnSchema{service::FunctionValueType::String, "Power state"}},
    };
}

PowerManager::FunctionHandlerList PowerManager::get_function_handlers()
{
    return {
        [this](service::FunctionParameterMap &&args) -> service::FunctionResult {
            const double id = std::get<double>(args.at("OutputId"));
            if (!std::isfinite(id) || id < 1 || id > UINT32_MAX || std::floor(id) != id)
                return {.success = false, .error_message = "Invalid display output id"};
            configure(static_cast<uint32_t>(id));
            return {.success = true};
        },
        [this](service::FunctionParameterMap &&) -> service::FunctionResult {
            notify_user_activity();
            return {.success = true};
        },
        [this](service::FunctionParameterMap &&args) -> service::FunctionResult {
            const double revision = std::get<double>(args.at("ActivityRevision"));
            if (!std::isfinite(revision) || revision < 0 || revision > UINT32_MAX || std::floor(revision) != revision)
                return {.success = false, .error_message = "Invalid activity revision"};
            set_ui_conditions(static_cast<uint32_t>(revision), std::get<bool>(args.at("Blocked")),
                              std::get<bool>(args.at("DisplayReady")));
            return {.success = true};
        },
        [this](service::FunctionParameterMap &&) -> service::FunctionResult {
            return {.success = true, .data = std::string(state_name(state()))};
        },
    };
}

std::vector<service::EventSchema> PowerManager::get_event_schemas()
{
    return {{"StateChanged", "Power state transition", {
        {"Previous", "Previous state", service::EventItemType::String},
        {"Current", "Current state", service::EventItemType::String}}}};
}

void PowerManager::update(const PowerManagerInput &source_input)
{
    PowerManagerInput input = source_input;

    if (restore_pending_) {
        if (static_cast<uint32_t>(input.now_ms - restore_retry_ms_) < 1000) {
            return;
        }
        restore_retry_ms_ = input.now_ms;
        if (!restore_backlight()) {
            return;
        }
        restore_pending_ = false;
        policy_.recover_active(input.now_ms, input.activity_revision);
    }

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
        if (!restore_backlight()) {
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

}

bool PowerManager::set_temporary_brightness(int brightness)
{
    brightness = std::clamp(brightness, 0, 100);
#if defined(ESP_PLATFORM)
    auto result = service::Display::get_instance().set_backlight_pwm_temporary(
                      display_output_id_, static_cast<uint8_t>(brightness)
                  );
    if (!result) {
        BROOKESIA_LOGE("Failed to set temporary brightness to %1%%%: %2%", brightness, result.error());
        return false;
    }
#else
    if (host_fail_next_backlight_.exchange(false)) return false;
    host_backlight_pwm_ = brightness;
#endif
    return true;
}

bool PowerManager::restore_backlight()
{
#if defined(ESP_PLATFORM)
    auto result = service::Display::get_instance().restore_backlight(display_output_id_);
    if (!result) BROOKESIA_LOGE("Backlight restore failed: %1%", result.error());
    return result.has_value();
#else
    if (host_fail_next_backlight_.exchange(false)) return false;
    host_backlight_pwm_ = 100;
    return true;
#endif
}
