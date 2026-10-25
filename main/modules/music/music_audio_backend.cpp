#include "music_audio_backend.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>


#include "brookesia/service_helper.hpp"
#include "brookesia/service_manager.hpp"
#include "music_log.hpp"
#include "music_pthread.hpp"

namespace {

using AudioHelper = esp_brookesia::service::helper::Audio;
using PlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
using esp_brookesia::service::helper::Timeout;

constexpr uint32_t kCallTimeoutMs = 500;
constexpr size_t kMaxQueuedCommands = 16;

std::string to_url(const std::string &path)
{
    return "file://" + (!path.empty() && path[0] == '/' ? path.substr(1) : path);
}

class AudioBackend final : public music::PlayerBackend {
public:
    AudioBackend(std::function<void(music::BackendState)> on_state,
                 std::function<void(music::BackendCommand, uint64_t)> on_failed)
        : on_state_(std::move(on_state)), on_failed_(std::move(on_failed))
    {
        subscribe();
    }

    ~AudioBackend() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            quit_ = true;
        }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        connection_.disconnect();
    }

    bool play(const std::string &path, uint64_t seq) override
    {
        return enqueue({music::BackendCommand::Play, path, seq});
    }
    bool pause(uint64_t seq) override { return enqueue({music::BackendCommand::Pause, {}, seq}); }
    bool resume(uint64_t seq) override { return enqueue({music::BackendCommand::Resume, {}, seq}); }
    bool stop(uint64_t seq) override { return enqueue({music::BackendCommand::Stop, {}, seq}); }

private:
    struct Command {
        music::BackendCommand type;
        std::string path;
        uint64_t seq = 0;
    };

    bool enqueue(Command command)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (quit_ || queue_.size() >= kMaxQueuedCommands) {
                MUSIC_LOG("cmd queue full");
                return false;
            }
            if (!worker_.joinable()) start_worker();
            queue_.push_back(std::move(command));
        }
        cv_.notify_one();
        return true;
    }

    // Created on the first command; the stack lives in external RAM.
    void start_worker()
    {
        music::ScopedThreadConfig config("music_cmd", 4096);
        worker_ = std::thread([this] { run(); });
    }

    void run()
    {
        for (;;) {
            Command command;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return quit_ || !queue_.empty(); });
                if (quit_) return;
                command = std::move(queue_.front());
                queue_.pop_front();
            }
            if (!execute(command) && on_failed_) on_failed_(command.type, command.seq);
        }
    }

    bool execute(const Command &command)
    {
        switch (command.type) {
        case music::BackendCommand::Play:
            MUSIC_LOG("cmd play %s", command.path.c_str());
            subscribe();
            return check("play", PlaybackHelper::call_function_sync(
                PlaybackHelper::FunctionId::Play, to_url(command.path),
                BROOKESIA_DESCRIBE_TO_JSON(AudioHelper::PlayUrlConfig{}).as_object(),
                Timeout(kCallTimeoutMs)));
        case music::BackendCommand::Pause:
            MUSIC_LOG("cmd pause");
            return check("pause", PlaybackHelper::call_function_sync(
                PlaybackHelper::FunctionId::Pause, Timeout(kCallTimeoutMs)));
        case music::BackendCommand::Resume:
            MUSIC_LOG("cmd resume");
            return check("resume", PlaybackHelper::call_function_sync(
                PlaybackHelper::FunctionId::Resume, Timeout(kCallTimeoutMs)));
        case music::BackendCommand::Stop:
            MUSIC_LOG("cmd stop");
            return check("stop", PlaybackHelper::call_function_sync(
                PlaybackHelper::FunctionId::Stop, Timeout(kCallTimeoutMs)));
        }
        return false;
    }

    template <typename Result>
    static bool check(const char *name, const Result &result)
    {
        if (result) return true;
        MUSIC_LOG("cmd %s refused: %s", name, result.error().c_str());
        return false;
    }

    void subscribe()
    {
        if (connection_.connected()) return;
        connection_ = PlaybackHelper::subscribe_event(
            PlaybackHelper::EventId::PlayStateChanged,
            [this](const std::string &, const std::string &state) {
                music::BackendState mapped;
                if (state == "Idle") mapped = music::BackendState::Idle;
                else if (state == "Playing") mapped = music::BackendState::Playing;
                else if (state == "Paused") mapped = music::BackendState::Paused;
                else return;
                MUSIC_LOG("evt %s", state.c_str());
                if (on_state_) on_state_(mapped);
            });
    }

    std::function<void(music::BackendState)> on_state_;
    std::function<void(music::BackendCommand, uint64_t)> on_failed_;
    esp_brookesia::service::EventRegistry::SignalConnection connection_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Command> queue_;
    bool quit_ = false;
    std::thread worker_;
};

} // namespace

namespace music {

std::unique_ptr<PlayerBackend> make_audio_backend(std::function<void(BackendState)> on_state,
                                                  std::function<void(BackendCommand, uint64_t)> on_failed)
{
    return std::make_unique<AudioBackend>(std::move(on_state), std::move(on_failed));
}

} // namespace music
