/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <cstdint>
#include <mutex>

// Decides whether the native Emote renderer may run: only while Emote owns the display
// and no power suspend holds it. A source-only pause is requested at once; a power pause
// waits until the renderer is quiesced. The backend must not block on render locks.
class EmoteRenderOwner {
public:
    static constexpr uint32_t ACTIVATION_TIMEOUT_MS = 800;

    struct Backend {
        virtual ~Backend() = default;
        virtual bool request_pause() = 0;
        virtual bool set_paused(bool paused) = 0;
    };

    // Returns true when the renderer matches the request; false means retry later.
    bool set_power_paused(bool paused, uint32_t now_ms, Backend &backend)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        power_ = paused;
        return apply(now_ms, backend);
    }

    // A non-Emote source owns the display now.
    void source_lost()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = State::Lost;
        ++epoch_;
    }

    // Returns 0 when Emote already owns the display, otherwise the token for activation_ready().
    uint32_t begin_activation(uint32_t now_ms)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Owned) {
            return 0;
        }
        state_ = State::Activating;
        start_ms_ = now_ms;
        return ++epoch_;
    }

    // Emote state is current; a stale token is ignored.
    void activation_ready(uint32_t token)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Activating && token == epoch_) {
            state_ = State::Owned;
        }
    }

    bool update(uint32_t now_ms, Backend &backend)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return apply(now_ms, backend);
    }

    bool paused() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return gfx_ != Gfx::Running;
    }

    // True once after an activation was released by the timeout instead of the barrier.
    bool take_timed_out()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool value = timed_out_;
        timed_out_ = false;
        return value;
    }

private:
    enum class State : uint8_t { Lost, Activating, Owned };
    enum class Gfx : uint8_t { Running, Requested, Quiesced };

    bool source_blocked(uint32_t now_ms)
    {
        if (state_ == State::Activating && (now_ms - start_ms_) >= ACTIVATION_TIMEOUT_MS) {
            state_ = State::Owned;
            timed_out_ = true;
        }
        return state_ != State::Owned;
    }

    bool apply(uint32_t now_ms, Backend &backend)
    {
        const bool blocked = source_blocked(now_ms);
        if (!power_ && !blocked) {
            if (gfx_ == Gfx::Running) {
                return true;
            }
            if (!backend.set_paused(false)) {
                return false;
            }
            gfx_ = Gfx::Running;
            return true;
        }
        if (power_) {
            if (gfx_ == Gfx::Quiesced) {
                return true;
            }
            if (!backend.set_paused(true)) {
                return false;
            }
            gfx_ = Gfx::Quiesced;
            return true;
        }
        if (gfx_ != Gfx::Running) {
            return true;
        }
        if (!backend.request_pause()) {
            return false;
        }
        gfx_ = Gfx::Requested;
        return true;
    }

    mutable std::mutex mutex_;
    State state_ = State::Lost;
    uint32_t epoch_ = 0, start_ms_ = 0;
    Gfx gfx_ = Gfx::Running;
    bool power_ = false, timed_out_ = false;
};
