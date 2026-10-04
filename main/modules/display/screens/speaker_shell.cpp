/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "esp_lv_adapter.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>
#include "private/utils.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_helper.hpp"
#include "modules/battery_monitor.hpp"
#include "modules/imu_gesture.hpp"
#include "modules/touch_sensor.hpp"
#include "modules/weather_config.hpp"
#include "modules/developer_mode.hpp"
#include "modules/display/display.hpp"

extern "C" {
#include "boot_splash.h"
#include "speaker_ui.h"
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_level1_36_36);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_level2_36_36);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_level3_36_36);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_lock_48_48);
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
constexpr uint32_t WIFI_AGENT_AUDIO_DRAIN_MS = 100;
constexpr uint32_t BOOT_SPLASH_START_DELAY_MS = 1050;
constexpr uint16_t MEMORY_POLL_TICKS = 50;
constexpr std::time_t MIN_VALID_NETWORK_TIME = 1704067200; // 2024-01-01 UTC
constexpr uint8_t WIFI_OPEN_AP_DELAY_TICKS = 10;
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

lv_obj_t *find_first_keyboard(lv_obj_t *object)
{
    if (object == nullptr) {
        return nullptr;
    }
    if (lv_obj_check_type(object, &lv_keyboard_class)) {
        return object;
    }
    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *keyboard = find_first_keyboard(lv_obj_get_child(object, index))) {
            return keyboard;
        }
    }
    return nullptr;
}

const lv_image_dsc_t *wifi_signal_image(int rssi)
{
    if (rssi >= -60) {
        return &esp_brookesia_app_icon_wlan_level3_36_36;
    }
    if (rssi >= -75) {
        return &esp_brookesia_app_icon_wlan_level2_36_36;
    }
    return &esp_brookesia_app_icon_wlan_level1_36_36;
}

template <size_t N>
int nearest_level(int percent, const std::array<int, N> &levels)
{
    int best_level = 0;
    int best_distance = std::abs(percent - levels[0]);
    for (size_t index = 1; index < levels.size(); ++index) {
        const int distance = std::abs(percent - levels[index]);
        if (distance < best_distance) {
            best_level = static_cast<int>(index);
            best_distance = distance;
        }
    }
    return best_level;
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
    boot_splash_create(BOOT_SPLASH_START_DELAY_MS);
    speaker_ui_set_wifi_managed_externally(true);
    speaker_ui_set_input(input);
    configure_about();
    attach_developer_mode_handler();
    attach_self_test_handler();

    display_output_id_ = display_output_id;
    task_scheduler_ = std::move(task_scheduler);
    last_quick_brightness_level_ = speaker_ui_get_quick_brightness_level();
    last_quick_volume_level_ = speaker_ui_get_quick_volume_level();
    last_wlan_enabled_ = speaker_ui_is_wlan_on();
    touch_sensor_user_enabled_ = TouchSensor::get_instance().is_enabled();
    touch_sensor_effective_enabled_ = touch_sensor_user_enabled_;
    speaker_ui_set_touch_sensor_on(touch_sensor_user_enabled_);
    ensure_control_event_subscriptions();
    refresh_control_state();
    service_timer_ = lv_timer_create(service_timer_callback, SERVICE_POLL_PERIOD_MS, this);
    BROOKESIA_CHECK_NULL_RETURN(service_timer_, false, "Failed to create Speaker UI service adapter timer");

    started_ = true;

    BROOKESIA_LOGI("Speaker UI shell started on %1%x%2%", source.width(), source.height());
    return true;
}

void ScreenSpeakerShell::developer_mode_clicked_callback(lv_event_t *event)
{
    LV_UNUSED(event);
    DeveloperMode::request_and_restart();
}

void ScreenSpeakerShell::attach_developer_mode_handler()
{
    BROOKESIA_CHECK_FALSE_EXIT(
        speaker_ui_set_developer_mode_callback(developer_mode_clicked_callback, this),
        "Failed to attach Speaker UI developer-mode handler"
    );
}

void ScreenSpeakerShell::self_test_run_clicked_callback(lv_event_t *event)
{
    auto *shell = static_cast<ScreenSpeakerShell *>(lv_event_get_user_data(event));
    if (shell != nullptr) {
        shell->run_self_test();
    }
}

void ScreenSpeakerShell::attach_self_test_handler()
{
    BROOKESIA_CHECK_FALSE_EXIT(
        speaker_ui_set_self_test_run_callback(self_test_run_clicked_callback, this),
        "Failed to attach Speaker UI self-test handler"
    );
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
    poll_display_mode();
    sync_service_control_ui();
    poll_brightness();
    poll_volume();
    poll_wifi();
    poll_memory();
    poll_battery();
    poll_touch_sensor();
    poll_self_test();
    poll_factory_reset();
}

void ScreenSpeakerShell::run_self_test()
{
    if (self_test_running_.exchange(true)) {
        return;
    }

    for (int item = 0; item < SPEAKER_UI_SELF_TEST_COUNT; ++item) {
        self_test_results_[item].store(SPEAKER_UI_SELF_TEST_TESTING);
        speaker_ui_set_self_test_status(
            static_cast<speaker_ui_self_test_item_t>(item), SPEAKER_UI_SELF_TEST_TESTING
        );
    }

    const bool posted = task_scheduler_->post([this]() {
        using AudioEncoderHelper = service::helper::AudioEncoder<0>;
        using AudioPlaybackHelper = service::helper::AudioPlayback;
        using WifiHelper = service::helper::Wifi;

        auto publish = [this](speaker_ui_self_test_item_t item, bool passed) {
            self_test_results_[item].store(
                passed ? SPEAKER_UI_SELF_TEST_PASS : SPEAKER_UI_SELF_TEST_FAIL
            );
            self_test_results_dirty_ = true;
        };

        // Reaching this callback from the rendered Self-test page confirms that
        // the panel, LVGL input path and display service are alive.
        publish(SPEAKER_UI_SELF_TEST_DISPLAY, true);
        publish(SPEAKER_UI_SELF_TEST_TOUCH, TouchSensor::get_instance().is_initialized());

        bool speaker_ok = AudioPlaybackHelper::is_running();
        if (speaker_ok) {
            auto volume_result = AudioPlaybackHelper::call_function_sync<double>(
                                     AudioPlaybackHelper::FunctionId::GetVolume,
                                     service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                                 );
            speaker_ok = static_cast<bool>(volume_result);
        }
        publish(SPEAKER_UI_SELF_TEST_SPEAKER, speaker_ok);

        bool microphone_ok = AudioEncoderHelper::is_running();
        if (microphone_ok) {
            auto wake_words_result = AudioEncoderHelper::call_function_sync<boost::json::array>(
                                         AudioEncoderHelper::FunctionId::GetAFEWakeWords,
                                         service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                                     );
            microphone_ok = static_cast<bool>(wake_words_result);
        }
        publish(SPEAKER_UI_SELF_TEST_MICROPHONE, microphone_ok);

        publish(SPEAKER_UI_SELF_TEST_BMI270, ImuGesture::get_instance().is_initialized());

        const auto battery = BatteryMonitor::get_instance().get_snapshot();
        publish(SPEAKER_UI_SELF_TEST_BATTERY, battery.valid);
        publish(SPEAKER_UI_SELF_TEST_CHARGING, battery.valid && battery.charging);

        const bool wifi_connected = WifiHelper::is_running() &&
                                    (wifi_state_.load() ==
                                     static_cast<int>(WifiHelper::GeneralState::Connected));
        publish(SPEAKER_UI_SELF_TEST_WIFI, wifi_connected);
        publish(SPEAKER_UI_SELF_TEST_NTP, std::time(nullptr) >= MIN_VALID_NETWORK_TIME);

        publish(SPEAKER_UI_SELF_TEST_MEMORY, heap_caps_check_integrity_all(true));

        uint8_t flash_probe[32]{};
        const esp_partition_t *app_partition = esp_partition_find_first(
                                                   ESP_PARTITION_TYPE_APP,
                                                   ESP_PARTITION_SUBTYPE_ANY, nullptr
                                               );
        const bool flash_ok = (app_partition != nullptr) &&
                              (esp_partition_read(
                                   app_partition, 0, flash_probe, sizeof(flash_probe)
                               ) == ESP_OK);
        publish(SPEAKER_UI_SELF_TEST_FLASH, flash_ok);

        self_test_running_ = false;
        BROOKESIA_LOGI("Speaker UI hardware self-test completed");
    });

    if (!posted) {
        for (int item = 0; item < SPEAKER_UI_SELF_TEST_COUNT; ++item) {
            self_test_results_[item].store(SPEAKER_UI_SELF_TEST_FAIL);
        }
        self_test_results_dirty_ = true;
        self_test_running_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI hardware self-test");
    }
}

void ScreenSpeakerShell::poll_self_test()
{
    if (!self_test_results_dirty_.exchange(false)) {
        return;
    }
    for (int item = 0; item < SPEAKER_UI_SELF_TEST_COUNT; ++item) {
        const int status = self_test_results_[item].load();
        if (status == SPEAKER_UI_SELF_TEST_PASS || status == SPEAKER_UI_SELF_TEST_FAIL) {
            speaker_ui_set_self_test_status(
                static_cast<speaker_ui_self_test_item_t>(item),
                static_cast<speaker_ui_self_test_status_t>(status)
            );
        }
    }
}

void ScreenSpeakerShell::poll_display_mode()
{
    // Keep LVGL visible until the startup overlay has finished. The normal
    // idle-screen policy switches to the native Emote source immediately
    // afterwards.
    if (boot_splash_is_active()) {
        return;
    }

    auto &display = Display::get_instance();
    if (!display.emote_ready()) {
        return;
    }

    if (display_mode_result_ready_.exchange(false, std::memory_order_acquire)) {
        const bool requested_idle = display_mode_requested_idle_.load(std::memory_order_relaxed);
        if (display_mode_switch_succeeded_.load(std::memory_order_relaxed)) {
            idle_display_mode_ = requested_idle;
            idle_display_mode_initialized_ = true;
            BROOKESIA_LOGI(
                "Speaker UI display source: %1%", requested_idle ? "Native Emote" : "LVGL"
            );
        } else {
            BROOKESIA_LOGE("Failed to switch display source for Speaker UI idle state");
        }
    }

    const bool idle_active = speaker_ui_is_idle_active();
    if (idle_display_mode_initialized_ && (idle_active == idle_display_mode_)) {
        return;
    }

    bool expected = false;
    if (!display_mode_switch_in_flight_.compare_exchange_strong(expected, true)) {
        return;
    }

    const bool posted = task_scheduler_->post([this, idle_active]() {
        auto &worker_display = Display::get_instance();
        const bool switched = idle_active ? worker_display.show_emote() : worker_display.show_ui();
        display_mode_requested_idle_.store(idle_active, std::memory_order_relaxed);
        display_mode_switch_succeeded_.store(switched, std::memory_order_relaxed);
        display_mode_result_ready_.store(true, std::memory_order_release);
        display_mode_switch_in_flight_.store(false, std::memory_order_release);
    });
    if (!posted) {
        display_mode_switch_in_flight_.store(false, std::memory_order_release);
        BROOKESIA_LOGE("Failed to schedule display source switch");
    }
}

void ScreenSpeakerShell::poll_battery()
{
    const auto snapshot = BatteryMonitor::get_instance().get_snapshot();
    if (!snapshot.valid || (snapshot.revision == battery_revision_)) {
        return;
    }
    if (speaker_ui_set_battery_state(snapshot.charging, snapshot.percentage) &&
            speaker_ui_set_about_battery_measurements(snapshot.voltage_mv, snapshot.current_ma)) {
        battery_revision_ = snapshot.revision;
    }
}

void ScreenSpeakerShell::configure_about()
{
    std::array<char, 24> ui_version{};
    std::array<char, 16> resolution{};
    std::array<char, 16> flash{};
    std::array<char, 16> chip_version{};
    std::array<char, 24> chip_mac{};
    std::array<char, 24> chip_features{};

    std::snprintf(
        ui_version.data(), ui_version.size(), "LVGL %d.%d.%d",
        LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH
    );
    std::snprintf(
        resolution.data(), resolution.size(), "%lux%lu",
        static_cast<unsigned long>(SPEAKER_UI_WIDTH), static_cast<unsigned long>(SPEAKER_UI_HEIGHT)
    );

    uint32_t flash_bytes = 0;
    if (esp_flash_get_size(nullptr, &flash_bytes) == ESP_OK) {
        std::snprintf(flash.data(), flash.size(), "%luMB",
                      static_cast<unsigned long>(flash_bytes / (1024U * 1024U)));
    } else {
        std::snprintf(flash.data(), flash.size(), "Unknown");
    }

    esp_chip_info_t chip_info{};
    esp_chip_info(&chip_info);
    std::snprintf(
        chip_version.data(), chip_version.size(), "v%u.%u",
        static_cast<unsigned>(chip_info.revision / 100),
        static_cast<unsigned>(chip_info.revision % 100)
    );
    std::snprintf(
        chip_features.data(), chip_features.size(), "%u CPU cores",
        static_cast<unsigned>(chip_info.cores)
    );

    uint8_t mac[6]{};
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        std::snprintf(
            chip_mac.data(), chip_mac.size(), "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
        );
    } else {
        std::snprintf(chip_mac.data(), chip_mac.size(), "Unknown");
    }

    const auto *app_description = esp_app_get_description();
    const speaker_ui_about_info_t info{
        .firmware = (app_description != nullptr) ? app_description->version : "Unknown",
        .os = "FreeRTOS",
        .os_version = tskKERNEL_VERSION_NUMBER,
        .ui = "ESP-Brookesia",
        .ui_version = ui_version.data(),
        .manufacturer = "Espressif",
        .board = "ESP-VoCat V1.0",
        .resolution = resolution.data(),
        .flash = flash.data(),
        .ram_main = "512KB",
        .ram_minor = "16MB",
        .battery_capacity = "650 mAh",
        .chip_name = "ESP32-S3",
        .chip_version = chip_version.data(),
        .chip_mac = chip_mac.data(),
        .chip_features = chip_features.data(),
    };
    if (!speaker_ui_set_about_info(&info)) {
        BROOKESIA_LOGW("Failed to populate Settings About information");
    }
}

void ScreenSpeakerShell::poll_touch_sensor()
{
    const bool user_enabled = speaker_ui_is_touch_sensor_on();
    if (user_enabled != touch_sensor_user_enabled_) {
        touch_sensor_user_enabled_ = user_enabled;
    }

    // The black Idle screen is the current Home placeholder. The future eye
    // screen will replace this content while preserving the same screen role.
    const bool effective_enabled = touch_sensor_user_enabled_ && speaker_ui_is_idle_active();
    if (effective_enabled == touch_sensor_effective_enabled_) {
        return;
    }
    if (TouchSensor::get_instance().set_enabled(effective_enabled)) {
        touch_sensor_effective_enabled_ = effective_enabled;
    } else {
        speaker_ui_set_touch_sensor_on(touch_sensor_user_enabled_);
    }
}

void ScreenSpeakerShell::ensure_control_event_subscriptions()
{
    using AudioPlaybackHelper = service::helper::AudioPlayback;

    if (control_events_subscribed_) {
        return;
    }

    brightness_event_connection_ = DisplayHelper::subscribe_event(
                                       DisplayHelper::EventId::BacklightBrightnessChanged,
    [this](const std::string &, double output_id, const std::string &, double brightness) {
        if (static_cast<uint32_t>(output_id) == display_output_id_) {
            service_brightness_.store(static_cast<int>(brightness));
        }
    });
    volume_event_connection_ = AudioPlaybackHelper::subscribe_event(
                                   AudioPlaybackHelper::EventId::VolumeChanged,
    [this](const std::string &, double volume) {
        service_volume_.store(static_cast<int>(volume));
    });
    mute_event_connection_ = AudioPlaybackHelper::subscribe_event(
                                 AudioPlaybackHelper::EventId::MuteChanged,
    [this](const std::string &, bool muted) {
        service_muted_.store(muted ? 1 : 0);
    });

    control_events_subscribed_ = brightness_event_connection_.connected() &&
                                 volume_event_connection_.connected() && mute_event_connection_.connected();
    if (!control_events_subscribed_) {
        BROOKESIA_LOGE("Failed to subscribe to Speaker UI brightness/volume events");
    }
}

void ScreenSpeakerShell::refresh_control_state()
{
    using AudioPlaybackHelper = service::helper::AudioPlayback;

    auto brightness_result = DisplayHelper::call_function_sync<double>(
                                 DisplayHelper::FunctionId::GetBacklightBrightness,
                                 display_output_id_, service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                             );
    if (brightness_result) {
        service_brightness_.store(static_cast<int>(brightness_result.value()));
    } else {
        BROOKESIA_LOGE("Failed to read Speaker UI brightness: %1%", brightness_result.error());
    }

    auto volume_result = AudioPlaybackHelper::call_function_sync<double>(
                             AudioPlaybackHelper::FunctionId::GetVolume,
                             service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                         );
    if (volume_result) {
        service_volume_.store(static_cast<int>(volume_result.value()));
    } else {
        BROOKESIA_LOGE("Failed to read Speaker UI volume: %1%", volume_result.error());
    }

    auto mute_result = AudioPlaybackHelper::call_function_sync<bool>(
                           AudioPlaybackHelper::FunctionId::GetMute,
                           service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                       );
    if (mute_result) {
        service_muted_.store(mute_result.value() ? 1 : 0);
    } else {
        BROOKESIA_LOGE("Failed to read Speaker UI mute state: %1%", mute_result.error());
    }
}

void ScreenSpeakerShell::sync_service_control_ui()
{
    const int brightness = service_brightness_.load();
    if ((brightness >= 0) && (brightness != synced_service_brightness_)) {
        const int quick_level = nearest_level(brightness, QUICK_BRIGHTNESS_PERCENT);
        if (speaker_ui_set_quick_brightness_level(quick_level)) {
            last_quick_brightness_level_ = quick_level;
        }
        if ((observed_brightness_slider_ != nullptr) && (pending_slider_brightness_ < 0)) {
            lv_slider_set_value(observed_brightness_slider_, brightness, LV_ANIM_OFF);
            last_slider_brightness_ = brightness;
        }
        synced_service_brightness_ = brightness;
    }

    const int volume = service_volume_.load();
    const int muted = service_muted_.load();
    if ((volume >= 0) && (muted >= 0)) {
        const int effective_volume = muted != 0 ? 0 : volume;
        if (effective_volume != synced_effective_volume_) {
            const int quick_level = muted != 0 ? -1 : nearest_level(volume, QUICK_VOLUME_PERCENT);
            if (speaker_ui_set_quick_volume_level(quick_level)) {
                last_quick_volume_level_ = quick_level;
            }
            if ((observed_volume_slider_ != nullptr) && (pending_slider_volume_ < 0)) {
                lv_slider_set_value(observed_volume_slider_, effective_volume, LV_ANIM_OFF);
                last_slider_volume_ = effective_volume;
            }
            synced_effective_volume_ = effective_volume;
        }
    }
}

void ScreenSpeakerShell::factory_reset_clicked_callback(lv_event_t *event)
{
    auto *shell = static_cast<ScreenSpeakerShell *>(lv_event_get_user_data(event));
    if ((shell != nullptr) && !shell->factory_reset_in_progress_) {
        shell->factory_reset_in_progress_ = true;
        shell->perform_factory_reset();
    }
}

void ScreenSpeakerShell::poll_factory_reset()
{
    if (factory_reset_handler_attached_ || !speaker_ui_is_screen_active("restore")) {
        return;
    }

    auto *button_label = find_label(lv_screen_active(), "Restore settings");
    auto *button = button_label == nullptr ? nullptr : lv_obj_get_parent(button_label);
    if ((button == nullptr) || !lv_obj_check_type(button, &lv_button_class)) {
        BROOKESIA_LOGE("Failed to locate Speaker UI factory-reset button");
        return;
    }

    lv_obj_add_event_cb(button, factory_reset_clicked_callback, LV_EVENT_CLICKED, this);
    factory_reset_handler_attached_ = true;
    BROOKESIA_LOGI("Speaker UI factory-reset handler attached");
}

void ScreenSpeakerShell::perform_factory_reset()
{
    using AgentHelper = service::helper::AgentManager;
    using AudioPlaybackHelper = service::helper::AudioPlayback;
    using WifiHelper = service::helper::Wifi;

    BROOKESIA_LOGI("Speaker UI factory reset started");

    if (WifiHelper::is_running()) {
        auto result = WifiHelper::call_function_sync(WifiHelper::FunctionId::ResetData);
        if (!result) {
            BROOKESIA_LOGE("Failed to reset Wi-Fi data: %1%", result.error());
        }
    }
    if (AgentHelper::is_running()) {
        auto result = AgentHelper::call_function_sync(AgentHelper::FunctionId::ResetData);
        if (!result) {
            BROOKESIA_LOGE("Failed to reset agent data: %1%", result.error());
        }
    }
    if (DisplayHelper::is_running()) {
        auto result = DisplayHelper::call_function_sync(DisplayHelper::FunctionId::ResetData, 0);
        if (!result) {
            BROOKESIA_LOGE("Failed to reset display data: %1%", result.error());
        }
    }
    if (AudioPlaybackHelper::is_running()) {
        auto result = AudioPlaybackHelper::call_function_sync(AudioPlaybackHelper::FunctionId::ResetData);
        if (!result) {
            BROOKESIA_LOGE("Failed to reset audio playback data: %1%", result.error());
        }
    }
    if (!WeatherConfig::get_instance().clear()) {
        BROOKESIA_LOGE("Failed to reset weather configuration");
    }

    BROOKESIA_LOGI("Speaker UI factory reset complete; restarting");
    esp_restart();
}

void ScreenSpeakerShell::attach_memory_ui()
{
    auto find_memory_bar = [](const char *label_text) -> lv_obj_t * {
        auto *label = find_label(lv_layer_top(), label_text);
        auto *row = label == nullptr ? nullptr : lv_obj_get_parent(label);
        if ((row == nullptr) || (lv_obj_get_child_count(row) < 2)) {
            return nullptr;
        }
        auto *bar = lv_obj_get_child(row, 1);
        return lv_obj_check_type(bar, &lv_bar_class) ? bar : nullptr;
    };

    memory_internal_bar_ = find_memory_bar("  SRAM:");
    memory_external_bar_ = find_memory_bar("PSRAM:");
    if ((memory_internal_bar_ == nullptr) || (memory_external_bar_ == nullptr)) {
        BROOKESIA_LOGE("Failed to locate Speaker UI memory bars");
    }
}

void ScreenSpeakerShell::poll_memory()
{
    if (++memory_poll_count_ < MEMORY_POLL_TICKS) {
        return;
    }
    memory_poll_count_ = 0;

    if ((memory_internal_bar_ == nullptr) || (memory_external_bar_ == nullptr)) {
        attach_memory_ui();
        if ((memory_internal_bar_ == nullptr) || (memory_external_bar_ == nullptr)) {
            return;
        }
    }

    auto snapshot = lib_utils::MemoryProfiler::get_instance().get_profiling_latest_snapshot();
    if (snapshot == nullptr) {
        return;
    }

    lv_bar_set_value(
        memory_internal_bar_, static_cast<int32_t>(snapshot->memory.internal.used_percent), LV_ANIM_OFF
    );
    lv_bar_set_value(
        memory_external_bar_, static_cast<int32_t>(snapshot->memory.external.used_percent), LV_ANIM_OFF
    );
    if (!memory_snapshot_logged_) {
        BROOKESIA_LOGI(
            "Speaker UI memory bars: SRAM %1%%% used, PSRAM %2%%% used",
            snapshot->memory.internal.used_percent, snapshot->memory.external.used_percent
        );
        memory_snapshot_logged_ = true;
    }
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

    if (slider != observed_brightness_slider_) {
        observed_brightness_slider_ = slider;
        const int service_brightness = service_brightness_.load();
        if (service_brightness >= 0) {
            lv_slider_set_value(slider, service_brightness, LV_ANIM_OFF);
        }
        last_slider_brightness_ = lv_slider_get_value(slider);
        pending_slider_brightness_ = -1;
        slider_stable_poll_count_ = 0;
    } else if (const int brightness = lv_slider_get_value(slider); brightness != last_slider_brightness_) {
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
    } else {
        service_brightness_.store(percent);
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

    if (slider != observed_volume_slider_) {
        observed_volume_slider_ = slider;
        const int service_volume = service_volume_.load();
        const int service_muted = service_muted_.load();
        if ((service_volume >= 0) && (service_muted >= 0)) {
            lv_slider_set_value(slider, service_muted != 0 ? 0 : service_volume, LV_ANIM_OFF);
        }
        last_slider_volume_ = lv_slider_get_value(slider);
        pending_slider_volume_ = -1;
        volume_slider_stable_poll_count_ = 0;
    } else if (const int volume = lv_slider_get_value(slider); volume != last_slider_volume_) {
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
        } else {
            service_muted_.store(1);
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
    } else {
        service_volume_.store(percent);
        service_muted_.store(percent == 0 ? 1 : 0);
    }
}

void ScreenSpeakerShell::poll_wifi()
{
    using WifiHelper = service::helper::Wifi;

    if (!WifiHelper::is_running()) {
        return;
    }

    ensure_wifi_event_subscriptions();

    if (softap_stopped_event_.exchange(false) && speaker_ui_is_screen_active("softap")) {
        BROOKESIA_LOGI("SoftAP provisioning stopped; returning Speaker UI to WLAN");
        speaker_ui_show("wlan");
    }

    const bool enabled = speaker_ui_is_wlan_on();
    const bool wlan_active = speaker_ui_is_screen_active("wlan");
    const bool softap_active = speaker_ui_is_screen_active("softap");

    if (wifi_scan_result_received_.exchange(false)) {
        wifi_scan_after_enable_pending_ = false;
    }

    if (softap_active != softap_screen_was_active_) {
        softap_screen_was_active_ = softap_active;
        if (!softap_active) {
            // Wait for provisioning shutdown before restarting the WLAN scan.
            wifi_scan_after_enable_pending_ = enabled && wlan_active;
        }
        request_softap_provision(softap_active && enabled);
    }
    if (softap_active) {
        update_softap_ui();
    }
    if (enabled != last_wlan_enabled_) {
        last_wlan_enabled_ = enabled;
        wifi_scan_after_enable_pending_ = enabled && wlan_active;
        if (!enabled) {
            wifi_state_ = static_cast<int>(WifiHelper::GeneralState::Max);
            wifi_scan_waiting_for_result_ = false;
            wifi_scan_result_received_ = false;
        }
        {
            std::lock_guard lock(wifi_scan_mutex_);
            wifi_scan_entries_.clear();
        }
        wifi_scan_dirty_ = true;
        request_wifi_enabled(enabled);
    }

    if (++wifi_state_poll_count_ >= WIFI_STATE_POLL_TICKS) {
        wifi_state_poll_count_ = 0;
        request_wifi_state();
    }
    if (wlan_active) {
        attach_wifi_ui_handlers();
        if (!wifi_screen_was_active_ && enabled) {
            wifi_scan_after_enable_pending_ = true;
        }
        const int wifi_state = wifi_state_.load();
        const bool scan_ready =
            (wifi_state == static_cast<int>(WifiHelper::GeneralState::Started)) ||
            (wifi_state == static_cast<int>(WifiHelper::GeneralState::Connected));
        const bool scan_pending = wifi_scan_after_enable_pending_ && !wifi_action_in_flight_.load();
        if (scan_pending && enabled && scan_ready &&
                !softap_action_in_flight_.load() &&
                !wifi_connect_in_flight_.load() &&
                !wifi_scan_request_in_flight_.load() &&
                !wifi_scan_waiting_for_result_.load()) {
            request_wifi_scan();
        }
        update_wifi_scan_ui();
        update_wifi_status_ui();
    } else if (wifi_screen_was_active_) {
        wifi_scan_after_enable_pending_ = false;
        wifi_scan_waiting_for_result_ = false;
        wifi_scan_result_received_ = false;
        request_wifi_scan_stop();
    }
    wifi_screen_was_active_ = wlan_active;

    if (wifi_open_ap_pending_) {
        if (wifi_open_ap_countdown_ > 0) {
            --wifi_open_ap_countdown_;
        } else {
            wifi_open_ap_pending_ = false;
            request_wifi_connect(wifi_selected_ssid_, "");
            speaker_ui_show("wlan");
        }
    }
}

void ScreenSpeakerShell::request_softap_provision(bool enabled)
{
    if (softap_action_in_flight_.exchange(true)) {
        return;
    }

    const bool posted = task_scheduler_->post([this, enabled]() {
        using WifiHelper = service::helper::Wifi;

        const auto function = enabled ? WifiHelper::FunctionId::TriggerSoftApProvisionStart :
                              WifiHelper::FunctionId::TriggerSoftApProvisionStop;
        auto result = WifiHelper::call_function_sync(
                          function, service::helper::Timeout(5000)
                      );
        if (!result) {
            BROOKESIA_LOGE(
                "Failed to %1% Speaker UI SoftAP provisioning: %2%",
                enabled ? "start" : "stop", result.error()
            );
        } else if (enabled) {
            auto params_result = WifiHelper::call_function_sync<boost::json::object>(
                                     WifiHelper::FunctionId::GetSoftApParams,
                                     service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                                 );
            WifiHelper::SoftApParams params;
            if (!params_result || !BROOKESIA_DESCRIBE_FROM_JSON(params_result.value(), params)) {
                BROOKESIA_LOGE("Failed to read Speaker UI SoftAP parameters");
            } else {
                const std::string softap_name = params.ssid;
                {
                    std::lock_guard lock(softap_state_mutex_);
                    softap_ssid_ = std::move(params.ssid);
                    softap_password_ = std::move(params.password);
                }
                softap_ui_dirty_ = true;
                BROOKESIA_LOGI("Speaker UI SoftAP provisioning requested for '%1%'", softap_name);
            }
        }
        softap_action_in_flight_ = false;
    });

    if (!posted) {
        softap_action_in_flight_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI SoftAP provisioning action");
    }
}

void ScreenSpeakerShell::update_softap_ui()
{
    if (!softap_ui_dirty_.exchange(false)) {
        return;
    }

    std::string ssid;
    std::string password;
    {
        std::lock_guard lock(softap_state_mutex_);
        ssid = softap_ssid_;
        password = softap_password_;
    }
    if (!speaker_ui_set_softap_credentials(ssid.c_str(), password.c_str())) {
        BROOKESIA_LOGE("Failed to update Speaker UI SoftAP credentials");
    }
}

void ScreenSpeakerShell::wifi_network_selected_callback(lv_event_t *event)
{
    auto *shell = static_cast<ScreenSpeakerShell *>(lv_event_get_user_data(event));
    auto *row = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if ((shell == nullptr) || (row == nullptr) || (lv_obj_get_child_count(row) == 0)) {
        return;
    }

    auto *label = lv_obj_get_child(row, 0);
    if (!lv_obj_check_type(label, &lv_label_class)) {
        return;
    }
    const char *ssid = lv_label_get_text(label);
    if (ssid == nullptr) {
        return;
    }
    shell->wifi_selected_ssid_ = ssid;

    bool locked = true;
    {
        std::lock_guard lock(shell->wifi_scan_mutex_);
        auto match = std::find_if(
                         shell->wifi_scan_entries_.begin(), shell->wifi_scan_entries_.end(),
        [&](const auto &entry) { return entry.ssid == shell->wifi_selected_ssid_; }
                     );
        if (match != shell->wifi_scan_entries_.end()) {
            locked = match->locked;
        }
    }
    if (!locked) {
        // The unchanged UI opens its password page for every row. Defer until
        // that click dispatch finishes, connect without a password, then return.
        shell->wifi_open_ap_pending_ = true;
        shell->wifi_open_ap_countdown_ = WIFI_OPEN_AP_DELAY_TICKS;
    }
}

void ScreenSpeakerShell::wifi_password_ready_callback(lv_event_t *event)
{
    auto *shell = static_cast<ScreenSpeakerShell *>(lv_event_get_user_data(event));
    auto *keyboard = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if ((shell == nullptr) || (keyboard == nullptr) || shell->wifi_selected_ssid_.empty()) {
        return;
    }

    auto *textarea = lv_keyboard_get_textarea(keyboard);
    const char *password = textarea == nullptr ? nullptr : lv_textarea_get_text(textarea);
    if ((password != nullptr) && (std::string_view(password).size() >= 8)) {
        shell->request_wifi_connect(shell->wifi_selected_ssid_, password);
    }
}

void ScreenSpeakerShell::request_wifi_enabled(bool enabled)
{
    if (wifi_action_in_flight_.exchange(true)) {
        return;
    }

    const bool restart_softap = enabled && softap_screen_was_active_;
    const bool posted = task_scheduler_->post([this, enabled, restart_softap]() {
        using AgentHelper = service::helper::AgentManager;
        using AudioEncoderHelper = service::helper::AudioEncoder<0>;
        using WifiHelper = service::helper::Wifi;

        if (!enabled) {
            // Stop XiaoZhi while the network is still available. If Wi-Fi is
            // stopped first, the transport can force Agent Stop while the
            // encoder fetch task is blocked in its FIFO, racing recorder
            // teardown and tripping the interrupt watchdog.
            auto agent_state_result = AgentHelper::call_function_sync<std::string>(
                                          AgentHelper::FunctionId::GetGeneralState,
                                          service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                                      );
            AgentHelper::GeneralState agent_state = AgentHelper::GeneralState::Max;
            if (agent_state_result) {
                (void)BROOKESIA_DESCRIBE_STR_TO_ENUM(agent_state_result.value(), agent_state);
            }
            if ((agent_state == AgentHelper::GeneralState::Started) ||
                    (agent_state == AgentHelper::GeneralState::Slept)) {
                if (AudioEncoderHelper::is_running()) {
                    auto pause_result = AudioEncoderHelper::call_function_sync(
                                            AudioEncoderHelper::FunctionId::Pause,
                                            service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                                        );
                    if (!pause_result) {
                        BROOKESIA_LOGW("Failed to pause audio encoder before WiFi stop: %1%", pause_result.error());
                    }
                    vTaskDelay(pdMS_TO_TICKS(WIFI_AGENT_AUDIO_DRAIN_MS));
                }

                auto stop_agent_result = AgentHelper::call_function_sync(
                                             AgentHelper::FunctionId::TriggerGeneralAction,
                                             BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Stop),
                                             service::helper::Timeout(5000)
                                         );
                if (!stop_agent_result) {
                    BROOKESIA_LOGW("Failed to stop Agent before WiFi stop: %1%", stop_agent_result.error());
                }
            }

            (void)WifiHelper::call_function_sync(
                WifiHelper::FunctionId::TriggerScanStop,
                service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
            );
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
        if (result && restart_softap) {
            request_softap_provision(true);
        }
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

            if ((parsed_state == static_cast<int>(WifiHelper::GeneralState::Connected)) ||
                    (parsed_state == static_cast<int>(WifiHelper::GeneralState::Connecting))) {
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
            } else {
                std::lock_guard lock(wifi_state_mutex_);
                wifi_ssid_.clear();
            }
            wifi_scan_dirty_ = true;
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

void ScreenSpeakerShell::ensure_wifi_event_subscriptions()
{
    using WifiHelper = service::helper::Wifi;

    if (wifi_events_subscribed_) {
        return;
    }
    wifi_scan_event_connection_ = WifiHelper::subscribe_event(
                                      WifiHelper::EventId::ScanApInfosUpdated,
    [this](const std::string &, const service::EventItemMap & items) {
        auto item = items.find("ApInfos");
        if (item == items.end()) {
            return;
        }
        auto *array = std::get_if<boost::json::array>(&item->second);
        if (array == nullptr) {
            return;
        }

        std::vector<WifiHelper::ScanApInfo> ap_infos;
        if (!BROOKESIA_DESCRIBE_FROM_JSON(*array, ap_infos)) {
            BROOKESIA_LOGE("Failed to parse Speaker UI WiFi scan result");
            return;
        }
        std::sort(ap_infos.begin(), ap_infos.end(), [](const auto &left, const auto &right) {
            return left.rssi > right.rssi;
        });

        std::vector<WifiScanEntry> entries;
        entries.reserve(ap_infos.size());
        for (const auto &ap : ap_infos) {
            if (ap.ssid.empty() || std::any_of(entries.begin(), entries.end(), [&](const auto &entry) {
                    return entry.ssid == ap.ssid;
                })) {
                continue;
            }
            entries.push_back({.ssid = ap.ssid, .locked = ap.is_locked, .rssi = ap.rssi});
        }
        {
            std::lock_guard lock(wifi_scan_mutex_);
            wifi_scan_entries_ = std::move(entries);
        }
        wifi_scan_waiting_for_result_ = false;
        wifi_scan_result_received_ = true;
        wifi_scan_dirty_ = true;
    }
                                  );
    softap_event_connection_ = WifiHelper::subscribe_event(
                                   WifiHelper::EventId::SoftApEventHappened,
    [this](const std::string &, const std::string & event) {
        WifiHelper::SoftApEvent softap_event;
        if (!BROOKESIA_DESCRIBE_STR_TO_ENUM(event, softap_event)) {
            BROOKESIA_LOGE("Failed to parse Speaker UI SoftAP event: %1%", event);
            return;
        }
        if (softap_event == WifiHelper::SoftApEvent::Stopped) {
            softap_stopped_event_ = true;
        }
    }
                               );
    wifi_events_subscribed_ = wifi_scan_event_connection_.connected() && softap_event_connection_.connected();
    if (!wifi_events_subscribed_) {
        BROOKESIA_LOGE("Failed to subscribe to Speaker UI WiFi events");
    }
}

void ScreenSpeakerShell::request_wifi_scan()
{
    if (wifi_scan_request_in_flight_.exchange(true)) {
        return;
    }
    const bool posted = task_scheduler_->post([this]() {
        using WifiHelper = service::helper::Wifi;

        WifiHelper::ScanParams params{
            .ap_count = 20,
            .interval_ms = 15000,
            .timeout_ms = 15000,
        };
        auto json = BROOKESIA_DESCRIBE_TO_JSON(params);
        auto params_result = WifiHelper::call_function_sync(
                                 WifiHelper::FunctionId::SetScanParams, json.as_object(),
                                 service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                             );
        wifi_scan_waiting_for_result_ = true;
        auto scan_result = params_result ? WifiHelper::call_function_sync(
                               WifiHelper::FunctionId::TriggerScanStart,
                               service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                           ) : std::expected<void, std::string>(std::unexpected(params_result.error()));
        if (!scan_result) {
            wifi_scan_waiting_for_result_ = false;
            BROOKESIA_LOGE("Failed to start Speaker UI WiFi scan: %1%", scan_result.error());
        }
        wifi_scan_request_in_flight_ = false;
    });
    if (!posted) {
        wifi_scan_request_in_flight_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI WiFi scan");
    }
}

void ScreenSpeakerShell::request_wifi_scan_stop()
{
    if (wifi_scan_stop_in_flight_.exchange(true)) {
        return;
    }
    const bool posted = task_scheduler_->post([this]() {
        using WifiHelper = service::helper::Wifi;

        auto result = WifiHelper::call_function_sync(
                          WifiHelper::FunctionId::TriggerScanStop,
                          service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                      );
        if (!result) {
            BROOKESIA_LOGE("Failed to stop Speaker UI WiFi scan: %1%", result.error());
        }
        wifi_scan_stop_in_flight_ = false;
    });
    if (!posted) {
        wifi_scan_stop_in_flight_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI WiFi scan stop");
    }
}

void ScreenSpeakerShell::request_wifi_connect(std::string ssid, std::string password)
{
    using WifiHelper = service::helper::Wifi;

    if (ssid.empty() || wifi_connect_in_flight_.exchange(true)) {
        return;
    }
    {
        std::lock_guard lock(wifi_state_mutex_);
        wifi_ssid_ = ssid;
    }
    wifi_state_ = static_cast<int>(WifiHelper::GeneralState::Connecting);

    const bool posted = task_scheduler_->post(
    [this, ssid = std::move(ssid), password = std::move(password)]() {
        using WifiHelper = service::helper::Wifi;

        // Scanning is only needed while choosing a network. Stop it before
        // associating so it cannot keep consuming radio/CPU after success.
        (void)WifiHelper::call_function_sync(
            WifiHelper::FunctionId::TriggerScanStop,
            service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
        );
        auto set_result = WifiHelper::call_function_sync(
                              WifiHelper::FunctionId::SetConnectAp, ssid, password,
                              service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                          );
        auto connect_result = set_result ? WifiHelper::call_function_sync(
                                  WifiHelper::FunctionId::TriggerGeneralAction,
                                  BROOKESIA_DESCRIBE_TO_STR(WifiHelper::GeneralAction::Connect),
                                  service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                              ) : std::expected<void, std::string>(std::unexpected(set_result.error()));
        if (!connect_result) {
            BROOKESIA_LOGE("Failed to connect Speaker UI WiFi to '%1%': %2%", ssid, connect_result.error());
        }
        wifi_connect_in_flight_ = false;
        request_wifi_state();
    }
    );
    if (!posted) {
        wifi_connect_in_flight_ = false;
        BROOKESIA_LOGE("Failed to schedule Speaker UI WiFi connection");
    }
}

void ScreenSpeakerShell::attach_wifi_ui_handlers()
{
    if (wifi_handlers_attached_) {
        return;
    }

    size_t index = 0;
    for (const char *ssid : {"ESP-Lab", "NTT_Office", "Guest"}) {
        auto *label = find_label(lv_screen_active(), ssid);
        if (label == nullptr) {
            return;
        }
        auto *row = lv_obj_get_parent(label);
        if (row == nullptr) {
            return;
        }
        wifi_network_rows_[index] = row;
        wifi_network_labels_[index] = label;
        lv_obj_add_event_cb(row, wifi_network_selected_callback, LV_EVENT_CLICKED, this);

        auto *icons = lv_obj_get_child_count(row) > 1 ? lv_obj_get_child(row, 1) : nullptr;
        if ((icons != nullptr) && (lv_obj_get_child_count(icons) > 1)) {
            wifi_network_lock_icons_[index] = lv_obj_get_child(icons, 1);
        } else if (icons != nullptr) {
            auto *lock_icon = lv_image_create(icons);
            lv_obj_remove_style_all(lock_icon);
            lv_image_set_src(lock_icon, &esp_brookesia_app_icon_wlan_lock_48_48);
            lv_image_set_scale(lock_icon, LV_SCALE_NONE / 2);
            lv_obj_set_size(lock_icon, 24, 24);
            lv_image_set_inner_align(lock_icon, LV_IMAGE_ALIGN_CENTER);
            wifi_network_lock_icons_[index] = lock_icon;
        }
        ++index;
    }

    wifi_keyboard_ = find_first_keyboard(lv_layer_top());
    if (wifi_keyboard_ == nullptr) {
        return;
    }
    lv_obj_add_event_cb(wifi_keyboard_, wifi_password_ready_callback, LV_EVENT_READY, this);
    wifi_handlers_attached_ = true;
    wifi_scan_dirty_ = true;
}

void ScreenSpeakerShell::update_wifi_scan_ui()
{
    if (!wifi_handlers_attached_) {
        return;
    }

    if (wifi_scan_dirty_.exchange(false)) {
        std::vector<WifiScanEntry> entries;
        {
            std::lock_guard lock(wifi_scan_mutex_);
            entries = wifi_scan_entries_;
        }
        std::string connected_ssid;
        if (wifi_state_.load() == static_cast<int>(service::helper::Wifi::GeneralState::Connected)) {
            std::lock_guard lock(wifi_state_mutex_);
            connected_ssid = wifi_ssid_;
        }
        wifi_scan_visible_count_ = 0;

        for (const auto &entry : entries) {
            if (!connected_ssid.empty() && (entry.ssid == connected_ssid)) {
                continue;
            }
            if (wifi_scan_visible_count_ >= wifi_network_rows_.size()) {
                break;
            }
            const size_t index = wifi_scan_visible_count_++;
            auto *row = wifi_network_rows_[index];
            auto *label = wifi_network_labels_[index];
            if ((row == nullptr) || (label == nullptr)) {
                continue;
            }
            lv_label_set_text(label, entry.ssid.c_str());
            lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);

            auto *icons = lv_obj_get_child_count(row) > 1 ? lv_obj_get_child(row, 1) : nullptr;
            auto *signal = (icons != nullptr) && (lv_obj_get_child_count(icons) > 0) ?
                           lv_obj_get_child(icons, 0) : nullptr;
            if (signal != nullptr) {
                lv_image_set_src(signal, wifi_signal_image(entry.rssi));
            }
            auto *lock_icon = wifi_network_lock_icons_[index];
            if (lock_icon != nullptr) {
                if (entry.locked) {
                    lv_obj_remove_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
                    if (icons != nullptr) {
                        lv_obj_set_width(icons, 52);
                    }
                } else {
                    lv_obj_add_flag(lock_icon, LV_OBJ_FLAG_HIDDEN);
                    if (icons != nullptr) {
                        lv_obj_set_width(icons, 24);
                    }
                }
            }
        }
    }

    // The imported UI has a timed demo reveal. Keep only rows backed by a real
    // scan hidden on every poll so that timer cannot resurrect placeholder APs.
    for (size_t index = wifi_scan_visible_count_; index < wifi_network_rows_.size(); ++index) {
        auto *row = wifi_network_rows_[index];
        if (row != nullptr) {
            lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        }
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
            // The imported layout uses a demo SSID. Erase it before the
            // connecting group can become visible.
            lv_label_set_text(wifi_connected_name_label_, "");
            auto *row = lv_obj_get_parent(wifi_connected_name_label_);
            auto *panel = row == nullptr ? nullptr : lv_obj_get_parent(row);
            wifi_connected_group_ = panel == nullptr ? nullptr : lv_obj_get_parent(panel);
        }
    }

    const int state = wifi_state_.load();
    const bool wlan_enabled = speaker_ui_is_wlan_on();
    const bool connected = wlan_enabled &&
                           (state == static_cast<int>(WifiHelper::GeneralState::Connected));
    const bool connecting = wlan_enabled &&
                            (state == static_cast<int>(WifiHelper::GeneralState::Connecting));
    std::string ssid;
    {
        std::lock_guard lock(wifi_state_mutex_);
        ssid = wifi_ssid_;
    }
    const bool show_connection = (connected || connecting) && !ssid.empty();
    if (wifi_connected_group_ != nullptr) {
        if (show_connection) {
            lv_obj_remove_flag(wifi_connected_group_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(wifi_connected_group_, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (wifi_connected_status_label_ != nullptr) {
        lv_label_set_text(wifi_connected_status_label_, connected ? "Connected" : "Connecting...");
    }
    if (show_connection && (wifi_connected_name_label_ != nullptr)) {
        lv_label_set_text(wifi_connected_name_label_, ssid.c_str());
    }
}
