/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "esp_lv_adapter.h"
#include "private/utils.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils.hpp"

extern "C" {
#include "speaker_ui.h"
}

#include "speaker_shell.hpp"

using namespace esp_brookesia;
using LvglDisplaySource = gui::lvgl::DisplaySource;

namespace {

constexpr uint32_t SPEAKER_UI_WIDTH = 360;
constexpr uint32_t SPEAKER_UI_HEIGHT = 360;

} // namespace

bool ScreenSpeakerShell::start()
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
    started_ = true;

    BROOKESIA_LOGI("Speaker UI shell started on %1%x%2%", source.width(), source.height());
    return true;
}

