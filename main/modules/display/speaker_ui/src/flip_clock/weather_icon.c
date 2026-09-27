/**
 * @file weather_icon.c
 *
 * Weather icon widget. Uses a pre-rendered embedded image per (type, size)
 * when one exists (see images/weather_icon_images.h -- 9 states x 3 sizes,
 * alpha-only bitmaps recolored at runtime via
 * lv_obj_set_style_image_recolor()); WEATHER_ICON_PARTLY_CLOUDY and _NIGHT
 * are "combo" icons composed from two layered images (sun/moon + cloud).
 * Falls back to a hand-drawn vector icon (sun, cloud, rain, storm, snow,
 * fog, partly-cloudy, clear night) for any (type, size) with no matching
 * embedded image.
 */

#include "weather_icon.h"
#include <stddef.h>
#include <stdbool.h>

#define COL_SUN        lv_color_hex(0xffc94d)
#define COL_SUN_CORE   lv_color_hex(0xffe08a)
#define COL_CLOUD      lv_color_hex(0xd7dde5)
#define COL_CLOUD_DARK lv_color_hex(0x8b93a1)
#define COL_RAIN       lv_color_hex(0x5aa9e6)
#define COL_SNOW       lv_color_hex(0xffffff)
#define COL_BOLT       lv_color_hex(0xffd23f)
#define COL_MOON       lv_color_hex(0xdfe6f0)
#define COL_FOG        lv_color_hex(0xb7bfc9)

static void draw_circle(lv_layer_t * layer, lv_point_t center, int32_t r, lv_color_t color, lv_opa_t opa)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t area = { center.x - r, center.y - r, center.x + r, center.y + r };
    lv_draw_rect(layer, &dsc, &area);
}

static void draw_cloud(lv_layer_t * layer, const lv_area_t * coords, int32_t size, lv_color_t color, int32_t y_off)
{
    int32_t cx = (coords->x1 + coords->x2) / 2;
    int32_t cy = (coords->y1 + coords->y2) / 2 + y_off;
    int32_t r = size / 5;

    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = color;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = r;
    lv_area_t base = { cx - (int32_t)(size * 0.42f), cy - r / 2,
                        cx + (int32_t)(size * 0.42f), cy + r + r / 2 };
    lv_draw_rect(layer, &rdsc, &base);

    draw_circle(layer, (lv_point_t) { cx - (int32_t)(size * 0.22f), cy - r / 3 }, (int32_t)(r * 1.15f), color, LV_OPA_COVER);
    draw_circle(layer, (lv_point_t) { cx + (int32_t)(size * 0.05f), cy - r },        (int32_t)(r * 1.5f),  color, LV_OPA_COVER);
    draw_circle(layer, (lv_point_t) { cx + (int32_t)(size * 0.30f), cy - r / 4 },    (int32_t)(r * 1.05f), color, LV_OPA_COVER);
}

static void draw_sun(lv_layer_t * layer, const lv_area_t * coords, int32_t size, int32_t x_off, int32_t y_off)
{
    int32_t cx = (coords->x1 + coords->x2) / 2 + x_off;
    int32_t cy = (coords->y1 + coords->y2) / 2 + y_off;
    int32_t r = size / 3;

    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = COL_SUN;
    ldsc.width = LV_MAX(2, size / 22);
    ldsc.round_end = 1;
    ldsc.round_start = 1;
    for(int i = 0; i < 8; i++) {
        int32_t r1 = (int32_t)(r * 1.35f);
        int32_t r2 = (int32_t)(r * 1.85f);
        ldsc.p1.x = cx + (int32_t)(r1 * lv_trigo_cos(i * 45) / 32767.0f);
        ldsc.p1.y = cy + (int32_t)(r1 * lv_trigo_sin(i * 45) / 32767.0f);
        ldsc.p2.x = cx + (int32_t)(r2 * lv_trigo_cos(i * 45) / 32767.0f);
        ldsc.p2.y = cy + (int32_t)(r2 * lv_trigo_sin(i * 45) / 32767.0f);
        lv_draw_line(layer, &ldsc);
    }
    draw_circle(layer, (lv_point_t) { cx, cy }, r, COL_SUN, LV_OPA_COVER);
    draw_circle(layer, (lv_point_t) { cx, cy }, (int32_t)(r * 0.65f), COL_SUN_CORE, LV_OPA_COVER);
}

static void draw_moon(lv_layer_t * layer, const lv_area_t * coords, int32_t size, int32_t x_off, int32_t y_off)
{
    int32_t cx = (coords->x1 + coords->x2) / 2 + x_off;
    int32_t cy = (coords->y1 + coords->y2) / 2 + y_off;
    int32_t r = size / 3;

    /* Always a full disc, not a crescent -- there's no moon-phase
     * calculation behind this icon set, so a full disc just reads as
     * "night" without implying a specific phase. */
    draw_circle(layer, (lv_point_t) { cx, cy }, r, COL_MOON, LV_OPA_COVER);
}

static void draw_rain(lv_layer_t * layer, const lv_area_t * coords, int32_t size)
{
    int32_t cx = (coords->x1 + coords->x2) / 2;
    int32_t cy = (coords->y1 + coords->y2) / 2 + size / 5;
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = COL_RAIN;
    ldsc.width = LV_MAX(2, size / 16);
    ldsc.round_start = 1;
    ldsc.round_end = 1;
    int32_t drop = size / 5;
    for(int i = -1; i <= 1; i++) {
        int32_t x = cx + i * (size / 5);
        ldsc.p1.x = x - drop / 3; ldsc.p1.y = cy;
        ldsc.p2.x = x + drop / 3; ldsc.p2.y = cy + drop;
        lv_draw_line(layer, &ldsc);
    }
}

static void draw_snow(lv_layer_t * layer, const lv_area_t * coords, int32_t size)
{
    int32_t cx = (coords->x1 + coords->x2) / 2;
    int32_t cy = (coords->y1 + coords->y2) / 2 + size / 5;
    for(int i = -1; i <= 1; i++) {
        draw_circle(layer, (lv_point_t) { cx + i * (size / 5), cy + (i == 0 ? size / 10 : 0) },
                    LV_MAX(2, size / 18), COL_SNOW, LV_OPA_COVER);
    }
}

static void draw_bolt(lv_layer_t * layer, const lv_area_t * coords, int32_t size)
{
    int32_t cx = (coords->x1 + coords->x2) / 2;
    int32_t cy = (coords->y1 + coords->y2) / 2 + size / 6;
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = COL_BOLT;
    ldsc.width = LV_MAX(2, size / 14);
    ldsc.round_start = 1;
    ldsc.round_end = 1;

    ldsc.p1.x = cx + size / 14; ldsc.p1.y = cy - size / 10;
    ldsc.p2.x = cx - size / 12; ldsc.p2.y = cy + size / 12;
    lv_draw_line(layer, &ldsc);

    ldsc.p1.x = cx - size / 12; ldsc.p1.y = cy + size / 12;
    ldsc.p2.x = cx + size / 20; ldsc.p2.y = cy + size / 12;
    lv_draw_line(layer, &ldsc);

    ldsc.p1.x = cx + size / 20; ldsc.p1.y = cy + size / 12;
    ldsc.p2.x = cx - size / 16; ldsc.p2.y = cy + size / 3;
    lv_draw_line(layer, &ldsc);
}

static void draw_fog(lv_layer_t * layer, const lv_area_t * coords, int32_t size)
{
    int32_t cx = (coords->x1 + coords->x2) / 2;
    int32_t cy = (coords->y1 + coords->y2) / 2;
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = COL_FOG;
    ldsc.width = LV_MAX(3, size / 12);
    ldsc.round_start = 1;
    ldsc.round_end = 1;
    for(int i = 0; i < 3; i++) {
        int32_t y = cy - size / 5 + i * (size / 5);
        int32_t half = (i == 1) ? size / 3 : size / 4;
        ldsc.p1.x = cx - half; ldsc.p1.y = y;
        ldsc.p2.x = cx + half; ldsc.p2.y = y;
        lv_draw_line(layer, &ldsc);
    }
}

/* Per-object state. */
typedef struct {
    weather_icon_type_t type;
    int32_t size;
    lv_obj_t * img;   /* lazily-created lv_image child; NULL until first use, hidden (not deleted) when no image matches. For a combo icon (see below), this is the sun/moon layer. */
    lv_obj_t * img2;  /* second lv_image child, only used by combo icons (the cloud layer drawn on top of img) -- NULL otherwise */
} weather_icon_t;

/* sdl_ui_simulator port: no embedded weather-icon bitmaps (see the
 * dropped images/weather_icon_images.h include above) -- every icon
 * always renders via the hand-drawn vector draw callback below.
 * apply_type()'s combo-icon path already falls back gracefully when
 * this returns NULL (that's the "missing base image" branch it was
 * written for), so no other change is needed. */
static const lv_image_dsc_t * find_icon_image(weather_icon_type_t type, int32_t size)
{
    (void)type;
    (void)size;
    return NULL;
}

/* WEATHER_ICON_PARTLY_CLOUDY and _NIGHT are "combo" icons: composed at
 * runtime from two single-shape images already embedded (a small sun/moon
 * layer plus a cloud layer on top), each recolored independently, so each
 * layer can have its own tint. */
typedef struct {
    weather_icon_type_t type;        /* the combo state, e.g. WEATHER_ICON_PARTLY_CLOUDY */
    weather_icon_type_t base_type;   /* which single-shape icon is the sun/moon layer underneath the cloud */
} combo_icon_entry_t;

static const combo_icon_entry_t combo_icons[] = {
    { WEATHER_ICON_PARTLY_CLOUDY,       WEATHER_ICON_SUNNY },
    { WEATHER_ICON_PARTLY_CLOUDY_NIGHT, WEATHER_ICON_CLEAR_NIGHT },
};

static bool find_combo_base(weather_icon_type_t type, weather_icon_type_t * base_type)
{
    for(size_t i = 0; i < sizeof(combo_icons) / sizeof(combo_icons[0]); i++) {
        if(combo_icons[i].type == type) {
            *base_type = combo_icons[i].base_type;
            return true;
        }
    }
    return false;
}

/* Layout fractions (of the icon's own size) for the two composited layers.
 * Kept inside [0, size] on both axes at every size in use (72/40/38px) so
 * neither layer clips against the icon container's edge. */
#define COMBO_BASE_FRAC   0.55f   /* sun/moon layer size, as a fraction of the icon size */
#define COMBO_BASE_X      0.04f
#define COMBO_BASE_Y      0.04f
#define COMBO_CLOUD_FRAC  0.74f   /* cloud layer size, as a fraction of the icon size */
#define COMBO_CLOUD_X     0.22f
#define COMBO_CLOUD_Y     0.26f

/* Places one composited layer: sets its source/tint, then uses LVGL's
 * image zoom to shrink the native source bitmap down to
 * frac*container_size. Pivot is forced to the image's top-left corner so
 * the zoom anchors there, making the following lv_obj_set_pos() place the
 * shrunk image's top-left exactly at (x_frac, y_frac). */
static void position_combo_layer(lv_obj_t * img, const lv_image_dsc_t * dsc, lv_color_t tint,
                                  int32_t container_size, float frac, float x_frac, float y_frac)
{
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_set_style_image_recolor(img, tint, 0);
    lv_image_set_pivot(img, 0, 0);
    lv_image_set_scale(img, (uint32_t)(frac * 256.0f));
    lv_obj_set_pos(img, (int32_t)(x_frac * container_size), (int32_t)(y_frac * container_size));
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
}

/* Per-type tint. The embedded images are alpha-only (no baked-in color),
 * so this is what gives each icon its color at runtime. */
static lv_color_t icon_tint_color(weather_icon_type_t type)
{
    switch(type) {
        case WEATHER_ICON_SUNNY:               return COL_SUN;
        case WEATHER_ICON_PARTLY_CLOUDY:       return COL_SUN;
        case WEATHER_ICON_PARTLY_CLOUDY_NIGHT: return COL_MOON;
        case WEATHER_ICON_CLOUDY:              return COL_CLOUD_DARK;
        case WEATHER_ICON_RAIN:                return COL_RAIN;
        case WEATHER_ICON_STORM:               return COL_BOLT;
        case WEATHER_ICON_SNOW:                return COL_SNOW;
        case WEATHER_ICON_FOG:                 return COL_FOG;
        case WEATHER_ICON_CLEAR_NIGHT:          return COL_MOON;
        default:                               return COL_SUN;
    }
}

static lv_obj_t * ensure_img_child(lv_obj_t * parent, lv_obj_t ** slot)
{
    if(*slot == NULL) {
        *slot = lv_image_create(parent);
        lv_obj_remove_flag(*slot, LV_OBJ_FLAG_SCROLLABLE);
    }
    return *slot;
}

/* Lazily creates/updates the lv_image child(ren) when matching embedded
 * image(s) exist for (wi->type, wi->size); hides them (rather than
 * deleting) when they don't, so the vector draw fallback takes over. */
static void apply_type(lv_obj_t * obj, weather_icon_t * wi)
{
    weather_icon_type_t base_type;
    if(find_combo_base(wi->type, &base_type)) {
        const lv_image_dsc_t * base_dsc = find_icon_image(base_type, wi->size);
        const lv_image_dsc_t * cloud_dsc = find_icon_image(WEATHER_ICON_CLOUDY, wi->size);
        if(base_dsc != NULL && cloud_dsc != NULL) {
            ensure_img_child(obj, &wi->img);
            ensure_img_child(obj, &wi->img2);
            position_combo_layer(wi->img, base_dsc, icon_tint_color(base_type), wi->size,
                                  COMBO_BASE_FRAC, COMBO_BASE_X, COMBO_BASE_Y);
            /* img2 (the cloud) is created after img, so it's already the
             * later sibling LVGL draws on top -- no explicit z-order call
             * needed for it to layer over the sun/moon. */
            position_combo_layer(wi->img2, cloud_dsc, COL_CLOUD_DARK, wi->size,
                                  COMBO_CLOUD_FRAC, COMBO_CLOUD_X, COMBO_CLOUD_Y);
            lv_obj_invalidate(obj);
            return;
        }
        /* Missing base image -- hide any combo layers and fall through to
         * the vector fallback below. */
        if(wi->img != NULL) lv_obj_add_flag(wi->img, LV_OBJ_FLAG_HIDDEN);
        if(wi->img2 != NULL) lv_obj_add_flag(wi->img2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_invalidate(obj);
        return;
    }

    /* Not a combo type -- a single-shape icon has (at most) one image
     * layer, so make sure any combo-icon img2 left over from a previous
     * weather_icon_set_type() call is hidden. */
    if(wi->img2 != NULL) lv_obj_add_flag(wi->img2, LV_OBJ_FLAG_HIDDEN);

    const lv_image_dsc_t * img_dsc = find_icon_image(wi->type, wi->size);
    if(img_dsc != NULL) {
        ensure_img_child(obj, &wi->img);
        /* Reset any combo-layer transform left on this lv_image object
         * before reusing it as a plain, un-scaled, centered icon. */
        lv_image_set_scale(wi->img, LV_SCALE_NONE);
        lv_image_set_pivot(wi->img, wi->size / 2, wi->size / 2);
        lv_image_set_src(wi->img, img_dsc);
        lv_obj_set_style_image_recolor_opa(wi->img, LV_OPA_COVER, 0);
        lv_obj_set_style_image_recolor(wi->img, icon_tint_color(wi->type), 0);
        lv_obj_center(wi->img);
        lv_obj_remove_flag(wi->img, LV_OBJ_FLAG_HIDDEN);
    }
    else if(wi->img != NULL) {
        lv_obj_add_flag(wi->img, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_invalidate(obj);
}

static void icon_delete_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    weather_icon_t * wi = (weather_icon_t *)lv_obj_get_user_data(obj);
    lv_free(wi);
}

static void icon_draw_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    weather_icon_t * wi = (weather_icon_t *)lv_obj_get_user_data(obj);

    /* If a real image is active for this (type, size), the lv_image child
     * handles drawing -- only run the hand-drawn fallback otherwise. */
    if(wi->img != NULL && !lv_obj_has_flag(wi->img, LV_OBJ_FLAG_HIDDEN)) return;

    weather_icon_type_t type = wi->type;
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    int32_t size = lv_area_get_width(&coords);

    switch(type) {
        case WEATHER_ICON_SUNNY:
            draw_sun(layer, &coords, size, 0, 0);
            break;
        case WEATHER_ICON_CLEAR_NIGHT:
            draw_moon(layer, &coords, size, 0, 0);
            break;
        case WEATHER_ICON_PARTLY_CLOUDY:
            draw_sun(layer, &coords, size, -size / 8, -size / 8);
            draw_cloud(layer, &coords, (int32_t)(size * 0.9f), COL_CLOUD, size / 6);
            break;
        case WEATHER_ICON_PARTLY_CLOUDY_NIGHT:
            draw_moon(layer, &coords, size, -size / 8, -size / 8);
            /* COL_CLOUD_DARK, not the lighter COL_CLOUD the day variant
             * uses: gives the moon contrast against the cloud. */
            draw_cloud(layer, &coords, (int32_t)(size * 0.9f), COL_CLOUD_DARK, size / 6);
            break;
        case WEATHER_ICON_CLOUDY:
            draw_cloud(layer, &coords, size, COL_CLOUD_DARK, 0);
            break;
        case WEATHER_ICON_RAIN:
            draw_cloud(layer, &coords, size, COL_CLOUD_DARK, -size / 8);
            draw_rain(layer, &coords, size);
            break;
        case WEATHER_ICON_STORM:
            draw_cloud(layer, &coords, size, COL_CLOUD_DARK, -size / 8);
            draw_bolt(layer, &coords, size);
            break;
        case WEATHER_ICON_SNOW:
            draw_cloud(layer, &coords, size, COL_CLOUD, -size / 8);
            draw_snow(layer, &coords, size);
            break;
        case WEATHER_ICON_FOG:
            draw_fog(layer, &coords, size);
            break;
    }
}

lv_obj_t * weather_icon_create(lv_obj_t * parent, weather_icon_type_t type, int32_t size)
{
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    weather_icon_t * wi = lv_malloc(sizeof(weather_icon_t));
    LV_ASSERT_MALLOC(wi);
    wi->type = type;
    wi->size = size;
    wi->img = NULL;
    wi->img2 = NULL;

    lv_obj_set_user_data(obj, wi);
    lv_obj_add_event_cb(obj, icon_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, icon_delete_event_cb, LV_EVENT_DELETE, NULL);

    apply_type(obj, wi);
    return obj;
}

void weather_icon_set_type(lv_obj_t * icon, weather_icon_type_t type)
{
    weather_icon_t * wi = (weather_icon_t *)lv_obj_get_user_data(icon);
    wi->type = type;
    apply_type(icon, wi);
}
