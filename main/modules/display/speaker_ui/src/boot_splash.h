#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Shows a brief animated boot splash (a scribble accent draws itself in,
 * then a greeting fades in) as a full-screen overlay on lv_layer_top(), on
 * top of whatever screen is already active, then removes itself
 * automatically after the reveal plus a short hold. Ported from
 * HTC_Flip_Clock_with_weather's boot_splash.c/.h.
 *
 * Call once after speaker_ui_create() has loaded the Idle screen. The start
 * delay keeps the overlay black until the application restores and enables
 * the backlight. Deleting the overlay reveals the normal screen underneath.
 */
void boot_splash_create(uint32_t start_delay_ms);

/** Returns true until the overlay has finished and deleted itself. */
bool boot_splash_is_active(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*BOOT_SPLASH_H*/
