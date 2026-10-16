/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boost/format.hpp"
#include "private/display_impl.hpp"
#include "brookesia/lib_utils/function_guard.hpp"

namespace esp_brookesia::service {

void Display::end_sync_frame(uint32_t output_id)
{
    std::lock_guard lock(mutex_);
    auto it = outputs_.find(output_id);
    if (it != outputs_.end() && it->second.outstanding_sync_frames > 0)
        --it->second.outstanding_sync_frames;
}

std::expected<void, std::string> Display::set_frame_admission(uint32_t output_id, bool enabled)
{
    std::optional<AsyncFrame> dropped;
    {
        std::lock_guard lock(mutex_);
        auto it = outputs_.find(output_id);
        if (it == outputs_.end()) return std::unexpected("Display output is not available");
        auto &output = it->second;
        if (enabled && (output.panel_power_busy || output.panel_needs_wake))
            return std::unexpected("Panel power command is busy or wake is incomplete");
        if (output.frame_admission_enabled != enabled) ++output.admission_generation;
        output.frame_admission_enabled = enabled;
        if (!enabled && output.pending_frame) {
            dropped = std::move(output.pending_frame);
            output.pending_frame.reset();
        }
    }
    // Completion may re-enter Display; never invoke it while holding the service mutex.
    if (dropped) {
        complete_async_frame(std::move(*dropped), PresentResult::DroppedNotActive);
    }
    return {};
}

std::expected<bool, std::string> Display::is_frame_output_drained(uint32_t output_id) const
{
    std::shared_ptr<std::mutex> draw_mutex;
    {
        std::lock_guard lock(mutex_);
        auto it = outputs_.find(output_id);
        if (it == outputs_.end()) return std::unexpected("Display output is not available");
        const auto &output = it->second;
        if (output.frame_admission_enabled || output.pending_frame || output.inflight_frame_id ||
            output.outstanding_async_frames || output.outstanding_sync_frames ||
            output.panel_completion_uncertain || output.panel_power_busy) return false;
        draw_mutex = output.draw_mutex;
    }
    if (!draw_mutex) return std::unexpected("Display draw mutex is not available");
    std::unique_lock draw_lock(*draw_mutex, std::try_to_lock);
    if (!draw_lock.owns_lock()) return false;
    std::lock_guard lock(mutex_);
    auto it = outputs_.find(output_id);
    if (it == outputs_.end() || it->second.draw_mutex != draw_mutex)
        return std::unexpected("Display output changed while checking drain");
    const auto &output = it->second;
    return !output.frame_admission_enabled && !output.pending_frame &&
           !output.inflight_frame_id && !output.outstanding_async_frames && !output.outstanding_sync_frames &&
           !output.panel_completion_uncertain && !output.panel_power_busy;
}


Display::PresentResult Display::present_buffer_frame_sync(
    uint32_t source_id, std::string_view output_name, const FrameInfo &frame, BufferOutputWriter writer
)
{
    if (writer == nullptr) {
        return PresentResult::Error;
    }

    std::shared_ptr<std::mutex> draw_mutex;
    uint32_t output_id = 0;
    uint64_t admission_generation = 0;

    {
        std::lock_guard lock(mutex_);
        if (sources_.find(source_id) == sources_.end()) {
            return PresentResult::Error;
        }
        auto parsed_output_id = find_output_id_locked(output_name);
        if (!parsed_output_id) {
            return PresentResult::Error;
        }
        output_id = parsed_output_id.value();
        auto &output = outputs_.at(output_id);
        if (output.info.slot != OutputSlot::Buffer) {
            return PresentResult::Error;
        }
        if (!output.frame_admission_enabled || output.active_source_id != source_id) {
            return PresentResult::DroppedNotActive;
        }
        const size_t bpp = bytes_per_pixel(frame.pixel_format);
        const size_t expected_size = static_cast<size_t>(frame.width) * frame.height * bpp;
        if ((bpp == 0) || !is_frame_valid_for_output(frame, output, expected_size)) {
            return PresentResult::DroppedInvalidFrame;
        }

        draw_mutex = output.draw_mutex;
        admission_generation = output.admission_generation;
        ++output.outstanding_sync_frames;
    }
    lib_utils::FunctionGuard frame_guard([this, output_id] { end_sync_frame(output_id); });

    if (!draw_mutex) {
        return PresentResult::Error;
    }

    std::string presented_output_name;
    PresentResult result = PresentResult::Error;
    {
        std::lock_guard draw_lock(*draw_mutex);
        BufferOutputView view;
        {
            std::lock_guard lock(mutex_);
            auto output_it = outputs_.find(output_id);
            if ((output_it == outputs_.end()) || !output_it->second.frame_admission_enabled ||
                output_it->second.admission_generation != admission_generation ||
                (output_it->second.active_source_id != source_id)) {
                return PresentResult::DroppedNotActive;
            }
            auto &output = output_it->second;
            if (output.info.slot != OutputSlot::Buffer) {
                return PresentResult::Error;
            }
            view = BufferOutputView{
                .info = output.info,
                .buffer = output.buffer.buffer,
                .stride_bytes = output.buffer.stride_bytes,
            };
        }
        result = writer(view) ? PresentResult::Presented : PresentResult::Error;
        presented_output_name = view.info.name;
    }

    if (result == PresentResult::Presented) {
        emit_frame_presented(presented_output_name, frame, source_id);
    }
    return result;
}


Display::AsyncSubmitResult Display::present_frame_async(
    uint32_t source_id, std::string_view output_name, const FrameInfo &frame, const RawBuffer &data,
    CompletionCallback on_complete, uint32_t timeout_ms
)
{
    if (on_complete == nullptr) {
        return {
            .frame_id = 0,
            .state = PresentSubmitState::Error,
        };
    }

    AsyncFrame dropped_frame;
    AsyncFrame failed_frame;
    bool has_dropped_frame = false;
    bool has_failed_frame = false;
    uint32_t frame_id = 0;
    PresentSubmitState submit_state = PresentSubmitState::Queued;
    {
        std::lock_guard lock(mutex_);
        if (sources_.find(source_id) == sources_.end()) {
            return {
                .frame_id = 0,
                .state = PresentSubmitState::Error,
            };
        }
        auto parsed_output_id = find_output_id_locked(output_name);
        if (!parsed_output_id) {
            return {
                .frame_id = 0,
                .state = PresentSubmitState::Error,
            };
        }

        auto &output = outputs_.at(parsed_output_id.value());
        if (!output.frame_admission_enabled || output.active_source_id != source_id) {
            return {
                .frame_id = 0,
                .state = PresentSubmitState::DroppedNotActive,
            };
        }
        if ((data.data_ptr == nullptr) || !is_frame_valid_for_output(frame, output, data.data_size)) {
            return {
                .frame_id = 0,
                .state = PresentSubmitState::DroppedInvalidFrame,
            };
        }

        frame_id = allocate_frame_id_locked();
        if (output.pending_frame.has_value()) {
            dropped_frame = std::move(output.pending_frame.value());
            has_dropped_frame = true;
        }
        output.pending_frame = AsyncFrame{
            .frame_id = frame_id,
            .source_id = source_id,
            .output_id = parsed_output_id.value(),
            .admission_generation = output.admission_generation,
            .output_name = output.info.name,
            .frame = frame,
            .data = data,
            .timeout_ms = timeout_ms,
            .on_complete = std::move(on_complete),
        };
        ++output.outstanding_async_frames;

        if (!schedule_render_output_locked(parsed_output_id.value())) {
            failed_frame = std::move(output.pending_frame.value());
            output.pending_frame.reset();
            frame_id = 0;
            submit_state = PresentSubmitState::Error;
            has_failed_frame = true;
        }
    }

    if (has_dropped_frame) {
        complete_async_frame(std::move(dropped_frame), PresentResult::DroppedQueueFull);
    }
    if (has_failed_frame) {
        complete_async_frame(std::move(failed_frame), PresentResult::Error);
    }

    return {
        .frame_id = frame_id,
        .state = submit_state,
    };
}


std::expected<boost::json::array, std::string> Display::function_get_outputs()
{
    return to_json_array(get_outputs());
}


std::expected<boost::json::array, std::string> Display::function_get_sources()
{
    return to_json_array(get_sources());
}


std::expected<double, std::string> Display::function_register_source(const boost::json::object &source_json)
{
    SourceInfo source;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(boost::json::value(source_json), source)) {
        return std::unexpected("Failed to parse Display source info");
    }
    auto result = register_source(std::move(source));
    if (!result) {
        return std::unexpected(result.error());
    }
    return static_cast<double>(result.value());
}


std::expected<void, std::string> Display::function_unregister_source(const std::string &source_name)
{
    return unregister_source(source_name);
}


std::expected<void, std::string> Display::function_request_output(
    const std::string &source_name, const std::string &output_name
)
{
    return request_output(source_name, output_name);
}


std::expected<void, std::string> Display::function_release_output(
    const std::string &source_name, const std::string &output_name
)
{
    return release_output(source_name, output_name);
}


std::expected<void, std::string> Display::function_set_active_source(
    const std::string &output_name, const std::string &source_name
)
{
    return set_active_source(output_name, source_name);
}


std::expected<std::string, std::string> Display::function_get_active_source(const std::string &output_name)
{
    return get_active_source(output_name);
}


std::expected<void, std::string> Display::function_set_active_source_role(
    const std::string &output_name, const std::string &role
)
{
    return set_active_source_role(output_name, role);
}


std::expected<std::string, std::string> Display::function_get_active_role(const std::string &output_name)
{
    return get_active_role(output_name);
}


std::expected<boost::json::array, std::string> Display::function_get_source_roles()
{
    return to_json_array(get_source_roles());
}


std::expected<void, std::string> Display::function_set_touch_gesture_config(
    double output_id, const boost::json::object &config_json
)
{
    auto parsed_output_id = validate_output_id_param(output_id);
    if (!parsed_output_id) {
        return std::unexpected(parsed_output_id.error());
    }
    TouchGestureConfig config;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(boost::json::value(config_json), config)) {
        return std::unexpected("Failed to parse Display touch gesture config");
    }
    return set_touch_gesture_config(parsed_output_id.value(), config);
}


std::expected<boost::json::object, std::string> Display::function_get_touch_gesture_config(
    double output_id
)
{
    auto parsed_output_id = validate_output_id_param(output_id);
    if (!parsed_output_id) {
        return std::unexpected(parsed_output_id.error());
    }
    auto result = get_touch_gesture_config(parsed_output_id.value());
    if (!result) {
        return std::unexpected(result.error());
    }
    return BROOKESIA_DESCRIBE_TO_JSON(result.value()).as_object();
}


std::expected<uint32_t, std::string> Display::find_output_id_locked(std::string_view output_name) const
{
    if (output_name.empty()) {
        if (outputs_.empty()) {
            return std::unexpected("No Display output is available");
        }
        return outputs_.begin()->first;
    }
    auto output_it = std::find_if(outputs_.begin(), outputs_.end(), [output_name](const auto & item) {
        return item.second.info.name == output_name;
    });
    if (output_it == outputs_.end()) {
        return std::unexpected((boost::format("Display output '%1%' is not available") %
                                std::string(output_name)).str());
    }
    return output_it->first;
}
}
