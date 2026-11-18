# Keep pinned dependencies untouched; compile narrowly reviewed generated sources.
function(audio_ownership_replace_source component relative old new)
    idf_component_get_property(component_dir ${component} COMPONENT_DIR)
    idf_component_get_property(component_lib ${component} COMPONENT_LIB)
    set(input "${component_dir}/${relative}")
    get_filename_component(filename "${relative}" NAME)
    set(output "${CMAKE_BINARY_DIR}/audio_ownership/${component}/${filename}")
    set(patch_key "AUDIO_OWNERSHIP_${component}_${filename}")
    get_property(already_patched GLOBAL PROPERTY "${patch_key}" SET)
    if(already_patched)
        file(READ "${output}" source)
    else()
        file(READ "${input}" source)
    endif()
    string(FIND "${source}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Audio ownership patch requires review: ${input}")
    endif()
    string(REPLACE "${old}" "${new}" source "${source}")
    file(CONFIGURE OUTPUT "${output}" CONTENT "${source}" @ONLY)
    set_property(GLOBAL PROPERTY "${patch_key}" TRUE)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    get_target_property(sources ${component_lib} SOURCES)
    list(FILTER sources EXCLUDE REGEX "(^|/)${filename}$")
    set_property(TARGET ${component_lib} PROPERTY SOURCES "${sources}")
    get_filename_component(source_dir "${input}" DIRECTORY)
    target_include_directories(${component_lib} PRIVATE "${source_dir}")
    target_sources(${component_lib} PRIVATE "${output}")
endfunction()

# Complete borrowed writes before transport shutdown joins the receiver.
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
    "    on_shutdown();\n    stop_audio_encoder();\n    stop_audio_decoder();"
    "    stop_audio_decoder();\n    on_shutdown();\n    stop_audio_encoder();")

# Bound HAL feed waits so closing a decoder cannot wait forever for its mutex.
# This patch changes only the compressed feeder path.
audio_ownership_replace_source(jason-mao__av_processor "src/audio_processor.c"
    "    } else {\n        int ret = esp_gmf_db_acquire_write(fd->fifo, &blk, data_size, portMAX_DELAY);"
    "    } else {\n        int ret = esp_gmf_db_acquire_write(fd->fifo, &blk, data_size, pdMS_TO_TICKS(100));")

# Recorder reads must finish before close destroys the FIFO. A dedicated mutex
# avoids holding the encoder-state lock needed by the AFE event callback.
audio_ownership_replace_source(espressif__brookesia_hal_adaptor "src/audio/processor_impl.cpp"
    "#include <memory>" "#include <memory>\n#include <mutex>")
audio_ownership_replace_source(espressif__brookesia_hal_adaptor "src/audio/processor_impl.cpp"
    "namespace esp_brookesia::hal {"
    "namespace esp_brookesia::hal {\nnamespace { std::mutex recorder_lifetime_mutex; }")
audio_ownership_replace_source(espressif__brookesia_hal_adaptor "src/audio/processor_impl.cpp"
    "int AudioProcessorCore::read_encoded_data(uint8_t *data, size_t size)\n{"
    "int AudioProcessorCore::read_encoded_data(uint8_t *data, size_t size)\n{\n    std::lock_guard lifetime_lock(recorder_lifetime_mutex);")
audio_ownership_replace_source(espressif__brookesia_hal_adaptor "src/audio/processor_impl.cpp"
    "void AudioProcessorCore::stop_encoder()\n{"
    "void AudioProcessorCore::stop_encoder()\n{\n    std::lock_guard lifetime_lock(recorder_lifetime_mutex);")
audio_ownership_replace_source(jason-mao__av_processor "src/audio_processor.c"
    "int ret = esp_gmf_fifo_acquire_read(rec->fifo, &blk, data_size, portMAX_DELAY);"
    "int ret = esp_gmf_fifo_acquire_read(rec->fifo, &blk, data_size, pdMS_TO_TICKS(100));\n    if (ret == ESP_GMF_IO_TIMEOUT) return 0;")

# Expose an audio-only hook to owned agents without changing pinned packages.
# The transport lifecycle and Suspend/Resume implementations remain unchanged.
idf_component_get_property(agent_dir espressif__brookesia_agent_manager COMPONENT_DIR)
idf_component_get_property(agent_lib espressif__brookesia_agent_manager COMPONENT_LIB)
set(base_header "${agent_dir}/include/brookesia/agent_manager/base.hpp")
file(READ "${base_header}" base_source)
string(REPLACE "#include <memory>" "#include <memory>\n#include <mutex>\n#include <cstdint>" base_source "${base_source}")
set(playback_field "    std::shared_ptr<service::dataflow::AudioPlaybackOperation> audio_playback_operation_;")
string(FIND "${base_source}" "${playback_field}" playback_field_found)
if(playback_field_found EQUAL -1)
    message(FATAL_ERROR "Decoder lifecycle fields require review: ${base_header}")
endif()
string(REPLACE "${playback_field}"
    "    std::mutex audio_playback_lifecycle_mutex_;\n    uint32_t audio_playback_generation_ = 0;\n${playback_field}"
    base_source "${base_source}")
set(base_anchor "    bool is_general_action_running(GeneralAction action);")
string(FIND "${base_source}" "${base_anchor}" base_found)
if(base_found EQUAL -1)
    message(FATAL_ERROR "Audio ownership API requires review: ${base_header}")
endif()
string(REPLACE "${base_anchor}" [=[
    // Called only on the agent state strand. This does not stop the transport.
    bool set_audio_resources_enabled(bool enabled)
    {
        if (!enabled) {
            stop_audio_decoder();
            stop_audio_encoder();
            set_manual_listening(false);
            set_listening(false);
            set_speaking(false);
            reset_interrupted_speaking();
            return true;
        }
        if (!start_audio_decoder()) return false;
        if (!start_audio_encoder()) {
            stop_audio_decoder();
            return false;
        }
        return true;
    }
    bool are_audio_resources_enabled() const { return is_encoder_started() && is_decoder_started(); }
    bool can_change_audio_resources() const { return !is_suspended(); }
    bool is_session_asleep() { return is_general_event_ready(GeneralEvent::Slept); }
    virtual bool is_audio_resources_blocked() const { return false; }
    bool is_general_action_running(GeneralAction action);
]=] base_source "${base_source}")
set(agent_overlay "${CMAKE_BINARY_DIR}/audio_ownership/include")
file(CONFIGURE OUTPUT "${agent_overlay}/brookesia/agent_manager/base.hpp" CONTENT "${base_source}" @ONLY)
target_include_directories(${agent_lib} BEFORE PUBLIC "${agent_overlay}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${base_header}")

audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
    "        switch (afe_event) {"
    "        if (is_audio_resources_blocked()) return;\n        switch (afe_event) {")
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
    "bool Base::do_start()\n{\n    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();"
    "bool Base::do_start()\n{\n    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();\n    if (is_audio_resources_blocked()) return on_startup();")

# Queue rearm to avoid taking mutex_ recursively from restart_timer. Both timer
# access and the canceled-state check then use the same lock as cancellation.
audio_ownership_replace_source(espressif__brookesia_lib_utils "src/task_scheduler.cpp"
    "    handle->timer->expires_after(std::chrono::milliseconds(handle->interval_ms));\n    handle->timer->async_wait([this, handle, task](const boost::system::error_code & ec) {"
    "    boost::asio::post(*io_context_, [this, handle, task]() {\n    boost::lock_guard<boost::mutex> timer_lock(mutex_);\n    if (handle->state != TaskState::Running || !handle->timer) return;\n    handle->timer->expires_after(std::chrono::milliseconds(handle->interval_ms));\n    handle->timer->async_wait([this, handle, task](const boost::system::error_code & ec) {")
audio_ownership_replace_source(espressif__brookesia_lib_utils "src/task_scheduler.cpp"
    "    });\n}\n\nvoid TaskScheduler::Impl::cancel_internal"
    "    });\n    });\n}\n\nvoid TaskScheduler::Impl::cancel_internal")

# Pending WakeUp can have no queued actions yet. Never read front() on an empty queue.
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/state_machine.cpp"
    "        auto front_action = general_action_queue_.front();\n        if ((action == front_action) || (action == agent_->get_running_general_action())) {"
    "        if ((!general_action_queue_.empty() && action == general_action_queue_.front()) ||\n                action == agent_->get_running_general_action()) {")

# Recorder shutdown uses a bounded poll. Keep ordinary GMF timeout errors,
# but do not emit an error for this intentional recorder timeout.
idf_component_get_property(av_lib jason-mao__av_processor COMPONENT_LIB)
target_include_directories(${av_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
audio_ownership_replace_source(jason-mao__av_processor "src/audio_processor.c"
    "#include \"esp_gmf_fifo.h\""
    "#include \"esp_gmf_fifo.h\"\n#include \"audio_fifo_poll.h\"")
audio_ownership_replace_source(jason-mao__av_processor "src/audio_processor.c"
    "esp_gmf_fifo_acquire_read(rec->fifo, &blk, data_size, pdMS_TO_TICKS(100))"
    "audio_ownership_fifo_acquire_read_quiet(rec->fifo, &blk, data_size, pdMS_TO_TICKS(100))")

audio_ownership_replace_source(espressif__gmf_core "data_bus/esp_gmf_fifo.c"
[=[esp_gmf_err_io_t esp_gmf_fifo_acquire_read(esp_gmf_fifo_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)]=]
[=[static esp_gmf_err_io_t audio_ownership_fifo_acquire_read_impl(esp_gmf_fifo_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks, bool quiet_timeout)]=])

audio_ownership_replace_source(espressif__gmf_core "data_bus/esp_gmf_fifo.c"
[=[            ESP_LOGE(TAG, "FIFO acquire read timeout");]=]
[=[            if (!quiet_timeout) ESP_LOGE(TAG, "FIFO acquire read timeout");]=])

audio_ownership_replace_source(espressif__gmf_core "data_bus/esp_gmf_fifo.c"
[=[esp_gmf_err_io_t esp_gmf_fifo_release_read(]=]
[=[esp_gmf_err_io_t esp_gmf_fifo_acquire_read(esp_gmf_fifo_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    return audio_ownership_fifo_acquire_read_impl(handle, blk, wanted_size, block_ticks, false);
}

esp_gmf_err_io_t audio_ownership_fifo_acquire_read_quiet(esp_gmf_fifo_handle_t handle, esp_gmf_data_bus_block_t *blk, uint32_t wanted_size, int block_ticks)
{
    return audio_ownership_fifo_acquire_read_impl(handle, blk, wanted_size, block_ticks, true);
}

esp_gmf_err_io_t esp_gmf_fifo_release_read(]=])

# Replace the pinned write handler with a lifecycle-fenced implementation. Keep
# both boundaries explicit so dependency changes require a source review.
file(READ "${agent_dir}/src/base.cpp" pinned_base_source)
string(FIND "${pinned_base_source}" "bool Base::feed_audio_decoder_data(" write_start)
string(FIND "${pinned_base_source}" "bool Base::on_start()" write_end)
if(write_start LESS 0 OR write_end LESS write_start)
    message(FATAL_ERROR "Agent write-handler boundaries require review")
endif()
math(EXPR write_length "${write_end} - ${write_start}")
string(SUBSTRING "${pinned_base_source}" ${write_start} ${write_length} pinned_write_handler)
file(READ "${CMAKE_CURRENT_LIST_DIR}/agent_audio_write.inc" fenced_write_handler)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/agent_audio_write.inc")
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
    "${pinned_write_handler}" "${fenced_write_handler}\n")
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
    "bool Base::start_audio_decoder()\n{"
    "bool Base::start_audio_decoder()\n{\n    std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);")
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
    "void Base::stop_audio_decoder()\n{"
    "void Base::stop_audio_decoder()\n{\n    std::lock_guard lifecycle_lock(audio_playback_lifecycle_mutex_);\n    ++audio_playback_generation_;")
audio_ownership_replace_source(espressif__brookesia_agent_manager "src/base.cpp"
[=[    if (audio_playback_operation_) {
        audio_playback_operation_->close();
        audio_playback_operation_.reset();
    }
    if (audio_capture_operation_) {]=]
[=[    stop_audio_decoder();
    if (audio_capture_operation_) {]=])

# Propagate actual render failures to the feeder pipeline.
audio_ownership_replace_source(jason-mao__av_processor "src/audio_processor.c"
[=[        esp_audio_render_stream_write(audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], blk->buf, blk->valid_size);]=]
[=[        esp_audio_render_err_t ret = esp_audio_render_stream_write(
            audio_manager.stream[AUDIO_RENDER_STREAM_FEEDER], blk->buf, blk->valid_size);
        if (ret != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Feeder render output failed: %d", ret);
            return ESP_FAIL;
        }]=])
