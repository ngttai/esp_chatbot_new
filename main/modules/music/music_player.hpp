#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace music {

enum class PlayerState : uint8_t {
    Stopped,
    Preparing,
    Playing,
    Paused,
    Ended,
    Error,
};

// Playback state as reported by the audio service.
enum class BackendState : uint8_t {
    Idle,
    Playing,
    Paused,
};

enum class BackendCommand : uint8_t {
    Play,
    Pause,
    Resume,
    Stop,
};

// Commands may complete asynchronously: a true return only means the command
// was accepted; a later failure is reported with Player::on_command_failed.
class PlayerBackend {
public:
    virtual ~PlayerBackend() = default;
    // `seq` identifies the command; a later failure is reported with it.
    virtual bool play(const std::string &path, uint64_t seq) = 0;
    virtual bool pause(uint64_t seq) = 0;
    virtual bool resume(uint64_t seq) = 0;
    virtual bool stop(uint64_t seq) = 0;
};

struct PlayerSnapshot {
    PlayerState state = PlayerState::Stopped;
    size_t index = 0;
    uint32_t elapsed_ms = 0;
    uint32_t duration_ms = 0;
    // Increases on every state or track change.
    uint32_t version = 0;
};

// Single owner of the playback state. Switching tracks stops the old one,
// waits for the backend to report Idle and only then starts the newest
// requested track. An Idle that Music did not cause while playing means the
// track ended; a decoder failure after playback started cannot be told apart.
class Player {
public:
    struct Config {
        std::function<uint64_t()> now_ms;
        // Preparing without a Playing event for this long is an error.
        uint32_t prepare_timeout_ms = 5000;
        // Waiting for Idle while switching tracks; the switch then fails so a
        // late Idle of the old track can never be taken for the new one.
        uint32_t switch_timeout_ms = 2000;
        // A refused or failed stop is retried while the backend keeps playing.
        uint32_t stop_retry_ms = 500;
        uint8_t stop_retry_limit = 3;
    };

    Player(PlayerBackend &backend, Config config);

    Player(const Player &) = delete;
    Player &operator=(const Player &) = delete;

    // Starts a track; the current one is stopped first. Only the last request
    // made while a switch is pending is kept.
    bool play(size_t index, const std::string &path, uint32_t duration_ms);
    bool pause();
    bool resume();
    // A refused stop keeps the state so it can be retried.
    bool stop();

    // Feeds the state reported by the audio service. Any thread.
    void on_backend_state(BackendState state);
    // Reports that a command accepted earlier was refused by the audio service.
    // `seq` is the number the Player gave the command; a failure of a
    // superseded or already confirmed command is ignored.
    void on_command_failed(BackendCommand command, uint64_t seq);
    // Supplies the duration of `index` when it was unknown at play(). Ignored
    // once a duration is known or for another track.
    void set_duration(size_t index, uint32_t duration_ms);
    // Handles Preparing and switch timeouts. Call periodically.
    void tick();

    PlayerSnapshot snapshot() const;

    // Runs when a track finishes by itself.
    void set_ended_handler(std::function<void(size_t index)> handler);

private:
    struct Pending {
        size_t index;
        std::string path;
        uint32_t duration_ms;
    };

    uint32_t elapsed_locked() const;
    void freeze_locked();
    void set_state_locked(PlayerState state);
    bool start_locked(size_t index, const std::string &path, uint32_t duration_ms);
    void retry_stop_locked();
    bool issue_play(const std::string &path);
    bool issue_pause();
    bool issue_resume();
    bool issue_stop();

    PlayerBackend &backend_;
    Config config_;
    mutable std::recursive_mutex mutex_;
    std::function<void(size_t)> ended_handler_;

    PlayerState state_ = PlayerState::Stopped;
    BackendState backend_state_ = BackendState::Idle;
    size_t index_ = 0;
    uint32_t duration_ms_ = 0;
    uint32_t accumulated_ms_ = 0;
    uint64_t run_start_ms_ = 0;
    uint64_t prepare_start_ms_ = 0;
    std::optional<Pending> pending_;
    // A stop was issued and the matching Idle has not arrived yet.
    bool stop_outstanding_ = false;
    // A stop was refused while Music no longer wants sound.
    bool stop_failed_ = false;
    // A cancelled start may still report one Idle while the next track prepares.
    bool stale_idle_ = false;
    uint32_t version_ = 0;
    // Accepted commands, for matching late failures to the command that failed.
    uint64_t issued_ = 0;
    struct Playing {
        PlayerState state;
        size_t index;
        uint32_t duration_ms;
        uint32_t elapsed_ms;
    };
    // The track that keeps sounding if the stop of a switch is refused.
    std::optional<Playing> switch_from_;
    uint64_t last_seq_[4] = {};
    uint64_t next_stop_retry_ms_ = 0;
    uint8_t stop_retries_ = 0;
};

} // namespace music
