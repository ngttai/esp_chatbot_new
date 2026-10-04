/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/display_impl.hpp"

namespace esp_brookesia::service {



Display::OutputTouchCapability Display::make_output_touch_capability_locked(const TouchContext &touch) const
{
    return OutputTouchCapability{
        .id = touch.info.id,
        .name = touch.info.name,
        .instance = touch.info.touch_instance,
        .max_points = touch.info.max_points,
        .operation_mode = touch.info.operation_mode,
    };
}


Display::OutputBacklightCapability Display::make_output_backlight_capability(const OutputContext &output) const
{
    return OutputBacklightCapability{
        .instance = output.backlight ? output.backlight.instance_name() : std::string(),
        .on_off_supported = output.backlight ? output.backlight->is_light_on_off_supported() : false,
    };
}


void Display::refresh_output_capability_locked(OutputContext &output)
{
    output.info.touch.reset();
    if (output.touch_id != INVALID_TOUCH_ID) {
        auto touch_it = touches_.find(output.touch_id);
        if (touch_it != touches_.end()) {
            output.info.touch = make_output_touch_capability_locked(touch_it->second);
        }
    }

    output.info.backlight.reset();
    if (output.backlight) {
        output.info.backlight = make_output_backlight_capability(output);
    }
}


void Display::refresh_touch_bound_outputs_locked()
{
    for (auto &[_, touch] : touches_) {
        touch.info.bound_outputs.clear();
    }
    for (const auto &[_, output] : outputs_) {
        if (output.touch_id == INVALID_TOUCH_ID) {
            continue;
        }
        auto touch_it = touches_.find(output.touch_id);
        if (touch_it != touches_.end()) {
            touch_it->second.info.bound_outputs.push_back(output.info.name);
        }
    }
    for (auto &[_, output] : outputs_) {
        refresh_output_capability_locked(output);
    }
}


bool Display::start_touch_tasks()
{
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");

    std::vector<uint32_t> touch_ids;
    {
        std::lock_guard lock(mutex_);
        touch_ids.reserve(touches_.size());
        for (const auto &[touch_id, _] : touches_) {
            touch_ids.push_back(touch_id);
        }
    }

    bool all_started = true;
    for (const auto touch_id : touch_ids) {
        all_started = start_touch_task(touch_id) && all_started;
    }

    return all_started;
}


bool Display::start_touch_task(uint32_t touch_id)
{
    auto scheduler = get_task_scheduler();
    BROOKESIA_CHECK_NULL_RETURN(scheduler, false, "Task scheduler is not available");

    std::shared_ptr<hal::display::TouchIface> touch_handle;
    TouchInfo touch_info;
    std::shared_ptr<TouchInterruptBridge> old_interrupt_bridge;
    lib_utils::TaskScheduler::TaskId old_poll_task_id = 0;
    {
        std::lock_guard lock(mutex_);
        auto touch_it = touches_.find(touch_id);
        if (touch_it == touches_.end()) {
            return false;
        }
        touch_handle = touch_it->second.touch.get();
        touch_info = touch_it->second.info;
        old_interrupt_bridge = std::move(touch_it->second.interrupt_bridge);
        old_poll_task_id = touch_it->second.poll_task_id;
        touch_it->second.poll_task_id = 0;
        touch_it->second.read_scheduled = false;
    }
    if (!touch_handle) {
        return false;
    }

    if (old_poll_task_id != 0) {
        scheduler->cancel(old_poll_task_id);
    }
    if (old_interrupt_bridge) {
        (void)touch_handle->register_interrupt_handler(nullptr, nullptr);
        old_interrupt_bridge->stop();
    }

    if (touch_info.operation_mode == hal::display::TouchIface::OperationMode::Interrupt) {
        auto interrupt_bridge = std::make_shared<TouchInterruptBridge>();
        if (!interrupt_bridge->start(this, touch_id, touch_handle)) {
            BROOKESIA_LOGW("Failed to register Display touch interrupt handler for %1%", touch_info.name);
            return false;
        }
        bool stored = false;
        {
            std::lock_guard lock(mutex_);
            if (auto touch_it = touches_.find(touch_id); touch_it != touches_.end()) {
                touch_it->second.interrupt_bridge = interrupt_bridge;
                stored = true;
            }
        }
        if (!stored) {
            (void)touch_handle->register_interrupt_handler(nullptr, nullptr);
            interrupt_bridge->stop();
            return false;
        }
        (void)schedule_touch_read(touch_id);
        return true;
    }

    lib_utils::TaskScheduler::TaskId poll_task_id = 0;
    auto poll_task = [this, touch_id]() -> bool {
        read_touch(touch_id);
        return true;
    };
    const bool scheduled = scheduler->post_periodic(
                               std::move(poll_task),
                               static_cast<int>(BROOKESIA_SERVICE_DISPLAY_TOUCH_POLL_INTERVAL_MS), &poll_task_id,
                               get_touch_task_group()
                           );
    if (!scheduled) {
        BROOKESIA_LOGW("Failed to schedule Display touch polling for %1%", touch_info.name);
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        if (auto touch_it = touches_.find(touch_id); touch_it != touches_.end()) {
            touch_it->second.poll_task_id = poll_task_id;
        }
    }
    (void)schedule_touch_read(touch_id);
    return true;
}


void Display::stop_touch_tasks()
{
    auto scheduler = get_task_scheduler();
    std::vector<std::pair<std::shared_ptr<hal::display::TouchIface>, std::shared_ptr<TouchInterruptBridge>>>
    interrupt_bridges;
    std::vector<lib_utils::TaskScheduler::TaskId> poll_task_ids;

    {
        std::lock_guard lock(mutex_);
        for (auto &[_, touch] : touches_) {
            touch.read_scheduled = false;
            if (touch.info.operation_mode == hal::display::TouchIface::OperationMode::Interrupt) {
                interrupt_bridges.emplace_back(touch.touch.get(), std::move(touch.interrupt_bridge));
            }
            if (touch.poll_task_id != 0) {
                poll_task_ids.push_back(touch.poll_task_id);
                touch.poll_task_id = 0;
            }
        }
    }

    for (auto &[touch, bridge] : interrupt_bridges) {
        if (touch) {
            (void)touch->register_interrupt_handler(nullptr, nullptr);
        }
        if (bridge) {
            bridge->stop();
        }
    }
    if (scheduler) {
        for (const auto task_id : poll_task_ids) {
            scheduler->cancel(task_id);
        }
        if (scheduler->is_running()) {
            scheduler->cancel_group(get_touch_task_group());
            (void)scheduler->wait_group(get_touch_task_group(), 200);
        }
    }
}


bool Display::schedule_touch_read(uint32_t touch_id)
{
    auto scheduler = get_task_scheduler();
    if ((scheduler == nullptr) || !scheduler->is_running()) {
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        auto touch_it = touches_.find(touch_id);
        if (touch_it == touches_.end()) {
            return false;
        }
        if (touch_it->second.read_scheduled) {
            return true;
        }
        touch_it->second.read_scheduled = true;
    }

    const bool scheduled = scheduler->post([this, touch_id]() {
        read_touch(touch_id);
    }, nullptr, get_touch_task_group());
    if (!scheduled) {
        std::lock_guard lock(mutex_);
        if (auto touch_it = touches_.find(touch_id); touch_it != touches_.end()) {
            touch_it->second.read_scheduled = false;
        }
    }
    return scheduled;
}


void Display::read_touch(uint32_t touch_id)
{
    std::shared_ptr<hal::display::TouchIface> touch_handle;
    std::vector<std::string> output_names;
    {
        std::lock_guard lock(mutex_);
        auto touch_it = touches_.find(touch_id);
        if (touch_it == touches_.end()) {
            return;
        }
        touch_handle = touch_it->second.touch.get();
        output_names = touch_it->second.info.bound_outputs;
    }

    std::vector<TouchPoint> points;
    bool read_success = false;
    if (touch_handle) {
        read_success = touch_handle->read_points(points);
    }

    TouchSnapshot snapshot;
    {
        std::lock_guard lock(mutex_);
        auto touch_it = touches_.find(touch_id);
        if (touch_it == touches_.end()) {
            return;
        }
        touch_it->second.read_scheduled = false;
        if (!read_success) {
            BROOKESIA_LOGW("Failed to read Display touch %1%", touch_it->second.info.name);
            return;
        }

        auto &cached_snapshot = touch_it->second.snapshot;
        cached_snapshot.points = std::move(points);
        cached_snapshot.sequence++;
        if (cached_snapshot.sequence == 0) {
            cached_snapshot.sequence = 1;
        }
        cached_snapshot.updated_at_ms = get_current_time_ms();
        cached_snapshot.valid = true;
        snapshot = cached_snapshot;
        output_names = touch_it->second.info.bound_outputs;
    }

    emit_touch_updated(output_names, snapshot);
}


void Display::emit_touch_updated(const std::vector<std::string> &output_names, const TouchSnapshot &snapshot)
{
    for (const auto &output_name : output_names) {
        touch_updated_signal_(output_name, snapshot);
    }
}


void Display::emit_touch_gesture(const TouchGestureInfo &info)
{
    touch_gesture_signal_(info.output_name, info);
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::TouchGesture),
        std::vector<EventItem> {EventItem(BROOKESIA_DESCRIBE_TO_JSON(info).as_object())}
    );
}


void Display::emit_output_registered(const OutputInfo &info)
{
    output_registered_signal_(info);
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::OutputRegistered),
        std::vector<EventItem> {EventItem(BROOKESIA_DESCRIBE_TO_JSON(info).as_object())}
    );
}


void Display::emit_output_unregistered(const std::string &output_name)
{
    output_unregistered_signal_(output_name);
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::OutputUnregistered),
        std::vector<EventItem> {output_name}
    );
}


void Display::emit_frame_presented(const std::string &output_name, const FrameInfo &frame)
{
    frame_presented_signal_(output_name, frame);
}


void Display::emit_source_state_changed(const std::string &source_name, const std::string &output_name, SourceState state)
{
    source_state_changed_signal_(source_name, output_name, state);
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::SourceStateChanged),
    std::vector<EventItem> {
        source_name,
        output_name,
        BROOKESIA_DESCRIBE_ENUM_TO_STR(state),
    }
    );
}


void Display::emit_active_source_changed(const std::string &output_name, const std::string &source_name)
{
    active_source_changed_signal_(output_name, source_name);
    publish_event(
        BROOKESIA_DESCRIBE_ENUM_TO_STR(Helper::EventId::ActiveSourceChanged),
        std::vector<EventItem> {output_name, source_name}
    );
}
}
