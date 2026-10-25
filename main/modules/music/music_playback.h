#ifndef MUSIC_PLAYBACK_H
#define MUSIC_PLAYBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MUSIC_PLAYBACK_STOPPED,
    MUSIC_PLAYBACK_PREPARING,
    MUSIC_PLAYBACK_PLAYING,
    MUSIC_PLAYBACK_PAUSED,
    MUSIC_PLAYBACK_ENDED,
    MUSIC_PLAYBACK_ERROR,
} music_playback_state_t;

typedef struct {
    music_playback_state_t state;
    size_t index;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    /* Changes on every state or track change. */
    uint32_t version;
} music_playback_info_t;

/* Creates the player once. Safe to call repeatedly. */
void music_playback_start(void);

/* Plays the track at `index` of the music library, replacing the current one. */
bool music_playback_play(size_t index);
bool music_playback_pause(void);
bool music_playback_resume(void);
bool music_playback_stop(void);

/* Applies queued audio events and timeouts. Call from the UI thread periodically. */
void music_playback_pump(void);

/* Test hook: the backend reports Idle before the pending start produced any
 * sound, as when the file cannot be opened. Queued ahead of all other events. */
void music_playback_test_report_open_failure(void);

void music_playback_get_info(music_playback_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_PLAYBACK_H */
