/* SPDX-License-Identifier: CC0-1.0 */
#pragma once
#include <memory>
#include <string>
#include "brookesia/lib_utils/task_scheduler.hpp"

bool start_render_suspend_participants(
    std::shared_ptr<esp_brookesia::lib_utils::TaskScheduler> scheduler,
    uint32_t output_id, const std::string &output_name, uint32_t width, uint32_t height, bool has_emote);
