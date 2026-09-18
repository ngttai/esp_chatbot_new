#ifndef FLIP_DIGIT_H
#define FLIP_DIGIT_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create one mechanical "flip" digit tile (0-9).
 * @param parent    parent object
 * @param w         tile width in px
 * @param h         tile height in px
 * @param value     initial value (0-9)
 */
lv_obj_t * flip_digit_create(lv_obj_t * parent, int32_t w, int32_t h, int value);

/**
 * Set a new value. If it differs from the current one, plays the
 * two-stage flip animation (top flap folds down, bottom flap unrolls).
 */
void flip_digit_set_value(lv_obj_t * digit, int value);

/**
 * Replays the flip animation for whatever value is already showing (a
 * same-digit flip), without changing the displayed value. See
 * flip_clock_replay() in flip_clock.h, which calls this for all 4 digits.
 */
void flip_digit_replay(lv_obj_t * digit);

int flip_digit_get_value(lv_obj_t * digit);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*FLIP_DIGIT_H*/
