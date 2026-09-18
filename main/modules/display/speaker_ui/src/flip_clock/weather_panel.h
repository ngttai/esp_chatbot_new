#ifndef WEATHER_PANEL_H
#define WEATHER_PANEL_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Compact weather panel for tight/round faces: weather icon, current
 * temperature and condition text, stacked and centered -- no forecast
 * row, no sun/wind row. Ported from HTC_Flip_Clock_with_weather's
 * weather_panel.c (compact variant only -- the full weather_panel_create()
 * layout for rectangular faces wasn't ported, sdl_ui_simulator is
 * round-only). Backed by weather_source.c (mock data only).
 */
lv_obj_t * weather_panel_create_compact(lv_obj_t * parent, int32_t w);

/**
 * Narrow (or widen) an already-created compact panel to a new OUTER width
 * (the panel's own on-screen footprint). Safe to call once, right after
 * creation.
 */
void weather_panel_compact_set_width(lv_obj_t * panel, int32_t outer_w);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*WEATHER_PANEL_H*/
