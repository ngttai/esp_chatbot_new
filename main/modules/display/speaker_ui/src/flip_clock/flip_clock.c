/**
 * @file flip_clock.c
 *
 * Composes 4 flip_digit tiles into an HH:MM (24h) clock, ticking off the
 * system clock (localtime()) every second. Also builds the colon
 * separator and exposes flip_clock_replay() to replay the flip flourish
 * on demand without changing the displayed time.
 */

#include "flip_clock.h"
#include "flip_digit.h"
#include "display_profile.h"
#include <time.h>
#include <stdio.h>

/* Tile geometry, sized around each profile's DIGIT_FONT (see
 * flip_digit.c / digit_fonts.h). The round face uses a smaller font and
 * tighter gaps to leave room for the date + current condition below the
 * clock while clearing the circular bezel. */
#if DISPLAY_ROUND
    #define DIGIT_W       57
    #define DIGIT_H       106
    #define DIGIT_GAP     4
    #define GROUP_GAP     9
    #define COLON_W       10
    #define DOT_SIZE      11
    #define DOT_OFFSET    16
    /* Keeps the clock's bounding box flush with the tiles' visible edge;
     * the gap below the clock is controlled by the caller's own layout. */
    #define ROOT_BOTTOM_H 0
#else
    #define DIGIT_W       83
    #define DIGIT_H       160
    #define DIGIT_GAP     5
    #define GROUP_GAP     13
    #define COLON_W       14
    #define DOT_SIZE      16
    #define DOT_OFFSET    24
    /* Keeps the clock's bounding box flush with the tiles' visible edge;
     * the gap below the clock is controlled by the caller's own layout. */
    #define ROOT_BOTTOM_H 0
#endif

/* Dark outline color for the colon dots (see make_colon()) -- without a
 * border, the near-white dot fill can blend into light-toned parts of the
 * background image. A flat border here is plain stroke drawing, not a
 * blur/shadow layer, so it's unaffected by the CPU issue documented in
 * flip_digit.c. */
#define DOT_BORDER_COLOR lv_color_hex(0x14161d)
#define DOT_BORDER_W      2

typedef struct {
    lv_obj_t * h_tens;
    lv_obj_t * h_ones;
    lv_obj_t * m_tens;
    lv_obj_t * m_ones;
} flip_clock_t;

static void tick_timer_cb(lv_timer_t * t)
{
    flip_clock_t * fc = (flip_clock_t *)lv_timer_get_user_data(t);
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    /* 24h display: no 12h wraparound, no AM/PM. */
    flip_digit_set_value(fc->h_tens, tm_now.tm_hour / 10);
    flip_digit_set_value(fc->h_ones, tm_now.tm_hour % 10);
    flip_digit_set_value(fc->m_tens, tm_now.tm_min / 10);
    flip_digit_set_value(fc->m_ones, tm_now.tm_min % 10);
}

static lv_obj_t * make_colon(lv_obj_t * parent)
{
    lv_obj_t * cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, COLON_W, DIGIT_H);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    for(int i = 0; i < 2; i++) {
        lv_obj_t * dot = lv_obj_create(cont);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xf3f4f6), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(dot, DOT_BORDER_COLOR, 0);
        lv_obj_set_style_border_width(dot, DOT_BORDER_W, 0);
        lv_obj_set_style_border_opa(dot, LV_OPA_COVER, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(dot, LV_ALIGN_CENTER, 0, i == 0 ? -DOT_OFFSET : DOT_OFFSET);
    }
    return cont;
}

lv_obj_t * flip_clock_create(lv_obj_t * parent)
{
    lv_obj_t * root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, LV_SIZE_CONTENT, DIGIT_H + ROOT_BOTTOM_H);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    flip_clock_t * fc = lv_malloc_zeroed(sizeof(flip_clock_t));

    int32_t x = 0;
    fc->h_tens = flip_digit_create(root, DIGIT_W, DIGIT_H, 0);
    lv_obj_set_pos(fc->h_tens, x, 0); x += DIGIT_W + DIGIT_GAP;
    fc->h_ones = flip_digit_create(root, DIGIT_W, DIGIT_H, 0);
    lv_obj_set_pos(fc->h_ones, x, 0); x += DIGIT_W + GROUP_GAP;

    lv_obj_t * colon = make_colon(root);
    lv_obj_set_pos(colon, x, 0); x += COLON_W + GROUP_GAP;

    fc->m_tens = flip_digit_create(root, DIGIT_W, DIGIT_H, 0);
    lv_obj_set_pos(fc->m_tens, x, 0); x += DIGIT_W + DIGIT_GAP;
    fc->m_ones = flip_digit_create(root, DIGIT_W, DIGIT_H, 0);
    lv_obj_set_pos(fc->m_ones, x, 0); x += DIGIT_W;

    lv_obj_set_width(root, x);
    lv_obj_set_user_data(root, fc);

    lv_timer_t * timer = lv_timer_create(tick_timer_cb, 1000, fc);
    lv_timer_ready(timer); /* run immediately so the clock isn't blank for 1s */

    return root;
}

void flip_clock_replay(lv_obj_t * clock)
{
    flip_clock_t * fc = (flip_clock_t *)lv_obj_get_user_data(clock);
    flip_digit_replay(fc->h_tens);
    flip_digit_replay(fc->h_ones);
    flip_digit_replay(fc->m_tens);
    flip_digit_replay(fc->m_ones);
}
