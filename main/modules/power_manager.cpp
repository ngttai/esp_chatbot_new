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

const char *suspend_phase_name(SuspendCoordinator::Phase phase)
{
    switch (phase) {
    case SuspendCoordinator::Phase::Idle: return "Idle";
    case SuspendCoordinator::Phase::Preparing: return "Preparing";
    case SuspendCoordinator::Phase::Committing: return "Committing";
    case SuspendCoordinator::Phase::Suspended: return "Suspended";
    case SuspendCoordinator::Phase::Resuming: return "Resuming";
    case SuspendCoordinator::Phase::Backoff: return "Backoff";
    case SuspendCoordinator::Phase::Recovery: return "Recovery";
    }
    return "Unknown";
}

const char *suspend_reason_name(SuspendCoordinator::Reason reason)
{
    switch (reason) {
    case SuspendCoordinator::Reason::None: return "None";
    case SuspendCoordinator::Reason::Activity: return "Activity";
    case SuspendCoordinator::Reason::Busy: return "Busy";
    case SuspendCoordinator::Reason::Error: return "Error";
    case SuspendCoordinator::Reason::PrepareTimeout: return "PrepareTimeout";
    case SuspendCoordinator::Reason::CommitTimeout: return "CommitTimeout";
    case SuspendCoordinator::Reason::ResumeTimeout: return "ResumeTimeout";
    case SuspendCoordinator::Reason::ActuatorError: return "ActuatorError";
    case SuspendCoordinator::Reason::Stopped: return "Stopped";
    }
    return "Unknown";
}

std::optional<uint32_t> uint_parameter(const service::FunctionParameterMap &args, const char *name)
{
    const double value = std::get<double>(args.at(name));
    if (!std::isfinite(value) || value < 0 || value > UINT32_MAX || std::floor(value) != value) return std::nullopt;
    return static_cast<uint32_t>(value);
}

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
    suspend_(CONFIG_EXAMPLE_PWR_PREPARE_TIMEOUT_MS, CONFIG_EXAMPLE_PWR_RESUME_TIMEOUT_MS,
             CONFIG_EXAMPLE_PWR_SUSPEND_RETRY_MS),
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
    if (suspend_.snapshot().phase == SuspendCoordinator::Phase::Recovery)
        retry_resume_session_ = suspend_.snapshot().session;
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
    suspend_.interrupt();
    const auto input = inputs_.snapshot();
    if (input.configured && suspend_.snapshot().phase != SuspendCoordinator::Phase::Recovery) {
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
    const PowerManagerInput power_input{
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
    };
    update_suspend(power_input);
    const auto phase = suspend_.snapshot().phase;
    if (phase == SuspendCoordinator::Phase::Idle || phase == SuspendCoordinator::Phase::Backoff)
        update(power_input);
    publish_state();
    return true;
}

bool PowerManager::register_participant(uint8_t id)
{
    std::lock_guard lock(lifecycle_mutex_);
    if (!running_ || policy_.state() == PowerPolicyState::HardwareSleep ||
            policy_.state() == PowerPolicyState::Waking) return false;
    return suspend_.register_participant(id);
}

bool PowerManager::unregister_participant(uint8_t id)
{
    std::lock_guard lock(lifecycle_mutex_);
    return running_ && suspend_.unregister_participant(id);
}

bool PowerManager::report_suspend_status(uint32_t session, uint8_t id, SuspendCoordinator::Report status)
{
    std::lock_guard lock(lifecycle_mutex_);
    return running_ && suspend_.report(session, id, status);
}

bool PowerManager::retry_resume(uint32_t session)
{
    std::lock_guard lock(lifecycle_mutex_);
    const auto snapshot = suspend_.snapshot();
    if (!running_ || snapshot.phase != SuspendCoordinator::Phase::Recovery || snapshot.session != session) return false;
    retry_resume_session_ = session;
    return true;
}

SuspendCoordinator::Snapshot PowerManager::suspend_status() const
{
    std::lock_guard lock(lifecycle_mutex_);
    return suspend_.snapshot();
}

void PowerManager::update_suspend(const PowerManagerInput &input)
{
    using Command = SuspendCoordinator::Command;
    const bool wake = input.activity_revision != suspend_activity_revision_ ||
                      input.blocked || !input.display_ready || !input.agent_idle_allowed;
    Command command;
    if (retry_resume_session_ != 0 && retry_resume_session_ == suspend_.snapshot().session) {
        retry_resume_session_ = 0;
        command = suspend_.retry_resume(input.now_ms);
    } else {
        command = suspend_.poll(input.now_ms, wake);
    }
    if (command == Command::Suspended) {
        if (set_temporary_brightness(0)) policy_.confirm_hardware_sleep();
        else command = suspend_.resume_after_actuator_error(input.now_ms);
    } else if (command == Command::Resumed && suspend_.snapshot().resume_to_active) {
        if (!restore_backlight()) {
            restore_pending_ = true;
            restore_retry_ms_ = input.now_ms;
        }
        policy_.recover_active(input.now_ms, input.activity_revision);
    }
    dispatch_suspend_command(command);
}

void PowerManager::dispatch_suspend_command(SuspendCoordinator::Command command)
{
    const char *event = nullptr;
    switch (command) {
    case SuspendCoordinator::Command::Prepare: event = "PrepareSuspend"; break;
    case SuspendCoordinator::Command::Commit: event = "CommitSuspend"; break;
    case SuspendCoordinator::Command::Cancel: event = "CancelSuspend"; break;
    case SuspendCoordinator::Command::Resume: event = "Resume"; break;
    case SuspendCoordinator::Command::Recovery: event = "SuspendRecovery"; break;
    default: return;
    }
    const auto snapshot = suspend_.snapshot();
    BROOKESIA_LOGI("Pwr %1%: session=%2%, mask=%3%, reason=%4%", event, snapshot.session,
                  static_cast<unsigned>(snapshot.expected), suspend_reason_name(snapshot.reason));
    if (!publish_event(event, std::vector<service::EventItem>{
            static_cast<double>(snapshot.session), static_cast<double>(snapshot.expected),
            std::string(suspend_reason_name(snapshot.reason))})) {
        BROOKESIA_LOGE("Pwr failed to publish %1%; acknowledgement deadline remains active", event);
    }
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
        {"RegisterParticipant", "Register a required suspend participant", {
            {"ParticipantId", "Stable id in [1,8]", service::FunctionValueType::Number}}},
        {"UnregisterParticipant", "Remove a suspend participant outside a session", {
            {"ParticipantId", "Stable id in [1,8]", service::FunctionValueType::Number}}},
        {"ReportSuspendStatus", "Acknowledge the current suspend session", {
            {"SessionId", "Suspend session", service::FunctionValueType::Number},
            {"ParticipantId", "Registered participant", service::FunctionValueType::Number},
            {"Status", "Ready, Busy, Error, Suspended, Resumed", service::FunctionValueType::String}}},
        {.name = "GetSuspendStatus", .description = "Read coordination status and acknowledgement masks",
         .return_value = service::FunctionReturnSchema{service::FunctionValueType::Object, "Suspend status"}},
        {"RetryResume", "Retry recovery with a fresh session", {
            {"SessionId", "Current recovery session", service::FunctionValueType::Number}}},
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
        [this](service::FunctionParameterMap &&args) -> service::FunctionResult {
            auto id = uint_parameter(args, "ParticipantId");
            const bool ok = id && *id >= 1 && *id <= 8 && register_participant(static_cast<uint8_t>(*id));
            return {.success = ok, .error_message = ok ? "" : "Invalid participant or active suspend session"};
        },
        [this](service::FunctionParameterMap &&args) -> service::FunctionResult {
            auto id = uint_parameter(args, "ParticipantId");
            const bool ok = id && *id >= 1 && *id <= 8 && unregister_participant(static_cast<uint8_t>(*id));
            return {.success = ok, .error_message = ok ? "" : "Invalid participant or active suspend session"};
        },
        [this](service::FunctionParameterMap &&args) -> service::FunctionResult {
            auto session = uint_parameter(args, "SessionId");
            auto id = uint_parameter(args, "ParticipantId");
            const auto &status = std::get<std::string>(args.at("Status"));
            std::optional<SuspendCoordinator::Report> report;
            if (status == "Ready") report = SuspendCoordinator::Report::Ready;
            else if (status == "Busy") report = SuspendCoordinator::Report::Busy;
            else if (status == "Error") report = SuspendCoordinator::Report::Error;
            else if (status == "Suspended") report = SuspendCoordinator::Report::Suspended;
            else if (status == "Resumed") report = SuspendCoordinator::Report::Resumed;
            const bool ok = session && id && *id >= 1 && *id <= 8 && report &&
                report_suspend_status(*session, static_cast<uint8_t>(*id), *report);
            return {.success = ok, .error_message = ok ? "" : "Stale session, participant or unexpected status"};
        },
        [this](service::FunctionParameterMap &&) -> service::FunctionResult {
            const auto snapshot = suspend_status();
            return {.success = true, .data = boost::json::object{
                {"Phase", suspend_phase_name(snapshot.phase)}, {"SessionId", snapshot.session},
                {"RegisteredMask", snapshot.registered}, {"ExpectedMask", snapshot.expected},
                {"ReadyMask", snapshot.ready}, {"SuspendedMask", snapshot.suspended},
                {"ResumedMask", snapshot.resumed}, {"Reason", suspend_reason_name(snapshot.reason)}}};
        },
        [this](service::FunctionParameterMap &&args) -> service::FunctionResult {
            auto session = uint_parameter(args, "SessionId");
            const bool ok = session && retry_resume(*session);
            return {.success = ok, .error_message = ok ? "" : "Recovery session is not current"};
        },
    };
}

std::vector<service::EventSchema> PowerManager::get_event_schemas()
{
    std::vector<service::EventSchema> schemas{{"StateChanged", "Power state transition", {
        {"Previous", "Previous state", service::EventItemType::String},
        {"Current", "Current state", service::EventItemType::String}}}};
    for (const char *name : {"PrepareSuspend", "CommitSuspend", "CancelSuspend", "Resume", "SuspendRecovery"}) {
        schemas.push_back({name, "Suspend coordination", {
            {"SessionId", "Current session", service::EventItemType::Number},
            {"ParticipantMask", "Participants that must acknowledge", service::EventItemType::Number},
            {"Reason", "Transition reason", service::EventItemType::String}}});
    }
    return schemas;
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
        if (suspend_.snapshot().registered != 0) {
            policy_.defer_hardware_sleep();
            if (suspend_.begin(input.now_ms)) {
                suspend_activity_revision_ = input.activity_revision;
                dispatch_suspend_command(SuspendCoordinator::Command::Prepare);
            }
            break;
        }
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
