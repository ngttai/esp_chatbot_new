/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "brookesia/lib_utils/task_scheduler.hpp"
#include "brookesia/service_manager/event/registry.hpp"
#include "lvgl.h"

class ScreenSpeakerShell {
public:
    bool start(
        uint32_t display_output_id,
        std::shared_ptr<esp_brookesia::lib_utils::TaskScheduler> task_scheduler
    );

private:
    struct WifiScanEntry {
        std::string ssid;
        bool locked = false;
        int rssi = 0;
    };

    static void service_timer_callback(lv_timer_t *timer);
    static void factory_reset_clicked_callback(lv_event_t *event);
    static void developer_mode_clicked_callback(lv_event_t *event);
    static void wifi_network_selected_callback(lv_event_t *event);
    static void wifi_password_ready_callback(lv_event_t *event);
    void poll_service_controls();
    void poll_brightness();
    void poll_volume();
    void poll_wifi();
    void poll_memory();
    void poll_battery();
    void poll_touch_sensor();
    void poll_factory_reset();
    void poll_display_mode();
    void configure_about();
    void attach_developer_mode_handler();
    void perform_factory_reset();
    void ensure_control_event_subscriptions();
    void refresh_control_state();
    void sync_service_control_ui();
    void apply_brightness(int percent);
    void apply_quick_volume(int level);
    void apply_volume(int percent);
    void request_wifi_enabled(bool enabled);
    void request_wifi_state();
    void request_wifi_scan();
    void request_wifi_scan_stop();
    void request_wifi_connect(std::string ssid, std::string password);
    void ensure_wifi_event_subscriptions();
    void attach_wifi_ui_handlers();
    void update_wifi_scan_ui();
    void update_wifi_status_ui();
    void attach_memory_ui();

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
    std::atomic_int service_brightness_{-1};
    std::atomic_int service_volume_{-1};
    std::atomic_int service_muted_{-1};
    int synced_service_brightness_ = -1;
    int synced_effective_volume_ = -1;
    bool control_events_subscribed_ = false;
    lv_obj_t *memory_internal_bar_ = nullptr;
    lv_obj_t *memory_external_bar_ = nullptr;
    uint16_t memory_poll_count_ = 0;
    bool memory_snapshot_logged_ = false;
    uint32_t battery_revision_ = 0;
    bool touch_sensor_user_enabled_ = true;
    bool touch_sensor_effective_enabled_ = true;
    bool factory_reset_handler_attached_ = false;
    bool factory_reset_in_progress_ = false;
    bool idle_display_mode_initialized_ = false;
    bool idle_display_mode_ = false;
    lv_obj_t *wifi_connected_group_ = nullptr;
    lv_obj_t *wifi_connected_name_label_ = nullptr;
    lv_obj_t *wifi_connected_status_label_ = nullptr;
    bool last_wlan_enabled_ = true;
    uint16_t wifi_state_poll_count_ = 0;
    std::atomic_bool wifi_action_in_flight_{false};
    std::atomic_bool wifi_state_request_in_flight_{false};
    std::atomic_bool wifi_scan_request_in_flight_{false};
    std::atomic_bool wifi_scan_stop_in_flight_{false};
    std::atomic_bool wifi_connect_in_flight_{false};
    std::atomic_int wifi_state_{-1};
    std::mutex wifi_state_mutex_;
    std::string wifi_ssid_;
    std::mutex wifi_scan_mutex_;
    std::vector<WifiScanEntry> wifi_scan_entries_;
    std::array<lv_obj_t *, 3> wifi_network_rows_{};
    std::array<lv_obj_t *, 3> wifi_network_labels_{};
    std::array<lv_obj_t *, 3> wifi_network_lock_icons_{};
    lv_obj_t *wifi_keyboard_ = nullptr;
    std::string wifi_selected_ssid_;
    bool wifi_events_subscribed_ = false;
    bool wifi_handlers_attached_ = false;
    bool wifi_screen_was_active_ = false;
    bool wifi_scan_after_enable_pending_ = false;
    bool wifi_open_ap_pending_ = false;
    uint8_t wifi_open_ap_countdown_ = 0;
    size_t wifi_scan_visible_count_ = 0;
    std::atomic_bool wifi_scan_dirty_{false};
    esp_brookesia::service::EventRegistry::SignalConnection wifi_scan_event_connection_;
    esp_brookesia::service::EventRegistry::SignalConnection brightness_event_connection_;
    esp_brookesia::service::EventRegistry::SignalConnection volume_event_connection_;
    esp_brookesia::service::EventRegistry::SignalConnection mute_event_connection_;
};
