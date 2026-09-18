/**
 * @file weather_panel.c
 *
 * Compact weather panel (round/tight faces), ported from
 * HTC_Flip_Clock_with_weather's weather_panel.c -- that file's full
 * (rectangular-face) variant and its forecast/sun/wind rows weren't
 * ported, since sdl_ui_simulator only ever targets the round 360x360
 * face. Refreshed on a timer from weather_source.c (mock data only,
 * see that file -- the original's weather_client.c, which this replaces,
 * did real network fetches).
 */

#include "weather_panel.h"
#include "weather_icon.h"
#include "weather_source.h"
#include <stdio.h>

#define REFRESH_PERIOD_MS 12000
/* Semi-transparent instead of LV_OPA_COVER: lets the background scene show
 * through slightly, clipped to the panel's own radius. */
#define PANEL_OPA LV_OPA_70

static lv_obj_t * make_label(lv_obj_t * parent, const lv_font_t * font, lv_color_t color)
{
    lv_obj_t * l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

typedef struct {
    lv_obj_t * city_label;
    lv_obj_t * icon;
    lv_obj_t * temp_label;
    lv_obj_t * condition_label;
} compact_weather_t;

static void compact_refresh(compact_weather_t * cw)
{
    weather_data_t w;
    weather_client_get(&w);
    char buf[16];

    lv_label_set_text(cw->city_label, w.city);
    weather_icon_set_type(cw->icon, w.icon);
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", w.current_temp);
    lv_label_set_text(cw->temp_label, buf);
    lv_label_set_text(cw->condition_label, w.condition);
}

static void compact_refresh_timer_cb(lv_timer_t * t)
{
    compact_weather_t * cw = (compact_weather_t *)lv_timer_get_user_data(t);
    compact_refresh(cw);
}

/* Rounded translucent box around the compact panel's content, so the
 * text isn't sitting directly on the raw background. Content width stays
 * exactly `w`; the box itself is drawn wider (w + 2*COMPACT_PAD_HOR) so
 * the padding doesn't squeeze the content into a narrower column. */
#define COMPACT_PAD_HOR 12
#define COMPACT_PAD_VER 8

lv_obj_t * weather_panel_create_compact(lv_obj_t * parent, int32_t w)
{
    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w + 2 * COMPACT_PAD_HOR, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x20242f), 0);
    lv_obj_set_style_bg_opa(root, PANEL_OPA, 0);
    lv_obj_set_style_radius(root, 12, 0);
    lv_obj_set_style_pad_hor(root, COMPACT_PAD_HOR, 0);
    lv_obj_set_style_pad_ver(root, COMPACT_PAD_VER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    compact_weather_t * cw = lv_malloc_zeroed(sizeof(compact_weather_t));
    lv_obj_set_user_data(root, cw);

    cw->city_label = make_label(root, &lv_font_montserrat_14, lv_color_hex(0xf3f4f6));
    lv_obj_set_width(cw->city_label, w);
    lv_obj_set_style_text_align(cw->city_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(cw->city_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(cw->city_label, LV_ALIGN_TOP_MID, 0, 0);

    cw->icon = weather_icon_create(root, WEATHER_ICON_SUNNY, 40);
    lv_obj_align_to(cw->icon, cw->city_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    cw->temp_label = make_label(root, &lv_font_montserrat_32, lv_color_hex(0xf3f4f6));
    lv_obj_set_width(cw->temp_label, w);
    lv_obj_set_style_text_align(cw->temp_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align_to(cw->temp_label, cw->icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    cw->condition_label = make_label(root, &lv_font_montserrat_14, lv_color_hex(0xf3f4f6));
    lv_obj_set_width(cw->condition_label, w);
    lv_obj_set_style_text_align(cw->condition_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(cw->condition_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align_to(cw->condition_label, cw->temp_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);

    lv_obj_update_layout(root);
    lv_obj_set_height(root, lv_obj_get_height(cw->condition_label) +
                            lv_obj_get_y(cw->condition_label) + COMPACT_PAD_VER);

    compact_refresh(cw);
    lv_timer_create(compact_refresh_timer_cb, REFRESH_PERIOD_MS, cw);

    return root;
}

void weather_panel_compact_set_width(lv_obj_t * panel, int32_t outer_w)
{
    compact_weather_t * cw = (compact_weather_t *)lv_obj_get_user_data(panel);
    int32_t w = outer_w - 2 * COMPACT_PAD_HOR;
    lv_obj_set_width(panel, outer_w);
    lv_obj_set_width(cw->city_label, w);
    lv_obj_set_width(cw->temp_label, w);
    lv_obj_set_width(cw->condition_label, w);

    lv_obj_align(cw->city_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align_to(cw->icon, cw->city_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_obj_align_to(cw->temp_label, cw->icon, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    lv_obj_align_to(cw->condition_label, cw->temp_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
}
