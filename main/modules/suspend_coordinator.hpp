#pragma once

#include <cstdint>

// Single-owner state machine. The service serializes access; no allocation or waiting.
class SuspendCoordinator {
public:
    static constexpr uint8_t MAX_PARTICIPANTS = 8;
    enum class Phase : uint8_t { Idle, Preparing, Committing, Suspended, Resuming, Backoff, Recovery };
    enum class Report : uint8_t { Ready, Busy, Error, Suspended, Resumed };
    enum class Command : uint8_t { None, Prepare, Commit, Cancel, Resume, Suspended, Resumed, Recovery };
    enum class Reason : uint8_t { None, Activity, Busy, Error, PrepareTimeout, CommitTimeout, ResumeTimeout, ActuatorError, Stopped };
    struct Snapshot {
        Phase phase = Phase::Idle;
        Reason reason = Reason::None;
        uint32_t session = 0;
        uint8_t registered = 0;
        uint8_t expected = 0;
        uint8_t ready = 0;
        uint8_t suspended = 0;
        uint8_t resumed = 0;
        bool resume_to_active = false;
    };

    SuspendCoordinator(uint32_t prepare_ms, uint32_t resume_ms, uint32_t retry_ms)
        : prepare_ms_(prepare_ms), resume_ms_(resume_ms), retry_ms_(retry_ms) {}

    Snapshot snapshot() const { return state_; }
    bool register_participant(uint8_t id)
    {
        if (!can_register() || !valid_id(id) || (state_.registered & bit(id))) return false;
        state_.registered |= bit(id);
        return true;
    }
    bool unregister_participant(uint8_t id)
    {
        if (!can_register() || !valid_id(id) || !(state_.registered & bit(id))) return false;
        state_.registered &= static_cast<uint8_t>(~bit(id));
        return true;
    }
    bool begin(uint32_t now)
    {
        if (state_.phase != Phase::Idle || state_.registered == 0) return false;
        next_session();
        state_.expected = state_.registered;
        state_.ready = state_.suspended = state_.resumed = 0;
        state_.reason = Reason::None;
        state_.resume_to_active = false;
        rejected_ = false;
        wake_pending_ = false;
        state_.phase = Phase::Preparing;
        since_ms_ = now;
        return true;
    }
    bool report(uint32_t session, uint8_t id, Report result)
    {
        if (session != state_.session || !valid_id(id) || !(state_.expected & bit(id))) return false;
        if (state_.phase == Phase::Preparing) {
            if (result == Report::Ready) { state_.ready |= bit(id); return true; }
            if (result == Report::Busy || result == Report::Error) {
                rejected_ = true;
                state_.reason = result == Report::Busy ? Reason::Busy : Reason::Error;
                return true;
            }
        } else if (state_.phase == Phase::Committing) {
            if (result == Report::Suspended) { state_.suspended |= bit(id); return true; }
            if (result == Report::Error || result == Report::Busy) {
                rejected_ = true;
                state_.reason = Reason::Error;
                return true;
            }
        } else if (state_.phase == Phase::Resuming && result == Report::Resumed) {
            state_.resumed |= bit(id);
            return true;
        }
        return false;
    }
    Command poll(uint32_t now, bool wake)
    {
        switch (state_.phase) {
        case Phase::Preparing:
            if (wake) return start_resume(now, Command::Cancel, Reason::Activity, true);
            if (rejected_) return start_resume(now, Command::Cancel, state_.reason, false);
            if (elapsed(now) >= prepare_ms_)
                return start_resume(now, Command::Cancel, Reason::PrepareTimeout, false);
            if (state_.ready == state_.expected) {
                state_.phase = Phase::Committing;
                since_ms_ = now;
                return Command::Commit;
            }
            break;
        case Phase::Committing:
            wake_pending_ |= wake;
            if (rejected_) return start_resume(now, Command::Resume, state_.reason, true);
            // Commit acknowledgement is bounded by the prepare timeout in this phase.
            if (elapsed(now) >= prepare_ms_)
                return start_resume(now, Command::Resume, Reason::CommitTimeout, true);
            if (state_.suspended == state_.expected) {
                if (wake_pending_) return start_resume(now, Command::Resume, Reason::Activity, true);
                state_.phase = Phase::Suspended;
                return Command::Suspended;
            }
            break;
        case Phase::Suspended:
            if (wake) return start_resume(now, Command::Resume, Reason::Activity, true);
            break;
        case Phase::Resuming:
            state_.resume_to_active |= wake;
            if (elapsed(now) >= resume_ms_) {
                state_.phase = Phase::Recovery;
                state_.reason = Reason::ResumeTimeout;
                return Command::Recovery;
            }
            if (state_.resumed == state_.expected) {
                state_.phase = state_.resume_to_active ? Phase::Idle : Phase::Backoff;
                since_ms_ = now;
                return Command::Resumed;
            }
            break;
        case Phase::Backoff:
            if (wake || elapsed(now) >= retry_ms_) state_.phase = Phase::Idle;
            break;
        case Phase::Idle:
        case Phase::Recovery:
            break;
        }
        return Command::None;
    }
    Command resume_after_actuator_error(uint32_t now)
    {
        if (state_.phase != Phase::Suspended) return Command::None;
        return start_resume(now, Command::Resume, Reason::ActuatorError, true);
    }
    Command retry_resume(uint32_t now)
    {
        if (state_.phase != Phase::Recovery) return Command::None;
        next_session();
        state_.ready = state_.suspended = 0;
        return start_resume(now, Command::Resume, state_.reason, true);
    }
    void interrupt()
    {
        if (state_.phase == Phase::Idle || state_.phase == Phase::Backoff) return;
        next_session();
        state_.phase = Phase::Recovery;
        state_.reason = Reason::Stopped;
        state_.ready = state_.suspended = state_.resumed = 0;
    }

private:
    static bool valid_id(uint8_t id) { return id >= 1 && id <= MAX_PARTICIPANTS; }
    static uint8_t bit(uint8_t id) { return static_cast<uint8_t>(1u << (id - 1)); }
    bool can_register() const { return state_.phase == Phase::Idle || state_.phase == Phase::Backoff; }
    uint32_t elapsed(uint32_t now) const { return now - since_ms_; }
    void next_session() { if (++state_.session == 0) ++state_.session; }
    Command start_resume(uint32_t now, Command command, Reason reason, bool to_active)
    {
        state_.phase = Phase::Resuming;
        state_.reason = reason;
        state_.resume_to_active = to_active;
        state_.resumed = 0;
        since_ms_ = now;
        return command;
    }

    Snapshot state_;
    uint32_t prepare_ms_;
    uint32_t resume_ms_;
    uint32_t retry_ms_;
    uint32_t since_ms_ = 0;
    bool rejected_ = false;
    bool wake_pending_ = false;
};
