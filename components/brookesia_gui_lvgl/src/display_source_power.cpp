/* SPDX-License-Identifier: Apache-2.0 */
#include "brookesia/gui_lvgl/display_source.hpp"
#include "brookesia/gui_lvgl/backend.hpp"
#include "port/private/threading.hpp"
#include "private/types.hpp"

namespace esp_brookesia::gui::lvgl {
bool DisplaySource::try_set_render_paused(bool paused)
{
    if (!try_lock_thread()) return false;
    auto *disp = display();
    auto *timer = disp ? lv_display_get_refr_timer(disp) : nullptr;
    if (timer) {
        if (paused) {
            lv_timer_pause(timer);
        } else if (lv_timer_get_paused(timer)) {
            // Invalidate all layers; do not pause the worker that reads touch input.
            for (auto *layer : {lv_display_get_screen_active(disp), lv_display_get_layer_bottom(disp),
                               lv_display_get_layer_top(disp), lv_display_get_layer_sys(disp)}) {
                if (layer) lv_obj_invalidate(layer);
            }
            lv_timer_resume(timer);
            lv_timer_ready(timer);
        }
    }
    unlock_thread();
    return timer != nullptr;
}
}
