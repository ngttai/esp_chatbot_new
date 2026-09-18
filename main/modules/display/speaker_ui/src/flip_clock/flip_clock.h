#ifndef FLIP_CLOCK_H
#define FLIP_CLOCK_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build a full HH:MM flip-clock widget (4 flip_digit tiles + colon) that
 * keeps itself in sync with the host's system clock every second.
 */
lv_obj_t * flip_clock_create(lv_obj_t * parent);

/**
 * Replays the boot-style flip animation on all 4 digit tiles at once,
 * without changing the displayed time (each tile flips to the value
 * it's already showing -- see flip_digit_replay()).
 */
void flip_clock_replay(lv_obj_t * clock);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*FLIP_CLOCK_H*/
