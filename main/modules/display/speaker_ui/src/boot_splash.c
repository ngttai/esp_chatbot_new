/**
 * @file boot_splash.c
 *
 * A brief animated boot splash shown as a full-screen overlay on
 * lv_layer_top(), on top of whatever screen speaker_ui_create() already
 * loaded: a wavy "scribble" line progressively reveals itself (see
 * build_scribble_points()), then a "HELLO! / EVERYONE" greeting fades in
 * underneath it, holds briefly, then the overlay deletes itself, exposing
 * the screen underneath. Not a separate screen/state, just a temporary
 * overlay. Ported near-verbatim from HTC_Flip_Clock_with_weather's
 * boot_splash.c -- the display there is round too, so the round-profile
 * sizes/fonts carry over directly; the small differences are that this
 * project's display size is a fixed constant rather than a runtime
 * profile, its greeting uses this project's own font family instead of
 * LVGL's built-in Montserrat (for visual consistency with every other
 * screen), and dismissing it needs no app_manager_go_home()-style call --
 * speaker_ui_create() already loaded the real starting screen underneath,
 * so deleting the overlay is enough to reveal it.
 *
 * Animation is driven entirely by hand-rolled lv_timer steppers (not
 * lv_anim_t), advancing plain property values (revealed point count,
 * text opacity) on each tick -- matching the reference project's own
 * choice here, made to avoid a known LVGL/SDL CPU-pegging issue with
 * lv_anim_t driving a non-identity transform (see this simulator's
 * clip_corner CPU investigation for the same underlying mechanism).
 */

#include "boot_splash.h"
#include "lvgl.h"
#include <math.h>

#define SIZE 360   /* round display size -- mirrors speaker_ui.c's own SIZE */

#define BG_COLOR       lv_color_hex(0x000000)
#define TEXT_COLOR     lv_color_hex(0xf3f4f6)
#define SCRIBBLE_COLOR lv_color_hex(0xf3f4f6)

LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_26)
#define GREETING_FONT_BIG   esp_brookesia_font_maison_neue_book_26
#define SCRIBBLE_W 130
#define SCRIBBLE_H 34

#define BLOCK_GAP 14   /* vertical gap between the scribble and the text block */

#define SCRIBBLE_POINTS   22
#define SCRIBBLE_LINE_W   3
#define SCRIBBLE_STAGE_MS 650                                    /* total time to fully reveal the scribble */
#define SCRIBBLE_STEP_MS  (SCRIBBLE_STAGE_MS / (SCRIBBLE_POINTS - 1))

#define FADE_STEPS    10
#define FADE_STAGE_MS 220
#define FADE_STEP_MS  (FADE_STAGE_MS / FADE_STEPS)

#define HOLD_MS 850   /* how long the fully-revealed splash stays up before it's dismissed */

#define PI_F 3.14159265358979f

typedef struct {
    lv_obj_t * root;       /* full-screen overlay; deleting this tears down the whole splash */
    lv_obj_t * scribble;   /* small obj whose LV_EVENT_DRAW_MAIN draws the revealed prefix of pts[] */
    lv_obj_t * text_box;   /* container for the two greeting labels, faded in via opa */
    lv_point_t pts[SCRIBBLE_POINTS];
    int revealed;    /* how many of pts[] are currently revealed (>=2 draws at least one segment) */
    int fade_step;   /* 0..FADE_STEPS, drives text_box's opa ramp */
} boot_splash_t;

static bool splash_active;

/* Builds a wavy "pen scribble" line whose amplitude tapers to ~0 at both
 * ends, sampled at SCRIBBLE_POINTS points across `w`/`h`. A formula
 * rather than fixed coordinates, so it scales to any w/h. */
static void build_scribble_points(lv_point_t * pts, int32_t w, int32_t h)
{
    const float cycles = 1.6f;
    for(int i = 0; i < SCRIBBLE_POINTS; i++) {
        float t = (float)i / (float)(SCRIBBLE_POINTS - 1);   /* 0..1 across the width */
        float envelope = sinf(PI_F * t);                      /* 0 at both ends, 1 at the middle */
        float wave = sinf(t * cycles * 2.0f * PI_F);
        pts[i].x = (int32_t)(t * w);
        pts[i].y = (int32_t)((h / 2.0f) - envelope * wave * (h / 2.0f - SCRIBBLE_LINE_W));
    }
}

static void scribble_draw_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    lv_layer_t * layer = lv_event_get_layer(e);
    boot_splash_t * bs = (boot_splash_t *)lv_obj_get_user_data(obj);
    if(bs->revealed < 2) return;

    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);

    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = SCRIBBLE_COLOR;
    dsc.width = SCRIBBLE_LINE_W;
    dsc.round_start = 1;
    dsc.round_end = 1;
    dsc.opa = LV_OPA_COVER;

    for(int i = 0; i < bs->revealed - 1; i++) {
        dsc.p1.x = coords.x1 + bs->pts[i].x;
        dsc.p1.y = coords.y1 + bs->pts[i].y;
        dsc.p2.x = coords.x1 + bs->pts[i + 1].x;
        dsc.p2.y = coords.y1 + bs->pts[i + 1].y;
        lv_draw_line(layer, &dsc);
    }
}

static void root_delete_event_cb(lv_event_t * e)
{
    boot_splash_t * bs = (boot_splash_t *)lv_obj_get_user_data(lv_event_get_target(e));
    splash_active = false;
    lv_free(bs);
}

static void dismiss_timer_cb(lv_timer_t * t)
{
    boot_splash_t * bs = (boot_splash_t *)lv_timer_get_user_data(t);
    /* The screen underneath (Idle, loaded by speaker_ui_create() before this
     * overlay was ever created) is already the one to reveal -- unlike the
     * reference project's app_manager-based apps, there's no separate
     * "reveal the default app for the first time" step to trigger here. */
    lv_obj_del(bs->root);   /* fires root_delete_event_cb above, which frees bs */
    lv_timer_del(t);
}

static void fade_step_timer_cb(lv_timer_t * t)
{
    boot_splash_t * bs = (boot_splash_t *)lv_timer_get_user_data(t);
    bs->fade_step++;

    lv_opa_t opa = (lv_opa_t)((bs->fade_step * 255) / FADE_STEPS);
    lv_obj_set_style_opa(bs->text_box, opa, 0);

    if(bs->fade_step >= FADE_STEPS) {
        lv_timer_del(t);
        lv_timer_create(dismiss_timer_cb, HOLD_MS, bs);
    }
}

static void reveal_step_timer_cb(lv_timer_t * t)
{
    boot_splash_t * bs = (boot_splash_t *)lv_timer_get_user_data(t);
    bs->revealed++;
    lv_obj_invalidate(bs->scribble);

    if(bs->revealed >= SCRIBBLE_POINTS) {
        lv_timer_del(t);
        lv_obj_remove_flag(bs->text_box, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create(fade_step_timer_cb, FADE_STEP_MS > 0 ? FADE_STEP_MS : 1, bs);
    }
}

static void begin_timer_cb(lv_timer_t * t)
{
    boot_splash_t * bs = (boot_splash_t *)lv_timer_get_user_data(t);
    lv_timer_del(t);
    lv_timer_create(reveal_step_timer_cb, SCRIBBLE_STEP_MS > 0 ? SCRIBBLE_STEP_MS : 1, bs);
}

void boot_splash_create(uint32_t start_delay_ms)
{
    boot_splash_t * bs = lv_malloc_zeroed(sizeof(boot_splash_t));
    LV_ASSERT_MALLOC(bs);

    lv_obj_t * root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, SIZE, SIZE);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, BG_COLOR, 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    /* radius alone (no clip_corner) circular-clips this object's bg fill. */
    lv_obj_set_style_radius(root, LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    bs->root = root;
    lv_obj_set_user_data(root, bs);
    lv_obj_add_event_cb(root, root_delete_event_cb, LV_EVENT_DELETE, NULL);

    /* Scribble flourish (drawn via scribble_draw_event_cb, positioned
     * below once the full block's height is known). */
    lv_obj_t * scribble = lv_obj_create(root);
    lv_obj_remove_style_all(scribble);
    lv_obj_set_size(scribble, SCRIBBLE_W, SCRIBBLE_H);
    lv_obj_remove_flag(scribble, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(scribble, bs);
    lv_obj_add_event_cb(scribble, scribble_draw_event_cb, LV_EVENT_DRAW_MAIN, NULL);
    bs->scribble = scribble;
    build_scribble_points(bs->pts, SCRIBBLE_W, SCRIBBLE_H);

    /* Greeting text -- starts fully transparent and hidden; both are
     * flipped once the scribble finishes (see reveal_step_timer_cb). */
    lv_obj_t * text_box = lv_obj_create(root);
    lv_obj_remove_style_all(text_box);
    lv_obj_set_size(text_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(text_box, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(text_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(text_box, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(text_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(text_box, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(text_box, LV_OBJ_FLAG_HIDDEN);
    bs->text_box = text_box;

    lv_obj_t * line1 = lv_label_create(text_box);
    lv_label_set_text(line1, "HELLO!");
    lv_obj_set_style_text_font(line1, &GREETING_FONT_BIG, 0);
    lv_obj_set_style_text_color(line1, TEXT_COLOR, 0);

    lv_obj_t * line2 = lv_label_create(text_box);
    lv_label_set_text(line2, "EVERYONE");
    lv_obj_set_style_text_font(line2, &GREETING_FONT_BIG, 0);
    lv_obj_set_style_text_color(line2, TEXT_COLOR, 0);

    /* Measure the text block once, then position with one-time
     * lv_obj_set_pos() calls -- not lv_obj_align()/align_to(), whose live
     * constraint on an LV_SIZE_CONTENT object pegs a CPU core on this
     * LVGL/SDL combo. */
    lv_obj_update_layout(text_box);
    int32_t text_w = lv_obj_get_width(text_box);
    int32_t text_h = lv_obj_get_height(text_box);
    int32_t block_h = SCRIBBLE_H + BLOCK_GAP + text_h;
    int32_t block_y = (SIZE - block_h) / 2;

    lv_obj_set_pos(scribble, (SIZE - SCRIBBLE_W) / 2, block_y);
    lv_obj_set_pos(text_box, (SIZE - text_w) / 2, block_y + SCRIBBLE_H + BLOCK_GAP);

    splash_active = true;
    lv_timer_create(begin_timer_cb, start_delay_ms > 0 ? start_delay_ms : 1, bs);
}

bool boot_splash_is_active(void)
{
    return splash_active;
}
