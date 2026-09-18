#pragma once

#include <cstdint>

#include "lvgl.h"

namespace host_sim {

class BrightnessAdapter {
public:
    bool start(uint32_t backlight_output_id);
    void stop();

private:
    static void timer_callback(lv_timer_t *timer);
    void poll();
    void apply(int percent);

    uint32_t output_id_ = 0;
    lv_timer_t *timer_ = nullptr;
    lv_obj_t *observed_slider_ = nullptr;
    int last_quick_level_ = 0;
    int last_slider_value_ = -1;
};

} // namespace host_sim
