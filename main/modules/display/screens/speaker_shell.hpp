/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "brookesia/lib_utils/task_scheduler.hpp"
#include "lvgl.h"

class ScreenSpeakerShell {
public:
    bool start(
        uint32_t display_output_id,
        std::shared_ptr<esp_brookesia::lib_utils::TaskScheduler> task_scheduler
    );

private:
    static void service_timer_callback(lv_timer_t *timer);
    void poll_service_controls();
    void poll_brightness();
    void poll_volume();
    void poll_wifi();
    void apply_brightness(int percent);
    void apply_quick_volume(int level);
    void apply_volume(int percent);
    void request_wifi_enabled(bool enabled);
    void request_wifi_state();
    void update_wifi_status_ui();

    bool started_ = false;
    uint32_t display_output_id_ = 0;
    std::shared_ptr<esp_brookesia::lib_utils::TaskScheduler> task_scheduler_;
    lv_timer_t *service_timer_ = nullptr;
    lv_obj_t *observed_brightness_slider_ = nullptr;
    int last_quick_brightness_level_ = 0;
    int last_slider_brightness_ = -1;
    int pending_slider_brightness_ = -1;
    uint8_t slider_stable_poll_count_ = 0;
    lv_obj_t *observed_volume_slider_ = nullptr;
    int last_quick_volume_level_ = -1;
    int last_slider_volume_ = -1;
    int pending_slider_volume_ = -1;
    uint8_t volume_slider_stable_poll_count_ = 0;
    lv_obj_t *wifi_connected_group_ = nullptr;
    lv_obj_t *wifi_connected_name_label_ = nullptr;
    lv_obj_t *wifi_connected_status_label_ = nullptr;
    bool last_wlan_enabled_ = true;
    uint16_t wifi_state_poll_count_ = 0;
    std::atomic_bool wifi_action_in_flight_{false};
    std::atomic_bool wifi_state_request_in_flight_{false};
    std::atomic_int wifi_state_{-1};
    std::mutex wifi_state_mutex_;
    std::string wifi_ssid_;
};
