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

} // namespace

bool ScreenSpeakerShell::start(uint32_t display_output_id)
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

    esp_lv_adapter_lock(-1);
    lib_utils::FunctionGuard unlock_guard([]() {
        esp_lv_adapter_unlock();
    });

    speaker_ui_create();
    speaker_ui_set_input(input);

    display_output_id_ = display_output_id;
    last_quick_brightness_level_ = speaker_ui_get_quick_brightness_level();
    last_quick_volume_level_ = speaker_ui_get_quick_volume_level();
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
