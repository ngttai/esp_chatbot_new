/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>

#include "lvgl.h"

class ScreenSpeakerShell {
public:
    bool start(uint32_t display_output_id);

private:
    static void brightness_timer_callback(lv_timer_t *timer);
    void poll_brightness();
    void apply_brightness(int percent);

    bool started_ = false;
    uint32_t display_output_id_ = 0;
    lv_timer_t *brightness_timer_ = nullptr;
    lv_obj_t *observed_brightness_slider_ = nullptr;
    int last_quick_brightness_level_ = 0;
    int last_slider_brightness_ = -1;
    int pending_slider_brightness_ = -1;
    uint8_t slider_stable_poll_count_ = 0;
};
