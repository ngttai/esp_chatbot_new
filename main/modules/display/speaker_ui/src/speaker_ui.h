#ifndef SPEAKER_UI_H
#define SPEAKER_UI_H

#include <stdbool.h>
#include "lvgl.h"

void speaker_ui_create(void);
void speaker_ui_set_input(lv_indev_t *input);
void speaker_ui_set_wifi_managed_externally(bool managed);
bool speaker_ui_is_launcher_active(void);
bool speaker_ui_is_idle_active(void);
bool speaker_ui_is_screen_active(const char *screen_name);
int speaker_ui_launcher_page(void);
bool speaker_ui_show(const char *screen_name);
bool speaker_ui_settings_bar_stays_fixed(void);
bool speaker_ui_get_active_home_bar_geometry(int32_t *center_x, int32_t *width);
int32_t speaker_ui_get_settings_scroll_y(void);
bool speaker_ui_is_wlan_on(void);
bool speaker_ui_is_quick_wifi_on(void);
bool speaker_ui_is_wlan_section_visible(const char *section);
bool speaker_ui_get_wlan_switch_center(int32_t *x, int32_t *y);
bool speaker_ui_get_quick_wifi_button_center(int32_t *x, int32_t *y);
bool speaker_ui_get_quick_volume_button_center(int32_t *x, int32_t *y);
bool speaker_ui_get_quick_brightness_button_center(int32_t *x, int32_t *y);
int speaker_ui_get_quick_volume_level(void);
int speaker_ui_get_quick_brightness_level(void);
bool speaker_ui_set_quick_volume_level(int level);
bool speaker_ui_set_quick_brightness_level(int level);
bool speaker_ui_is_wlan_keyboard_visible(void);
bool speaker_ui_wlan_keyboard_bound(void);
bool speaker_ui_set_wlan_password(const char *text);
bool speaker_ui_confirm_wlan_password(void);

#endif
