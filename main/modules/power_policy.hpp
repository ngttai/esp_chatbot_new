/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>

enum class PowerPolicyState : uint8_t {
    Boot,
    Active,
    WaitAgentSleep,
    Dimmed,
    HardwareSleep,
    Waking,
};

enum class PowerPolicyAction : uint8_t {
    None,
    EnterDim,
    ExitDim,
    EnterHardwareSleep,
    WakeHardware,
};

struct PowerPolicyInput {
    uint32_t now_ms = 0;
    uint32_t activity_revision = 0;
    bool agent_idle_allowed = false;
    bool blocked = false;
    bool display_ready = false;
};

class PowerPolicy {
public:
    PowerPolicy(uint32_t dim_timeout_ms, uint32_t hardware_sleep_timeout_ms,
                bool hardware_sleep_enabled):
        dim_timeout_ms_(dim_timeout_ms),
        hardware_sleep_timeout_ms_(hardware_sleep_timeout_ms),
        hardware_sleep_enabled_(hardware_sleep_enabled)
    {}

    PowerPolicyAction update(const PowerPolicyInput &input)
    {
        if (!initialized_) {
            if (!input.display_ready) {
                return PowerPolicyAction::None;
            }
            initialize_active(input);
            return PowerPolicyAction::None;
        }

        const bool activity_changed = input.activity_revision != activity_revision_;
        if (activity_changed) {
            activity_revision_ = input.activity_revision;
            last_activity_ms_ = input.now_ms;
        }

        if (!input.display_ready || input.blocked || activity_changed) {
            return activate(input.now_ms, input.activity_revision);
        }

        switch (state_) {
        case PowerPolicyState::Boot:
            initialize_active(input);
            return PowerPolicyAction::None;

        case PowerPolicyState::Active:
            if (elapsed(input.now_ms, last_activity_ms_) < dim_timeout_ms_) {
                return PowerPolicyAction::None;
            }
            if (!input.agent_idle_allowed) {
                state_ = PowerPolicyState::WaitAgentSleep;
                return PowerPolicyAction::None;
            }
            state_ = PowerPolicyState::Dimmed;
            dimmed_since_ms_ = input.now_ms;
            return PowerPolicyAction::EnterDim;

        case PowerPolicyState::WaitAgentSleep:
            if (!input.agent_idle_allowed) {
                return PowerPolicyAction::None;
            }
            state_ = PowerPolicyState::Dimmed;
            dimmed_since_ms_ = input.now_ms;
            return PowerPolicyAction::EnterDim;

        case PowerPolicyState::Dimmed:
            if (!input.agent_idle_allowed) {
                return activate(input.now_ms, input.activity_revision);
            }
            if (hardware_sleep_enabled_ &&
                    elapsed(input.now_ms, dimmed_since_ms_) >= hardware_sleep_timeout_ms_) {
                state_ = PowerPolicyState::HardwareSleep;
                return PowerPolicyAction::EnterHardwareSleep;
            }
            return PowerPolicyAction::None;

        case PowerPolicyState::HardwareSleep:
            if (!input.agent_idle_allowed) {
                state_ = PowerPolicyState::Waking;
                return PowerPolicyAction::WakeHardware;
            }
            return PowerPolicyAction::None;

        case PowerPolicyState::Waking:
            state_ = PowerPolicyState::Active;
            last_activity_ms_ = input.now_ms;
            return PowerPolicyAction::None;
        }
        return PowerPolicyAction::None;
    }

    void recover_active(uint32_t now_ms, uint32_t activity_revision)
    {
        initialized_ = true;
        state_ = PowerPolicyState::Active;
        activity_revision_ = activity_revision;
        last_activity_ms_ = now_ms;
    }

    PowerPolicyState state() const { return state_; }

private:
    static uint32_t elapsed(uint32_t now_ms, uint32_t since_ms)
    {
        return static_cast<uint32_t>(now_ms - since_ms);
    }

    void initialize_active(const PowerPolicyInput &input)
    {
        initialized_ = true;
        state_ = PowerPolicyState::Active;
        activity_revision_ = input.activity_revision;
        last_activity_ms_ = input.now_ms;
    }

    PowerPolicyAction activate(uint32_t now_ms, uint32_t activity_revision)
    {
        const auto previous_state = state_;
        state_ = PowerPolicyState::Active;
        activity_revision_ = activity_revision;
        last_activity_ms_ = now_ms;
        if (previous_state == PowerPolicyState::Dimmed) {
            return PowerPolicyAction::ExitDim;
        }
        if (previous_state == PowerPolicyState::HardwareSleep) {
            state_ = PowerPolicyState::Waking;
            return PowerPolicyAction::WakeHardware;
        }
        return PowerPolicyAction::None;
    }

    uint32_t dim_timeout_ms_ = 0;
    uint32_t hardware_sleep_timeout_ms_ = 0;
    uint32_t last_activity_ms_ = 0;
    uint32_t dimmed_since_ms_ = 0;
    uint32_t activity_revision_ = 0;
    PowerPolicyState state_ = PowerPolicyState::Boot;
    bool hardware_sleep_enabled_ = false;
    bool initialized_ = false;
};
