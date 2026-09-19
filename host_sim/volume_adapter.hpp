#pragma once

#include "lvgl.h"

namespace host_sim {

class VolumeAdapter {
public:
    bool start();
    void stop();

private:
    static void timer_callback(lv_timer_t *timer);
    void poll();
    bool read_service_state();
    bool apply_quick_level(int level);
    bool apply_slider_value(int value);
    void synchronize_quick_level();
    void synchronize_slider();

    lv_timer_t *timer_ = nullptr;
    lv_obj_t *sound_slider_ = nullptr;
    int current_volume_ = 0;
    bool current_mute_ = false;
    int last_quick_level_ = -1;
    int last_slider_value_ = -1;
    bool quick_synchronized_ = false;
};

} // namespace host_sim
