#ifndef MUSIC_UI_H
#define MUSIC_UI_H

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the Music UI inside `parent`.
 * Track data comes from music_data; playback state is UI-only. */
void music_ui_create(lv_obj_t *parent);
void music_ui_show_now_playing(void);
void music_ui_show_playlist(void);
size_t music_ui_get_track_index(void);
bool music_ui_is_playing(void);
bool music_ui_is_playlist_visible(void);
int32_t music_ui_get_playlist_scroll_y(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_UI_H */
