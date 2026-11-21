#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MUSIC_SPECTRUM_BANDS 16
/* UI thread only. Disabling stops capture and computation; storage is reused. */
bool music_spectrum_set_enabled(bool enabled);
bool music_spectrum_is_enabled(void);
/* Selects the exact file URI and clears samples from the previous song. */
void music_spectrum_set_track(const char *path);
/* Reads a fresh FFT frame, 0..100. No allocation; false if no new samples. */
bool music_spectrum_read(uint8_t levels[MUSIC_SPECTRUM_BANDS]);
/* PCM tap: task context, bounded try-lock/copy, no FFT, allocation or logging. */
void music_spectrum_feed(const char *uri, const uint8_t *data, size_t bytes,
                         unsigned rate, unsigned channels, unsigned bits);
#ifdef __cplusplus
}
#endif
