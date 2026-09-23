/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "esp_lv_adapter.h"
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
constexpr uint32_t BRIGHTNESS_POLL_PERIOD_MS = 20;
constexpr uint32_t DISPLAY_SERVICE_TIMEOUT_MS = 1000;
constexpr uint8_t SLIDER_STABLE_POLLS = 8;
constexpr std::array<int, 3> QUICK_BRIGHTNESS_PERCENT{{40, 70, 100}};

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
    brightness_timer_ = lv_timer_create(
                            brightness_timer_callback, BRIGHTNESS_POLL_PERIOD_MS, this
                        );
    BROOKESIA_CHECK_NULL_RETURN(brightness_timer_, false, "Failed to create brightness adapter timer");

    started_ = true;

    BROOKESIA_LOGI("Speaker UI shell started on %1%x%2%", source.width(), source.height());
    return true;
}

void ScreenSpeakerShell::brightness_timer_callback(lv_timer_t *timer)
{
    auto *shell = static_cast<ScreenSpeakerShell *>(lv_timer_get_user_data(timer));
    if (shell != nullptr) {
        shell->poll_brightness();
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
