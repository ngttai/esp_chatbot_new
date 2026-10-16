/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <cstdint>

// ST77916 sleep timings use monotonic deadlines, not scheduler-worker delays.
class PanelPower {
public:
    enum class Command : uint8_t { DisplayOff, SleepIn, SleepOut, DisplayOn };
    enum class Result : uint8_t { Pending, Ready, Error };
    struct Backend {
        virtual ~Backend() = default;
        virtual bool blackout() = 0;
        virtual bool command(Command command) = 0;
    };
    Result suspend(uint32_t now, Backend &backend)
    {
        if (state_ == State::Awake) {
            if (wake_deadline_valid_ && elapsed(now) < WAKE_SETTLE_MS) return Result::Pending;
            if (!backend.blackout()) return Result::Error;
            if (!backend.command(Command::DisplayOff)) return fault(now);
            state_ = State::DisplayOff;
            return Result::Pending;
        }
        if (state_ == State::DisplayOff) {
            since_ = now;
            if (!backend.command(Command::SleepIn)) return fault(now);
            state_ = State::SleepInWait;
            return Result::Pending;
        }
        if (state_ == State::SleepInWait) {
            if (elapsed(now) < SLEEP_SETTLE_MS) return Result::Pending;
            state_ = State::Asleep;
        }
        return state_ == State::Asleep ? Result::Ready : Result::Error;
    }
    Result resume(uint32_t now, Backend &backend)
    {
        if (state_ == State::Awake) return Result::Ready;
        if (state_ == State::DisplayOff) {
            if (!backend.command(Command::DisplayOn)) return fault(now);
            state_ = State::Awake;
            return Result::Ready;
        }
        if (state_ == State::SleepInWait && elapsed(now) < SLEEP_SETTLE_MS) return Result::Pending;
        // A failed write may have reached the panel. Keep the conservative settle interval.
        if (state_ == State::Fault && elapsed(now) < WAKE_SETTLE_MS) return Result::Pending;
        if (state_ != State::SleepOutWait) {
            if (!backend.blackout()) return Result::Error;
            since_ = now;
            if (!backend.command(Command::SleepOut)) return fault(now);
            state_ = State::SleepOutWait;
            wake_deadline_valid_ = true;
            return Result::Pending;
        }
        if (elapsed(now) < WAKE_SETTLE_MS) return Result::Pending;
        if (!backend.command(Command::DisplayOn)) return fault(now);
        state_ = State::Awake;
        return Result::Ready;
    }
    static constexpr uint32_t SLEEP_SETTLE_MS = 5;
    // Driver boot sequence waits 120ms after SLPOUT; also fences SLPOUT -> next SLPIN.
    static constexpr uint32_t WAKE_SETTLE_MS = 120;
private:
    enum class State : uint8_t { Awake, DisplayOff, SleepInWait, Asleep, SleepOutWait, Fault };
    uint32_t elapsed(uint32_t now) const { return now - since_; }
    Result fault(uint32_t now) { since_ = now; state_ = State::Fault; return Result::Error; }
    State state_ = State::Awake;
    uint32_t since_ = 0;
    bool wake_deadline_valid_ = false;
};
