#pragma once

#include <mutex>
#include "wifi_control_state.hpp"

class WifiControl {
public:
    static WifiControl &get_instance();
    void init();
    bool is_enabled() const { return state_.snapshot().confirmed; }
    bool requested_enabled() const { return state_.snapshot().requested; }
    uint32_t request_revision() const { return state_.snapshot().revision; }
    bool is_request_current(uint32_t revision, bool enabled) const
    {
        return state_.is_request_current(revision, enabled);
    }
    bool has_pending() const { return state_.pending(); }
    void request_enabled(bool enabled) { state_.request(enabled); }
    void cancel_pending_request()
    {
        const auto state = state_.snapshot();
        state_.transition_done(state.revision, state.confirmed, false);
    }
    bool process_pending();
    bool reset_preferences();

private:
    bool apply_enabled(bool enabled, bool &actual);
    WifiControlState state_;
    std::mutex mutex_;
};
