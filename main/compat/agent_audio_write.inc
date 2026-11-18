bool Base::feed_audio_decoder_data(const uint8_t *data, size_t data_size)
{
    if (is_speaking_disabled()) return true;
    if ((get_chat_mode() == ChatMode::HalfDuplex) && is_listening()) return true;

    std::shared_ptr<df::AudioPlaybackOperation> operation;
    uint32_t generation;
    {
        std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);
        if (is_audio_resources_blocked()) return true;
        operation = audio_playback_operation_;
        generation = audio_playback_generation_;
    }
    // Call only while holding the lifecycle lock. A UI request can block audio
    // before Stop obtains the lock; a retired operation must never be reopened.
    auto is_current_locked = [this, &operation, generation]() {
        return generation == audio_playback_generation_ && operation == audio_playback_operation_ &&
               !is_audio_resources_blocked();
    };
    if (!operation || !operation->is_available()) {
        std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);
        if (!is_current_locked() || !operation) return true;
        BROOKESIA_LOGE("Audio playback DataFlow operation is not available");
        return false;
    }

    struct WaitContext {
        std::mutex mutex;
        std::condition_variable cv;
        df::AudioWriteResult result = df::AudioWriteResult::Error;
        bool done = false;
    } wait_context;
    auto release_callback = [&wait_context](df::AudioWriteResult result) {
        std::lock_guard lock(wait_context.mutex);
        wait_context.result = result;
        wait_context.done = true;
        wait_context.cv.notify_one();
    };
    auto result = operation->write_borrowed(
        AGENT_AUDIO_OUTPUT_NAME, std::span<const uint8_t>(data, data_size), std::move(release_callback),
        AGENT_AUDIO_WRITE_TIMEOUT_MS
    );
    if (result != df::AudioWriteResult::Written) {
        std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);
        if (!is_current_locked()) return true;
        BROOKESIA_LOGE("Failed to write audio stream data: %1%", BROOKESIA_DESCRIBE_TO_STR(result));
        return false;
    }

    std::unique_lock lock(wait_context.mutex);
    const bool completed = wait_context.cv.wait_for(
        lock, std::chrono::milliseconds(AGENT_AUDIO_WRITE_COMPLETION_TIMEOUT_MS),
        [&wait_context]() { return wait_context.done; }
    );
    if (!completed) {
        lock.unlock();
        bool recovering = false;
        {
            std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);
            if (is_current_locked()) {
                recovering = true;
                BROOKESIA_LOGW("Timed out waiting for agent audio write completion; resetting decoder stream");
                auto close_result = operation->close_stream(AGENT_AUDIO_OUTPUT_NAME);
                if (!close_result) {
                    BROOKESIA_LOGW("Failed to close agent audio stream after timeout: %1%", close_result.error());
                }
            }
        }
        // Cancellation is not permission to return the borrowed packet early.
        // Stop/close must finish the callback, even when recovery is skipped.
        lock.lock();
        wait_context.cv.wait(lock, [&wait_context]() { return wait_context.done; });
        lock.unlock();
        if (!recovering) return true;
        {
            std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);
            if (!is_current_locked()) return true;
            auto open_result = operation->open_stream(
                AGENT_AUDIO_OUTPUT_NAME, to_dataflow_audio_stream_config(get_audio_config().decoder)
            );
            if (!open_result) {
                BROOKESIA_LOGW("Failed to reopen agent audio stream after timeout: %1%", open_result.error());
            }
        }
        return false;
    }
    const auto complete_result = wait_context.result;
    lock.unlock();
    if (complete_result != df::AudioWriteResult::Written) {
        std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);
        if (!is_current_locked()) return true;
        BROOKESIA_LOGE("Failed to complete audio stream write: %1%", BROOKESIA_DESCRIBE_TO_STR(complete_result));
        return false;
    }
    return true;
}
