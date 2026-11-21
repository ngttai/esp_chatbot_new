#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSIC_DATA_PENDING,
    MUSIC_DATA_READY,
    MUSIC_DATA_FAILED,
} music_data_state_t;

typedef struct {
    char title[96];
    char artist[64];
    char album[64];
    uint32_t duration_ms;
    music_data_state_t state;
} music_data_track_t;

/* Native little-endian RGB565 thumbnail. Valid until the next get_cover call
 * on the UI thread. NULL means that fallback artwork should be displayed. */
typedef struct {
    const uint8_t *pixels;
    uint16_t width;
    uint16_t height;
} music_data_cover_image_t;
typedef const music_data_cover_image_t *music_data_cover_t;

/* Overrides the music folder. Must be called before music_data_start(). */
void music_data_set_root(const char *path);

/* Starts the background scanner once. Safe to call repeatedly. */
void music_data_start(void);

size_t music_data_count(void);
bool music_data_scan_done(void);
uint32_t music_data_version(void);
bool music_data_get(size_t index, music_data_track_t *out);
/* Copies the file path of a track; false when it does not fit or does not exist. */
bool music_data_get_path(size_t index, char *out, size_t size);
music_data_cover_t music_data_get_cover(size_t index);

void music_data_set_ui_idle(bool idle);
void music_data_set_pinned(size_t index);
void music_data_set_wanted(const size_t *indices, size_t count);

#ifdef __cplusplus
}
#endif
