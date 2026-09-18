#ifndef DIGIT_FONTS_H
#define DIGIT_FONTS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Digits-only (0-9) custom fonts, generated from Liberation Sans Narrow
 * Bold via lv_font_conv (see src/ui/fonts/README.md). Larger than LVGL's
 * bundled Montserrat (which tops out at 48px); the narrow face keeps the
 * digits from outgrowing the screen width.
 */
extern const lv_font_t lv_font_digit_64; /* used on the RECT (410x502) face */
extern const lv_font_t lv_font_digit_44; /* used on the ROUND (360x360) face */

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*DIGIT_FONTS_H*/
