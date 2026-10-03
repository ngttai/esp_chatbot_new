/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/display_impl.hpp"

namespace esp_brookesia::service {



bool Display::schedule_render_output_locked(uint32_t output_id)
{
    auto output_it = outputs_.find(output_id);
    if (output_it == outputs_.end()) {
        return false;
    }
    auto &output = output_it->second;
    if (output.render_scheduled) {
        return true;
    }

    auto scheduler = get_task_scheduler();
    if ((scheduler == nullptr) || !scheduler->is_running()) {
        return false;
    }

    const bool scheduled = scheduler->post([this, output_id]() {
        render_output(output_id);
    }, nullptr, get_render_task_group());
    output.render_scheduled = scheduled;
    return scheduled;
}


void Display::render_output(uint32_t output_id)
{
    while (true) {
        AsyncFrame frame;
        OutputDrawTarget target;
        std::shared_ptr<std::mutex> draw_mutex;
        bool has_frame = false;

        {
            std::lock_guard lock(mutex_);
            auto output_it = outputs_.find(output_id);
            if (output_it == outputs_.end()) {
                return;
            }
            auto &output = output_it->second;
            if (!output.pending_frame.has_value()) {
                output.render_scheduled = false;
                return;
            }

            frame = std::move(output.pending_frame.value());
            output.pending_frame.reset();
            output.inflight_frame_id = frame.frame_id;
            draw_mutex = output.draw_mutex;
            has_frame = true;
        }

        if (!has_frame) {
            continue;
        }

        PresentResult result = PresentResult::Error;
        if (draw_mutex == nullptr) {
            result = PresentResult::Error;
        } else {
            std::lock_guard draw_lock(*draw_mutex);
            bool is_active = false;
            bool output_exists = false;
            {
                std::lock_guard lock(mutex_);
                auto output_it = outputs_.find(output_id);
                output_exists = output_it != outputs_.end();
                is_active = output_exists && (output_it->second.active_source_id == frame.source_id);
                if (is_active) {
                    const auto &output = output_it->second;
                    target = OutputDrawTarget{
                        .info = output.info,
                        .panel = output.panel.get(),
                        .buffer = output.buffer,
                    };
                }
            }

            if (!output_exists) {
                result = PresentResult::Error;
            } else if (!is_active) {
                result = PresentResult::DroppedNotActive;
            } else {
                result = present_frame_to_output(target, frame.frame, frame.data, frame.timeout_ms);
            }
        }

        {
            std::lock_guard lock(mutex_);
            auto output_it = outputs_.find(output_id);
            if ((output_it != outputs_.end()) && (output_it->second.inflight_frame_id == frame.frame_id)) {
                output_it->second.inflight_frame_id = 0;
            }
        }
        if (result == PresentResult::Presented) {
            emit_frame_presented(frame.output_name, frame.frame);
        }
        complete_async_frame(std::move(frame), result);
    }
}


void Display::drop_pending_frames_locked(std::vector<AsyncFrame> &dropped_frames)
{
    for (auto &[_, output] : outputs_) {
        if (!output.pending_frame.has_value()) {
            continue;
        }
        dropped_frames.push_back(std::move(output.pending_frame.value()));
        output.pending_frame.reset();
        if (output.inflight_frame_id == 0) {
            output.render_scheduled = false;
        }
    }
}


void Display::complete_async_frame(AsyncFrame frame, PresentResult result)
{
    if (frame.on_complete != nullptr) {
        frame.on_complete(frame.frame_id, result);
    }
}


void Display::bind_default_touches_locked()
{
    for (auto &[_, output] : outputs_) {
        output.touch_id = INVALID_TOUCH_ID;
    }

    if (touches_.empty()) {
        refresh_touch_bound_outputs_locked();
        return;
    }

    std::set<uint32_t> bound_touch_ids;
    for (auto &[output_id, output] : outputs_) {
        if (output.info.group_id.empty()) {
            continue;
        }

        std::vector<uint32_t> matched_touch_ids;
        for (const auto &[touch_id, touch] : touches_) {
            if (touch.info.group_id == output.info.group_id) {
                matched_touch_ids.push_back(touch_id);
            }
        }
        if (matched_touch_ids.empty()) {
            continue;
        }
        if (matched_touch_ids.size() > 1) {
            BROOKESIA_LOGW(
                "Display output %1% group_id(%2%) matches %3% touch devices; leave unbound",
                output.info.name, output.info.group_id, matched_touch_ids.size()
            );
            continue;
        }
        output.touch_id = matched_touch_ids.front();
        bound_touch_ids.insert(output.touch_id);
        BROOKESIA_LOGI(
            "Bound display output %1% to touch %2% by group_id(%3%)", output.info.name,
            touches_.at(output.touch_id).info.name, output.info.group_id
        );
    }

    std::vector<uint32_t> ungrouped_touch_ids;
    for (const auto &[touch_id, touch] : touches_) {
        if (!touch.info.group_id.empty() || (bound_touch_ids.find(touch_id) != bound_touch_ids.end())) {
            continue;
        }
        ungrouped_touch_ids.push_back(touch_id);
    }

    const bool share_single_touch = ungrouped_touch_ids.size() == 1;
    const uint32_t single_touch_id = share_single_touch ? ungrouped_touch_ids.front() : INVALID_TOUCH_ID;
    for (auto &[output_id, output] : outputs_) {
        if (output.touch_id != INVALID_TOUCH_ID) {
            continue;
        }
        if (!output.info.group_id.empty()) {
            continue;
        }
        if (share_single_touch) {
            output.touch_id = single_touch_id;
            BROOKESIA_LOGI(
                "Bound display output %1% to touch %2% by single-touch fallback", output.info.name,
                touches_.at(output.touch_id).info.name
            );
            continue;
        }
        auto touch_it = touches_.find(output_id);
        if ((touch_it != touches_.end()) && touch_it->second.info.group_id.empty()) {
            output.touch_id = output_id;
            BROOKESIA_LOGI(
                "Bound display output %1% to touch %2% by legacy id fallback", output.info.name,
                touch_it->second.info.name
            );
        }
    }
    refresh_touch_bound_outputs_locked();
}
}
