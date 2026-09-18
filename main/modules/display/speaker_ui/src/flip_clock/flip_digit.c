/**
 * @file flip_digit.c
 *
 * A single mechanical "flip clock" digit tile, built from plain LVGL
 * widgets + animations (no bitmap assets).
 *
 * The tile is built from 4 stacked layers:
 *   - top_static / bottom_static : always show the *current* digit, split
 *     in half by clipping a full-size label inside a half-height container.
 *   - top_flap / bottom_flap     : transient layers that play the actual
 *     flip animation and are hidden again once finished.
 *
 * Flip sequence when the value changes:
 *   1) top_flap shows the OLD digit's top half, top_static is switched to
 *      the NEW digit right away (hidden underneath). top_flap's HEIGHT is
 *      then shrunk from a full half-tile down to 0 with its bottom edge
 *      pinned at the hinge line, which reads as the top card folding down
 *      onto the center hinge.
 *   2) bottom_flap shows the NEW digit's bottom half, starts at height 0
 *      (pinned to the hinge line) and grows down to a full half-tile,
 *      which reads as the new bottom card unrolling into place.
 *      bottom_static is switched to the NEW digit at the same time (it
 *      stays hidden under the flap until the flap has grown enough to
 *      cover it).
 *
 * CPU-safety rule: the fold/unroll animates each flap's HEIGHT via a plain
 * lv_timer stepper (flip_step_timer_cb() below). Do NOT drive it with
 * lv_obj_set_style_transform_scale_y() + lv_anim_t instead -- on this
 * LVGL + SDL2 software-renderer combo, drawing any object with a
 * non-identity transform leaves the display permanently in its fast-
 * refresh "animating" mode, pegging a CPU core at ~95-100% even after the
 * animation finishes and the transform is reset. Plain
 * lv_obj_set_height()/lv_obj_set_pos() resizing avoids that code path
 * entirely and is what this file uses.
 */

#include "flip_digit.h"
#include "display_profile.h"
#include "digit_fonts.h"
#include <stdio.h>
#include <stdlib.h>

/* Custom digits-only fonts (see digit_fonts.h) -- larger than LVGL's
 * bundled Montserrat, which tops out at 48px. */
#if DISPLAY_ROUND
    #define DIGIT_FONT lv_font_digit_44
#else
    #define DIGIT_FONT lv_font_digit_64
#endif

#define FLIP_STAGE_MS   170
#define HINGE_COLOR     lv_color_hex(0x000000)
#define CARD_TOP_COLOR  lv_color_hex(0x3a3f4d)
#define CARD_BOT_COLOR  lv_color_hex(0x2a2e39)
#define TEXT_COLOR      lv_color_hex(0xf3f4f6)
/* Semi-transparent tile background: gives a "frosted glass" look (the
 * background scene shows through) without any runtime blur. Drawn as the
 * object's own background fill, so it respects `cont`'s radius style
 * automatically. */
#define PANEL_OPA  LV_OPA_70

/* Steps for the hand-rolled flip stepper (see the file header for why this
 * is used instead of lv_anim_t + a transform) -- 12 steps over
 * FLIP_STAGE_MS (170ms) is a ~14ms tick, smooth for a half-tile-tall flap
 * at this size, and coarser than the main loop's own 5ms poll. */
#define FLIP_ANIM_STEPS   12
#define FLIP_ANIM_STEP_MS (FLIP_STAGE_MS / FLIP_ANIM_STEPS)

typedef struct {
    lv_obj_t * top_static;
    lv_obj_t * bottom_static;
    lv_obj_t * top_flap;
    lv_obj_t * bottom_flap;
    lv_obj_t * top_flap_label;
    lv_obj_t * bottom_flap_label;
    lv_obj_t * top_static_label;
    lv_obj_t * bottom_static_label;
    int32_t w, h;
    int value;
} flip_digit_t;

/* Positions a full-size label (as if inside one continuous `tile_h`-tall
 * box) inside a half-height container that clips it to only the top or
 * bottom half. `top` picks which half:
 *   - top container's local origin coincides with the full tile's origin,
 *     so its label uses the "centered in a tile_h box" offset directly.
 *   - bottom container's local origin starts at tile_h/2, so that offset
 *     is shifted up by tile_h/2 to land the label's bottom continuation
 *     at the top of this container. */
static void position_label_half(lv_obj_t * label, int32_t tile_h, bool top)
{
    lv_obj_update_layout(label);
    int32_t lh = lv_obj_get_height(label);
    int32_t full_box_y = (tile_h - lh) / 2;
    lv_obj_set_pos(label, 0, top ? full_box_y : full_box_y - tile_h / 2);
}

static lv_obj_t * make_half_container(lv_obj_t * parent, int32_t w, int32_t h,
                                       int32_t y, lv_color_t bg, bool top)
{
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, w, h / 2);
    lv_obj_set_pos(cont, 0, y);
    lv_obj_set_style_bg_color(cont, bg, 0);
    /* This is the object's own background fill (see PANEL_OPA above), so
     * it's clipped to `cont`'s rounded radius automatically. */
    lv_obj_set_style_bg_opa(cont, PANEL_OPA, 0);
    lv_obj_set_style_radius(cont, 8, 0);
    LV_UNUSED(top); /* LVGL has no per-corner radius -- the hinge-side
                      * corners are rounded too. */
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    return cont;
}

static lv_obj_t * make_full_label(lv_obj_t * half_container, int32_t w, int32_t local_y)
{
    lv_obj_t * label = lv_label_create(half_container);
    lv_obj_set_width(label, w);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &DIGIT_FONT, 0);
    lv_obj_set_style_text_color(label, TEXT_COLOR, 0);
    lv_obj_set_pos(label, 0, local_y);
    return label;
}

static void top_flap_ready_cb(lv_obj_t * digit);
static void start_bottom_flap(lv_obj_t * digit);

static void hide_obj(lv_obj_t * obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}
static void show_obj(lv_obj_t * obj)
{
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

/* ---- hand-rolled height stepper (replaces lv_anim_t + a transform --
 * see the file header comment for why) ---- */

typedef struct {
    lv_obj_t * digit;
    lv_obj_t * flap;
    int32_t w;         /* the flap's (constant) width, cached once rather than
                           read back via lv_obj_get_width() on every step */
    int32_t half_h;    /* the flap's full (un-collapsed) height, i.e. h/2 */
    int32_t hinge_y;   /* the flap's fixed-edge y (its edge at the hinge line) */
    bool grow;         /* false: top flap folding, height half_h -> 0, bottom edge fixed;
                           true:  bottom flap unrolling, height 0 -> half_h, top edge fixed at hinge */
    int step;
    void (*done_cb)(lv_obj_t * digit);
} flip_step_t;

/* Plain quadratic ease -- t/steps in, eased 0..1000 out. */
static int32_t ease_in_1000(int32_t t1000)
{
    return (int32_t)(((int64_t)t1000 * t1000) / 1000);
}
static int32_t ease_out_1000(int32_t t1000)
{
    int32_t u = 1000 - t1000;
    return 1000 - (int32_t)(((int64_t)u * u) / 1000);
}

static void flip_step_timer_cb(lv_timer_t * t)
{
    flip_step_t * s = (flip_step_t *)lv_timer_get_user_data(t);
    s->step++;

    int32_t t1000 = (int32_t)(((int64_t)s->step * 1000) / FLIP_ANIM_STEPS);
    if(t1000 > 1000) t1000 = 1000;
    /* Folding (shrink) eases in (fast start/slow end); unrolling (grow)
     * eases out (slow start/fast end). */
    int32_t p1000 = s->grow ? ease_out_1000(t1000) : ease_in_1000(t1000);

    if(s->grow) {
        /* bottom flap: top edge pinned at the hinge, height grows down */
        int32_t height = (int32_t)(((int64_t)s->half_h * p1000) / 1000);
        lv_obj_set_size(s->flap, s->w, height);
        lv_obj_set_pos(s->flap, 0, s->hinge_y);
    }
    else {
        /* top flap: bottom edge pinned at the hinge, height shrinks so the
         * bottom edge (y + height) stays put -- y = hinge_y - height. */
        int32_t height = (int32_t)(((int64_t)s->half_h * (1000 - p1000)) / 1000);
        lv_obj_set_size(s->flap, s->w, height);
        lv_obj_set_pos(s->flap, 0, s->hinge_y - height);
    }

    if(s->step >= FLIP_ANIM_STEPS) {
        lv_obj_t * digit = s->digit;
        void (*done_cb)(lv_obj_t *) = s->done_cb;
        lv_free(s);
        lv_timer_del(t);
        if(done_cb) done_cb(digit);
    }
}

/* Starts a step timer that resizes `flap`'s height from 0 to `half_h`
 * (grow == true, top edge pinned at `hinge_y`) or from `half_h` to 0
 * (grow == false, bottom edge pinned at `hinge_y`) over FLIP_STAGE_MS,
 * then calls done_cb(digit). Caller must set the flap's start size/pos
 * and show it before calling this. */
static void start_flip_step(lv_obj_t * digit, lv_obj_t * flap, int32_t w, int32_t half_h,
                             int32_t hinge_y, bool grow, void (*done_cb)(lv_obj_t *))
{
    flip_step_t * s = lv_malloc_zeroed(sizeof(flip_step_t));
    s->digit = digit;
    s->flap = flap;
    s->w = w;
    s->half_h = half_h;
    s->hinge_y = hinge_y;
    s->grow = grow;
    s->done_cb = done_cb;
    s->step = 0;
    lv_timer_create(flip_step_timer_cb, FLIP_ANIM_STEP_MS > 0 ? FLIP_ANIM_STEP_MS : 1, s);
}

lv_obj_t * flip_digit_create(lv_obj_t * parent, int32_t w, int32_t h, int value)
{
    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    flip_digit_t * fd = lv_malloc_zeroed(sizeof(flip_digit_t));
    fd->w = w;
    fd->h = h;
    fd->value = value;

    /* Static halves (always visible, never move). */
    fd->top_static    = make_half_container(root, w, h, 0,     CARD_TOP_COLOR, true);
    fd->bottom_static = make_half_container(root, w, h, h / 2, CARD_BOT_COLOR, false);
    fd->top_static_label    = make_full_label(fd->top_static, w, 0);
    fd->bottom_static_label = make_full_label(fd->bottom_static, w, -h / 2);

    /* Flap halves (only visible during the ~340ms flip transition, and
     * they move/resize every step of it). */
    fd->top_flap    = make_half_container(root, w, h, 0,     CARD_TOP_COLOR, true);
    fd->bottom_flap = make_half_container(root, w, h, h / 2, CARD_BOT_COLOR, false);
    fd->top_flap_label    = make_full_label(fd->top_flap, w, 0);
    fd->bottom_flap_label = make_full_label(fd->bottom_flap, w, -h / 2);
    hide_obj(fd->top_flap);
    hide_obj(fd->bottom_flap);

    /* center hinge line */
    lv_obj_t * hinge = lv_obj_create(root);
    lv_obj_remove_style_all(hinge);
    lv_obj_set_size(hinge, w, 3);
    lv_obj_set_pos(hinge, 0, h / 2 - 1);
    lv_obj_set_style_bg_color(hinge, HINGE_COLOR, 0);
    lv_obj_set_style_bg_opa(hinge, LV_OPA_40, 0);
    lv_obj_remove_flag(hinge, LV_OBJ_FLAG_SCROLLABLE);

    char buf[4];
    snprintf(buf, sizeof(buf), "%d", value % 10);
    lv_label_set_text(fd->top_static_label, buf);
    lv_label_set_text(fd->bottom_static_label, buf);
    position_label_half(fd->top_static_label, h, true);
    position_label_half(fd->bottom_static_label, h, false);

    lv_obj_set_user_data(root, fd);
    return root;
}

int flip_digit_get_value(lv_obj_t * digit)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);
    return fd->value;
}

static void bottom_flap_done_cb(lv_obj_t * digit)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);
    hide_obj(fd->bottom_flap);
}

static void start_bottom_flap(lv_obj_t * digit)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", fd->value);
    lv_label_set_text(fd->bottom_flap_label, buf);
    position_label_half(fd->bottom_flap_label, fd->h, false);
    lv_label_set_text(fd->bottom_static_label, buf);
    position_label_half(fd->bottom_static_label, fd->h, false);

    /* Start collapsed (height 0) right at the hinge line, then grow down. */
    lv_obj_set_size(fd->bottom_flap, fd->w, 0);
    lv_obj_set_pos(fd->bottom_flap, 0, fd->h / 2);
    show_obj(fd->bottom_flap);
    start_flip_step(digit, fd->bottom_flap, fd->w, fd->h / 2, fd->h / 2, true, bottom_flap_done_cb);
}

static void top_flap_ready_cb(lv_obj_t * digit)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);
    hide_obj(fd->top_flap);
    start_bottom_flap(digit);
}

/* Plays the top-flap-folds/bottom-flap-unrolls animation to `value`,
 * unconditionally (no check against the current value -- callers decide
 * whether a same-value call should be a no-op or still animate; see
 * flip_digit_set_value() vs. flip_digit_replay()). Not reentrant: a call
 * made while a previous flip is still animating is not guarded against
 * (a flip takes ~340ms, comfortably under the 1s tick this is normally
 * driven at). */
static void do_flip(lv_obj_t * digit, int value)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);

    char old_buf[4];
    snprintf(old_buf, sizeof(old_buf), "%d", fd->value);

    /* Stage 1: top flap shows the OLD digit and folds down, revealing the
     * NEW digit already placed underneath on top_static. */
    lv_label_set_text(fd->top_flap_label, old_buf);
    position_label_half(fd->top_flap_label, fd->h, true);

    fd->value = value;
    char new_buf[4];
    snprintf(new_buf, sizeof(new_buf), "%d", value);
    lv_label_set_text(fd->top_static_label, new_buf);
    position_label_half(fd->top_static_label, fd->h, true);

    /* Start full-height (fd->h / 2), then shrink up toward the hinge. */
    lv_obj_set_size(fd->top_flap, fd->w, fd->h / 2);
    lv_obj_set_pos(fd->top_flap, 0, 0);
    show_obj(fd->top_flap);
    start_flip_step(digit, fd->top_flap, fd->w, fd->h / 2, fd->h / 2, false, top_flap_ready_cb);
}

void flip_digit_set_value(lv_obj_t * digit, int value)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);
    value = value % 10;
    if(value < 0) value += 10;
    if(value == fd->value) return;
    do_flip(digit, value);
}

void flip_digit_replay(lv_obj_t * digit)
{
    flip_digit_t * fd = (flip_digit_t *)lv_obj_get_user_data(digit);
    do_flip(digit, fd->value);
}
