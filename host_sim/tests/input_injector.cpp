#include "input_injector.hpp"

#include <utility>
#include <vector>

#include "boost/chrono.hpp"
#include "boost/thread.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/service_display/service_display.hpp"
#include "lvgl.h"

namespace host_sim::tests {
namespace {

constexpr int PUMP_STEP_MS = 5;
constexpr int INPUT_SYNC_RETRIES = 100;

} // namespace

InputInjector::InputInjector(std::string output_name, lv_indev_t *input)
    : output_name_(std::move(output_name)), input_(input)
{
}

InputInjector::~InputInjector()
{
    (void)esp_brookesia::service::Display::get_instance().clear_injected_touch(output_name_);
}

bool InputInjector::inject(int32_t x, int32_t y)
{
    auto result = esp_brookesia::service::Display::get_instance().inject_touch(output_name_, x, y);
    if (!result) {
        last_error_ = result.error();
        return false;
    }
    return true;
}

bool InputInjector::press(int32_t x, int32_t y)
{
    return inject(x, y) && synchronize(true, x, y);
}

bool InputInjector::move(int32_t x, int32_t y)
{
    return inject(x, y) && synchronize(true, x, y);
}

bool InputInjector::release()
{
    auto result = esp_brookesia::service::Display::get_instance().inject_touch(
        output_name_, std::vector<esp_brookesia::service::Display::TouchPoint>{}
    );
    if (!result) {
        last_error_ = result.error();
        return false;
    }
    return synchronize(false, 0, 0);
}

bool InputInjector::click(int32_t x, int32_t y)
{
    if (!press(x, y)) {
        return false;
    }
    pump(8);
    if (!release()) {
        return false;
    }
    pump(30);
    return true;
}

void InputInjector::pump(int count) const
{
    boost::this_thread::sleep_for(boost::chrono::milliseconds(count * PUMP_STEP_MS));
}

bool InputInjector::synchronize(bool pressed, int32_t x, int32_t y)
{
    if (input_ == nullptr) {
        last_error_ = "LVGL input is null";
        return false;
    }

    for (int attempt = 0; attempt < INPUT_SYNC_RETRIES; ++attempt) {
        esp_brookesia::gui::lvgl::lock_thread();
        lv_timer_handler();
        const bool state_matches =
            lv_indev_get_state(input_) == (pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED);
        lv_point_t point{};
        if (pressed) lv_indev_get_point(input_, &point);
        esp_brookesia::gui::lvgl::unlock_thread();

        if (state_matches && (!pressed || (point.x == x && point.y == y))) {
            return true;
        }
        boost::this_thread::sleep_for(boost::chrono::milliseconds(1));
    }

    last_error_ = pressed ? "LVGL did not observe the injected pressed point"
                          : "LVGL did not observe the injected release";
    return false;
}

const std::string &InputInjector::last_error() const
{
    return last_error_;
}

} // namespace host_sim::tests
