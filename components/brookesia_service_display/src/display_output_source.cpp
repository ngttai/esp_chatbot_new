/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boost/format.hpp"
#include "private/display_impl.hpp"

namespace esp_brookesia::service {



std::vector<Display::SourceInfo> Display::get_sources() const
{
    std::lock_guard lock(mutex_);

    std::vector<SourceInfo> sources;
    sources.reserve(sources_.size());
    for (const auto &[_, source] : sources_) {
        sources.push_back(source.info);
    }
    return sources;
}


std::expected<Display::BufferOutputView, std::string> Display::get_buffer_output(std::string_view output_name) const
{
    std::lock_guard lock(mutex_);

    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }

    const auto &output = outputs_.at(output_id.value());
    if (output.info.slot != OutputSlot::Buffer) {
        return std::unexpected((boost::format("Display output '%1%' is not a buffer output") %
                                output.info.name).str());
    }
    if ((output.buffer.buffer.data_ptr == nullptr) || (output.buffer.stride_bytes == 0)) {
        return std::unexpected((boost::format("Display output '%1%' buffer is not available") %
                                output.info.name).str());
    }

    return BufferOutputView{
        .info = output.info,
        .buffer = output.buffer.buffer,
        .stride_bytes = output.buffer.stride_bytes,
    };
}


std::expected<uint32_t, std::string> Display::register_output(BufferOutputConfig config)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (config.name.empty()) {
        return std::unexpected("Display output name cannot be empty");
    }
    const size_t bpp = bytes_per_pixel(config.pixel_format);
    if (!is_buffer_output_valid(config, bpp)) {
        return std::unexpected((boost::format("Invalid buffer display output '%1%'") % config.name).str());
    }

    OutputInfo registered_info;
    uint32_t output_id = 0;
    {
        std::lock_guard lock(mutex_);
        auto duplicate_it = std::find_if(outputs_.begin(), outputs_.end(), [&config](const auto & item) {
            return item.second.info.name == config.name;
        });
        if (duplicate_it != outputs_.end()) {
            return std::unexpected((boost::format("Display output '%1%' is already registered") % config.name).str());
        }

        output_id = next_output_id_++;
        if (next_output_id_ == INVALID_SOURCE_ID) {
            next_output_id_ = 1;
        }

        const size_t stride_bytes = (config.stride_bytes == 0) ?
                                    (static_cast<size_t>(config.width) * bpp) : config.stride_bytes;
        OutputContext output{};
        output.info = OutputInfo{
            .id = output_id,
            .name = std::move(config.name),
            .width = config.width,
            .height = config.height,
            .pixel_format = config.pixel_format,
            .slot = OutputSlot::Buffer,
            .panel_instance = {},
            .group_id = {},
        };
        output.buffer = BufferOutputContext{
            .buffer = config.buffer,
            .stride_bytes = stride_bytes,
        };
        output.draw_mutex = std::make_shared<std::mutex>();
        output.active_source_id = INVALID_SOURCE_ID;
        output.gesture_config = build_default_touch_gesture_config_locked(output);
        output.backlight_brightness = BROOKESIA_SERVICE_DISPLAY_BACKLIGHT_BRIGHTNESS_DEFAULT;
        output.backlight_on = false;
        output.dynamic_output = true;
        registered_info = output.info;
        outputs_.emplace(output_id, std::move(output));
    }

    BROOKESIA_LOGI(
        "Registered buffer display output %1%: %2%x%3%, pixel_format=%4%", registered_info.name,
        registered_info.width, registered_info.height, BROOKESIA_DESCRIBE_ENUM_TO_STR(registered_info.pixel_format)
    );
    emit_output_registered(registered_info);
    return output_id;
}


std::expected<void, std::string> Display::unregister_output(std::string_view output_name)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (output_name.empty()) {
        return std::unexpected("Display output name cannot be empty");
    }

    uint32_t output_id = 0;
    std::shared_ptr<std::mutex> draw_mutex;
    {
        std::lock_guard lock(mutex_);
        auto parsed_output_id = find_output_id_locked(output_name);
        if (!parsed_output_id) {
            return std::unexpected(parsed_output_id.error());
        }
        output_id = parsed_output_id.value();
        const auto &output = outputs_.at(output_id);
        if (!output.dynamic_output) {
            return std::unexpected((boost::format("Display output '%1%' is not dynamic") % output.info.name).str());
        }
        draw_mutex = output.draw_mutex;
    }

    if (!draw_mutex) {
        return std::unexpected("Display output draw mutex is not available");
    }
    stop_touch_gesture_task(output_id);
    std::lock_guard draw_lock(*draw_mutex);

    std::string resolved_output_name;
    std::string active_source_name;
    std::vector<std::string> revoked_source_names;
    std::vector<AsyncFrame> dropped_frames;
    {
        std::lock_guard lock(mutex_);
        auto output_it = outputs_.find(output_id);
        if (output_it == outputs_.end()) {
            return {};
        }
        auto &output = output_it->second;
        if (!output.dynamic_output) {
            return std::unexpected((boost::format("Display output '%1%' is not dynamic") % output.info.name).str());
        }
        resolved_output_name = output.info.name;
        if (output.active_source_id != INVALID_SOURCE_ID) {
            auto source_it = sources_.find(output.active_source_id);
            if (source_it != sources_.end()) {
                active_source_name = source_it->second.info.name;
                revoked_source_names.push_back(active_source_name);
            }
        }
        if (output.pending_frame.has_value()) {
            dropped_frames.push_back(std::move(output.pending_frame.value()));
            output.pending_frame.reset();
        }

        for (auto &[_, source] : sources_) {
            if (source.requested_outputs.erase(resolved_output_name) > 0) {
                if (source.info.name != active_source_name) {
                    revoked_source_names.push_back(source.info.name);
                }
            }
        }
        outputs_.erase(output_it);
        refresh_touch_bound_outputs_locked();
    }

    if (!active_source_name.empty()) {
        emit_active_source_changed(resolved_output_name, "");
    }
    for (const auto &source_name : revoked_source_names) {
        emit_source_state_changed(source_name, resolved_output_name, SourceState::Revoked);
    }
    for (auto &frame : dropped_frames) {
        complete_async_frame(std::move(frame), PresentResult::Error);
    }
    emit_output_unregistered(resolved_output_name);
    return {};
}


std::expected<void, std::string> Display::set_touch_gesture_config(
    uint32_t output_id, TouchGestureConfig config
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    uint32_t resolved_output_id = 0;
    bool should_start = false;
    bool should_stop = false;
    {
        std::lock_guard lock(mutex_);
        if (outputs_.find(output_id) == outputs_.end()) {
            return std::unexpected((boost::format("Display output id %1% is not available") % output_id).str());
        }
        auto &output = outputs_.at(output_id);
        auto resolved_config = resolve_touch_gesture_config_locked(output, config);
        if (!resolved_config) {
            return std::unexpected(resolved_config.error());
        }

        resolved_output_id = output_id;
        should_stop = output.gesture_config.enabled && !resolved_config->enabled;
        should_start = resolved_config->enabled;
        output.gesture_config = resolved_config.value();
        output.gesture_direction_tan_threshold =
            std::tan((static_cast<float>(output.gesture_config.threshold.direction_angle) * PI) / 180.0F);
        reset_touch_gesture_state_locked(output);
    }

    if (should_stop) {
        stop_touch_gesture_task(resolved_output_id);
    }
    if (should_start && is_running()) {
        if (!start_touch_gesture_task(resolved_output_id)) {
            return std::unexpected("Failed to start Display touch gesture task");
        }
    }
    return {};
}


std::expected<Display::TouchGestureConfig, std::string> Display::get_touch_gesture_config(
    uint32_t output_id
) const
{
    std::lock_guard lock(mutex_);

    if (outputs_.find(output_id) == outputs_.end()) {
        return std::unexpected((boost::format("Display output id %1% is not available") % output_id).str());
    }
    return outputs_.at(output_id).gesture_config;
}


std::expected<Display::TouchSnapshot, std::string> Display::get_touch_snapshot(std::string_view output_name) const
{
    std::lock_guard lock(mutex_);

    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }
    const auto touch_id = outputs_.at(output_id.value()).touch_id;
    if (touch_id == INVALID_TOUCH_ID) {
        return std::unexpected((boost::format("Display output '%1%' has no bound touch") %
                                outputs_.at(output_id.value()).info.name).str());
    }
    auto touch_it = touches_.find(touch_id);
    if (touch_it == touches_.end()) {
        return std::unexpected((boost::format("Bound display touch %1% is not available") % touch_id).str());
    }
    // Synthetic injection (if active) overrides the hardware snapshot so callers can simulate input.
    if (touch_it->second.injected_points.has_value()) {
        TouchSnapshot snapshot = touch_it->second.snapshot;
        snapshot.points = *touch_it->second.injected_points;
        // Use the per-touch injection counter (bumped by inject_touch) so every press/release is
        // observed exactly once by the input reader, which de-duplicates by sequence.
        snapshot.sequence = touch_it->second.injected_sequence;
        snapshot.updated_at_ms = get_current_time_ms();
        snapshot.valid = true;
        return snapshot;
    }
    return touch_it->second.snapshot;
}


std::expected<void, std::string> Display::inject_touch(std::string_view output_name, std::vector<TouchPoint> points)
{
    std::lock_guard lock(mutex_);

    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }
    const auto touch_id = outputs_.at(output_id.value()).touch_id;
    if (touch_id == INVALID_TOUCH_ID) {
        return std::unexpected((boost::format("Display output '%1%' has no bound touch") %
                                outputs_.at(output_id.value()).info.name).str());
    }
    auto touch_it = touches_.find(touch_id);
    if (touch_it == touches_.end()) {
        return std::unexpected((boost::format("Bound display touch %1% is not available") % touch_id).str());
    }
    // Advance past the hardware sequence so the override is always seen as a fresh frame.
    touch_it->second.injected_sequence =
        std::max(touch_it->second.injected_sequence, touch_it->second.snapshot.sequence) + 1;
    touch_it->second.injected_points = std::move(points);
    return {};
}


std::expected<void, std::string> Display::inject_touch(std::string_view output_name, int32_t x, int32_t y)
{
    std::vector<TouchPoint> points;
    points.push_back(TouchPoint{
        .x = static_cast<int16_t>(x),
        .y = static_cast<int16_t>(y),
        .pressure = 1,
        .track_id = 0,
    });
    return inject_touch(output_name, std::move(points));
}


std::expected<void, std::string> Display::clear_injected_touch(std::string_view output_name)
{
    std::lock_guard lock(mutex_);

    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }
    const auto touch_id = outputs_.at(output_id.value()).touch_id;
    if (touch_id == INVALID_TOUCH_ID) {
        return std::unexpected((boost::format("Display output '%1%' has no bound touch") %
                                outputs_.at(output_id.value()).info.name).str());
    }
    auto touch_it = touches_.find(touch_id);
    if (touch_it == touches_.end()) {
        return std::unexpected((boost::format("Bound display touch %1% is not available") % touch_id).str());
    }
    touch_it->second.injected_points.reset();
    return {};
}


std::expected<hal::display::TouchIface::DriverSpecific, std::string> Display::get_touch_driver_specific(
    std::string_view output_name
) const
{
    std::shared_ptr<hal::display::TouchIface> touch;
    std::string resolved_output_name;
    {
        std::lock_guard lock(mutex_);
        auto output_id = find_output_id_locked(output_name);
        if (!output_id) {
            return std::unexpected(output_id.error());
        }
        const auto &output = outputs_.at(output_id.value());
        resolved_output_name = output.info.name;
        if (output.touch_id == INVALID_TOUCH_ID) {
            return std::unexpected((boost::format("Display output '%1%' has no bound touch") %
                                    resolved_output_name).str());
        }
        auto touch_it = touches_.find(output.touch_id);
        if (touch_it == touches_.end()) {
            return std::unexpected((boost::format("Bound display touch %1% is not available") %
                                    output.touch_id).str());
        }
        touch = touch_it->second.touch.get();
    }

    if (!touch) {
        return std::unexpected((boost::format("Display output '%1%' touch handle is not available") %
                                resolved_output_name).str());
    }
    hal::display::TouchIface::DriverSpecific specific;
    if (!touch->get_driver_specific(specific)) {
        return std::unexpected((boost::format("Failed to get Display output '%1%' touch driver data") %
                                resolved_output_name).str());
    }
    return specific;
}


esp_brookesia::lib_utils::connection Display::connect_touch_updated(
    std::string_view output_name, const TouchUpdatedSignal::slot_type &slot
)
{
    std::string filtered_output_name(output_name);
    if (filtered_output_name.empty()) {
        std::lock_guard lock(mutex_);
        if (!outputs_.empty()) {
            filtered_output_name = outputs_.begin()->second.info.name;
        }
    }
    return touch_updated_signal_.connect([filtered_output_name, slot](const std::string & updated_output_name,
    const TouchSnapshot & snapshot) {
        if (updated_output_name == filtered_output_name) {
            slot(updated_output_name, snapshot);
        }
    });
}


esp_brookesia::lib_utils::connection Display::connect_touch_gesture(
    std::string_view output_name, const TouchGestureSignal::slot_type &slot
)
{
    std::string filtered_output_name(output_name);
    if (filtered_output_name.empty()) {
        std::lock_guard lock(mutex_);
        if (!outputs_.empty()) {
            filtered_output_name = outputs_.begin()->second.info.name;
        }
    }
    return touch_gesture_signal_.connect(
    [filtered_output_name, slot](const std::string & updated_output_name, const TouchGestureInfo & info) {
        if (updated_output_name == filtered_output_name) {
            slot(updated_output_name, info);
        }
    }
           );
}


esp_brookesia::lib_utils::connection Display::connect_frame_presented(
    std::string_view output_name, const FramePresentedSignal::slot_type &slot
)
{
    std::string filtered_output_name(output_name);
    if (filtered_output_name.empty()) {
        std::lock_guard lock(mutex_);
        if (!outputs_.empty()) {
            filtered_output_name = outputs_.begin()->second.info.name;
        }
    }
    auto filtered_slot = [filtered_output_name, slot](
                             const std::string & presented_output_name, const FrameInfo & frame
    ) {
        if (presented_output_name == filtered_output_name) {
            slot(presented_output_name, frame);
        }
    };
    return frame_presented_signal_.connect(filtered_slot);
}


std::expected<uint32_t, std::string> Display::register_source(SourceInfo source)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!is_valid_source_name(source.name)) {
        return std::unexpected("Display source name cannot be empty");
    }

    uint32_t assigned_source_id = INVALID_SOURCE_ID;
    {
        std::lock_guard lock(mutex_);
        auto duplicate_it = std::find_if(sources_.begin(), sources_.end(), [&source](const auto & item) {
            return item.second.info.name == source.name;
        });
        if (duplicate_it != sources_.end()) {
            return std::unexpected((boost::format("Display source '%1%' is already registered") % source.name).str());
        }

        assigned_source_id = next_source_id_++;
        if (next_source_id_ == INVALID_SOURCE_ID) {
            next_source_id_ = 1;
        }
        source.id = assigned_source_id;
        sources_.emplace(assigned_source_id, SourceContext{
            .info = source,
            .requested_outputs = {},
        });
    }

    emit_source_state_changed(source.name, "", SourceState::Registered);
    return assigned_source_id;
}


std::expected<void, std::string> Display::unregister_source(uint32_t source_id)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::string source_name;
    std::vector<std::string> cleared_outputs;
    std::vector<AsyncFrame> dropped_frames;
    {
        std::lock_guard lock(mutex_);
        auto source_it = sources_.find(source_id);
        if (source_it == sources_.end()) {
            return std::unexpected((boost::format("Display source %1% is not registered") % source_id).str());
        }

        source_name = source_it->second.info.name;
        sources_.erase(source_it);
        for (auto &[_, output] : outputs_) {
            if (output.active_source_id == source_id) {
                output.active_source_id = INVALID_SOURCE_ID;
                cleared_outputs.push_back(output.info.name);
            }
            if (output.pending_frame.has_value() && (output.pending_frame->source_id == source_id)) {
                dropped_frames.push_back(std::move(output.pending_frame.value()));
                output.pending_frame.reset();
            }
        }
    }

    for (const auto &output_name : cleared_outputs) {
        emit_active_source_changed(output_name, "");
        emit_source_state_changed(source_name, output_name, SourceState::Revoked);
    }
    emit_source_state_changed(source_name, "", SourceState::Revoked);
    for (auto &frame : dropped_frames) {
        complete_async_frame(std::move(frame), PresentResult::DroppedNotActive);
    }
    return {};
}


std::expected<void, std::string> Display::unregister_source(std::string_view source_name)
{
    uint32_t source_id = INVALID_SOURCE_ID;
    {
        std::lock_guard lock(mutex_);
        auto parsed_source_id = find_source_id_locked(source_name);
        if (!parsed_source_id) {
            return std::unexpected(parsed_source_id.error());
        }
        source_id = parsed_source_id.value();
    }
    return unregister_source(source_id);
}


std::expected<void, std::string> Display::request_output(uint32_t source_id, std::string_view output_name)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::string source_name;
    std::string resolved_output_name;
    {
        std::lock_guard lock(mutex_);
        auto validation = validate_source_output_locked(source_id, output_name);
        if (!validation) {
            return std::unexpected(validation.error());
        }
        auto &source = sources_.at(source_id);
        auto output_id = find_output_id_locked(output_name).value();
        resolved_output_name = outputs_.at(output_id).info.name;
        source.requested_outputs.insert(resolved_output_name);
        source_name = source.info.name;
    }

    emit_source_state_changed(source_name, resolved_output_name, SourceState::Requested);
    return {};
}


std::expected<void, std::string> Display::request_output(
    std::string_view source_name, std::string_view output_name
)
{
    uint32_t source_id = INVALID_SOURCE_ID;
    {
        std::lock_guard lock(mutex_);
        auto parsed_source_id = find_source_id_locked(source_name);
        if (!parsed_source_id) {
            return std::unexpected(parsed_source_id.error());
        }
        source_id = parsed_source_id.value();
    }
    return request_output(source_id, output_name);
}


std::expected<void, std::string> Display::release_output(uint32_t source_id, std::string_view output_name)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::string source_name;
    std::string resolved_output_name;
    bool cleared_active = false;
    std::vector<AsyncFrame> dropped_frames;
    {
        std::lock_guard lock(mutex_);
        auto validation = validate_source_output_locked(source_id, output_name);
        if (!validation) {
            return std::unexpected(validation.error());
        }

        auto output_id = find_output_id_locked(output_name).value();
        auto &source = sources_.at(source_id);
        auto &output = outputs_.at(output_id);
        source.requested_outputs.erase(output.info.name);
        source_name = source.info.name;
        resolved_output_name = output.info.name;
        if (output.active_source_id == source_id) {
            output.active_source_id = INVALID_SOURCE_ID;
            cleared_active = true;
        }
        if (output.pending_frame.has_value() && (output.pending_frame->source_id == source_id)) {
            dropped_frames.push_back(std::move(output.pending_frame.value()));
            output.pending_frame.reset();
        }
    }

    if (cleared_active) {
        emit_active_source_changed(resolved_output_name, "");
    }
    emit_source_state_changed(source_name, resolved_output_name, SourceState::Revoked);
    for (auto &frame : dropped_frames) {
        complete_async_frame(std::move(frame), PresentResult::DroppedNotActive);
    }
    return {};
}


std::expected<void, std::string> Display::release_output(
    std::string_view source_name, std::string_view output_name
)
{
    uint32_t source_id = INVALID_SOURCE_ID;
    {
        std::lock_guard lock(mutex_);
        auto parsed_source_id = find_source_id_locked(source_name);
        if (!parsed_source_id) {
            return std::unexpected(parsed_source_id.error());
        }
        source_id = parsed_source_id.value();
    }
    return release_output(source_id, output_name);
}


std::expected<void, std::string> Display::set_active_source(
    std::string_view output_name, std::string_view source_name
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::string resolved_output_name;
    std::string previous_source_name;
    std::string next_source_name(source_name);
    uint32_t previous_source_id = INVALID_SOURCE_ID;
    uint32_t next_source_id = INVALID_SOURCE_ID;
    std::vector<AsyncFrame> dropped_frames;

    {
        std::lock_guard lock(mutex_);
        auto output_id = find_output_id_locked(output_name);
        if (!output_id) {
            return std::unexpected(output_id.error());
        }

        auto &output = outputs_.at(output_id.value());
        resolved_output_name = output.info.name;
        previous_source_id = output.active_source_id;
        if (previous_source_id != INVALID_SOURCE_ID) {
            previous_source_name = sources_.at(previous_source_id).info.name;
        }

        if (!source_name.empty()) {
            auto source_id = find_source_id_locked(source_name);
            if (!source_id) {
                return std::unexpected(source_id.error());
            }
            next_source_id = source_id.value();
            auto &source = sources_.at(next_source_id);
            next_source_name = source.info.name;
            source.requested_outputs.insert(resolved_output_name);
        } else {
            next_source_name.clear();
        }

        output.active_source_id = next_source_id;
        if ((previous_source_id != INVALID_SOURCE_ID) && (previous_source_id != next_source_id) &&
                output.pending_frame.has_value() && (output.pending_frame->source_id == previous_source_id)) {
            dropped_frames.push_back(std::move(output.pending_frame.value()));
            output.pending_frame.reset();
        }
    }

    if ((previous_source_id != INVALID_SOURCE_ID) && (previous_source_id != next_source_id)) {
        emit_source_state_changed(previous_source_name, resolved_output_name, SourceState::Dummy);
    }
    if (next_source_id != INVALID_SOURCE_ID) {
        emit_source_state_changed(next_source_name, resolved_output_name, SourceState::Granted);
    }
    emit_active_source_changed(resolved_output_name, next_source_name);
    for (auto &frame : dropped_frames) {
        complete_async_frame(std::move(frame), PresentResult::DroppedNotActive);
    }
    return {};
}


std::expected<std::string, std::string> Display::get_active_source(std::string_view output_name) const
{
    std::lock_guard lock(mutex_);

    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }

    const auto &output = outputs_.at(output_id.value());
    if (output.active_source_id == INVALID_SOURCE_ID) {
        return std::string();
    }

    auto source_it = sources_.find(output.active_source_id);
    if (source_it == sources_.end()) {
        return std::unexpected((boost::format("Active display source %1% is not registered") %
                                output.active_source_id).str());
    }
    return source_it->second.info.name;
}


std::expected<void, std::string> Display::set_active_source_role(
    std::string_view output_name, std::string_view role
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (role.empty()) {
        return std::unexpected("Display source role cannot be empty");
    }

    std::string source_name;
    {
        std::lock_guard lock(mutex_);
        auto source_it = std::find_if(sources_.begin(), sources_.end(), [role](const auto & item) {
            return item.second.info.role == role;
        });
        if (source_it == sources_.end()) {
            return std::unexpected((boost::format("Display source role '%1%' is not registered") %
                                    std::string(role)).str());
        }
        source_name = source_it->second.info.name;
    }

    return set_active_source(output_name, source_name);
}


std::expected<std::string, std::string> Display::get_active_role(std::string_view output_name) const
{
    std::lock_guard lock(mutex_);

    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }

    const auto &output = outputs_.at(output_id.value());
    if (output.active_source_id == INVALID_SOURCE_ID) {
        return std::string();
    }

    auto source_it = sources_.find(output.active_source_id);
    if (source_it == sources_.end()) {
        return std::unexpected((boost::format("Active display source %1% is not registered") %
                                output.active_source_id).str());
    }
    return source_it->second.info.role;
}


std::vector<std::string> Display::get_source_roles() const
{
    std::lock_guard lock(mutex_);

    std::vector<std::string> roles;
    for (const auto &[_, source] : sources_) {
        const auto &role = source.info.role;
        if (role.empty()) {
            continue;
        }
        if (std::find(roles.begin(), roles.end(), role) == roles.end()) {
            roles.push_back(role);
        }
    }
    return roles;
}


Display::PresentResult Display::present_frame_sync(
    uint32_t source_id, std::string_view output_name, const FrameInfo &frame, const RawBuffer &data,
    uint32_t timeout_ms
)
{
    OutputDrawTarget target;
    std::shared_ptr<std::mutex> draw_mutex;
    uint32_t output_id = 0;

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
        if (output.active_source_id != source_id) {
            return PresentResult::DroppedNotActive;
        }
        if ((data.data_ptr == nullptr) || !is_frame_valid_for_output(frame, output, data.data_size)) {
            return PresentResult::DroppedInvalidFrame;
        }

        draw_mutex = output.draw_mutex;
    }

    if (!draw_mutex) {
        return PresentResult::Error;
    }

    PresentResult result = PresentResult::Error;
    {
        std::lock_guard draw_lock(*draw_mutex);
        {
            std::lock_guard lock(mutex_);
            auto output_it = outputs_.find(output_id);
            if ((output_it == outputs_.end()) || (output_it->second.active_source_id != source_id)) {
                return PresentResult::DroppedNotActive;
            }
            const auto &output = output_it->second;
            target = OutputDrawTarget{
                .info = output.info,
                .panel = output.panel.get(),
                .buffer = output.buffer,
            };
        }
        result = present_frame_to_output(target, frame, data, timeout_ms);
    }

    if (result == PresentResult::Presented) {
        emit_frame_presented(target.info.name, frame);
    }
    return result;
}
}
