#include "music_player.hpp"

#include <algorithm>
#include <chrono>

namespace music {

namespace {

uint64_t steady_now_ms()
{
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

} // namespace

Player::Player(PlayerBackend &backend, Config config): backend_(backend), config_(std::move(config))
{
    if (!config_.now_ms) config_.now_ms = steady_now_ms;
}

void Player::set_ended_handler(std::function<void(size_t)> handler)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    ended_handler_ = std::move(handler);
}

uint32_t Player::elapsed_locked() const
{
    uint64_t total = accumulated_ms_;
    if (state_ == PlayerState::Playing) total += config_.now_ms() - run_start_ms_;
    if (duration_ms_ > 0) total = std::min<uint64_t>(total, duration_ms_);
    return uint32_t(total);
}

void Player::freeze_locked()
{
    accumulated_ms_ = elapsed_locked();
}

void Player::set_state_locked(PlayerState state)
{
    if (state_ == state) return;
    state_ = state;
    if (state_ != PlayerState::Preparing) stale_idle_ = false;
    version_++;
}

bool Player::start_locked(size_t index, const std::string &path, uint32_t duration_ms)
{
    index_ = index;
    duration_ms_ = duration_ms;
    accumulated_ms_ = 0;
    prepare_start_ms_ = config_.now_ms();
    state_ = PlayerState::Preparing;
    stop_failed_ = false;
    switch_from_.reset();
    version_++;
    if (!issue_play(path)) {
        if (state_ == PlayerState::Preparing) set_state_locked(PlayerState::Error);
        return false;
    }
    return true;
}

bool Player::issue_play(const std::string &path)
{
    const size_t slot = size_t(BackendCommand::Play);
    const uint64_t previous = last_seq_[slot];
    last_seq_[slot] = ++issued_;
    if (backend_.play(path, last_seq_[slot])) return true;
    last_seq_[slot] = previous;
    return false;
}

bool Player::issue_pause()
{
    const size_t slot = size_t(BackendCommand::Pause);
    const uint64_t previous = last_seq_[slot];
    last_seq_[slot] = ++issued_;
    if (backend_.pause(last_seq_[slot])) return true;
    last_seq_[slot] = previous;
    return false;
}

bool Player::issue_resume()
{
    const size_t slot = size_t(BackendCommand::Resume);
    const uint64_t previous = last_seq_[slot];
    last_seq_[slot] = ++issued_;
    if (backend_.resume(last_seq_[slot])) return true;
    last_seq_[slot] = previous;
    return false;
}

bool Player::issue_stop()
{
    const size_t slot = size_t(BackendCommand::Stop);
    const uint64_t previous = last_seq_[slot];
    last_seq_[slot] = ++issued_;
    if (backend_.stop(last_seq_[slot])) return true;
    last_seq_[slot] = previous;
    return false;
}

void Player::retry_stop_locked()
{
    if (!stop_failed_) return;
    if (issue_stop()) {
        stop_failed_ = false;
        stop_outstanding_ = backend_state_ != BackendState::Idle;
    }
}

bool Player::play(size_t index, const std::string &path, uint32_t duration_ms)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (backend_state_ == BackendState::Idle) {
        pending_.reset();
        stale_idle_ = false;
        if (state_ == PlayerState::Preparing) {
            // The previous play may still be starting although no event arrived.
            stale_idle_ = true;
            if (!issue_stop()) {
                stale_idle_ = false;
                return false;
            }
        }
        return start_locked(index, path, duration_ms);
    }

    const PlayerState old_state = state_;
    const size_t old_index = index_;
    const uint32_t old_duration = duration_ms_;
    const uint32_t old_elapsed = elapsed_locked();
    const std::optional<Pending> old_pending = pending_;

    if (!switch_from_ && (old_state == PlayerState::Playing || old_state == PlayerState::Paused)) {
        switch_from_ = Playing{old_state, old_index, old_duration, old_elapsed};
    }
    pending_ = Pending{index, path, duration_ms};
    index_ = index;
    duration_ms_ = duration_ms;
    accumulated_ms_ = 0;
    prepare_start_ms_ = config_.now_ms();
    state_ = PlayerState::Preparing;
    version_++;

    if (stop_outstanding_) return true;
    stop_outstanding_ = true;
    if (issue_stop()) return true;

    stop_outstanding_ = false;
    if (!old_pending) switch_from_.reset();
    pending_ = old_pending;
    index_ = old_index;
    duration_ms_ = old_duration;
    accumulated_ms_ = old_elapsed;
    state_ = old_state;
    if (state_ == PlayerState::Playing) run_start_ms_ = config_.now_ms();
    version_++;
    return false;
}

bool Player::pause()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_ != PlayerState::Playing) return false;
    if (!issue_pause()) return false;
    if (state_ != PlayerState::Playing) return true;
    freeze_locked();
    set_state_locked(PlayerState::Paused);
    return true;
}

bool Player::resume()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (state_ != PlayerState::Paused) return false;
    if (!issue_resume()) return false;
    if (state_ != PlayerState::Paused) return true;
    run_start_ms_ = config_.now_ms();
    set_state_locked(PlayerState::Playing);
    return true;
}

bool Player::stop()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (pending_) {
        pending_.reset();
        switch_from_.reset();
    } else {
        const bool active = state_ == PlayerState::Preparing || state_ == PlayerState::Playing ||
                            state_ == PlayerState::Paused;
        if (!active && !stop_failed_) {
            accumulated_ms_ = 0;
            set_state_locked(PlayerState::Stopped);
            return true;
        }
        const bool busy = backend_state_ != BackendState::Idle;
        if (!(busy && stop_outstanding_)) {
            if (busy) stop_outstanding_ = true;
            if (!issue_stop()) {
                stop_outstanding_ = false;
                return false;
            }
            stop_failed_ = false;
        }
    }
    accumulated_ms_ = 0;
    set_state_locked(PlayerState::Stopped);
    return true;
}

void Player::on_backend_state(BackendState state)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    backend_state_ = state;
    switch (state) {
    case BackendState::Playing:
        if (pending_) break;
        if (state_ == PlayerState::Preparing) {
            stale_idle_ = false;
            last_seq_[size_t(BackendCommand::Play)] = 0;
            accumulated_ms_ = 0;
            run_start_ms_ = config_.now_ms();
            set_state_locked(PlayerState::Playing);
        } else if (state_ == PlayerState::Paused) {
            last_seq_[size_t(BackendCommand::Resume)] = 0;
            run_start_ms_ = config_.now_ms();
            set_state_locked(PlayerState::Playing);
        } else if (state_ == PlayerState::Playing) {
            last_seq_[size_t(BackendCommand::Resume)] = 0;
        } else {
            retry_stop_locked();
        }
        break;
    case BackendState::Paused:
        if (pending_) break;
        if (state_ == PlayerState::Playing) {
            last_seq_[size_t(BackendCommand::Pause)] = 0;
            freeze_locked();
            set_state_locked(PlayerState::Paused);
        } else if (state_ == PlayerState::Paused) {
            last_seq_[size_t(BackendCommand::Pause)] = 0;
        } else {
            retry_stop_locked();
        }
        break;
    case BackendState::Idle:
        stop_outstanding_ = false;
        stop_failed_ = false;
        stop_retries_ = 0;
        last_seq_[size_t(BackendCommand::Stop)] = 0;
        switch_from_.reset();
        if (pending_) {
            const Pending next = *pending_;
            pending_.reset();
            start_locked(next.index, next.path, next.duration_ms);
            break;
        }
        if (stale_idle_ && state_ == PlayerState::Preparing) {
            stale_idle_ = false;
            break;
        }
        switch (state_) {
        case PlayerState::Preparing:
            set_state_locked(PlayerState::Error);
            break;
        case PlayerState::Playing: {
            const uint32_t elapsed = elapsed_locked();
            accumulated_ms_ = duration_ms_ > 0 ? duration_ms_ : elapsed;
            set_state_locked(PlayerState::Ended);
            if (ended_handler_) ended_handler_(index_);
            break;
        }
        case PlayerState::Paused:
            accumulated_ms_ = 0;
            set_state_locked(PlayerState::Stopped);
            break;
        default:
            break;
        }
        break;
    }
}

void Player::on_command_failed(BackendCommand command, uint64_t seq)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (seq != last_seq_[size_t(command)]) return;
    switch (command) {
    case BackendCommand::Play:
        if (state_ == PlayerState::Preparing && !pending_) {
            stale_idle_ = false;
            set_state_locked(PlayerState::Error);
        }
        break;
    case BackendCommand::Pause:
        if (state_ == PlayerState::Paused) {
            run_start_ms_ = config_.now_ms();
            set_state_locked(PlayerState::Playing);
        }
        break;
    case BackendCommand::Resume:
        if (state_ == PlayerState::Playing) {
            freeze_locked();
            set_state_locked(PlayerState::Paused);
        }
        break;
    case BackendCommand::Stop:
        stop_outstanding_ = false;
        if (backend_state_ == BackendState::Idle) break;
        stop_failed_ = true;
        next_stop_retry_ms_ = config_.now_ms() + config_.stop_retry_ms;
        if (pending_) {
            pending_.reset();
            if (switch_from_) {
                // The old track was never stopped and is still audible.
                index_ = switch_from_->index;
                duration_ms_ = switch_from_->duration_ms;
                accumulated_ms_ = switch_from_->elapsed_ms;
                run_start_ms_ = config_.now_ms();
                stop_failed_ = false;
                const PlayerState restored = switch_from_->state;
                switch_from_.reset();
                state_ = PlayerState::Preparing;
                set_state_locked(restored);
            } else {
                set_state_locked(PlayerState::Error);
            }
        }
        break;
    }
}

void Player::tick()
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (stop_failed_ && !stop_outstanding_ && backend_state_ != BackendState::Idle &&
            stop_retries_ < config_.stop_retry_limit && config_.now_ms() >= next_stop_retry_ms_) {
        stop_retries_++;
        next_stop_retry_ms_ = config_.now_ms() + config_.stop_retry_ms;
        retry_stop_locked();
    } else if (stop_failed_ && !stop_outstanding_ && backend_state_ != BackendState::Idle &&
               stop_retries_ >= config_.stop_retry_limit && config_.now_ms() >= next_stop_retry_ms_ &&
               (state_ == PlayerState::Stopped || state_ == PlayerState::Ended)) {
        set_state_locked(PlayerState::Error);
    }
    if (state_ != PlayerState::Preparing) return;
    if (pending_) {
        if (config_.now_ms() - prepare_start_ms_ < config_.switch_timeout_ms) return;
        pending_.reset();
        switch_from_.reset();
        stop_outstanding_ = false;
        stop_failed_ = true;
        set_state_locked(PlayerState::Error);
        return;
    }
    if (config_.now_ms() - prepare_start_ms_ < config_.prepare_timeout_ms) return;
    const bool stopped = issue_stop();
    if (state_ != PlayerState::Preparing) return;
    stop_failed_ = !stopped;
    stop_outstanding_ = stopped && backend_state_ != BackendState::Idle;
    set_state_locked(PlayerState::Error);
}

void Player::set_duration(size_t index, uint32_t duration_ms)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (duration_ms == 0) return;
    if (index == index_ && duration_ms_ == 0 && state_ != PlayerState::Stopped) duration_ms_ = duration_ms;
    // The track waiting for the old one to stop starts with this value.
    if (pending_ && pending_->index == index && pending_->duration_ms == 0) pending_->duration_ms = duration_ms;
    // The old track keeps sounding if a switch is rolled back.
    if (switch_from_ && switch_from_->index == index && switch_from_->duration_ms == 0) {
        switch_from_->duration_ms = duration_ms;
    }
}

PlayerSnapshot Player::snapshot() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    PlayerSnapshot out;
    out.state = state_;
    out.index = index_;
    out.elapsed_ms = elapsed_locked();
    out.duration_ms = duration_ms_;
    out.version = version_;
    return out;
}

} // namespace music
