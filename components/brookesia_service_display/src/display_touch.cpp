/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "boost/format.hpp"
#include "private/display_impl.hpp"

namespace esp_brookesia::service {



std::expected<uint32_t, std::string> Display::validate_output_id_param(double output_id) const
{
    if (!std::isfinite(output_id)) {
        return std::unexpected("Display output id must be finite");
    }
    if (output_id < 0) {
        return std::unexpected("Display output id must be non-negative");
    }
    constexpr double output_id_max = static_cast<double>(std::numeric_limits<uint32_t>::max());
    if (output_id > output_id_max) {
        return std::unexpected("Display output id is out of range");
    }
    const auto rounded = std::round(output_id);
    if (rounded != output_id) {
        return std::unexpected("Display output id must be an integer");
    }
    return static_cast<uint32_t>(rounded);
}


std::expected<uint32_t, std::string> Display::find_source_id_locked(std::string_view source_name) const
{
    if (source_name.empty()) {
        return std::unexpected("Display source name cannot be empty");
    }
    auto source_it = std::find_if(sources_.begin(), sources_.end(), [source_name](const auto & item) {
        return item.second.info.name == source_name;
    });
    if (source_it == sources_.end()) {
        return std::unexpected((boost::format("Display source '%1%' is not registered") %
                                std::string(source_name)).str());
    }
    return source_it->first;
}


std::expected<uint32_t, std::string> Display::find_touch_id_locked(std::string_view touch_name) const
{
    if (touch_name.empty()) {
        return std::unexpected("Display touch name cannot be empty");
    }
    auto touch_it = std::find_if(touches_.begin(), touches_.end(), [touch_name](const auto & item) {
        return item.second.info.name == touch_name;
    });
    if (touch_it == touches_.end()) {
        return std::unexpected((boost::format("Display touch '%1%' is not available") %
                                std::string(touch_name)).str());
    }
    return touch_it->first;
}


std::expected<void, std::string> Display::validate_source_output_locked(
    uint32_t source_id, std::string_view output_name
) const
{
    if (sources_.find(source_id) == sources_.end()) {
        return std::unexpected((boost::format("Display source %1% is not registered") % source_id).str());
    }
    auto output_id = find_output_id_locked(output_name);
    if (!output_id) {
        return std::unexpected(output_id.error());
    }
    return {};
}


Display::TouchGestureConfig Display::build_default_touch_gesture_config_locked(const OutputContext &output) const
{
    TouchGestureConfig config;
    config.enabled = false;
    config.detect_period_ms = 20;
    config.direction_lock_enabled = true;
    config.release_debounce_ms = 40;
    config.threshold.direction_horizon = static_cast<uint16_t>(
            std::max<uint32_t>(24, output.info.width / 6)
                                         );
    config.threshold.direction_vertical = static_cast<uint16_t>(
            std::max<uint32_t>(24, output.info.height / 6)
                                          );
    config.threshold.direction_angle = 45;
    config.threshold.horizontal_edge = static_cast<uint16_t>(
                                           std::max<uint32_t>(12, output.info.width / 10)
                                       );
    config.threshold.vertical_edge = static_cast<uint16_t>(
                                         std::max<uint32_t>(12, output.info.height / 10)
                                     );
    config.threshold.duration_short_ms = 220;
    config.threshold.speed_slow_px_per_ms = 0.6F;
    return config;
}


std::expected<Display::TouchGestureConfig, std::string> Display::resolve_touch_gesture_config_locked(
    const OutputContext &output, const TouchGestureConfig &config
) const
{
    auto resolved = config;
    const auto defaults = build_default_touch_gesture_config_locked(output);
    if (resolved.detect_period_ms == 0) {
        resolved.detect_period_ms = defaults.detect_period_ms;
    }
    if (resolved.release_debounce_ms == 0) {
        resolved.release_debounce_ms = defaults.release_debounce_ms;
    }
    if (resolved.threshold.direction_horizon == 0) {
        resolved.threshold.direction_horizon = defaults.threshold.direction_horizon;
    }
    if (resolved.threshold.direction_vertical == 0) {
        resolved.threshold.direction_vertical = defaults.threshold.direction_vertical;
    }
    if (resolved.threshold.direction_angle == 0) {
        resolved.threshold.direction_angle = defaults.threshold.direction_angle;
    }
    if (resolved.threshold.horizontal_edge == 0) {
        resolved.threshold.horizontal_edge = defaults.threshold.horizontal_edge;
    }
    if (resolved.threshold.vertical_edge == 0) {
        resolved.threshold.vertical_edge = defaults.threshold.vertical_edge;
    }
    if (resolved.threshold.duration_short_ms == 0) {
        resolved.threshold.duration_short_ms = defaults.threshold.duration_short_ms;
    }
    if (resolved.threshold.speed_slow_px_per_ms <= 0) {
        resolved.threshold.speed_slow_px_per_ms = defaults.threshold.speed_slow_px_per_ms;
    }

    if ((resolved.detect_period_ms < BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MIN_MS) ||
            (resolved.detect_period_ms > BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MAX_MS)) {
        return std::unexpected(
                   (boost::format("Touch gesture detect period must be in range [%1%, %2%] ms") %
                    BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MIN_MS %
                    BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MAX_MS).str()
               );
    }
    if (resolved.release_debounce_ms > BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MAX_MS) {
        return std::unexpected(
                   (boost::format("Touch gesture release debounce must be <= %1% ms") %
                    BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MAX_MS).str()
               );
    }
    if ((resolved.threshold.direction_angle == 0) || (resolved.threshold.direction_angle >= 90)) {
        return std::unexpected("Touch gesture direction angle must be in range [1, 89]");
    }
    if ((resolved.threshold.direction_horizon > output.info.width) ||
            (resolved.threshold.horizontal_edge > output.info.width)) {
        return std::unexpected("Touch gesture horizontal thresholds exceed output width");
    }
    if ((resolved.threshold.direction_vertical > output.info.height) ||
            (resolved.threshold.vertical_edge > output.info.height)) {
        return std::unexpected("Touch gesture vertical thresholds exceed output height");
    }
    return resolved;
}


void Display::reset_touch_gesture_state_locked(OutputContext &output)
{
    output.gesture_info = TouchGestureInfo{};
    output.gesture_info.output_id = output.info.id;
    output.gesture_info.output_name = output.info.name;
    output.gesture_touch_start_time_ms = 0;
    output.gesture_release_start_time_ms = 0;
    output.gesture_active_track_id = 0;
    output.gesture_detection_started = false;
    output.gesture_has_active_track = false;
    output.gesture_direction_locked = false;
    output.gesture_release_pending = false;
}


bool Display::start_touch_gesture_task(uint32_t output_id)
{
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");
    BROOKESIA_CHECK_FALSE_RETURN(scheduler->is_running(), false, "Task scheduler is not running");

    lib_utils::TaskScheduler::TaskId old_task_id = 0;
    uint16_t detect_period_ms = 0;
    bool enabled = false;
    {
        std::lock_guard lock(mutex_);
        auto output_it = outputs_.find(output_id);
        if (output_it == outputs_.end()) {
            return false;
        }
        old_task_id = output_it->second.gesture_task_id;
        output_it->second.gesture_task_id = 0;
        reset_touch_gesture_state_locked(output_it->second);
        enabled = output_it->second.gesture_config.enabled;
        detect_period_ms = output_it->second.gesture_config.detect_period_ms;
    }

    if (old_task_id != 0) {
        scheduler->cancel(old_task_id);
    }
    if (!enabled) {
        return true;
    }

    lib_utils::TaskScheduler::TaskId task_id = 0;
    auto gesture_task = [this, output_id]() -> bool {
        process_touch_gesture(output_id);
        return true;
    };
    const bool scheduled = scheduler->post_periodic(
                               std::move(gesture_task), static_cast<int>(detect_period_ms), &task_id,
                               get_touch_task_group()
                           );
    if (!scheduled) {
        return false;
    }

    bool stored = false;
    {
        std::lock_guard lock(mutex_);
        if (auto output_it = outputs_.find(output_id);
                (output_it != outputs_.end()) && output_it->second.gesture_config.enabled) {
            output_it->second.gesture_task_id = task_id;
            stored = true;
        }
    }
    if (!stored) {
        scheduler->cancel(task_id);
    }
    return stored;
}


void Display::stop_touch_gesture_task(uint32_t output_id)
{
    auto scheduler = get_task_scheduler();
    lib_utils::TaskScheduler::TaskId task_id = 0;
    {
        std::lock_guard lock(mutex_);
        auto output_it = outputs_.find(output_id);
        if (output_it == outputs_.end()) {
            return;
        }
        task_id = output_it->second.gesture_task_id;
        output_it->second.gesture_task_id = 0;
        reset_touch_gesture_state_locked(output_it->second);
    }
    if ((task_id != 0) && scheduler) {
        scheduler->cancel(task_id);
    }
}


void Display::stop_touch_gesture_tasks()
{
    auto scheduler = get_task_scheduler();
    std::vector<lib_utils::TaskScheduler::TaskId> task_ids;
    {
        std::lock_guard lock(mutex_);
        for (auto &[_, output] : outputs_) {
            if (output.gesture_task_id != 0) {
                task_ids.push_back(output.gesture_task_id);
                output.gesture_task_id = 0;
            }
            reset_touch_gesture_state_locked(output);
        }
    }
    if (!scheduler) {
        return;
    }
    for (const auto task_id : task_ids) {
        scheduler->cancel(task_id);
    }
}


void Display::process_touch_gesture(uint32_t output_id)
{
    std::optional<TouchGestureInfo> event_info;
    {
        std::lock_guard lock(mutex_);
        auto output_it = outputs_.find(output_id);
        if ((output_it == outputs_.end()) || !output_it->second.gesture_config.enabled) {
            return;
        }
        auto &output = output_it->second;
        if (output.touch_id == INVALID_TOUCH_ID) {
            reset_touch_gesture_state_locked(output);
            return;
        }
        auto touch_it = touches_.find(output.touch_id);
        if (touch_it == touches_.end()) {
            reset_touch_gesture_state_locked(output);
            return;
        }

        const auto &touch = touch_it->second;
        const auto &snapshot = touch.snapshot;
        const auto selected_point = select_touch_gesture_point(output, snapshot);
        const bool touched = selected_point.has_value();
        const uint64_t current_time_ms = get_current_time_ms();
        bool just_pressed = false;

        if (touched) {
            const auto point = normalize_touch_gesture_point(selected_point.value(), output, touch.info);
            output.gesture_release_pending = false;
            output.gesture_info.stop_x = point.x;
            output.gesture_info.stop_y = point.y;
            output.gesture_info.stop_area = get_touch_gesture_area(output, point.x, point.y);

            if (!output.gesture_detection_started) {
                output.gesture_detection_started = true;
                output.gesture_touch_start_time_ms = current_time_ms;
                output.gesture_has_active_track = selected_point->track_id != 0;
                output.gesture_active_track_id = selected_point->track_id;
                output.gesture_direction_locked = false;
                output.gesture_info = TouchGestureInfo{
                    .output_id = output.info.id,
                    .output_name = output.info.name,
                    .touch_name = touch.info.name,
                    .event_type = TouchGestureEventType::Press,
                    .direction = TouchGestureDirection::None,
                    .start_area = get_touch_gesture_area(output, point.x, point.y),
                    .stop_area = get_touch_gesture_area(output, point.x, point.y),
                    .start_x = point.x,
                    .start_y = point.y,
                    .stop_x = point.x,
                    .stop_y = point.y,
                };
                event_info = output.gesture_info;
                just_pressed = true;
            }

            output.gesture_info.touch_name = touch.info.name;
        } else if (!output.gesture_detection_started) {
            return;
        } else {
            if (!output.gesture_release_pending && (output.gesture_config.release_debounce_ms > 0)) {
                output.gesture_release_pending = true;
                output.gesture_release_start_time_ms = current_time_ms;
                return;
            }
            if (output.gesture_release_pending &&
                    ((current_time_ms - output.gesture_release_start_time_ms) <
                     output.gesture_config.release_debounce_ms)) {
                return;
            }
        }

        if (just_pressed) {
            // The first sample only announces Press; motion metrics start on the next tick.
        } else {
            auto &info = output.gesture_info;
            const auto &config = output.gesture_config;
            info.duration_ms = static_cast<uint32_t>(current_time_ms - output.gesture_touch_start_time_ms);
            info.flags_short_duration = (info.duration_ms < static_cast<uint32_t>(
                                             config.threshold.duration_short_ms
                                         ));

            const int distance_x = info.stop_x - info.start_x;
            const int distance_y = info.stop_y - info.start_y;
            if ((distance_x != 0) || (distance_y != 0)) {
                info.distance_px = std::sqrt(
                                       static_cast<float>((distance_x * distance_x) + (distance_y * distance_y))
                                   );
                info.speed_px_per_ms = (info.duration_ms > 0) ?
                                       (info.distance_px / static_cast<float>(info.duration_ms)) :
                                       std::numeric_limits<float>::infinity();
                info.flags_slow_speed = (info.speed_px_per_ms < config.threshold.speed_slow_px_per_ms);

                TouchGestureDirection direction = TouchGestureDirection::None;
                const float distance_tan = (distance_x == 0) ?
                                           std::numeric_limits<float>::infinity() :
                                           std::abs(static_cast<float>(distance_y) / static_cast<float>(distance_x));
                if (distance_tan > output.gesture_direction_tan_threshold) {
                    if (distance_y > static_cast<int>(config.threshold.direction_vertical)) {
                        direction = TouchGestureDirection::Down;
                    } else if (distance_y < -static_cast<int>(config.threshold.direction_vertical)) {
                        direction = TouchGestureDirection::Up;
                    }
                } else {
                    if (distance_x > static_cast<int>(config.threshold.direction_horizon)) {
                        direction = TouchGestureDirection::Right;
                    } else if (distance_x < -static_cast<int>(config.threshold.direction_horizon)) {
                        direction = TouchGestureDirection::Left;
                    }
                }

                if (direction != TouchGestureDirection::None) {
                    if (!output.gesture_direction_locked || !config.direction_lock_enabled) {
                        info.direction = direction;
                        output.gesture_direction_locked = config.direction_lock_enabled;
                    }
                }
            }

            info.event_type = touched ? TouchGestureEventType::Pressing : TouchGestureEventType::Release;
            event_info = info;
            if (!touched) {
                reset_touch_gesture_state_locked(output);
            }
        }
    }

    if (event_info.has_value()) {
        emit_touch_gesture(event_info.value());
    }
}


uint8_t Display::get_touch_gesture_area(const OutputContext &output, int x, int y) const
{
    uint8_t area = to_area_mask(TouchGestureArea::Center);
    const auto &threshold = output.gesture_config.threshold;
    area |= (y < threshold.vertical_edge) ? to_area_mask(TouchGestureArea::TopEdge) : 0;
    area |= ((static_cast<int>(output.info.height) - y) < threshold.vertical_edge) ?
            to_area_mask(TouchGestureArea::BottomEdge) : 0;
    area |= (x < threshold.horizontal_edge) ? to_area_mask(TouchGestureArea::LeftEdge) : 0;
    area |= ((static_cast<int>(output.info.width) - x) < threshold.horizontal_edge) ?
            to_area_mask(TouchGestureArea::RightEdge) : 0;
    return area;
}


std::optional<Display::TouchPoint> Display::select_touch_gesture_point(
    const OutputContext &output, const TouchSnapshot &snapshot
) const
{
    if (!snapshot.valid || snapshot.points.empty()) {
        return std::nullopt;
    }
    if (output.gesture_detection_started && output.gesture_has_active_track) {
        auto point_it = std::find_if(snapshot.points.begin(), snapshot.points.end(), [&](const auto & point) {
            return point.track_id == output.gesture_active_track_id;
        });
        if (point_it != snapshot.points.end()) {
            return *point_it;
        }
        return std::nullopt;
    }
    return snapshot.points.front();
}


Display::TouchPoint Display::normalize_touch_gesture_point(
    const TouchPoint &point, const OutputContext &output, const TouchInfo &touch
) const
{
    auto normalize_axis = [](int value, uint32_t src_max, uint32_t dst_size) -> int16_t {
        if (dst_size == 0)
        {
            return 0;
        }
        if (src_max == 0)
        {
            return static_cast<int16_t>(std::clamp(value, 0, static_cast<int>(dst_size - 1)));
        }
        const int normalized = static_cast<int>((static_cast<int64_t>(value) * dst_size) / src_max);
        return static_cast<int16_t>(std::clamp(normalized, 0, static_cast<int>(dst_size - 1)));
    };

    return TouchPoint{
        .x = normalize_axis(point.x, touch.x_max, output.info.width),
        .y = normalize_axis(point.y, touch.y_max, output.info.height),
        .pressure = point.pressure,
        .track_id = point.track_id,
    };
}


uint32_t Display::allocate_frame_id_locked()
{
    const uint32_t frame_id = next_frame_id_++;
    if (next_frame_id_ == INVALID_SOURCE_ID) {
        next_frame_id_ = 1;
    }
    return frame_id;
}


bool Display::is_frame_valid_for_output(const FrameInfo &frame, const OutputContext &output, size_t data_size) const
{
    if ((frame.width == 0) || (frame.height == 0)) {
        return false;
    }
    if (frame.pixel_format != output.info.pixel_format) {
        return false;
    }
    if ((frame.x > output.info.width) || (frame.y > output.info.height)) {
        return false;
    }
    if ((frame.width > output.info.width - frame.x) || (frame.height > output.info.height - frame.y)) {
        return false;
    }

    const size_t bpp = bytes_per_pixel(frame.pixel_format);
    if (bpp == 0) {
        return false;
    }
    const size_t expected_size = static_cast<size_t>(frame.width) * frame.height * bpp;
    return data_size == expected_size;
}


bool Display::is_buffer_output_valid(const BufferOutputConfig &config, size_t bpp) const
{
    if ((config.width == 0) || (config.height == 0) || (bpp == 0)) {
        return false;
    }
    auto *buffer_ptr = config.buffer.to_ptr<uint8_t>();
    if (buffer_ptr == nullptr) {
        return false;
    }
    const size_t row_bytes = static_cast<size_t>(config.width) * bpp;
    const size_t stride_bytes = (config.stride_bytes == 0) ? row_bytes : config.stride_bytes;
    if (stride_bytes < row_bytes) {
        return false;
    }
    if (config.height > 1) {
        const size_t max_size = std::numeric_limits<size_t>::max();
        if (stride_bytes > (max_size - row_bytes) / (config.height - 1)) {
            return false;
        }
    }
    const size_t required_size = (config.height > 0) ? (stride_bytes * (config.height - 1) + row_bytes) : 0;
    return config.buffer.data_size >= required_size;
}


Display::PresentResult Display::present_frame_to_output(
    const OutputDrawTarget &target, const FrameInfo &frame, const RawBuffer &data, uint32_t timeout_ms
) const
{
    switch (target.info.slot) {
    case OutputSlot::HalPanel: {
        if (!target.panel) {
            return PresentResult::Error;
        }
        const bool presented = target.panel->draw_bitmap_sync(
                                   frame.x, frame.y, frame.x + frame.width, frame.y + frame.height, data.data_ptr,
                                   timeout_ms
                               );
        return presented ? PresentResult::Presented : PresentResult::Error;
    }
    case OutputSlot::Buffer: {
        auto *target_data = target.buffer.buffer.to_ptr<uint8_t>();
        if ((target_data == nullptr) || (target.buffer.stride_bytes == 0)) {
            return PresentResult::Error;
        }
        const size_t bpp = bytes_per_pixel(frame.pixel_format);
        if (bpp == 0) {
            return PresentResult::Error;
        }
        const size_t row_bytes = static_cast<size_t>(frame.width) * bpp;
        const size_t dst_x_offset = static_cast<size_t>(frame.x) * bpp;
        for (uint32_t row = 0; row < frame.height; ++row) {
            const size_t src_offset = static_cast<size_t>(row) * row_bytes;
            const size_t dst_offset =
                (static_cast<size_t>(frame.y) + row) * target.buffer.stride_bytes + dst_x_offset;
            std::memcpy(target_data + dst_offset, data.data_ptr + src_offset, row_bytes);
        }
        return PresentResult::Presented;
    }
    default:
        return PresentResult::Error;
    }
}


size_t Display::bytes_per_pixel(PixelFormat pixel_format) const
{
    switch (pixel_format) {
    case PixelFormat::RGB565:
        return 2;
    case PixelFormat::RGB888:
        return 3;
    default:
        return 0;
    }
}
}
