#ifndef SPEAKER_UI_H
#define SPEAKER_UI_H

#include <stdbool.h>
#include "lvgl.h"

typedef struct {
    const char *firmware;
    const char *os;
    const char *os_version;
    const char *ui;
    const char *ui_version;
    const char *manufacturer;
    const char *board;
    const char *resolution;
    const char *flash;
    const char *ram_main;
    const char *ram_minor;
    const char *battery_capacity;
    const char *chip_name;
    const char *chip_version;
    const char *chip_mac;
    const char *chip_features;
} speaker_ui_about_info_t;

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
bool speaker_ui_set_battery_state(bool charging, int percentage);
bool speaker_ui_set_about_info(const speaker_ui_about_info_t *info);
bool speaker_ui_set_about_battery_measurements(int voltage_mv, int current_ma);
bool speaker_ui_is_touch_sensor_on(void);
bool speaker_ui_set_touch_sensor_on(bool enabled);
bool speaker_ui_is_wlan_keyboard_visible(void);
bool speaker_ui_wlan_keyboard_bound(void);
bool speaker_ui_set_wlan_password(const char *text);
bool speaker_ui_confirm_wlan_password(void);

#endif
