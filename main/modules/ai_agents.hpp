/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include "modules/agent_power_activity.hpp"
#include "brookesia/service_manager/event/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"

class AI_Agents {
public:
    struct Config {
        std::shared_ptr<esp_brookesia::lib_utils::TaskScheduler> task_scheduler;
        uint32_t afe_wakeup_start_timeout_ms = 30000;
        uint32_t afe_wakeup_end_timeout_ms = 10000;
        uint32_t emote_animation_duration_ms = 5000;
        uint32_t agent_restart_delay_s = 30;
        uint32_t coze_error_show_emote_delay_s = 2;
    };

    static AI_Agents &get_instance()
    {
        static AI_Agents instance;
        return instance;
    }

    bool init(const Config &config);

    bool is_initialized() const
    {
        return task_scheduler_ != nullptr;
    }

    bool is_slept() const
    {
        return is_slept_.load(std::memory_order_relaxed);
    }

    bool blocks_power_idle() const
    {
        return agent_blocks_power_idle({
            .listening = is_listening_.load(),
            .thinking = is_thinking_.load(),
            .speaking = is_speaking_.load(),
            .starting = power_starting_.load(),
            .session_requested = power_session_requested_.load(),
            .slept = is_slept_.load(),
            .stopped = is_stopped_.load(),
            .error = has_agent_error_.load(),
        });
    }

    // Coarse agent state for diagnostics; nullptr before init.
    const char *state_name() const
    {
        if (!is_initialized()) {
            return nullptr;
        }
        if (is_stopped_.load(std::memory_order_relaxed)) return "Stopped";
        if (has_agent_error_.load(std::memory_order_relaxed)) return "Error";
        if (power_starting_.load(std::memory_order_relaxed)) return "Starting";
        if (is_speaking_.load(std::memory_order_relaxed)) return "Speaking";
        if (is_thinking_.load(std::memory_order_relaxed)) return "Thinking";
        if (is_listening_.load(std::memory_order_relaxed)) return "Listening";
        if (is_sleeping_.load(std::memory_order_relaxed)) return "Sleeping";
        if (is_slept_.load(std::memory_order_relaxed)) return "Slept";
        if (is_suspended_.load(std::memory_order_relaxed)) return "Suspended";
        return "Idle";
    }

    bool wifi_link_connected() const
    {
        return is_wifi_connected_.load(std::memory_order_relaxed);
    }

    void init_coze();
    void init_openai();
    void init_xiaozhi();
    void handle_touch_sensor_click();
    void handle_touch_sensor_long_press();
    void set_emote_output_active(bool active);

private:
    enum class EmoteState : uint8_t {
        Idle,
        Listening,
        Thinking,
        Speaking,
        Sleeping,
        Error,
        Stopped,
        Max,
    };

    AI_Agents() = default;
    ~AI_Agents() = default;
    AI_Agents(const AI_Agents &) = delete;
    AI_Agents &operator=(const AI_Agents &) = delete;

    bool start_agent(uint32_t wifi_request_revision);
    void stop_agent();

    void process_agent_general_events();
    void process_agent_general_unexpected_events();
    void process_agent_general_suspend_status_changed();
    void process_bt_speaker_events();

    void process_emote();
    void process_emote_when_general_action_triggered();
    void process_emote_when_general_event_happened();
    void process_emote_when_suspend_status_changed();
    void process_emote_when_speaking_status_changed();
    void process_emote_when_listening_status_changed();
    void process_emote_when_agent_speaking_text_got();
    void process_emote_when_user_speaking_text_got();
    void process_emote_when_emote_got();
    void process_emote_when_power_battery_state_changed();
    void process_emote_when_coze_event_happened();
    EmoteState resolve_emote_state() const;
    void refresh_emote_state();
    void refresh_emote_state_delayed();
    void apply_emote_state(EmoteState state);

    void process_wifi_events();

    bool is_listening() const
    {
        return is_listening_.load(std::memory_order_relaxed);
    }

    bool is_speaking() const
    {
        return is_speaking_.load(std::memory_order_relaxed);
    }

    bool is_suspended() const
    {
        return is_suspended_.load(std::memory_order_relaxed);
    }

    bool is_sleeping() const
    {
        return is_sleeping_.load(std::memory_order_relaxed);
    }

    bool is_stopped() const
    {
        return is_stopped_.load(std::memory_order_relaxed);
    }

    bool is_inactive() const
    {
        return is_sleeping() || is_suspended() || is_stopped();
    }

    bool is_wifi_connected() const
    {
        return is_wifi_connected_.load(std::memory_order_relaxed);
    }

    static boost::json::object get_agent_coze_info();
    static boost::json::object get_agent_openai_info();

    std::shared_ptr<esp_brookesia::lib_utils::TaskScheduler> task_scheduler_;
    uint32_t emote_animation_duration_ms_ = 0;
    uint32_t agent_restart_delay_s_ = 0;
    uint32_t coze_error_show_emote_delay_s_ = 0;

    std::atomic_bool is_listening_{false};
    std::atomic_bool is_speaking_{false};
    std::atomic_bool is_suspended_{false};
    std::atomic_bool is_sleeping_{false};
    std::atomic_bool is_slept_{false};
    std::atomic_bool power_starting_{false};
    std::atomic_bool power_session_requested_{false};
    std::atomic_bool is_thinking_{false};
    std::atomic_bool has_agent_error_{false};
    std::atomic_bool is_stopped_{false};
    std::atomic_bool is_wifi_connected_{false};
    std::atomic_bool is_boot_idle_{false};
    std::atomic_bool emote_output_active_{true};
    std::atomic<EmoteState> emote_state_{EmoteState::Max};
    std::atomic_bool should_resume_bt_speaker_{false};
    std::atomic_int paused_bt_connection_id_{-1};

    // Keep service bindings to avoid frequent start and stop of services
    std::vector<esp_brookesia::service::ServiceBinding> service_bindings_;
    // Keep event connections
    std::vector<esp_brookesia::service::EventRegistry::SignalConnection> service_connections_;
};
