#pragma once

#include "brookesia/hal_interface/interfaces/power/battery.hpp"
#include "lvgl.h"

namespace host_sim {

class PowerAdapter {
public:
    bool start();
    void stop();

private:
    static void timer_callback(lv_timer_t *timer);

    void poll();
    bool locate_widgets();

    esp_brookesia::hal::InterfaceHandle<esp_brookesia::hal::power::BatteryIface> battery_;
    lv_timer_t *timer_ = nullptr;
    lv_obj_t *percentage_label_ = nullptr;
    lv_obj_t *charge_icon_ = nullptr;
};

} // namespace host_sim
