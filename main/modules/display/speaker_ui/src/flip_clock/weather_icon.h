#ifndef WEATHER_ICON_H
#define WEATHER_ICON_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_ICON_SUNNY = 0,
    WEATHER_ICON_PARTLY_CLOUDY,
    WEATHER_ICON_PARTLY_CLOUDY_NIGHT,
    WEATHER_ICON_CLOUDY,
    WEATHER_ICON_RAIN,
    WEATHER_ICON_STORM,
    WEATHER_ICON_SNOW,
    WEATHER_ICON_FOG,
    WEATHER_ICON_CLEAR_NIGHT,
} weather_icon_type_t;

/**
 * Create a small hand-drawn (vector, no bitmap assets) weather icon.
 * Easy to later swap for a real icon font / PNG set on actual hardware.
 */
lv_obj_t * weather_icon_create(lv_obj_t * parent, weather_icon_type_t type, int32_t size);
void weather_icon_set_type(lv_obj_t * icon, weather_icon_type_t type);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*WEATHER_ICON_H*/
