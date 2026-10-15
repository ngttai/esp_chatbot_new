/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>

#include "modules/power_policy.hpp"
#include "modules/power_service_inputs.hpp"
#include "brookesia/service_manager/service/base.hpp"

struct PowerManagerInput {
    uint32_t now_ms = 0;
    uint32_t activity_revision = 0;
    bool agent_idle_allowed = false;
    bool blocked = false;
    bool display_ready = false;
};

class PowerManager : public esp_brookesia::service::ServiceBase {
public:
    static PowerManager &get_instance();
    static std::shared_ptr<PowerManager> get_shared_instance();
    static constexpr const char *SERVICE_NAME = "Pwr";
    ~PowerManager() override = default;

    void configure(uint32_t display_output_id) { inputs_.configure(display_output_id); }
    void set_ui_conditions(uint32_t activity_revision, bool blocked, bool display_ready)
    {
        inputs_.set_ui_conditions(activity_revision, blocked, display_ready);
    }
    void notify_user_activity()
    {
        inputs_.notify_activity();
    }
    PowerPolicyState state() const { return published_state_.load(); }
#if !defined(ESP_PLATFORM)
    // Host stub controls exercise the same service lifecycle and scheduler.
    void set_host_agent_idle(bool idle) { host_agent_idle_ = idle; }
    void fail_next_host_backlight() { host_fail_next_backlight_ = true; }
    int host_backlight_pwm() const { return host_backlight_pwm_.load(); }
#endif

private:
    PowerManager();
    bool on_start() override;
    void on_stop() override;
    std::vector<esp_brookesia::service::FunctionSchema> get_function_schemas() override;
    FunctionHandlerList get_function_handlers() override;
    std::vector<esp_brookesia::service::EventSchema> get_event_schemas() override;
    bool tick(uint32_t generation);
    void update(const PowerManagerInput &input);
    void publish_state();

    bool set_temporary_brightness(int brightness);
    bool restore_backlight();

    PowerServiceInputs inputs_;
    std::mutex lifecycle_mutex_;
    PowerPolicy policy_;
    std::atomic<PowerPolicyState> published_state_{PowerPolicyState::Boot};
    esp_brookesia::lib_utils::TaskSchedulerTaskId tick_task_ = 0;
    uint32_t generation_ = 0;
    uint32_t display_output_id_ = 0;
    bool restore_pending_ = false;
    uint32_t restore_retry_ms_ = 0;
    bool running_ = false;
#if !defined(ESP_PLATFORM)
    std::atomic_bool host_agent_idle_{true};
    std::atomic_bool host_fail_next_backlight_{false};
    std::atomic_int host_backlight_pwm_{100};
#endif
};
