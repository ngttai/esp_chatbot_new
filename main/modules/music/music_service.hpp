#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "music_library.hpp"

namespace music {

struct TrackView {
    char title[96];
    char artist[64];
    char album[64];
    uint32_t duration_ms;
    MetaState state;
};

// Background track scanner and metadata loader. All methods are cheap and
// non-blocking, so they can be called from the UI thread.
class Service {
public:
    static constexpr size_t kMaxWanted = 8;

    using ParseFn = std::function<ParseResult(const char *, TrackMeta &, const ParseOptions &)>;

    struct Config {
        std::string root;
        // Idle time required after the UI settles before any SD access.
        uint32_t settle_ms = 150;
        // Replaceable parser, defaults to parse_mp3_file.
        ParseFn parse;
        // Runs on the caller's thread right before the worker thread is created.
        std::function<void()> thread_setup;
    };

    explicit Service(Config config);
    ~Service() { stop(); }

    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    void start();
    void stop();

    // False while the UI is scrolling or animating: loading is cancelled at once.
    void set_ui_idle(bool idle);

    // Track that must always be loaded first (the one shown as now playing).
    void set_pinned(size_t index);

    // Tracks currently on screen, in priority order. Replaces the previous set.
    void set_wanted(const size_t *indices, size_t count);

    size_t count() const;
    bool scan_done() const { return scan_done_.load(); }
    // Increases whenever the list or any track data changes.
    uint32_t version() const { return version_.load(); }
    bool snapshot(size_t index, TrackView &out) const;

private:
    void run();
    bool scan_phase();
    bool wait_until_settled(std::unique_lock<std::mutex> &lock);
    bool pick_next_locked(size_t &index) const;
    bool settled_locked() const;
    static bool scan_checkpoint(void *ctx);
    static bool parse_cancelled(void *ctx);

    struct CancelContext {
        Service *service;
        uint32_t epoch;
    };

    Config config_;
    Library library_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::thread thread_;

    std::atomic<bool> running_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> scan_done_{false};
    std::atomic<uint32_t> epoch_{0};
    std::atomic<uint32_t> version_{0};

    std::chrono::steady_clock::time_point idle_since_{};
    size_t pinned_ = 0;
    size_t wanted_[kMaxWanted] = {};
    size_t wanted_count_ = 0;
};

} // namespace music
