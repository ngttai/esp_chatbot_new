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

/* Opaque image source owned by the music service. NULL means that the track
 * has no decoded cover available and the UI should use its fallback artwork. */
typedef const void *music_data_cover_t;

/* Overrides the music folder. Must be called before music_data_start(). */
void music_data_set_root(const char *path);

/* Starts the background scanner once. Safe to call repeatedly. */
void music_data_start(void);

size_t music_data_count(void);
bool music_data_scan_done(void);
uint32_t music_data_version(void);
bool music_data_get(size_t index, music_data_track_t *out);
music_data_cover_t music_data_get_cover(size_t index);

void music_data_set_ui_idle(bool idle);
void music_data_set_pinned(size_t index);
void music_data_set_wanted(const size_t *indices, size_t count);

#ifdef __cplusplus
}
#endif
