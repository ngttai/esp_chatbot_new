/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "esp_lv_adapter.h"
#include <algorithm>
#include <array>
#include "private/utils.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_helper.hpp"

extern "C" {
#include "speaker_ui.h"
}

#include "speaker_shell.hpp"

using namespace esp_brookesia;
using LvglDisplaySource = gui::lvgl::DisplaySource;
using DisplayHelper = service::helper::Display;

namespace {

constexpr uint32_t SPEAKER_UI_WIDTH = 360;
constexpr uint32_t SPEAKER_UI_HEIGHT = 360;
constexpr uint32_t SERVICE_POLL_PERIOD_MS = 20;
constexpr uint32_t DISPLAY_SERVICE_TIMEOUT_MS = 1000;
constexpr uint8_t SLIDER_STABLE_POLLS = 8;
constexpr uint16_t WIFI_STATE_POLL_TICKS = 50;
constexpr std::array<int, 3> QUICK_BRIGHTNESS_PERCENT{{40, 70, 100}};
constexpr std::array<int, 3> QUICK_VOLUME_PERCENT{{30, 60, 90}};

lv_obj_t *find_first_slider(lv_obj_t *object)
{
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_slider_class)) {
        return object;
    }

    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *slider = find_first_slider(lv_obj_get_child(object, index))) {
            return slider;
        }
    }
    return nullptr;
}

lv_obj_t *find_label(lv_obj_t *object, const char *text)
{
    if ((object == nullptr) || (text == nullptr)) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_label_class)) {
        const char *value = lv_label_get_text(object);
        if ((value != nullptr) && (std::string_view(value) == text)) {
            return object;
        }
    }

    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *label = find_label(lv_obj_get_child(object, index), text)) {
            return label;
        }
    }
    return nullptr;
}

} // namespace

bool ScreenSpeakerShell::start(
    uint32_t display_output_id,
    std::shared_ptr<lib_utils::TaskScheduler> task_scheduler
)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (started_) {
        return true;
    }

    auto &source = LvglDisplaySource::get_instance();
    BROOKESIA_CHECK_FALSE_RETURN(source.is_started(), false, "LVGL display source is not started");
    BROOKESIA_CHECK_FALSE_RETURN(
        source.width() == SPEAKER_UI_WIDTH && source.height() == SPEAKER_UI_HEIGHT, false,
        "Speaker UI requires %1%x%2%, got %3%x%4%",
        SPEAKER_UI_WIDTH, SPEAKER_UI_HEIGHT, source.width(), source.height()
    );

    auto *input = source.input();
    BROOKESIA_CHECK_NULL_RETURN(input, false, "LVGL pointer input is not available");
    BROOKESIA_CHECK_NULL_RETURN(task_scheduler, false, "Backend task scheduler is not available");

    esp_lv_adapter_lock(-1);
    lib_utils::FunctionGuard unlock_guard([]() {
        esp_lv_adapter_unlock();
    });

    speaker_ui_create();
    speaker_ui_set_input(input);

    display_output_id_ = display_output_id;
    task_scheduler_ = std::move(task_scheduler);
    last_quick_brightness_level_ = speaker_ui_get_quick_brightness_level();
    last_quick_volume_level_ = speaker_ui_get_quick_volume_level();
    last_wlan_enabled_ = speaker_ui_is_wlan_on();
    service_timer_ = lv_timer_create(service_timer_callback, SERVICE_POLL_PERIOD_MS, this);
    BROOKESIA_CHECK_NULL_RETURN(service_timer_, false, "Failed to create Speaker UI service adapter timer");

    started_ = true;

    BROOKESIA_LOGI("Speaker UI shell started on %1%x%2%", source.width(), source.height());
    return true;
}

void ScreenSpeakerShell::service_timer_callback(lv_timer_t *timer)
{
    auto *shell = static_cast<ScreenSpeakerShell *>(lv_timer_get_user_data(timer));
    if (shell != nullptr) {
        shell->poll_service_controls();
    }
}

void ScreenSpeakerShell::poll_service_controls()
{
    poll_brightness();
    poll_volume();
    poll_wifi();
}

void ScreenSpeakerShell::poll_brightness()
{
    const int quick_level = speaker_ui_get_quick_brightness_level();
    if (quick_level != last_quick_brightness_level_) {
        last_quick_brightness_level_ = quick_level;
        if ((quick_level >= 0) &&
                (quick_level < static_cast<int>(QUICK_BRIGHTNESS_PERCENT.size()))) {
            apply_brightness(QUICK_BRIGHTNESS_PERCENT[quick_level]);
        }
    }

    if (!speaker_ui_is_screen_active("display")) {
        if (pending_slider_brightness_ >= 0) {
            apply_brightness(pending_slider_brightness_);
        }
        observed_brightness_slider_ = nullptr;
        last_slider_brightness_ = -1;
        pending_slider_brightness_ = -1;
        slider_stable_poll_count_ = 0;
        return;
    }

    auto *slider = find_first_slider(lv_screen_active());
    if (slider == nullptr) {
        return;
    }

    const int brightness = lv_slider_get_value(slider);
    if (slider != observed_brightness_slider_) {
        observed_brightness_slider_ = slider;
        last_slider_brightness_ = brightness;
        pending_slider_brightness_ = -1;
        slider_stable_poll_count_ = 0;
    } else if (brightness != last_slider_brightness_) {
        last_slider_brightness_ = brightness;
        pending_slider_brightness_ = brightness;
        slider_stable_poll_count_ = 0;
    } else if (pending_slider_brightness_ >= 0) {
        if (++slider_stable_poll_count_ >= SLIDER_STABLE_POLLS) {
            apply_brightness(pending_slider_brightness_);
            pending_slider_brightness_ = -1;
            slider_stable_poll_count_ = 0;
        }
    }
}

void ScreenSpeakerShell::apply_brightness(int percent)
{
    auto result = DisplayHelper::call_function_sync(
                      DisplayHelper::FunctionId::SetBacklightBrightness,
                      display_output_id_, percent,
                      service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                  );
    if (!result) {
        BROOKESIA_LOGE("Failed to set Speaker UI brightness to %1%%%: %2%", percent, result.error());
    }
}

void ScreenSpeakerShell::poll_volume()
{
    const int quick_level = speaker_ui_get_quick_volume_level();
    if (quick_level != last_quick_volume_level_) {
        last_quick_volume_level_ = quick_level;
        apply_quick_volume(quick_level);
    }

    if (!speaker_ui_is_screen_active("sound")) {
        if (pending_slider_volume_ >= 0) {
            apply_volume(pending_slider_volume_);
        }
        observed_volume_slider_ = nullptr;
        last_slider_volume_ = -1;
        pending_slider_volume_ = -1;
        volume_slider_stable_poll_count_ = 0;
        return;
    }

    auto *slider = find_first_slider(lv_screen_active());
    if (slider == nullptr) {
        return;
    }

    const int volume = lv_slider_get_value(slider);
    if (slider != observed_volume_slider_) {
        observed_volume_slider_ = slider;
        last_slider_volume_ = volume;
        pending_slider_volume_ = -1;
        volume_slider_stable_poll_count_ = 0;
    } else if (volume != last_slider_volume_) {
        last_slider_volume_ = volume;
        pending_slider_volume_ = volume;
        volume_slider_stable_poll_count_ = 0;
    } else if (pending_slider_volume_ >= 0) {
        if (++volume_slider_stable_poll_count_ >= SLIDER_STABLE_POLLS) {
            apply_volume(pending_slider_volume_);
            pending_slider_volume_ = -1;
            volume_slider_stable_poll_count_ = 0;
        }
    }
}

void ScreenSpeakerShell::apply_quick_volume(int level)
{
    using AudioPlaybackHelper = service::helper::AudioPlayback;

    if (level == -1) {
        auto result = AudioPlaybackHelper::call_function_sync(
                          AudioPlaybackHelper::FunctionId::SetMute, true,
                          service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                      );
        if (!result) {
            BROOKESIA_LOGE("Failed to mute Speaker UI audio: %1%", result.error());
        }
        return;
    }

    if ((level < 0) || (level >= static_cast<int>(QUICK_VOLUME_PERCENT.size()))) {
        BROOKESIA_LOGE("Invalid Speaker UI quick volume level: %1%", level);
        return;
    }
    apply_volume(QUICK_VOLUME_PERCENT[level]);
}

void ScreenSpeakerShell::apply_volume(int percent)
{
    using AudioPlaybackHelper = service::helper::AudioPlayback;

    percent = std::clamp(percent, 0, 100);
    auto volume_result = AudioPlaybackHelper::call_function_sync(
                             AudioPlaybackHelper::FunctionId::SetVolume,
                             static_cast<double>(percent),
                             service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                         );
    auto mute_result = AudioPlaybackHelper::call_function_sync(
                           AudioPlaybackHelper::FunctionId::SetMute, percent == 0,
                           service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                       );
    if (!volume_result || !mute_result) {
        BROOKESIA_LOGE("Failed to set Speaker UI volume to %1%%%", percent);
    }
}

void ScreenSpeakerShell::poll_wifi()
{
    using WifiHelper = service::helper::Wifi;

    if (!WifiHelper::is_running()) {
        return;
    }

    const bool enabled = speaker_ui_is_wlan_on();
    if (enabled != last_wlan_enabled_) {
        last_wlan_enabled_ = enabled;
        request_wifi_enabled(enabled);
    }

    if (++wifi_state_poll_count_ >= WIFI_STATE_POLL_TICKS) {
        wifi_state_poll_count_ = 0;
        request_wifi_state();
    }
    update_wifi_status_ui();
}

void ScreenSpeakerShell::request_wifi_enabled(bool enabled)
{
    if (wifi_action_in_flight_.exchange(true)) {
        return;
    }

    const bool posted = task_scheduler_->post([this, enabled]() {
        using WifiHelper = service::helper::Wifi;

        if (!enabled) {
            // Provisioning owns SoftAP independently from the station state. Stop it
            // first so the UI's WLAN-off state really disables the radio flow.
            (void)WifiHelper::call_function_sync(
                WifiHelper::FunctionId::TriggerSoftApProvisionStop,
                service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
            );
        }

        auto result = WifiHelper::call_function_sync(
                          WifiHelper::FunctionId::TriggerGeneralAction,
                          BROOKESIA_DESCRIBE_TO_STR(
                              enabled ? WifiHelper::GeneralAction::Start : WifiHelper::GeneralAction::Stop
                          ),
                          service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                      );
        if (!result) {
            BROOKESIA_LOGE(
                "Failed to turn Speaker UI WiFi %1%: %2%", enabled ? "on" : "off", result.error()
            );
        }
        wifi_action_in_flight_ = false;
        request_wifi_state();
    });

    if (!posted) {
        wifi_action_in_flight_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI WiFi action");
    }
}

void ScreenSpeakerShell::request_wifi_state()
{
    if (wifi_state_request_in_flight_.exchange(true)) {
        return;
    }

    const bool posted = task_scheduler_->post([this]() {
        using WifiHelper = service::helper::Wifi;

        auto state_result = WifiHelper::call_function_sync<std::string>(
                                WifiHelper::FunctionId::GetGeneralState,
                                service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                            );
        if (state_result) {
            const auto &state = state_result.value();
            int parsed_state = static_cast<int>(WifiHelper::GeneralState::Max);
            for (int candidate = 0; candidate < static_cast<int>(WifiHelper::GeneralState::Max); ++candidate) {
                auto enum_value = static_cast<WifiHelper::GeneralState>(candidate);
                if (state == BROOKESIA_DESCRIBE_TO_STR(enum_value)) {
                    parsed_state = candidate;
                    break;
                }
            }
            wifi_state_ = parsed_state;

            if (parsed_state == static_cast<int>(WifiHelper::GeneralState::Connected)) {
                auto ap_result = WifiHelper::call_function_sync<boost::json::object>(
                                     WifiHelper::FunctionId::GetConnectAp,
                                     service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                                 );
                if (ap_result) {
                    WifiHelper::ConnectApInfo ap_info;
                    if (BROOKESIA_DESCRIBE_FROM_JSON(ap_result.value(), ap_info)) {
                        std::lock_guard lock(wifi_state_mutex_);
                        wifi_ssid_ = ap_info.ssid;
                    }
                }
            }
        } else {
            BROOKESIA_LOGE("Failed to query Speaker UI WiFi state: %1%", state_result.error());
        }
        wifi_state_request_in_flight_ = false;
    });

    if (!posted) {
        wifi_state_request_in_flight_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI WiFi state query");
    }
}

void ScreenSpeakerShell::update_wifi_status_ui()
{
    using WifiHelper = service::helper::Wifi;

    if (!speaker_ui_is_screen_active("wlan")) {
        return;
    }

    if (wifi_connected_name_label_ == nullptr) {
        wifi_connected_name_label_ = find_label(lv_screen_active(), "Studio-WiFi");
        wifi_connected_status_label_ = find_label(lv_screen_active(), "Connected");
        if (wifi_connected_name_label_ != nullptr) {
            auto *row = lv_obj_get_parent(wifi_connected_name_label_);
            auto *panel = row == nullptr ? nullptr : lv_obj_get_parent(row);
            wifi_connected_group_ = panel == nullptr ? nullptr : lv_obj_get_parent(panel);
        }
    }

    const int state = wifi_state_.load();
    const bool connected = state == static_cast<int>(WifiHelper::GeneralState::Connected);
    const bool connecting = state == static_cast<int>(WifiHelper::GeneralState::Connecting);
    if (wifi_connected_group_ != nullptr) {
        if (connected || connecting) {
            lv_obj_remove_flag(wifi_connected_group_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wifi_connected_group_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (wifi_connected_status_label_ != nullptr) {
        lv_label_set_text(wifi_connected_status_label_, connected ? "Connected" : "Connecting...");
    }
    if (connected && (wifi_connected_name_label_ != nullptr)) {
        std::lock_guard lock(wifi_state_mutex_);
        if (!wifi_ssid_.empty()) {
            lv_label_set_text(wifi_connected_name_label_, wifi_ssid_.c_str());
        }
    }
}
