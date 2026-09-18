#include "brightness_adapter.hpp"

#include <array>
#include <cstdio>

#include "brookesia/service_helper.hpp"

extern "C" {
#include "speaker_ui.h"
}

namespace host_sim {
namespace {

using DisplayHelper = esp_brookesia::service::helper::Display;
constexpr uint32_t SERVICE_TIMEOUT_MS = 1000;
constexpr uint32_t POLL_PERIOD_MS = 20;
constexpr std::array<int, 3> QUICK_LEVEL_PERCENT{{40, 70, 100}};

lv_obj_t *find_first_slider(lv_obj_t *object)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_slider_class)) return object;

    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *slider = find_first_slider(lv_obj_get_child(object, index))) {
            return slider;
        }
    }
    return nullptr;
}

} // namespace

bool BrightnessAdapter::start(uint32_t backlight_output_id)
{
    if (timer_ != nullptr) return true;

    output_id_ = backlight_output_id;
    last_quick_level_ = speaker_ui_get_quick_brightness_level();
    timer_ = lv_timer_create(timer_callback, POLL_PERIOD_MS, this);
    return timer_ != nullptr;
}

void BrightnessAdapter::stop()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    observed_slider_ = nullptr;
    last_slider_value_ = -1;
}

void BrightnessAdapter::timer_callback(lv_timer_t *timer)
{
    auto *adapter = static_cast<BrightnessAdapter *>(lv_timer_get_user_data(timer));
    if (adapter != nullptr) adapter->poll();
}

void BrightnessAdapter::poll()
{
    const int quick_level = speaker_ui_get_quick_brightness_level();
    if (quick_level != last_quick_level_) {
        last_quick_level_ = quick_level;
        if (quick_level >= 0 && quick_level < static_cast<int>(QUICK_LEVEL_PERCENT.size())) {
            apply(QUICK_LEVEL_PERCENT[quick_level]);
        }
    }

    if (!speaker_ui_is_screen_active("display")) {
        observed_slider_ = nullptr;
        last_slider_value_ = -1;
        return;
    }

    auto *slider = find_first_slider(lv_screen_active());
    if (slider == nullptr) return;

    const int value = lv_slider_get_value(slider);
    if (slider != observed_slider_) {
        observed_slider_ = slider;
        last_slider_value_ = value;
    } else if (value != last_slider_value_) {
        last_slider_value_ = value;
        apply(value);
    }
}

void BrightnessAdapter::apply(int percent)
{
    auto result = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetBacklightBrightness,
        static_cast<double>(output_id_),
        static_cast<double>(percent),
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!result) {
        std::fprintf(stderr, "host_sim: could not set simulated brightness to %d%%: %s\n",
                     percent, result.error().c_str());
    }
}

} // namespace host_sim
