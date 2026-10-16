#pragma once

#include <cstdint>
#include <mutex>

// Producers update a fixed-size snapshot; the service consumes it periodically.
class PowerServiceInputs {
public:
    struct Snapshot {
        uint32_t activity_revision = 0;
        uint32_t output_id = 0;
        bool configured = false;
        bool blocked = true;
        bool display_ready = false;
    };

    void configure(uint32_t output_id)
    {
        std::lock_guard lock(mutex_);
        state_.output_id = output_id;
        state_.configured = true;
        state_.blocked = true;
        state_.display_ready = false;
        ++state_.activity_revision;
    }

    void notify_activity()
    {
        std::lock_guard lock(mutex_);
        ++state_.activity_revision;
    }

    void set_ui_conditions(uint32_t activity_revision, bool blocked, bool display_ready)
    {
        std::lock_guard lock(mutex_);
        if (activity_revision != ui_activity_revision_) {
            ui_activity_revision_ = activity_revision;
            ++state_.activity_revision;
        }
        state_.blocked = blocked;
        state_.display_ready = display_ready;
    }

    Snapshot snapshot() const
    {
        std::lock_guard lock(mutex_);
        return state_;
    }

private:
    mutable std::mutex mutex_;
    Snapshot state_;
    uint32_t ui_activity_revision_ = 0;
};
