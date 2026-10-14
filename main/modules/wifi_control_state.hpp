#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

inline std::optional<bool> wifi_confirmed_state(std::string_view state)
{
    if (state == "Idle" || state == "Inited") return false;
    if (state == "Started" || state == "Connected" || state == "Connecting" || state == "Disconnecting") return true;
    return std::nullopt;
}

class WifiControlState {
public:
    struct Snapshot {
        bool requested;
        bool confirmed;
        bool dirty;
        unsigned save_attempts;
        uint32_t revision;
    };

    void init(bool enabled)
    {
        std::lock_guard lock(mutex_);
        state_ = {enabled, enabled, false, 0, 0};
        persisted_ = enabled;
    }

    void request(bool enabled)
    {
        std::lock_guard lock(mutex_);
        state_.requested = enabled;
        ++state_.revision;
        state_.save_attempts = 0;
    }

    Snapshot snapshot() const
    {
        std::lock_guard lock(mutex_);
        return state_;
    }

    bool pending() const
    {
        auto state = snapshot();
        return state.requested != state.confirmed || (state.dirty && state.save_attempts < 3);
    }

    bool is_request_current(uint32_t revision, bool enabled) const
    {
        const auto state = snapshot();
        return state.revision == revision && state.requested == enabled;
    }

    void transition_done(uint32_t revision, bool actual, bool succeeded)
    {
        std::lock_guard lock(mutex_);
        state_.confirmed = actual;
        state_.dirty = actual != persisted_;
        state_.save_attempts = 0;
        if (!succeeded && revision == state_.revision) state_.requested = actual;
    }

    void save_done(bool value, bool succeeded)
    {
        std::lock_guard lock(mutex_);
        ++state_.save_attempts;
        if (succeeded) persisted_ = value;
        state_.dirty = state_.confirmed != persisted_;
    }

private:
    mutable std::mutex mutex_;
    Snapshot state_{true, true, false, 0, 0};
    bool persisted_ = true;
};
