#include "speaker_ui.h"

#include <inttypes.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "lvgl.h"
#include "flip_clock/flip_clock.h"
#include "flip_clock/weather_panel.h"

#define SIZE 360
#define BG lv_color_hex(0x1A1A1A)
#define PANEL lv_color_hex(0x38393A)
#define WHITE lv_color_hex(0xFFFFFF)
#define MUTED lv_color_hex(0xAAAAAA)
#define RED lv_color_hex(0xFF3034)
#define HOME_BAR_WIDTH 108
#define HOME_BAR_HEIGHT 6
#define HOME_BAR_BOTTOM_OFFSET -14
#define HOME_GESTURE_RANGE 20
#define HOME_BAR_COMPLETE_WIDTH 6
#define QUICK_GESTURE_EDGE 20
#define QUICK_OPEN_THRESHOLD (SIZE * 20 / 100)
#define QUICK_CLOSE_THRESHOLD (SIZE * 80 / 100)
#define APP_ICON_DEFAULT_SIZE 98
#define APP_ICON_PRESSED_SIZE 88
#define APP_ICON_DEFAULT_SCALE 224
#define APP_ICON_PRESSED_SCALE 201
#if defined(ESP_PLATFORM)
    #define CLOCK_PANEL_OPA LV_OPA_COVER
#else
    #define CLOCK_PANEL_OPA LV_OPA_70
#endif

LV_IMAGE_DECLARE(esp_brookesia_app_icon_launcher_settings_112_112);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_arrow_left_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_arrow_right_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_brightness_less_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_brightness_more_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_sound_less_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_sound_more_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wireless_wlan_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_media_sound_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_media_display_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_input_touch_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_more_about_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_more_developer_mode_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_more_restart_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_level1_36_36);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_level2_36_36);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_level3_36_36);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_wlan_lock_48_48);
LV_IMAGE_DECLARE(esp_brookesia_app_icon_launcher_ai_profile_112_112);
LV_IMAGE_DECLARE(img_app_2048);
LV_IMAGE_DECLARE(img_app_calculator);
LV_IMAGE_DECLARE(img_app_timer);
LV_IMAGE_DECLARE(img_app_pos);
LV_IMAGE_DECLARE(img_app_usbd_ncm);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_wifi_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_wifi_close_20_20);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_battery_charge_20_20);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_battery_level1_20_20);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_battery_level2_20_20);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_battery_level3_20_20);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_battery_level4_20_20);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_volume_high_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_volume_medium_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_volume_low_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_volume_off_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_brightness_high_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_brightness_medium_48_48);
LV_IMAGE_DECLARE(speaker_image_middle_quick_settings_brightness_low_48_48);
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_12)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_14)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_16)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_18)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_20)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_22)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_24)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_26)

lv_obj_t *ui_ContainerQuickSettings_create(lv_obj_t *parent);
void ui_ScreenScreenAIProfile_screen_init(void);
extern lv_obj_t *ui_ScreenScreenAIProfile;
extern lv_obj_t *ui_ScreenAIProfileTabviewTabView;
extern lv_obj_t *ui_ScreenAIProfileTabpageTabPageRole1;
extern lv_obj_t *ui_ScreenAIProfileTabpageTabPageRole2;
extern lv_obj_t *ui_ScreenAIProfilePanelPanelIndicator1;
extern lv_obj_t *ui_ScreenAIProfilePanelPanelIndicator2;

typedef struct { const char *name; const lv_image_dsc_t *image; lv_event_cb_t cb; } app_t;
static lv_obj_t *idle, *launcher, *quick, *settings, *ai, *timer_screen;
static lv_obj_t *settings_wlan, *settings_sound, *settings_display, *settings_about, *settings_self_test;
static lv_obj_t *settings_developer, *settings_restore;
static lv_obj_t *settings_developer_row;
static lv_obj_t *settings_wlan_verify, *settings_softap;
static lv_obj_t *page_box, *dots;
static lv_obj_t *timer_clock_widget;
static lv_obj_t *settings_scroller, *settings_wlan_scroller;
static lv_obj_t *launcher_home_bar, *quick_home_bar, *settings_home_bar, *ai_home_bar, *timer_home_bar;
static lv_obj_t *wlan_home_bar, *sound_home_bar, *display_home_bar, *about_home_bar, *self_test_home_bar;
static lv_obj_t *developer_home_bar, *factory_home_bar;
static lv_obj_t *wlan_verify_home_bar, *softap_home_bar;
static lv_obj_t *settings_wlan_value_label, *wlan_connected_name_label, *restore_status_label;
static lv_obj_t *about_firmware_label, *about_os_label, *about_os_version_label;
static lv_obj_t *about_ui_label, *about_ui_version_label;
static lv_obj_t *about_manufacturer_label, *about_board_label, *about_resolution_label;
static lv_obj_t *about_flash_label, *about_ram_main_label, *about_ram_minor_label;
static lv_obj_t *about_battery_capacity_label, *about_battery_voltage_label, *about_battery_current_label;
static lv_obj_t *about_chip_name_label, *about_chip_version_label, *about_chip_mac_label;
static lv_obj_t *about_chip_features_label;
static lv_obj_t *self_test_status_labels[SPEAKER_UI_SELF_TEST_COUNT];
static lv_obj_t *self_test_run_button;
static lv_obj_t *settings_wlan_switch;
static lv_obj_t *settings_touch_switch;
static lv_obj_t *settings_wlan_connected_group, *settings_wlan_available_group, *settings_wlan_softap_group;
/* Mock the real device's Wi-Fi-on reveal sequence (see set_wlan_enabled()): switching
 * WLAN on shows the Available networks group empty at first and the Connected network
 * row not at all; the scan results populate a few seconds in, and only then does the
 * Connected network row appear -- first reading "Connecting...", then settling to
 * "Connected" about a second later. Purely cosmetic (no real connect/scan state to time
 * against) and only played by the on/off toggle itself -- opening Settings > WLAN while
 * already on a network just shows the already-settled state, no re-animation. */
static lv_obj_t *wlan_connected_status_label;
static lv_obj_t *wlan_network_rows[3];
static lv_timer_t *wlan_entry_list_timer, *wlan_entry_connect_timer, *wlan_entry_settle_timer;
static lv_obj_t *quick_wifi_button, *quick_wifi_status_icon, *quick_time_label;
static lv_obj_t *quick_battery_status_icon, *quick_battery_percent_label;
static lv_obj_t *quick_volume_button, *quick_brightness_button;
/* VolumeLevel: MUTE(-1), LEVEL_1(0), LEVEL_2(1), LEVEL_3(2); BrightnessLevel: LEVEL_1(0)..LEVEL_3(2)
 * (no mute) -- mirrors firmware's QuickSettings::VolumeLevel / BrightnessLevel enums. */
static int quick_volume_level = -1;
static int quick_brightness_level = 0;
static lv_obj_t *settings_wlan_password;
static lv_obj_t *wlan_keyboard_container, *wlan_keyboard;
static lv_obj_t *softap_qr, *softap_info_label;
/* Single source of truth for the WLAN on/off state, mirrored (like firmware's shared
 * NVS-backed flag) onto the Settings > WLAN switch, the Quick Settings Wi-Fi button and
 * status icon, and the visibility of the WLAN screen's connected/available/SoftAP groups. */
static bool wlan_enabled = true;
static bool wlan_managed_externally = false;
static lv_indev_t *gesture_input;
static lv_timer_t *gesture_poll_timer;
static int page_index;
static lv_point_t home_gesture_start;
static lv_point_t launcher_gesture_start;
static uint32_t home_gesture_started_at;
static int home_gesture_bar_width;
static bool home_gesture_tracking;
static bool launcher_gesture_tracking;
static bool quick_gesture_tracking;
static bool quick_gesture_from_bottom;
static bool quick_open;

static void quick_time_update(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if(quick_time_label == NULL) return;

    time_t now = time(NULL);
    struct tm local;
    char formatted[16];
    localtime_r(&now, &local);
    if(strftime(formatted, sizeof(formatted), "%I:%M %p", &local) > 0) {
        lv_label_set_text(quick_time_label, formatted);
    }
}

static void show_settings(lv_event_t *e);
static void show_timer(lv_event_t *e);
static void show_launcher_long(lv_event_t *e);
static void render_page(void);
static void create_settings_subpages(void);
static lv_obj_t *scaled_image(lv_obj_t *parent, const lv_image_dsc_t *source, int32_t size);
static void quick_settings_set_y(int32_t y);
static void animate_quick_settings(int32_t target_y, bool opening);
static void close_quick_settings_immediate(void);
static void set_wlan_enabled(bool enabled, lv_obj_t *source);
static void quick_wifi_button_changed(lv_event_t *e);
static void sync_password_keyboard(bool show);

typedef struct {
    uint32_t child_idx;
    lv_obj_t *child;
} ui_comp_get_child_t;

static uint32_t s_ui_comp_event_id;

void esp_brookesia_squareline_ui_comp_init(void)
{
    if(s_ui_comp_event_id != 0) return;
    s_ui_comp_event_id = lv_event_register_id();
}

lv_event_code_t ui_comp_get_event_code(void)
{
    return s_ui_comp_event_id;
}

#define LV_EVENT_GET_COMP_CHILD ui_comp_get_event_code()

lv_obj_t *ui_comp_get_child(lv_obj_t *comp, uint32_t child_idx)
{
    ui_comp_get_child_t info = { .child_idx = child_idx, .child = NULL };
    lv_obj_send_event(comp, LV_EVENT_GET_COMP_CHILD, &info);
    return info.child;
}

void get_component_child_event_cb(lv_event_t *e)
{
    lv_obj_t **c = lv_event_get_user_data(e);
    ui_comp_get_child_t *info = lv_event_get_param(e);
    info->child = c[info->child_idx];
}

void del_component_child_event_cb(lv_event_t *e)
{
    lv_free(lv_event_get_user_data(e));
}

void _ui_arc_set_text_value(lv_obj_t *target, lv_obj_t *source,
                            const char *prefix, const char *postfix)
{
    lv_label_set_text_fmt(target, "%s%" PRId32 "%s", prefix, lv_arc_get_value(source), postfix);
}

static void bare(lv_obj_t *o)
{
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *screen_content(lv_obj_t *screen, lv_color_t color)
{
    bare(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    lv_obj_t *content = lv_obj_create(screen);
    bare(content);
    lv_obj_set_size(content, SIZE, SIZE);
    lv_obj_center(content);
    lv_obj_set_style_bg_color(content, color, 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
#if !defined(ESP_PLATFORM)
    /* SDL uses a square window, so mask it to emulate the round panel. On
     * hardware the physical VoCat display already provides that mask; keeping
     * the LVGL mask there only adds software clipping work. */
    lv_obj_set_style_radius(content, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(content, true, 0);
#endif
    lv_obj_set_style_text_color(content, WHITE, 0);
    return content;
}

static lv_obj_t *text(lv_obj_t *parent, const char *value, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *o = lv_label_create(parent);
    lv_label_set_text(o, value);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, color, 0);
    return o;
}

static void load(lv_obj_t *o)
{
    close_quick_settings_immediate();
    sync_password_keyboard(o == settings_wlan_verify);
    lv_screen_load_anim(o, LV_SCR_LOAD_ANIM_FADE_IN, 120, 0, false);
}
static void show_settings(lv_event_t *e) { LV_UNUSED(e); load(settings); }
static void show_timer(lv_event_t *e)
{
    LV_UNUSED(e);
    load(timer_screen);
    /* Boot-style flip flourish every time Clock becomes the foreground app
     * (app launch, or returning from Settings/Launcher) -- ported behavior
     * from the reference project's clock_app_show(). */
    flip_clock_replay(timer_clock_widget);
}
static void show_launcher_long(lv_event_t *e) { LV_UNUSED(e); load(launcher); }

static void show_screen_ref(lv_event_t *e)
{
    lv_obj_t **screen_ref = lv_event_get_user_data(e);
    if(screen_ref != NULL && *screen_ref != NULL) load(*screen_ref);
}

static const app_t apps[] = {
    // Keep the two system apps fixed on page one. Future apps are appended so
    // Clock and Settings do not move when the launcher grows.
    {"Clock", &img_app_timer, show_timer},
    {"Settings", &esp_brookesia_app_icon_launcher_settings_112_112, show_settings},
};

static int launcher_app_count(void)
{
    return (int)(sizeof(apps) / sizeof(apps[0]));
}

static int launcher_page_count(void)
{
    return (launcher_app_count() + 1) / 2;
}

static void set_app_icon_state(lv_obj_t *image, int32_t size, int32_t scale)
{
    lv_obj_t *frame = lv_obj_get_parent(image);
    lv_obj_invalidate(frame);
    lv_image_set_scale(image, scale);
    lv_obj_set_size(image, size, size);
    lv_obj_refr_size(image);
    lv_obj_center(image);
    lv_obj_invalidate(frame);
}

static void image_pressed(lv_event_t *e)
{
    set_app_icon_state(lv_event_get_current_target_obj(e), APP_ICON_PRESSED_SIZE, APP_ICON_PRESSED_SCALE);
}

static void image_released(lv_event_t *e)
{
    set_app_icon_state(lv_event_get_current_target_obj(e), APP_ICON_DEFAULT_SIZE, APP_ICON_DEFAULT_SCALE);
}

static void app_icon(lv_obj_t *parent, const app_t *app, int x)
{
    lv_obj_t *box = lv_obj_create(parent);
    bare(box); lv_obj_set_size(box, 140, 140); lv_obj_set_pos(box, x, 20);
    lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *frame = lv_obj_create(box);
    bare(frame); lv_obj_set_size(frame, APP_ICON_DEFAULT_SIZE, APP_ICON_DEFAULT_SIZE);
    lv_obj_align(frame, LV_ALIGN_TOP_MID, 0, 6); lv_obj_add_flag(frame, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_t *image = lv_image_create(frame);
    bare(image); lv_image_set_src(image, app->image); lv_image_set_scale(image, APP_ICON_DEFAULT_SCALE);
    lv_obj_set_size(image, APP_ICON_DEFAULT_SIZE, APP_ICON_DEFAULT_SIZE);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER); lv_obj_center(image);
    lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(image, image_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(image, image_released, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(image, image_released, LV_EVENT_RELEASED, NULL);
    if(app->cb) lv_obj_add_event_cb(image, app->cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *name = text(box, app->name, &esp_brookesia_font_maison_neue_book_16, WHITE);
    lv_obj_add_flag(name, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -7);
}

static void dot_click(lv_event_t *e)
{
    page_index = (int)(intptr_t)lv_event_get_user_data(e);
    render_page();
}

static void render_dots(void)
{
    lv_obj_clean(dots);
    int page_count = launcher_page_count();
    int total = 40 + ((page_count - 1) * 12) + ((page_count - 1) * 10);
    int x = (SIZE - total) / 2;
    for(int i = 0; i < page_count; i++) {
        int w = i == page_index ? 40 : 12;
        lv_obj_t *dot = lv_obj_create(dots);
        bare(dot); lv_obj_set_size(dot, w, 12); lv_obj_set_pos(dot, x, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, i == page_index ? WHITE : lv_color_hex(0xC6C6C6), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(dot, dot_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        x += w + 10;
    }
}

static void render_page(void)
{
    lv_obj_clean(page_box);
    int app_count = launcher_app_count();
    int page_count = launcher_page_count();
    if(page_index >= page_count) page_index = page_count - 1;
    int first = page_index * 2;
    if(first < app_count) app_icon(page_box, &apps[first], 26);
    if(first + 1 < app_count) app_icon(page_box, &apps[first + 1], 192);
    render_dots();
}

static lv_obj_t *active_home_bar(void)
{
    if(quick_open) return quick_home_bar;

    lv_obj_t *active = lv_screen_active();
    if(active == launcher) return launcher_home_bar;
    if(active == settings) return settings_home_bar;
    if(active == settings_wlan) return wlan_home_bar;
    if(active == settings_sound) return sound_home_bar;
    if(active == settings_display) return display_home_bar;
    if(active == settings_about) return about_home_bar;
    if(active == settings_self_test) return self_test_home_bar;
    if(active == settings_developer) return developer_home_bar;
    if(active == settings_restore) return factory_home_bar;
    if(active == settings_wlan_verify) return wlan_verify_home_bar;
    if(active == settings_softap) return softap_home_bar;
    if(active == ai) return ai_home_bar;
    if(active == timer_screen) return timer_home_bar;
    return NULL;
}

static int home_bar_width_for_offset(int offset)
{
    if(offset < 0) offset = 0;
    if(offset > HOME_GESTURE_RANGE) offset = HOME_GESTURE_RANGE;
    return HOME_BAR_WIDTH - (offset * HOME_BAR_WIDTH / HOME_GESTURE_RANGE);
}

static void set_home_bar_width(void *bar_ptr, int32_t width)
{
    lv_obj_t *bar = bar_ptr;
    if(width < 0) width = 0;
    if(width > HOME_BAR_WIDTH) width = HOME_BAR_WIDTH;
    lv_obj_set_width(bar, width);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, HOME_BAR_BOTTOM_OFFSET);
}

static void restore_home_bar(lv_obj_t *bar)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, bar);
    lv_anim_set_values(&animation, lv_obj_get_width(bar), HOME_BAR_WIDTH);
    lv_anim_set_duration(&animation, 500);
    lv_anim_set_path_cb(&animation, lv_anim_path_bounce);
    lv_anim_set_exec_cb(&animation, set_home_bar_width);
    lv_anim_start(&animation);
}

static void update_home_bar_from_point(const lv_point_t *point)
{
    lv_obj_t *bar = active_home_bar();
    if(!home_gesture_tracking || bar == NULL) return;

    int offset = home_gesture_start.y - point->y;
    home_gesture_bar_width = home_bar_width_for_offset(offset);
    set_home_bar_width(bar, home_gesture_bar_width);
}

static void poll_home_gesture(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    if(gesture_input == NULL || (!home_gesture_tracking && !quick_gesture_tracking) ||
       lv_indev_get_state(gesture_input) != LV_INDEV_STATE_PRESSED) return;

    lv_point_t point;
    lv_indev_get_point(gesture_input, &point);
    if(home_gesture_tracking) update_home_bar_from_point(&point);
    if(quick_gesture_tracking) quick_settings_set_y(point.y - SIZE);
}

static void input_gesture_event(lv_event_t *e)
{
    lv_indev_t *input = lv_event_get_target(e);
    lv_obj_t *bar = active_home_bar();
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t point;
    if(input == NULL) return;
    lv_indev_get_point(input, &point);

    if(code == LV_EVENT_PRESSED) {
        launcher_gesture_tracking = lv_screen_active() == launcher;
        if(launcher_gesture_tracking) launcher_gesture_start = point;

        /* Idle is a stripped-down "screensaver" overlay: only the long-press-to-Launcher
         * gesture (registered directly on its content object, see create_idle()) is live
         * there -- both the top-edge drag-down-to-Quick-Settings below and the swipe-up
         * Home gesture further down are disabled while it's the active screen. (Swipe-up
         * is already excluded for free: active_home_bar() has no case for `idle`, so
         * home_gesture_tracking never gets set while Idle is active.) */
        bool idle_active = lv_screen_active() == idle;

        /* A press starting in the top QUICK_GESTURE_EDGE px, on any screen other than
         * Quick Settings itself, begins a live drag-down preview -- matches the
         * firmware's GESTURE_AREA_TOP_EDGE handling, which isn't limited to one screen.
         * A press starting in the bottom edge band while Quick Settings is already open
         * begins the equivalent drag-to-close instead (firmware's GESTURE_AREA_BOTTOM_EDGE
         * branch, active only while it's visible) -- it takes over from the ordinary Home
         * gesture below so the panel scrolls with the finger instead of snapping shut on
         * any small swipe. */
        quick_gesture_tracking = false;
        if(!idle_active && !quick_open && point.y <= QUICK_GESTURE_EDGE) {
            quick_gesture_tracking = true;
            quick_gesture_from_bottom = false;
            lv_anim_delete(quick, NULL);
            lv_obj_remove_flag(quick, LV_OBJ_FLAG_HIDDEN);
            quick_settings_set_y(point.y - SIZE);
            /* Quick Settings and the WLAN keyboard both live on the top layer, so
             * opening one over the other would otherwise stack the keyboard's opaque
             * background right on top of the panel sliding into view. Hide it as soon as
             * the drag starts; tapping the password field again (if still on that screen
             * once Quick Settings closes) brings it back, same as the manual dismiss key. */
            sync_password_keyboard(false);
        }
        else if(quick_open && point.y >= SIZE - HOME_GESTURE_RANGE) {
            quick_gesture_tracking = true;
            quick_gesture_from_bottom = true;
            lv_anim_delete(quick, NULL);
            quick_settings_set_y(point.y - SIZE);
        }

        if(quick_gesture_tracking || bar == NULL || point.y < SIZE - 20) {
            home_gesture_tracking = false;
            return;
        }
        home_gesture_tracking = true;
        home_gesture_start = point;
        home_gesture_started_at = lv_tick_get();
        home_gesture_bar_width = HOME_BAR_WIDTH;
        lv_anim_delete(bar, set_home_bar_width);
        set_home_bar_width(bar, HOME_BAR_WIDTH);
    }
    else if(code == LV_EVENT_RELEASED && launcher_gesture_tracking) {
        int distance_x = point.x - launcher_gesture_start.x;
        int distance_y = point.y - launcher_gesture_start.y;
        int distance_x_abs = distance_x < 0 ? -distance_x : distance_x;
        int distance_y_abs = distance_y < 0 ? -distance_y : distance_y;
        launcher_gesture_tracking = false;
        if(distance_x_abs > 20 && distance_x_abs * 173 > distance_y_abs * 100) {
            if(distance_x < 0 && page_index + 1 < launcher_page_count()) page_index++;
            else if(distance_x > 0 && page_index > 0) page_index--;
            render_page();
        }
    }
    else if(code == LV_EVENT_PRESS_LOST) {
        launcher_gesture_tracking = false;
    }

    if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && home_gesture_tracking) {
        int distance_y = home_gesture_start.y - point.y;
        int distance_x = point.x - home_gesture_start.x;
        if(distance_x < 0) distance_x = -distance_x;
        uint32_t duration = lv_tick_elaps(home_gesture_started_at);
        home_gesture_bar_width = home_bar_width_for_offset(distance_y);
        set_home_bar_width(bar, home_gesture_bar_width);
        home_gesture_tracking = false;
        if(code == LV_EVENT_RELEASED && home_gesture_bar_width <= HOME_BAR_COMPLETE_WIDTH &&
           distance_y * 100 > distance_x * 173 && duration < 800) {
            /* Swiping up from the launcher goes to the idle screen; swiping up from any
             * other screen returns to the launcher first, matching the firmware's
             * single-hop Home gesture. (Quick Settings has its own bottom-edge drag-to-
             * close above, so home_gesture_tracking is never set while it's open.) */
            load(lv_screen_active() == launcher ? idle : launcher);
        }
        restore_home_bar(bar);
    }

    if((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && quick_gesture_tracking) {
        quick_gesture_tracking = false;
        bool open;
        if(quick_gesture_from_bottom) {
            /* Mirrors firmware's bottom_threshold_percent = 80: releasing low on the
             * screen (barely dragged up from where the finger started) springs back
             * open -- a short swipe just scrolls the panel instead of closing it; only
             * dragging up past that point commits to closing. */
            open = code == LV_EVENT_RELEASED && point.y > QUICK_CLOSE_THRESHOLD;
        } else {
            /* Mirrors firmware's top_threshold_percent = 20: dragging down past it
             * commits to opening; short of it, it snaps back closed. */
            open = code == LV_EVENT_RELEASED && point.y > QUICK_OPEN_THRESHOLD;
        }
        animate_quick_settings(open ? 0 : -SIZE, open);
    }
}

static void quick_settings_set_y(int32_t y)
{
    if(y > 0) y = 0;
    if(y < -SIZE) y = -SIZE;
    lv_obj_set_pos(quick, 0, y);
}

static void quick_settings_anim_set_y(void *obj, int32_t y) { lv_obj_set_pos(obj, 0, y); }

static void quick_settings_anim_ready(lv_anim_t *a)
{
    LV_UNUSED(a);
    if(lv_obj_get_y(quick) <= -SIZE) lv_obj_add_flag(quick, LV_OBJ_FLAG_HIDDEN);
}

/* Quick Settings lives on the LVGL top layer (drawn above whatever screen is active)
 * instead of being its own screen, so opening/closing it can be a real drag: the
 * actual panel slides into view following the finger (quick_settings_set_y, called
 * from the press/poll/release plumbing below) and, on release, animates the rest of
 * the way open or closed here -- matching the firmware's press/pressing/release drag
 * on `GESTURE_AREA_TOP_EDGE` plus its moveY_ToWithAnimation() snap, instead of a hard
 * screen swap to a separate placeholder. */
static void animate_quick_settings(int32_t target_y, bool opening)
{
    lv_obj_remove_flag(quick, LV_OBJ_FLAG_HIDDEN);
    lv_anim_delete(quick, NULL);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, quick);
    lv_anim_set_values(&animation, lv_obj_get_y(quick), target_y);
    lv_anim_set_duration(&animation, 180);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&animation, quick_settings_anim_set_y);
    lv_anim_set_ready_cb(&animation, quick_settings_anim_ready);
    lv_anim_start(&animation);
    quick_open = opening;
}

static lv_obj_t *create_home_indicator(lv_obj_t *content)
{
    lv_obj_t *bar = lv_bar_create(content);
    bare(bar);
    lv_obj_set_size(bar, HOME_BAR_WIDTH, HOME_BAR_HEIGHT);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, HOME_BAR_BOTTOM_OFFSET);
    lv_obj_set_style_radius(bar, 5, 0);
    lv_obj_set_style_bg_color(bar, BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(bar, 1, 0);
    lv_obj_set_style_radius(bar, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, WHITE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 100, LV_ANIM_OFF);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_FLOATING);
    return bar;
}

static void create_launcher(void)
{
    launcher = lv_obj_create(NULL); lv_obj_t *content = screen_content(launcher, BG);
    page_box = lv_obj_create(content); bare(page_box); lv_obj_set_size(page_box, SIZE, 180);
    lv_obj_add_flag(page_box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_align(page_box, LV_ALIGN_CENTER, 0, 0);
    dots = lv_obj_create(content); bare(dots); lv_obj_set_size(dots, SIZE, 36);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -30);
    /* Quick Settings opens via the top-edge drag gesture (see input_gesture_event),
     * from this screen or any other -- there is no dedicated tap zone, matching HW. */
    launcher_home_bar = create_home_indicator(content);
    page_index = 0; render_page();
}

static void create_idle(void)
{
    idle = lv_obj_create(NULL);
    lv_obj_t *content = screen_content(idle, lv_color_hex(0x000000));
    lv_obj_add_flag(content, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(content, show_launcher_long, LV_EVENT_LONG_PRESSED, NULL);
}

/* Quick Settings Volume/Brightness: firmware cycles the level on a short click (icon
 * image swapped to match) and on long press closes Quick Settings and jumps straight to
 * the matching Settings sub-page -- see QuickSettings::setVolume()/setBrightness() and
 * Manager::processQuickSettingsEventSignal()/system.cpp's quick-settings signal handler. */
static void quick_update_volume_icon(void)
{
    if(quick_volume_button == NULL) return;
    const lv_image_dsc_t *src = &speaker_image_middle_quick_settings_volume_high_48_48;
    if(quick_volume_level <= -1) src = &speaker_image_middle_quick_settings_volume_off_48_48;
    else if(quick_volume_level == 0) src = &speaker_image_middle_quick_settings_volume_low_48_48;
    else if(quick_volume_level == 1) src = &speaker_image_middle_quick_settings_volume_medium_48_48;
    lv_obj_set_style_bg_image_src(quick_volume_button, src, 0);
}

static void quick_update_brightness_icon(void)
{
    if(quick_brightness_button == NULL) return;
    const lv_image_dsc_t *src = &speaker_image_middle_quick_settings_brightness_high_48_48;
    if(quick_brightness_level == 0) src = &speaker_image_middle_quick_settings_brightness_low_48_48;
    else if(quick_brightness_level == 1) src = &speaker_image_middle_quick_settings_brightness_medium_48_48;
    lv_obj_set_style_bg_image_src(quick_brightness_button, src, 0);
}

static void quick_volume_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    quick_volume_level++;
    if(quick_volume_level > 2) quick_volume_level = -1; /* MAX wraps back to MUTE */
    quick_update_volume_icon();
}

static void quick_volume_long_pressed(lv_event_t *e)
{
    LV_UNUSED(e);
    load(settings_sound);
}

static void quick_brightness_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    quick_brightness_level++;
    if(quick_brightness_level > 2) quick_brightness_level = 0; /* MAX wraps back to LEVEL_1 */
    quick_update_brightness_icon();
}

static void quick_brightness_long_pressed(lv_event_t *e)
{
    LV_UNUSED(e);
    load(settings_display);
}

static void create_quick(void)
{
    quick = lv_obj_create(lv_layer_top());
    bare(quick);
    /* screen_content() re-bare()s its `screen` argument as its first step, which wipes
     * any size set beforehand -- unlike a real LVGL screen (auto-sized to the display),
     * a plain top-layer child has no such fallback, so the real SIZE x SIZE is set again
     * further down, once nothing will reset it. */
    lv_obj_t *content = screen_content(quick, BG);
    /* screen_content() always paints the outer `screen` object opaque BLACK and only
     * circularly clips the inner `content` child to the panel's own BG color -- fine for
     * a full screen whose square corners are never shown, but Quick Settings is dragged
     * partially into view, exposing `quick`'s own square corners around the circular
     * `content` fill. Match `quick`'s own background to `content`'s color so no
     * black-vs-BG seam (straight or curved) is visible while the panel is mid-drag. */
    lv_obj_set_style_bg_color(quick, BG, 0);
    lv_obj_t *quick_component = ui_ContainerQuickSettings_create(content);
    lv_obj_t *buttons = lv_obj_get_child(quick_component, 1);
    quick_wifi_button = lv_obj_get_child(lv_obj_get_child(buttons, 0), 0);
    /* Initial checked state is applied later by set_wlan_enabled(), once every screen
     * that shares the WLAN on/off state (this button, the Settings switch, the WLAN
     * screen's groups) has been created. */
    lv_obj_add_event_cb(quick_wifi_button, quick_wifi_button_changed, LV_EVENT_VALUE_CHANGED, NULL);

    /* Volume/Brightness buttons are siblings 1 and 2 of `buttons` (wifi is sibling 0),
     * matching ui_comp_quicksettings.c's cui_ContainerButtonsVolume/Brightness order. */
    quick_volume_button = lv_obj_get_child(lv_obj_get_child(buttons, 1), 0);
    quick_brightness_button = lv_obj_get_child(lv_obj_get_child(buttons, 2), 0);
    lv_obj_add_event_cb(quick_volume_button, quick_volume_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(quick_volume_button, quick_volume_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_event_cb(quick_brightness_button, quick_brightness_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(quick_brightness_button, quick_brightness_long_pressed, LV_EVENT_LONG_PRESSED, NULL);
    quick_update_volume_icon();
    quick_update_brightness_icon();

    /* Small Wi-Fi status icon in the top status row (distinct from the big button above):
     * quick_component -> status[0] -> internal[0] -> top[0] -> right[1] -> wifi icon[0],
     * mirroring the child order firmware builds in ui_comp_quicksettings.c. */
    lv_obj_t *status = lv_obj_get_child(quick_component, 0);
    lv_obj_t *status_top = lv_obj_get_child(lv_obj_get_child(status, 0), 0);
    quick_time_label = lv_obj_get_child(status_top, 0);
    lv_obj_t *status_right = lv_obj_get_child(status_top, 1);
    quick_wifi_status_icon = lv_obj_get_child(status_right, 0);
    quick_battery_status_icon = lv_obj_get_child(status_right, 1);
    quick_battery_percent_label = lv_obj_get_child(status_right, 2);
    quick_time_update(NULL);
    lv_timer_create(quick_time_update, 1000, NULL);

    lv_obj_t *memory = lv_obj_get_child(quick_component, 2);
    lv_obj_t *memory_internal = lv_obj_get_child(memory, 0);
    lv_bar_set_value(lv_obj_get_child(lv_obj_get_child(memory_internal, 0), 1), 43, LV_ANIM_OFF);
    lv_bar_set_value(lv_obj_get_child(lv_obj_get_child(memory_internal, 1), 1), 61, LV_ANIM_OFF);
    quick_home_bar = create_home_indicator(content);

    lv_obj_set_size(quick, SIZE, SIZE);
    lv_obj_update_layout(quick);
    lv_obj_set_pos(quick, 0, -SIZE);
    lv_obj_add_flag(quick, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *group(lv_obj_t *parent, int y, const char *title_value)
{
    LV_UNUSED(y);
    lv_obj_t *main = lv_obj_create(parent);
    bare(main);
    lv_obj_set_size(main, 288, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(main, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(main, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(main, 8, 0);
    if(title_value[0] != '\0') {
        text(main, title_value, &esp_brookesia_font_maison_neue_book_20, MUTED);
    }

    lv_obj_t *panel = lv_obj_create(main); bare(panel); lv_obj_set_size(panel, 288, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_bg_color(panel, PANEL, 0); lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_top(panel, 8, 0); lv_obj_set_style_pad_bottom(panel, 8, 0);
    lv_obj_set_style_pad_left(panel, 16, 0); lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_row(panel, 0, 0);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return panel;
}

static lv_obj_t *scaled_image(lv_obj_t *parent, const lv_image_dsc_t *source, int32_t size)
{
    lv_obj_t *image = lv_image_create(parent);
    bare(image);
    lv_image_set_src(image, source);
    lv_image_set_scale(image, LV_SCALE_NONE * size / source->header.w);
    lv_obj_set_size(image, size, size);
    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_CENTER);
    lv_obj_refr_size(image);
    return image;
}

static void add_cell_separator(lv_obj_t *cell)
{
    int32_t start = (int32_t)(intptr_t)lv_obj_get_user_data(cell);
    lv_obj_t *separator = lv_obj_create(cell);
    bare(separator);
    lv_obj_set_size(separator, 268 - start, 2);
    lv_obj_align(separator, LV_ALIGN_BOTTOM_LEFT, start, 0);
    lv_obj_set_style_bg_color(separator, WHITE, 0);
    lv_obj_set_style_bg_opa(separator, 64, 0);
}

static lv_obj_t *cell(lv_obj_t *parent, int32_t height, int32_t split_start)
{
    uint32_t count = lv_obj_get_child_count(parent);
    if(count > 0) add_cell_separator(lv_obj_get_child(parent, (int32_t)count - 1));

    lv_obj_t *cell_obj = lv_obj_create(parent);
    bare(cell_obj);
    lv_obj_set_size(cell_obj, 288, height);
    lv_obj_set_user_data(cell_obj, (void *)(intptr_t)split_start);
    return cell_obj;
}

static lv_obj_t *row(lv_obj_t *parent, const lv_image_dsc_t *icon, const char *name,
                     const char *value, bool show_arrow)
{
    lv_obj_t *r = cell(parent, 48, 72);
    lv_obj_t *i = scaled_image(r, icon, 36); lv_obj_align(i, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t *n = text(r, name, &esp_brookesia_font_maison_neue_book_22, WHITE); lv_obj_align(n, LV_ALIGN_LEFT_MID, 72, 0);
    if(value) {
        lv_obj_t *v = text(r, value, &esp_brookesia_font_maison_neue_book_20, MUTED);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, show_arrow ? -52 : -20, 0);
    }
    if(show_arrow) {
        lv_obj_t *arrow = scaled_image(r, &esp_brookesia_app_icon_arrow_right_48_48, 24);
        lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -20, 0);
    }
    return r;
}

static void cell_pressed(lv_event_t *e)
{
    lv_obj_t *cell = lv_event_get_current_target_obj(e);
    lv_obj_set_style_radius(cell, 8, 0);
    lv_obj_set_style_bg_color(cell, WHITE, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_10, 0);
}

static void cell_released(lv_event_t *e)
{
    lv_obj_set_style_bg_opa(lv_event_get_current_target_obj(e), LV_OPA_TRANSP, 0);
}

static void make_clickable(lv_obj_t *cell, lv_event_cb_t callback, void *user_data)
{
    lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cell, cell_pressed, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(cell, cell_released, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(cell, cell_released, LV_EVENT_PRESS_LOST, NULL);
    if(callback != NULL) lv_obj_add_event_cb(cell, callback, LV_EVENT_CLICKED, user_data);
}

static lv_obj_t *add_switch(lv_obj_t *parent, bool checked)
{
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 64, 32);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(sw, MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, RED, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, WHITE, LV_PART_KNOB);
    lv_obj_set_style_width(sw, 26, LV_PART_KNOB);
    lv_obj_set_style_height(sw, 26, LV_PART_KNOB);
    if(checked) lv_obj_add_state(sw, LV_STATE_CHECKED);
    return sw;
}

static lv_obj_t *plain_row_bound(lv_obj_t *parent, const char *name, const char *value,
                                 lv_obj_t **value_label_out)
{
    lv_obj_t *r = cell(parent, 48, 20);
    lv_obj_t *name_label = text(r, name, &esp_brookesia_font_maison_neue_book_22, WHITE);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 20, 0);
    if(value != NULL) {
        lv_obj_t *value_label = text(r, value, &esp_brookesia_font_maison_neue_book_20, MUTED);
        lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -20, 0);
        if(value_label_out != NULL) *value_label_out = value_label;
    }
    return r;
}

static lv_obj_t *plain_row(lv_obj_t *parent, const char *name, const char *value)
{
    return plain_row_bound(parent, name, value, NULL);
}

static lv_obj_t *double_value_row_bound(lv_obj_t *parent, const char *name,
                                        const char *main_value, const char *minor_value,
                                        lv_obj_t **main_label_out, lv_obj_t **minor_label_out)
{
    lv_obj_t *r = cell(parent, 72, 20);
    lv_obj_t *name_label = text(r, name, &esp_brookesia_font_maison_neue_book_22, WHITE);
    lv_obj_align(name_label, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t *main_label = text(r, main_value, &esp_brookesia_font_maison_neue_book_20, MUTED);
    lv_obj_align(main_label, LV_ALIGN_TOP_RIGHT, -20, 8);
    lv_obj_t *minor_label = text(r, minor_value, &esp_brookesia_font_maison_neue_book_20, MUTED);
    lv_obj_align(minor_label, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
    if(main_label_out != NULL) *main_label_out = main_label;
    if(minor_label_out != NULL) *minor_label_out = minor_label;
    return r;
}

static lv_obj_t *network_row(lv_obj_t *parent, const char *ssid, const char *status,
                             const lv_image_dsc_t *signal, bool locked)
{
    lv_obj_t *r = cell(parent, status ? 72 : 48, 20);
    lv_obj_t *name_label = text(r, ssid, &esp_brookesia_font_maison_neue_book_22, WHITE);
    lv_obj_align(name_label, status ? LV_ALIGN_TOP_LEFT : LV_ALIGN_LEFT_MID, 20, status ? 8 : 0);
    lv_obj_set_width(name_label, 200);
    if(status != NULL) {
        lv_obj_t *status_label = text(r, status, &esp_brookesia_font_maison_neue_book_20, MUTED);
        lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 20, -8);
    }
    if(signal != NULL) {
        lv_obj_t *icons = lv_obj_create(r);
        bare(icons); lv_obj_set_size(icons, locked ? 52 : 24, 24);
        lv_obj_align(icons, LV_ALIGN_RIGHT_MID, -20, 0);
        lv_obj_set_flex_flow(icons, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(icons, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(icons, 4, 0);
        scaled_image(icons, signal, 24);
        if(locked) scaled_image(icons, &esp_brookesia_app_icon_wlan_lock_48_48, 24);
    }
    return r;
}

static lv_obj_t *slider_row(lv_obj_t *parent, const lv_image_dsc_t *left_source,
                            const lv_image_dsc_t *right_source, int value)
{
    lv_obj_t *r = cell(parent, 48, 72);
    lv_obj_t *left = scaled_image(r, left_source, 36);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_t *right = scaled_image(r, right_source, 36);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_t *slider = lv_slider_create(r);
    lv_obj_set_size(slider, 173, 24);
    lv_obj_center(slider);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, MUTED, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, RED, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_width(slider, 0, LV_PART_KNOB);
    lv_obj_set_style_height(slider, 0, LV_PART_KNOB);
    return slider;
}

static void keep_header_in_foreground(lv_event_t *e)
{
    lv_obj_t *header = lv_event_get_user_data(e);
    lv_obj_t *content = lv_obj_get_parent(header);
    lv_obj_move_to_index(header, lv_obj_get_child_count(content) - 1);
}

static lv_obj_t *create_settings_content(lv_obj_t *content)
{
    lv_obj_t *scroller = lv_obj_create(content);
    bare(scroller);
    lv_obj_set_size(scroller, SIZE, 302);
    lv_obj_align(scroller, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_scroll_dir(scroller, LV_DIR_VER);
    lv_obj_add_flag(scroller, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scroller, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(scroller, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scroller, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(scroller, 16, 0);
    lv_obj_set_style_pad_bottom(scroller, 76, 0);
    lv_obj_set_style_pad_left(scroller, 0, 0);
    lv_obj_set_style_pad_right(scroller, 0, 0);
    lv_obj_set_style_pad_row(scroller, 16, 0);
    lv_obj_set_style_clip_corner(scroller, true, 0);

    /* The bottom HOME_GESTURE_RANGE px are the system's Home-gesture edge,
     * not list content: `scroller` extends underneath that band, so without
     * this guard a press starting there would both scroll the list and
     * drive the Home gesture at once. A transparent, non-scrollable strip
     * on top of the scroller in z-order intercepts the press first, so the
     * list never sees (and never scrolls for) a touch that starts in the
     * gesture band -- matching the firmware, where that edge belongs to the
     * gesture layer exclusively. */
    lv_obj_t *gesture_guard = lv_obj_create(content);
    bare(gesture_guard);
    lv_obj_set_size(gesture_guard, SIZE, HOME_GESTURE_RANGE);
    lv_obj_align(gesture_guard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(gesture_guard, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(gesture_guard, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);

    return scroller;
}

static lv_obj_t *create_settings_child(lv_obj_t **screen, lv_obj_t **home_bar,
                                       const char *back_name, lv_obj_t **back_screen)
{
    *screen = lv_obj_create(NULL);
    lv_obj_t *content = screen_content(*screen, BG);
    lv_obj_t *scroller = create_settings_content(content);

    lv_obj_t *header = lv_obj_create(content);
    bare(header);
    lv_obj_set_size(header, SIZE, 48);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(header, BG, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_FLOATING);
    lv_obj_add_event_cb(scroller, keep_header_in_foreground, LV_EVENT_SCROLL, header);

    lv_obj_t *back = lv_obj_create(header);
    bare(back); lv_obj_set_size(back, LV_SIZE_CONTENT, LV_SIZE_CONTENT); lv_obj_center(back);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(back, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(back, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *back_icon = scaled_image(back, &esp_brookesia_app_icon_arrow_left_48_48, 24);
    lv_obj_set_style_image_recolor(back_icon, RED, 0);
    lv_obj_set_style_image_recolor_opa(back_icon, LV_OPA_COVER, 0);
    text(back, back_name, &esp_brookesia_font_maison_neue_book_24, RED);
    lv_obj_add_event_cb(back, show_screen_ref, LV_EVENT_CLICKED, back_screen);
    *home_bar = create_home_indicator(content);
    return scroller;
}

static void create_settings(void)
{
    settings = lv_obj_create(NULL);
    lv_obj_t *content = screen_content(settings, BG);
    settings_scroller = create_settings_content(content);
    lv_obj_t *wireless = group(settings_scroller, 62, "Wireless");
    lv_obj_t *wlan_row = row(wireless, &esp_brookesia_app_icon_wireless_wlan_48_48,
                             "WLAN", "On", true);
    settings_wlan_value_label = lv_obj_get_child(wlan_row, 2);
    make_clickable(wlan_row, show_screen_ref, &settings_wlan);

    lv_obj_t *media = group(settings_scroller, 166, "Media");
    lv_obj_t *sound_row = row(media, &esp_brookesia_app_icon_media_sound_48_48,
                              "Sound", NULL, true);
    make_clickable(sound_row, show_screen_ref, &settings_sound);
    lv_obj_t *display_row = row(media, &esp_brookesia_app_icon_media_display_48_48,
                                "Display", NULL, true);
    make_clickable(display_row, show_screen_ref, &settings_display);

    lv_obj_t *input = group(settings_scroller, 318, "Input");
    lv_obj_t *touch_row = row(input, &esp_brookesia_app_icon_input_touch_48_48,
                              "Touch", NULL, false);
    settings_touch_switch = add_switch(touch_row, true);

    lv_obj_t *more = group(settings_scroller, 422, "More");
    lv_obj_t *about_row = row(more, &esp_brookesia_app_icon_more_about_48_48,
                              "About", NULL, true);
    make_clickable(about_row, show_screen_ref, &settings_about);
    lv_obj_t *self_test_row = row(more, &esp_brookesia_app_icon_more_developer_mode_48_48,
                                  "Self-test", NULL, true);
    make_clickable(self_test_row, show_screen_ref, &settings_self_test);
    settings_developer_row = row(more, &esp_brookesia_app_icon_more_developer_mode_48_48,
                                 "Developer Mode", NULL, false);
    make_clickable(settings_developer_row, show_screen_ref, &settings_developer);
    lv_obj_t *restore_row = row(more, &esp_brookesia_app_icon_more_restart_48_48,
                                "Restore Factory", NULL, false);
    make_clickable(restore_row, show_screen_ref, &settings_restore);
    settings_home_bar = create_home_indicator(content);
}

/* Single point of truth for WLAN on/off, mirroring firmware's shared NVS-backed flag:
 * flipping it from either UI (the Settings > WLAN switch or the Quick Settings Wi-Fi
 * button) updates every dependent widget. `source` is the widget that already reflects
 * the new state (because the user just toggled it) and so is skipped when re-applying
 * the checked state, avoiding a redundant VALUE_CHANGED re-entry. */
static void wlan_entry_reveal_list_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    for(size_t i = 0; i < sizeof(wlan_network_rows) / sizeof(wlan_network_rows[0]); i++) {
        if(wlan_network_rows[i] != NULL) lv_obj_remove_flag(wlan_network_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    wlan_entry_list_timer = NULL;
}

static void wlan_entry_reveal_connected_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if(settings_wlan_connected_group != NULL) lv_obj_remove_flag(settings_wlan_connected_group, LV_OBJ_FLAG_HIDDEN);
    if(wlan_connected_status_label != NULL) lv_label_set_text(wlan_connected_status_label, "Connecting...");
    wlan_entry_connect_timer = NULL;
}

static void wlan_entry_settle_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if(wlan_connected_status_label != NULL) lv_label_set_text(wlan_connected_status_label, "Connected");
    wlan_entry_settle_timer = NULL;
}

static void set_wlan_enabled(bool enabled, lv_obj_t *source)
{
    wlan_enabled = enabled;

    /* Cancel any reveal sequence already in flight -- re-toggling quickly, or switching
     * off mid-reveal, shouldn't leave a stale timer to un-hide something later that
     * shouldn't be visible anymore. */
    if(wlan_entry_list_timer != NULL) { lv_timer_delete(wlan_entry_list_timer); wlan_entry_list_timer = NULL; }
    if(wlan_entry_connect_timer != NULL) { lv_timer_delete(wlan_entry_connect_timer); wlan_entry_connect_timer = NULL; }
    if(wlan_entry_settle_timer != NULL) { lv_timer_delete(wlan_entry_settle_timer); wlan_entry_settle_timer = NULL; }

    if(settings_wlan_value_label != NULL) {
        lv_label_set_text(settings_wlan_value_label, enabled ? "On" : "Off");
    }

    if(settings_wlan_switch != NULL && settings_wlan_switch != source) {
        if(enabled) lv_obj_add_state(settings_wlan_switch, LV_STATE_CHECKED);
        else lv_obj_remove_state(settings_wlan_switch, LV_STATE_CHECKED);
    }

    /* Available networks / SoftAP Mode show and hide entirely with WLAN, instantly --
     * matches firmware's setAvailableVisible()/setSoftAPVisible() and the reference
     * footage (the "Available networks" title itself appears right away, empty). */
    lv_obj_t *groups[] = {settings_wlan_available_group, settings_wlan_softap_group};
    for(size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
        if(groups[i] == NULL) continue;
        if(enabled) lv_obj_remove_flag(groups[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(groups[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Connected network is different: switching WLAN off hides it immediately like the
     * other two groups, but switching it on plays the mocked reveal sequence (see the
     * three callbacks above) instead of showing everything at once -- matching the
     * reference footage of the real device switching Wi-Fi on: the Connected network
     * row doesn't appear at all until the scan settles and it reconnects. Network rows
     * are force-hidden up front (even if last left visible) so every reveal starts from
     * the same empty state. */
    if(settings_wlan_connected_group != NULL) lv_obj_add_flag(settings_wlan_connected_group, LV_OBJ_FLAG_HIDDEN);
    for(size_t i = 0; i < sizeof(wlan_network_rows) / sizeof(wlan_network_rows[0]); i++) {
        if(wlan_network_rows[i] != NULL) lv_obj_add_flag(wlan_network_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if(enabled && !wlan_managed_externally) {
        wlan_entry_list_timer = lv_timer_create(wlan_entry_reveal_list_cb, 3200, NULL);
        lv_timer_set_repeat_count(wlan_entry_list_timer, 1);
        wlan_entry_connect_timer = lv_timer_create(wlan_entry_reveal_connected_cb, 4100, NULL);
        lv_timer_set_repeat_count(wlan_entry_connect_timer, 1);
        wlan_entry_settle_timer = lv_timer_create(wlan_entry_settle_cb, 5000, NULL);
        lv_timer_set_repeat_count(wlan_entry_settle_timer, 1);
    }

    if(quick_wifi_button != NULL && quick_wifi_button != source) {
        if(enabled) lv_obj_add_state(quick_wifi_button, LV_STATE_CHECKED);
        else lv_obj_remove_state(quick_wifi_button, LV_STATE_CHECKED);
    }

    /* Simplified two-state version of firmware's setWifiIconState(): hidden when WLAN is
     * off, shown (using the component's default "no signal" artwork, the only status
     * icon asset vendored) when on -- the simulator has no real signal-level data to
     * pick between firmware's extra SIGNAL_1/2/3 icon variants. */
    if(quick_wifi_status_icon != NULL) {
        if(enabled) lv_obj_remove_flag(quick_wifi_status_icon, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(quick_wifi_status_icon, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wlan_switch_changed(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target_obj(e);
    set_wlan_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED), sw);
}

static void quick_wifi_button_changed(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    set_wlan_enabled(lv_obj_has_state(btn, LV_STATE_CHECKED), btn);
}

static void wlan_network_selected(lv_event_t *e)
{
    const char *ssid = lv_event_get_user_data(e);
    if(ssid == NULL) return;
    if(wlan_connected_name_label != NULL) lv_label_set_text(wlan_connected_name_label, ssid);
    load(settings_wlan_verify);
}

static void restore_mock_clicked(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_label_set_text(restore_status_label, "Restoring device settings...");
    lv_obj_set_style_text_color(restore_status_label, WHITE, 0);
}

static void create_settings_wlan(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_wlan, &wlan_home_bar, "Settings", &settings);
    settings_wlan_scroller = scroller;
    lv_obj_t *control = group(scroller, 54, "");
    lv_obj_t *control_row = plain_row(control, "WLAN", NULL);
    lv_obj_t *wlan_switch = add_switch(control_row, true);
    settings_wlan_switch = wlan_switch;
    lv_obj_add_event_cb(wlan_switch, wlan_switch_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *connected = group(scroller, 158, "Connected network");
    /* group() returns the inner panel; its parent is the flex item (title + panel) that
     * has to be hidden as a unit when WLAN is off, or the title would linger alone. */
    settings_wlan_connected_group = lv_obj_get_parent(connected);
    lv_obj_t *connected_row = network_row(connected, "Studio-WiFi", "Connected",
                                          &esp_brookesia_app_icon_wlan_level3_36_36, false);
    lv_obj_add_flag(settings_wlan_connected_group, LV_OBJ_FLAG_HIDDEN);
    wlan_connected_name_label = lv_obj_get_child(connected_row, 0);
    wlan_connected_status_label = lv_obj_get_child(connected_row, 1);

    lv_obj_t *available = group(scroller, 280, "Available networks");
    settings_wlan_available_group = lv_obj_get_parent(available);
    lv_obj_t *network = network_row(available, "ESP-Lab", NULL,
                                    &esp_brookesia_app_icon_wlan_level3_36_36, true);
    make_clickable(network, wlan_network_selected, "ESP-Lab");
    lv_obj_add_flag(network, LV_OBJ_FLAG_HIDDEN);
    wlan_network_rows[0] = network;
    network = network_row(available, "NTT_Office", NULL,
                          &esp_brookesia_app_icon_wlan_level2_36_36, true);
    make_clickable(network, wlan_network_selected, "NTT_Office");
    lv_obj_add_flag(network, LV_OBJ_FLAG_HIDDEN);
    wlan_network_rows[1] = network;
    network = network_row(available, "Guest", NULL,
                          &esp_brookesia_app_icon_wlan_level1_36_36, false);
    make_clickable(network, wlan_network_selected, "Guest");
    lv_obj_add_flag(network, LV_OBJ_FLAG_HIDDEN);
    wlan_network_rows[2] = network;

    lv_obj_t *provisioning = group(scroller, 500, "Provisioning");
    settings_wlan_softap_group = lv_obj_get_parent(provisioning);
    lv_obj_t *softap = plain_row(provisioning, "SoftAP Mode", NULL);
    lv_obj_t *softap_arrow = scaled_image(softap, &esp_brookesia_app_icon_arrow_right_48_48, 24);
    lv_obj_align(softap_arrow, LV_ALIGN_RIGHT_MID, -20, 0);
    make_clickable(softap, show_screen_ref, &settings_softap);
}

static void create_settings_sound(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_sound, &sound_home_bar, "Settings", &settings);
    lv_obj_t *volume = group(scroller, 78, "Volume");
    slider_row(volume, &esp_brookesia_app_icon_sound_less_48_48,
               &esp_brookesia_app_icon_sound_more_48_48, 68);
}

static void create_settings_display(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_display, &display_home_bar, "Settings", &settings);
    lv_obj_t *brightness = group(scroller, 78, "Brightness");
    slider_row(brightness, &esp_brookesia_app_icon_brightness_less_48_48,
               &esp_brookesia_app_icon_brightness_more_48_48, 72);
    lv_obj_t *auto_row = plain_row(brightness, "Auto adjust", NULL);
    add_switch(auto_row, false);
}

static void create_settings_about(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_about, &about_home_bar, "Settings", &settings);
    lv_obj_t *system = group(scroller, 72, "System");
    plain_row_bound(system, "Firmware", "v1.0.0", &about_firmware_label);
    plain_row_bound(system, "OS", "FreeRTOS", &about_os_label);
    plain_row_bound(system, "OS version", "11.1.0", &about_os_version_label);
    plain_row_bound(system, "UI", "ESP-Brookesia", &about_ui_label);
    plain_row_bound(system, "UI version", "LVGL 9.2.2", &about_ui_version_label);

    lv_obj_t *device = group(scroller, 380, "Device");
    plain_row_bound(device, "Manufacturer", "Espressif", &about_manufacturer_label);
    plain_row_bound(device, "Board", "ESP-VoCat", &about_board_label);
    plain_row_bound(device, "Resolution", "360x360", &about_resolution_label);
    plain_row_bound(device, "Flash", "16MB", &about_flash_label);
    double_value_row_bound(device, "RAM", "512KB", "16MB",
                           &about_ram_main_label, &about_ram_minor_label);
    plain_row_bound(device, "Battery capacity", "2000 mAh", &about_battery_capacity_label);
    plain_row_bound(device, "Battery voltage", "4.05 V", &about_battery_voltage_label);
    plain_row_bound(device, "Battery current", "-120 mA", &about_battery_current_label);

    lv_obj_t *chip = group(scroller, 832, "Chip");
    plain_row_bound(chip, "Name", "ESP32-S3", &about_chip_name_label);
    plain_row_bound(chip, "Version", "v0.2", &about_chip_version_label);
    plain_row_bound(chip, "MAC", "A4:CF:12:34", &about_chip_mac_label);
    plain_row_bound(chip, "Features", "Wi-Fi / BLE", &about_chip_features_label);
}

static void create_settings_self_test(void)
{
    lv_obj_t *scroller = create_settings_child(
                             &settings_self_test, &self_test_home_bar, "Settings", &settings
                         );

    lv_obj_t *interactive = group(scroller, 72, "Interactive");
    plain_row_bound(interactive, "Display", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_DISPLAY]);
    plain_row_bound(interactive, "Touch", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_TOUCH]);
    plain_row_bound(interactive, "Speaker", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_SPEAKER]);
    plain_row_bound(interactive, "Microphone", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_MICROPHONE]);

    lv_obj_t *hardware = group(scroller, 304, "Hardware");
    plain_row_bound(hardware, "BMI270", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_BMI270]);
    plain_row_bound(hardware, "Battery", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_BATTERY]);
    plain_row_bound(hardware, "Charging", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_CHARGING]);

    lv_obj_t *system = group(scroller, 488, "System");
    plain_row_bound(system, "Wi-Fi", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_WIFI]);
    plain_row_bound(system, "NTP", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_NTP]);
    plain_row_bound(system, "Memory", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_MEMORY]);
    plain_row_bound(system, "Flash", "Not tested",
                    &self_test_status_labels[SPEAKER_UI_SELF_TEST_FLASH]);

    self_test_run_button = lv_button_create(scroller);
    lv_obj_set_size(self_test_run_button, 224, 52);
    lv_obj_set_style_radius(self_test_run_button, 26, 0);
    lv_obj_set_style_bg_color(self_test_run_button, RED, 0);
    lv_obj_t *button_label = text(
                                 self_test_run_button, "Run all tests",
                                 &esp_brookesia_font_maison_neue_book_16, WHITE
                             );
    lv_obj_center(button_label);
}

static void create_settings_developer(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_developer, &developer_home_bar, "Settings", &settings);
    lv_obj_t *mode = group(scroller, 78, "Developer Mode");
    lv_obj_t *row_obj = plain_row(mode, "Developer tools", NULL);
    add_switch(row_obj, true);
    row_obj = plain_row(mode, "USB console", NULL);
    add_switch(row_obj, true);
    row_obj = plain_row(mode, "Verbose logging", NULL);
    add_switch(row_obj, false);

    lv_obj_t *info = text(scroller, "Simulation values only", &esp_brookesia_font_maison_neue_book_14, MUTED);
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 300);
}

static void create_settings_restore(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_restore, &factory_home_bar, "Settings", &settings);
    lv_obj_t *restore_group = group(scroller, 78, "Restore Factory");
    lv_obj_t *description = plain_row(restore_group, "Erase device settings", NULL);
    lv_obj_set_height(description, 56);

    lv_obj_t *button = lv_button_create(scroller);
    lv_obj_set_size(button, 224, 52); lv_obj_align(button, LV_ALIGN_TOP_MID, 0, 190);
    lv_obj_set_style_radius(button, 26, 0);
    lv_obj_set_style_bg_color(button, RED, 0);
    lv_obj_t *button_label = text(button, "Restore settings", &esp_brookesia_font_maison_neue_book_16, WHITE);
    lv_obj_center(button_label);
    lv_obj_add_event_cb(button, restore_mock_clicked, LV_EVENT_CLICKED, NULL);

    restore_status_label = text(scroller, "Wi-Fi and preferences will be erased", &esp_brookesia_font_maison_neue_book_14, MUTED);
    lv_obj_align(restore_status_label, LV_ALIGN_TOP_MID, 0, 265);
}

static void create_settings_wlan_verification(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_wlan_verify, &wlan_verify_home_bar,
                                                "Cancel", &settings_wlan);
    lv_obj_t *password_panel = group(scroller, 92, "");
    lv_obj_t *password_cell = cell(password_panel, 48, 20);
    lv_obj_t *password = lv_textarea_create(password_cell);
    lv_obj_set_size(password, 230, 48);
    lv_obj_align(password, LV_ALIGN_LEFT_MID, 20, 0);
    lv_textarea_set_one_line(password, true);
    lv_textarea_set_password_mode(password, true);
    lv_textarea_set_placeholder_text(password, "Password");
    lv_textarea_set_text(password, "");
    lv_obj_set_style_border_width(password, 0, 0);
    lv_obj_set_style_bg_opa(password, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(password, WHITE, 0);
    lv_obj_set_style_text_font(password, &esp_brookesia_font_maison_neue_book_20, 0);
    settings_wlan_password = password;
    lv_obj_t *advanced = group(scroller, 286, "Advanced settings");
    plain_row(advanced, "Proxy", "None");
    plain_row(advanced, "IP", "DHCP");
}

static void wlan_password_ready(lv_event_t *e)
{
    LV_UNUSED(e);
    /* Mirrors firmware's TEXT_EDIT_SEND_CONFIRM_EVENT_LEN_MIN: the OK key only confirms
     * once the password is at least 8 characters; shorter input is silently ignored. */
    if(settings_wlan_password == NULL || strlen(lv_textarea_get_text(settings_wlan_password)) < 8) return;
    load(settings_wlan);
}

static void wlan_password_pressed(lv_event_t *e)
{
    LV_UNUSED(e);
    /* Tapping the password field back in after dismissing the keyboard should bring it
     * back, matching firmware's touch-on-text-edit case in its gesture handler. */
    sync_password_keyboard(true);
}

static void wlan_keyboard_cancelled(lv_event_t *e)
{
    LV_UNUSED(e);
    /* Firmware has no dismiss key of its own (see create_wlan_keyboard()'s comment), but
     * a simulator with a mouse instead of a finger has no tap-outside-the-keyboard
     * gesture to fall back on, so the text layouts get a dismiss key -- LV_SYMBOL_
     * KEYBOARD, which LVGL's own default handler already recognizes and turns into this
     * CANCEL event (lv_keyboard_def_event_cb). Tapping the password field again (see
     * wlan_password_pressed above) brings it back. */
    sync_password_keyboard(false);
}

/* Ported from firmware's Keyboard class (esp_brookesia_keyboard.cpp) instead of insetting
 * LVGL's stock rectangular maps: firmware ships entirely custom key maps for TEXT_LOWER/
 * TEXT_UPPER/SPECIAL/NUMBER, each row totaling a fixed 20 "units" wide. A row spends fewer
 * of those units on real keys the closer it sits to the display's top/bottom edge, padding
 * the rest with blank filler keys (LV_KB_PHR_STR, two spaces) -- the same round-safety idea
 * as insetting, but with widths tuned per row for this exact geometry instead of computed
 * generically, which is why real keys end up noticeably bigger. Every real key keeps
 * firmware's exact button strings, including "123"/",.?!"/"Space"/"ABC"/"abc" tokens that
 * only firmware's own custom maps use. LVGL's built-in key handling recognizes "ABC"/"abc"
 * (mode switch) and the LV_SYMBOL_* keys (backspace/arrows/OK) automatically, but not
 * firmware's other custom tokens; wlan_keyboard_value_changed() below reimplements
 * firmware's Keyboard::processOnKeyboardValueChanged() to drive those manually. */
#define WLAN_KB_BTN(width) (LV_BUTTONMATRIX_CTRL_POPOVER | (width))
#define WLAN_KB_PHR(width) ((lv_buttonmatrix_ctrl_t)(width))
#define WLAN_KB_PHR_STR    "  "
#define WLAN_KB_SPACE_STR  "Space"
#define WLAN_KB_UPPER_STR  "ABC"
#define WLAN_KB_LOWER_STR  "abc"
#define WLAN_KB_NUMBER_STR "123"
#define WLAN_KB_SPEC_STR   ",.?!"

static const char *const wlan_kb_map_lc[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    WLAN_KB_PHR_STR, "a", "s", "d", "f", "g", "h", "j", "k", "l", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_UPPER_STR, "z", "x", "c", "v", "b", "n", "m", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_NUMBER_STR, WLAN_KB_SPEC_STR, WLAN_KB_SPACE_STR, LV_SYMBOL_BACKSPACE,
    LV_SYMBOL_KEYBOARD, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, LV_SYMBOL_LEFT, LV_SYMBOL_OK, LV_SYMBOL_RIGHT, WLAN_KB_PHR_STR, ""
};
static const lv_buttonmatrix_ctrl_t wlan_kb_ctrl_lc[] = {
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_PHR(1), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(1),
    WLAN_KB_PHR(1), WLAN_KB_BTN(3), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(1), WLAN_KB_BTN(3), WLAN_KB_BTN(3), WLAN_KB_BTN(5), WLAN_KB_BTN(4), WLAN_KB_BTN(2),
    WLAN_KB_PHR(2),
    WLAN_KB_PHR(3), WLAN_KB_BTN(4), WLAN_KB_BTN(6), WLAN_KB_BTN(4), WLAN_KB_PHR(3)
};

static const char *const wlan_kb_map_uc[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    WLAN_KB_PHR_STR, "A", "S", "D", "F", "G", "H", "J", "K", "L", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_LOWER_STR, "Z", "X", "C", "V", "B", "N", "M", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_NUMBER_STR, WLAN_KB_SPEC_STR, WLAN_KB_SPACE_STR, LV_SYMBOL_BACKSPACE,
    LV_SYMBOL_KEYBOARD, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, LV_SYMBOL_LEFT, LV_SYMBOL_OK, LV_SYMBOL_RIGHT, WLAN_KB_PHR_STR, ""
};
#define wlan_kb_ctrl_uc wlan_kb_ctrl_lc /* identical shape to lowercase, only labels differ */

static const char *const wlan_kb_map_spec[] = {
    "+", "|", "\\", "\"", "<", ">", "{", "}", "[", "]", "\n",
    WLAN_KB_PHR_STR, "~", "@", "#", "!", "%", "&", "*", "(", ")", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_LOWER_STR, "'", "/", "-", "_", ":", ";", "?", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_NUMBER_STR, ",", WLAN_KB_SPACE_STR, ".", LV_SYMBOL_BACKSPACE,
    WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, LV_SYMBOL_LEFT, LV_SYMBOL_OK, LV_SYMBOL_RIGHT, WLAN_KB_PHR_STR, ""
};
static const lv_buttonmatrix_ctrl_t wlan_kb_ctrl_spec[] = {
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_PHR(1), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(1),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(3), WLAN_KB_BTN(2), WLAN_KB_BTN(5), WLAN_KB_BTN(2), WLAN_KB_BTN(4),
    WLAN_KB_PHR(2),
    WLAN_KB_PHR(3), WLAN_KB_BTN(4), WLAN_KB_BTN(6), WLAN_KB_BTN(4), WLAN_KB_PHR(3)
};

static const char *const wlan_kb_map_num[] = {
    WLAN_KB_PHR_STR, "1", "2", "3", LV_SYMBOL_BACKSPACE, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, "4", "5", "6", WLAN_KB_LOWER_STR, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, "7", "8", "9", WLAN_KB_SPEC_STR, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, LV_SYMBOL_LEFT, "0", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, WLAN_KB_PHR_STR, ""
};
static const lv_buttonmatrix_ctrl_t wlan_kb_ctrl_num[] = {
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2)
};

static int wlan_kb_last_mode = LV_KEYBOARD_MODE_TEXT_LOWER;
static bool wlan_kb_ok_enabled = false;

static void wlan_kb_update_ok_enabled(void)
{
    wlan_kb_ok_enabled = settings_wlan_password != NULL &&
                          strlen(lv_textarea_get_text(settings_wlan_password)) >= 8;
}

/* Reimplements firmware's Keyboard::processOnKeyboardValueChanged(). LVGL's own default
 * handler (registered internally by lv_keyboard_create(), runs before this one) already
 * inserted/deleted literal text for every button it recognizes -- letters, backspace,
 * arrows, OK, and "abc"/"ABC". It doesn't recognize "123", ",.?!", "Space", or a blank
 * filler ("  "), so for those it fell through to its generic "insert this text literally"
 * case, inserting them into the password field. This handler runs after and cleans that
 * up: delete whatever got inserted, then perform the real action. */
static void wlan_keyboard_value_changed(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target_obj(e);
    int current_mode = (int)lv_keyboard_get_mode(kb);
    uint32_t btn_id = lv_buttonmatrix_get_selected_button(kb);
    const char *text = lv_buttonmatrix_get_button_text(kb, btn_id);
    if(text == NULL) return;

    if(!strcmp(text, WLAN_KB_NUMBER_STR)) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_NUMBER);
    } else if(!strcmp(text, WLAN_KB_SPEC_STR)) {
        lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_SPECIAL);
    }

    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    if(ta != NULL) {
        if(!strcmp(text, WLAN_KB_SPACE_STR)) {
            for(size_t i = 0; i < strlen(WLAN_KB_SPACE_STR); i++) lv_textarea_delete_char(ta);
            lv_textarea_add_text(ta, " ");
        } else if(!strcmp(text, WLAN_KB_NUMBER_STR)) {
            for(size_t i = 0; i < strlen(WLAN_KB_NUMBER_STR); i++) lv_textarea_delete_char(ta);
        } else if(!strcmp(text, WLAN_KB_SPEC_STR)) {
            for(size_t i = 0; i < strlen(WLAN_KB_SPEC_STR); i++) lv_textarea_delete_char(ta);
        } else if(!strcmp(text, WLAN_KB_PHR_STR) && current_mode == wlan_kb_last_mode) {
            for(size_t i = 0; i < strlen(WLAN_KB_PHR_STR); i++) lv_textarea_delete_char(ta);
        }
    }

    wlan_kb_last_mode = current_mode;
    wlan_kb_update_ok_enabled();
}

/* Firmware colors the OK key at draw time by inspecting each button's text as it's about
 * to be drawn (Keyboard::processOnKeyboardDrawTask), rather than via LVGL's normal
 * state-based styling -- there's no ctrl flag in this LVGL version that maps a button to
 * its own style state. This reproduces just the part that matters visually: OK is red
 * (firmware's 0xFF3034) once the password is long enough to confirm, and dimmed red
 * (80/255 opacity, matching firmware's ok_button_disabled_background_color) until then. */
static void wlan_keyboard_draw_task(lv_event_t *e)
{
    /* LVGL only creates a fill draw task for a button at all when its *style-resolved*
     * bg_opa is non-zero (see lv_draw_rect()'s has_fill check, which runs before this
     * event fires) -- so the keyboard's base item style below uses an opaque bg_opa
     * purely to guarantee every button gets a fill task to override here. The actual
     * per-button color/opacity, including "invisible" (opa 0) for ordinary keys, is
     * decided in this callback instead, the same way firmware's Keyboard::
     * processOnKeyboardDrawTask() drives every button's look from one place rather
     * than through LVGL's per-state style system. */
    lv_draw_task_t *draw_task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = (lv_draw_dsc_base_t *)lv_draw_task_get_draw_dsc(draw_task);
    if(base->part != LV_PART_ITEMS) return;

    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(draw_task);
    if(fill == NULL) return;

    lv_obj_t *kb = lv_event_get_target_obj(e);
    const char *text = lv_buttonmatrix_get_button_text(kb, base->id1);
    bool is_ok = text != NULL && !strcmp(text, LV_SYMBOL_OK);
    bool is_filler = text != NULL && !strcmp(text, WLAN_KB_PHR_STR);
    /* Firmware explicitly excludes the blank filler key from the press highlight
     * (processOnKeyboardDrawTask: "pressed && strcmp(text, LV_KB_PHR_STR) != 0") --
     * it's reserved round-safety padding, not a real key, so tapping it should look
     * like tapping nothing. */
    bool pressed = !is_filler && lv_buttonmatrix_get_selected_button(kb) == base->id1 &&
                   lv_obj_has_state(kb, LV_STATE_PRESSED);

    if(pressed) {
        fill->color = WHITE;
        fill->opa = LV_OPA_50;
    } else if(is_ok) {
        /* Firmware's OK key is red (0xFF3034) once the password is long enough to
         * confirm, dimmed to 80/255 opacity beforehand (ok_button_disabled_background_
         * color) -- everything else stays fully transparent. */
        fill->color = RED;
        fill->opa = wlan_kb_ok_enabled ? LV_OPA_COVER : 80;
    } else {
        fill->opa = LV_OPA_TRANSP;
    }
}

/* Firmware creates one keyboard on the system overlay layer and shows/binds it to
 * whichever text edit needs it; the WLAN password screen shows it as soon as it loads
 * and hides it on the way out. This mirrors that lifecycle for the one password field
 * the simulator has. Firmware itself has no dedicated "hide keyboard" key -- it relies
 * on a tap-outside-the-keyboard gesture this simulator doesn't reproduce (see README) --
 * but a mouse-driven simulator has no such gesture to fall back on, so the text layouts
 * add LV_SYMBOL_KEYBOARD next to backspace, wired to wlan_keyboard_cancelled() below.
 * Number and special layouts intentionally omit it. Tapping the password field again
 * brings the keyboard back. */
static void create_wlan_keyboard(void)
{
    wlan_keyboard_container = lv_obj_create(lv_layer_top());
    bare(wlan_keyboard_container);
    lv_obj_set_size(wlan_keyboard_container, SIZE, SIZE * 60 / 100);
    lv_obj_align(wlan_keyboard_container, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(wlan_keyboard_container, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(wlan_keyboard_container, LV_OPA_COVER, 0);
    lv_obj_add_flag(wlan_keyboard_container, LV_OBJ_FLAG_HIDDEN);

    wlan_keyboard = lv_keyboard_create(wlan_keyboard_container);
    lv_obj_set_size(wlan_keyboard, SIZE * 94 / 100, (SIZE * 60 / 100) * 85 / 100);
    lv_obj_align(wlan_keyboard, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_keyboard_set_map(wlan_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER, wlan_kb_map_lc, wlan_kb_ctrl_lc);
    lv_keyboard_set_map(wlan_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER, wlan_kb_map_uc, wlan_kb_ctrl_uc);
    lv_keyboard_set_map(wlan_keyboard, LV_KEYBOARD_MODE_SPECIAL, wlan_kb_map_spec, wlan_kb_ctrl_spec);
    lv_keyboard_set_map(wlan_keyboard, LV_KEYBOARD_MODE_NUMBER, wlan_kb_map_num, wlan_kb_ctrl_num);
    lv_keyboard_set_mode(wlan_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_flag(wlan_keyboard, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_obj_set_style_bg_opa(wlan_keyboard, LV_OPA_TRANSP, 0);
    /* LV_FONT_DEFAULT is the project's custom text font, which has no glyphs for LVGL's
     * built-in LV_SYMBOL_* icons (backspace/enter/arrows) the key maps use, so those keys
     * would render as tofu boxes. lv_font_montserrat_24 is one of LVGL's own bundled
     * fonts (already enabled for other UI text), includes them, and matches firmware's
     * own 24px keyboard button font (stylesheets/360x360/dark/keyboard.hpp). */
    lv_obj_set_style_text_font(wlan_keyboard, &lv_font_montserrat_24, LV_PART_ITEMS);
    lv_obj_set_style_text_color(wlan_keyboard, WHITE, LV_PART_ITEMS);
    /* Opaque on purpose -- see the comment in wlan_keyboard_draw_task(), which is what
     * actually decides each button's visible color, including making ordinary keys
     * transparent. */
    lv_obj_set_style_bg_opa(wlan_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_width(wlan_keyboard, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(wlan_keyboard, 8, LV_PART_ITEMS);
    lv_obj_add_event_cb(wlan_keyboard, wlan_keyboard_value_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(wlan_keyboard, wlan_keyboard_draw_task, LV_EVENT_DRAW_TASK_ADDED, NULL);
    lv_obj_add_event_cb(wlan_keyboard, wlan_password_ready, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(wlan_keyboard, wlan_keyboard_cancelled, LV_EVENT_CANCEL, NULL);
    if(settings_wlan_password != NULL) {
        lv_obj_add_event_cb(settings_wlan_password, wlan_password_pressed, LV_EVENT_PRESSED, NULL);
    }
}

static void sync_password_keyboard(bool show)
{
    if(wlan_keyboard_container == NULL) return;
    if(show) {
        lv_obj_remove_flag(wlan_keyboard_container, LV_OBJ_FLAG_HIDDEN);
        if(settings_wlan_password != NULL) lv_textarea_set_text(settings_wlan_password, "");
        lv_keyboard_set_textarea(wlan_keyboard, settings_wlan_password);
        lv_keyboard_set_mode(wlan_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        wlan_kb_last_mode = LV_KEYBOARD_MODE_TEXT_LOWER;
        wlan_kb_update_ok_enabled();
    } else {
        lv_obj_add_flag(wlan_keyboard_container, LV_OBJ_FLAG_HIDDEN);
    }
}

static void create_settings_softap(void)
{
    lv_obj_t *scroller = create_settings_child(&settings_softap, &softap_home_bar, "WLAN", &settings_wlan);
    lv_obj_t *panel = group(scroller, 0, "");
    lv_obj_t *qr_cell = cell(panel, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(qr_cell, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(qr_cell, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(qr_cell, 10, 0);
    lv_obj_set_style_pad_row(qr_cell, 10, 0);

    softap_qr = lv_qrcode_create(qr_cell);
    lv_qrcode_set_size(softap_qr, 100);
    lv_qrcode_set_dark_color(softap_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(softap_qr, WHITE);
    lv_obj_set_style_border_color(softap_qr, WHITE, 0);
    lv_obj_set_style_border_width(softap_qr, 10, 0);
    const char *qr_data = "WIFI:T:WPA;S:ESP-Speaker-Setup;P:esp123456;;";
    lv_qrcode_update(softap_qr, qr_data, strlen(qr_data));

    softap_info_label = text(qr_cell,
                              "Option 1: Scan QRCode -> connect Wi-Fi in pop-up browser\n"
                              "Option 2: Join Wi-Fi 'ESP-Speaker-Setup' -> visit '192.168.4.1' in browser",
                              &esp_brookesia_font_maison_neue_book_16, WHITE);
    lv_obj_set_width(softap_info_label, 280);
    lv_label_set_long_mode(softap_info_label, LV_LABEL_LONG_WRAP);
}

static void create_settings_subpages(void)
{
    create_settings_wlan();
    create_settings_wlan_verification();
    create_settings_softap();
    create_settings_sound();
    create_settings_display();
    create_settings_about();
    create_settings_self_test();
    create_settings_developer();
    create_settings_restore();
}

static void create_ai(void)
{
    ui_ScreenScreenAIProfile_screen_init();
    ai = ui_ScreenScreenAIProfile;
    lv_obj_set_size(ai, SIZE, SIZE);
    lv_obj_set_style_bg_color(ai, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ai, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui_ScreenAIProfileTabviewTabView, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(ui_ScreenAIProfileTabviewTabView, true, 0);
    lv_obj_set_style_bg_color(ui_ScreenAIProfileTabpageTabPageRole1, WHITE, 0);
    lv_obj_set_style_bg_opa(ui_ScreenAIProfileTabpageTabPageRole1, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(ui_ScreenAIProfileTabpageTabPageRole2, WHITE, 0);
    lv_obj_set_style_bg_opa(ui_ScreenAIProfileTabpageTabPageRole2, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(ui_ScreenAIProfilePanelPanelIndicator1, 128, 0);
    lv_obj_set_style_bg_opa(ui_ScreenAIProfilePanelPanelIndicator2, 50, 0);
    ai_home_bar = create_home_indicator(ai);
}

/* "Clock" app (Launcher label; internal names below stay `timer_*` --
 * this screen is reached the same way the old countdown-timer placeholder
 * was, see show_timer()/the "timer" key in speaker_ui_show() -- renaming
 * those too would touch self-tests and screenshot tooling for no visible
 * benefit). Built from the flip-clock + weather widgets ported from the
 * HTC_Flip_Clock_with_weather reference project (src/flip_clock/) instead
 * of the vendored ui_Screen_watch_digital digital watch: a full mechanical
 * flip-clock animation, the custom digit font, and a compact weather panel.
 * Its data source is selected by the firmware or simulator backend. */
static lv_obj_t *clock_date_label;

static void clock_date_update(lv_timer_t *t)
{
    LV_UNUSED(t);
    static const char *const weekdays[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char *const months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                         "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    lv_label_set_text_fmt(clock_date_label, "%s, %d %s", weekdays[local.tm_wday],
                          local.tm_mday, months[local.tm_mon]);
}

static void create_timer(void)
{
    timer_screen = lv_obj_create(NULL);
    lv_obj_t *content = screen_content(timer_screen, BG);

    /* Column of [flip clock, date chip, compact weather], horizontally
     * centered -- same composition as HTC_Flip_Clock_with_weather's own
     * round-face layout (clock_app.c's build_round_face()), ported below
     * along with its chord-safe vertical placement math (a circle is
     * narrower than its full diameter away from the center line, so a
     * wide/tall column sitting too close to the top or bottom can poke
     * past the curve -- solved here by measuring the actual chord width
     * available at the column's top and the weather panel's bottom edge,
     * narrowing the weather panel if it doesn't fit). */
    lv_obj_t *col = lv_obj_create(content);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_layout(col, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);

    timer_clock_widget = flip_clock_create(col);

    lv_obj_t *date_box = lv_obj_create(col);
    lv_obj_remove_style_all(date_box);
    lv_obj_set_size(date_box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(date_box, lv_color_hex(0x20242f), 0);
    /* Opaque on the partial-buffer QSPI target so each date refresh clears the
     * previous glyphs; SDL keeps the original translucent appearance. */
    lv_obj_set_style_bg_opa(date_box, CLOCK_PANEL_OPA, 0);
    lv_obj_set_style_radius(date_box, 12, 0);
    lv_obj_set_style_pad_hor(date_box, 12, 0);
    lv_obj_set_style_pad_ver(date_box, 4, 0);
    lv_obj_remove_flag(date_box, LV_OBJ_FLAG_SCROLLABLE);

    clock_date_label = lv_label_create(date_box);
    lv_obj_set_style_text_font(clock_date_label, &esp_brookesia_font_maison_neue_book_16, 0);
    lv_obj_set_style_text_color(clock_date_label, WHITE, 0);
    clock_date_update(NULL);
    lv_timer_create(clock_date_update, 30000, NULL);

    lv_obj_t *weather = weather_panel_create_compact(col, 200);

    lv_obj_update_layout(col);
    int32_t radius = SIZE / 2;
    int32_t half_w = lv_obj_get_width(col) / 2 + 6; /* +6px safety margin */
    int32_t min_offset = 32;                        /* top clearance, like the other screens' status-bar area */
    if(half_w < radius) {
        int32_t chord_half = (int32_t)sqrt((double)(radius * radius - half_w * half_w));
        int32_t geo_offset = radius - chord_half;
        if(geo_offset > min_offset) min_offset = geo_offset;
    }
    int32_t weather_bottom_y = min_offset + lv_obj_get_height(col);
    int32_t weather_half_w = lv_obj_get_width(weather) / 2;
    int32_t bottom_chord_half = 0;
    if(weather_bottom_y > 0 && weather_bottom_y < SIZE) {
        int32_t d = radius - weather_bottom_y;
        bottom_chord_half = (int32_t)sqrt((double)(radius * radius - d * d));
    }
    if(weather_half_w > bottom_chord_half - 6) {
        int32_t new_half_w = bottom_chord_half - 6;
        if(new_half_w < 40) new_half_w = 40; /* never shrink past the icon's own size */
        weather_panel_compact_set_width(weather, 2 * new_half_w);
        lv_obj_update_layout(col);
    }
    lv_obj_align(col, LV_ALIGN_TOP_MID, 0, min_offset);

    timer_home_bar = create_home_indicator(content);
}

void speaker_ui_create(void)
{
    esp_brookesia_squareline_ui_comp_init();
    create_launcher(); create_idle(); create_quick(); create_settings(); create_settings_subpages();
    create_wlan_keyboard();
    create_ai(); create_timer();
    /* Wire every WLAN-state-dependent widget (Settings switch, Quick Settings Wi-Fi
     * button/status icon, WLAN screen's connected/available/SoftAP groups) to the
     * initial state now that they all exist. */
    set_wlan_enabled(wlan_enabled, NULL);
    lv_screen_load(idle);
}

void speaker_ui_set_input(lv_indev_t *input)
{
    if(input == NULL) return;
    gesture_input = input;
    lv_indev_add_event_cb(input, input_gesture_event, LV_EVENT_PRESSED, NULL);
    lv_indev_add_event_cb(input, input_gesture_event, LV_EVENT_RELEASED, NULL);
    lv_indev_add_event_cb(input, input_gesture_event, LV_EVENT_PRESS_LOST, NULL);
    if(gesture_poll_timer == NULL) gesture_poll_timer = lv_timer_create(poll_home_gesture, 20, NULL);
}

void speaker_ui_set_wifi_managed_externally(bool managed)
{
    wlan_managed_externally = managed;
    if(!managed) return;

    if(wlan_entry_list_timer != NULL) { lv_timer_delete(wlan_entry_list_timer); wlan_entry_list_timer = NULL; }
    if(wlan_entry_connect_timer != NULL) { lv_timer_delete(wlan_entry_connect_timer); wlan_entry_connect_timer = NULL; }
    if(wlan_entry_settle_timer != NULL) { lv_timer_delete(wlan_entry_settle_timer); wlan_entry_settle_timer = NULL; }
    if(settings_wlan_connected_group != NULL) lv_obj_add_flag(settings_wlan_connected_group, LV_OBJ_FLAG_HIDDEN);
    for(size_t i = 0; i < sizeof(wlan_network_rows) / sizeof(wlan_network_rows[0]); i++) {
        if(wlan_network_rows[i] != NULL) lv_obj_add_flag(wlan_network_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
}

bool speaker_ui_is_launcher_active(void)
{
    return lv_screen_active() == launcher;
}

bool speaker_ui_is_idle_active(void)
{
    return lv_screen_active() == idle;
}

bool speaker_ui_is_screen_active(const char *name)
{
    lv_obj_t *active = lv_screen_active();
    if(!strcmp(name, "quick")) return quick_open;
    if(!strcmp(name, "settings")) return active == settings;
    if(!strcmp(name, "wlan")) return active == settings_wlan;
    if(!strcmp(name, "wlan-connect")) return active == settings_wlan_verify;
    if(!strcmp(name, "softap")) return active == settings_softap;
    if(!strcmp(name, "sound")) return active == settings_sound;
    if(!strcmp(name, "display")) return active == settings_display;
    if(!strcmp(name, "about")) return active == settings_about;
    if(!strcmp(name, "self-test")) return active == settings_self_test;
    if(!strcmp(name, "developer")) return active == settings_developer;
    if(!strcmp(name, "restore")) return active == settings_restore;
    return false;
}

int speaker_ui_launcher_page(void)
{
    return page_index;
}

static void close_quick_settings_immediate(void)
{
    quick_open = false;
    lv_anim_delete(quick, NULL);
    lv_obj_set_pos(quick, 0, -SIZE);
    lv_obj_add_flag(quick, LV_OBJ_FLAG_HIDDEN);
}

bool speaker_ui_show(const char *name)
{
    if(strcmp(name, "quick") != 0) close_quick_settings_immediate();
    sync_password_keyboard(!strcmp(name, "wlan-connect"));
    if(!strcmp(name, "idle") || !strcmp(name, "black")) lv_screen_load(idle);
    else if(!strcmp(name, "launcher")) lv_screen_load(launcher);
    else if(!strcmp(name, "quick")) {
        lv_anim_delete(quick, NULL);
        lv_obj_remove_flag(quick, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(quick, 0, 0);
        quick_open = true;
    }
    else if(!strcmp(name, "settings") || !strcmp(name, "settings-bottom")) {
        lv_screen_load(settings);
        lv_obj_update_layout(settings_scroller);
        lv_obj_scroll_to_y(settings_scroller,
                           !strcmp(name, "settings-bottom") ?
                               lv_obj_get_scroll_y(settings_scroller) + lv_obj_get_scroll_bottom(settings_scroller) : 0,
                           LV_ANIM_OFF);
    }
    else if(!strcmp(name, "wlan") || !strcmp(name, "wlan-bottom")) {
        lv_screen_load(settings_wlan);
        lv_obj_update_layout(settings_wlan_scroller);
        lv_obj_scroll_to_y(settings_wlan_scroller,
                           !strcmp(name, "wlan-bottom") ?
                               lv_obj_get_scroll_y(settings_wlan_scroller) +
                                   lv_obj_get_scroll_bottom(settings_wlan_scroller) : 0,
                           LV_ANIM_OFF);
    }
    else if(!strcmp(name, "wlan-connect")) lv_screen_load(settings_wlan_verify);
    else if(!strcmp(name, "softap")) lv_screen_load(settings_softap);
    else if(!strcmp(name, "sound")) lv_screen_load(settings_sound);
    else if(!strcmp(name, "display")) lv_screen_load(settings_display);
    else if(!strcmp(name, "about")) lv_screen_load(settings_about);
    else if(!strcmp(name, "self-test")) lv_screen_load(settings_self_test);
    else if(!strcmp(name, "developer")) lv_screen_load(settings_developer);
    else if(!strcmp(name, "restore")) lv_screen_load(settings_restore);
    else if(!strcmp(name, "ai")) lv_screen_load(ai);
    else if(!strcmp(name, "timer")) lv_screen_load(timer_screen);
    else return false;
    return true;
}

bool speaker_ui_settings_bar_stays_fixed(void)
{
    lv_area_t before;
    lv_area_t after;

    speaker_ui_show("settings");
    lv_obj_update_layout(settings);
    lv_obj_get_coords(settings_home_bar, &before);
    int32_t scroll_before = lv_obj_get_scroll_y(settings_scroller);

    lv_obj_scroll_to_y(settings_scroller,
                       scroll_before + lv_obj_get_scroll_bottom(settings_scroller), LV_ANIM_OFF);
    lv_obj_update_layout(settings);
    lv_obj_get_coords(settings_home_bar, &after);

    return lv_obj_get_scroll_y(settings_scroller) > scroll_before &&
           before.x1 == after.x1 && before.y1 == after.y1 &&
           before.x2 == after.x2 && before.y2 == after.y2;
}

int32_t speaker_ui_get_settings_scroll_y(void)
{
    return lv_obj_get_scroll_y(settings_scroller);
}

bool speaker_ui_get_active_home_bar_geometry(int32_t *center_x, int32_t *width)
{
    lv_obj_t *bar = active_home_bar();
    if(bar == NULL || center_x == NULL || width == NULL) return false;

    lv_obj_update_layout(bar);
    lv_area_t area;
    lv_obj_get_coords(bar, &area);
    *width = lv_area_get_width(&area);
    *center_x = area.x1 + (*width / 2);
    return true;
}

bool speaker_ui_is_wlan_on(void)
{
    return wlan_enabled;
}

bool speaker_ui_is_quick_wifi_on(void)
{
    return quick_wifi_button != NULL && lv_obj_has_state(quick_wifi_button, LV_STATE_CHECKED);
}

bool speaker_ui_is_wlan_section_visible(const char *section)
{
    lv_obj_t *group;
    if(!strcmp(section, "connected")) group = settings_wlan_connected_group;
    else if(!strcmp(section, "available")) group = settings_wlan_available_group;
    else if(!strcmp(section, "softap")) group = settings_wlan_softap_group;
    else return false;
    if(group == NULL) return false;
    return !lv_obj_has_flag(group, LV_OBJ_FLAG_HIDDEN);
}

static bool get_center(lv_obj_t *obj, int32_t *x, int32_t *y)
{
    if(obj == NULL || x == NULL || y == NULL) return false;
    lv_obj_update_layout(obj);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    *x = area.x1 + lv_area_get_width(&area) / 2;
    *y = area.y1 + lv_area_get_height(&area) / 2;
    return true;
}

bool speaker_ui_get_wlan_switch_center(int32_t *x, int32_t *y)
{
    return get_center(settings_wlan_switch, x, y);
}

bool speaker_ui_get_quick_wifi_button_center(int32_t *x, int32_t *y)
{
    return get_center(quick_wifi_button, x, y);
}

bool speaker_ui_get_quick_volume_button_center(int32_t *x, int32_t *y)
{
    return get_center(quick_volume_button, x, y);
}

bool speaker_ui_get_quick_brightness_button_center(int32_t *x, int32_t *y)
{
    return get_center(quick_brightness_button, x, y);
}

int speaker_ui_get_quick_volume_level(void)
{
    return quick_volume_level;
}

int speaker_ui_get_quick_brightness_level(void)
{
    return quick_brightness_level;
}

bool speaker_ui_set_quick_volume_level(int level)
{
    if(level < -1 || level > 2) return false;
    quick_volume_level = level;
    quick_update_volume_icon();
    return true;
}

bool speaker_ui_set_quick_brightness_level(int level)
{
    if(level < 0 || level > 2) return false;
    quick_brightness_level = level;
    quick_update_brightness_icon();
    return true;
}

bool speaker_ui_set_battery_state(bool charging, int percentage)
{
    if(quick_battery_status_icon == NULL || quick_battery_percent_label == NULL ||
       percentage < 0 || percentage > 100) return false;

    const lv_image_dsc_t *source = &speaker_image_middle_quick_settings_battery_charge_20_20;
    if(!charging) {
        if(percentage <= 25) source = &speaker_image_middle_quick_settings_battery_level1_20_20;
        else if(percentage <= 50) source = &speaker_image_middle_quick_settings_battery_level2_20_20;
        else if(percentage <= 75) source = &speaker_image_middle_quick_settings_battery_level3_20_20;
        else source = &speaker_image_middle_quick_settings_battery_level4_20_20;
    }
    lv_image_set_src(quick_battery_status_icon, source);
    lv_label_set_text_fmt(quick_battery_percent_label, "%d%%", percentage);
    return true;
}

bool speaker_ui_set_about_info(const speaker_ui_about_info_t *info)
{
    if(info == NULL || about_firmware_label == NULL || about_os_label == NULL ||
       about_os_version_label == NULL || about_ui_label == NULL || about_ui_version_label == NULL ||
       about_manufacturer_label == NULL || about_board_label == NULL || about_resolution_label == NULL ||
       about_flash_label == NULL || about_ram_main_label == NULL || about_ram_minor_label == NULL ||
       about_battery_capacity_label == NULL || about_battery_voltage_label == NULL ||
       about_battery_current_label == NULL || about_chip_name_label == NULL ||
       about_chip_version_label == NULL || about_chip_mac_label == NULL ||
       about_chip_features_label == NULL) return false;

    lv_label_set_text(about_firmware_label, info->firmware);
    lv_label_set_text(about_os_label, info->os);
    lv_label_set_text(about_os_version_label, info->os_version);
    lv_label_set_text(about_ui_label, info->ui);
    lv_label_set_text(about_ui_version_label, info->ui_version);
    lv_label_set_text(about_manufacturer_label, info->manufacturer);
    lv_label_set_text(about_board_label, info->board);
    lv_label_set_text(about_resolution_label, info->resolution);
    lv_label_set_text(about_flash_label, info->flash);
    lv_label_set_text(about_ram_main_label, info->ram_main);
    lv_label_set_text(about_ram_minor_label, info->ram_minor);
    lv_label_set_text(about_battery_capacity_label, info->battery_capacity);
    lv_label_set_text(about_battery_voltage_label, "--");
    lv_label_set_text(about_battery_current_label, "--");
    lv_label_set_text(about_chip_name_label, info->chip_name);
    lv_label_set_text(about_chip_version_label, info->chip_version);
    lv_label_set_text(about_chip_mac_label, info->chip_mac);
    lv_label_set_text(about_chip_features_label, info->chip_features);
    return true;
}

bool speaker_ui_set_about_battery_measurements(int voltage_mv, int current_ma)
{
    if(about_battery_voltage_label == NULL || about_battery_current_label == NULL || voltage_mv < 0) {
        return false;
    }

    const int centivolts = (voltage_mv + 5) / 10;
    lv_label_set_text_fmt(about_battery_voltage_label, "%d.%02d V", centivolts / 100, centivolts % 100);
    lv_label_set_text_fmt(about_battery_current_label, "%d mA", current_ma);
    return true;
}

bool speaker_ui_set_self_test_status(speaker_ui_self_test_item_t item,
                                     speaker_ui_self_test_status_t status)
{
    if(item < 0 || item >= SPEAKER_UI_SELF_TEST_COUNT ||
       status < SPEAKER_UI_SELF_TEST_NOT_TESTED || status > SPEAKER_UI_SELF_TEST_FAIL ||
       self_test_status_labels[item] == NULL) return false;

    static const char *const status_text[] = {
        "Not tested",
        "Testing",
        "Pass",
        "Fail",
    };
    static const uint32_t status_color[] = {
        0xAAAAAA,
        0xFFB020,
        0x34C759,
        0xFF3034,
    };
    lv_label_set_text(self_test_status_labels[item], status_text[status]);
    lv_obj_set_style_text_color(
        self_test_status_labels[item], lv_color_hex(status_color[status]), 0
    );
    return true;
}

bool speaker_ui_reset_self_test_statuses(void)
{
    for(int item = 0; item < SPEAKER_UI_SELF_TEST_COUNT; ++item) {
        if(!speaker_ui_set_self_test_status(
                (speaker_ui_self_test_item_t)item, SPEAKER_UI_SELF_TEST_NOT_TESTED)) return false;
    }
    return true;
}

bool speaker_ui_set_self_test_run_callback(lv_event_cb_t callback, void *user_data)
{
    if(self_test_run_button == NULL || callback == NULL) return false;
    lv_obj_add_event_cb(self_test_run_button, callback, LV_EVENT_CLICKED, user_data);
    return true;
}

bool speaker_ui_set_developer_mode_callback(lv_event_cb_t callback, void *user_data)
{
    if(settings_developer_row == NULL || callback == NULL) return false;
    lv_obj_remove_event_cb(settings_developer_row, show_screen_ref);
    lv_obj_add_event_cb(settings_developer_row, callback, LV_EVENT_CLICKED, user_data);
    return true;
}

bool speaker_ui_is_touch_sensor_on(void)
{
    return settings_touch_switch != NULL && lv_obj_has_state(settings_touch_switch, LV_STATE_CHECKED);
}

bool speaker_ui_set_touch_sensor_on(bool enabled)
{
    if(settings_touch_switch == NULL) return false;
    if(enabled) lv_obj_add_state(settings_touch_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(settings_touch_switch, LV_STATE_CHECKED);
    return true;
}

bool speaker_ui_is_wlan_keyboard_visible(void)
{
    return wlan_keyboard_container != NULL && !lv_obj_has_flag(wlan_keyboard_container, LV_OBJ_FLAG_HIDDEN);
}

bool speaker_ui_wlan_keyboard_bound(void)
{
    return wlan_keyboard != NULL && settings_wlan_password != NULL &&
           lv_keyboard_get_textarea(wlan_keyboard) == settings_wlan_password;
}

bool speaker_ui_set_wlan_password(const char *text)
{
    if(settings_wlan_password == NULL || text == NULL) return false;
    lv_textarea_set_text(settings_wlan_password, text);
    return true;
}

bool speaker_ui_confirm_wlan_password(void)
{
    wlan_password_ready(NULL);
    return true;
}

static bool wifi_qr_escape(const char *input, char *output, size_t output_size)
{
    if(input == NULL || output == NULL || output_size == 0) return false;
    size_t used = 0;
    for(const char *cursor = input; *cursor != '\0'; ++cursor) {
        const bool escaped = *cursor == '\\' || *cursor == ';' || *cursor == ',' ||
                             *cursor == ':' || *cursor == '"';
        if(used + (escaped ? 2U : 1U) >= output_size) return false;
        if(escaped) output[used++] = '\\';
        output[used++] = *cursor;
    }
    output[used] = '\0';
    return true;
}

bool speaker_ui_set_softap_credentials(const char *ssid, const char *password)
{
    if(softap_qr == NULL || softap_info_label == NULL || ssid == NULL || ssid[0] == '\0') return false;
    if(password == NULL) password = "";

    char escaped_ssid[65];
    char escaped_password[129];
    char qr_data[256];
    if(!wifi_qr_escape(ssid, escaped_ssid, sizeof(escaped_ssid)) ||
       !wifi_qr_escape(password, escaped_password, sizeof(escaped_password))) return false;

    const char *security = password[0] == '\0' ? "nopass" : "WPA";
    int length = snprintf(qr_data, sizeof(qr_data), "WIFI:T:%s;S:%s;P:%s;;",
                          security, escaped_ssid, escaped_password);
    if(length < 0 || (size_t)length >= sizeof(qr_data)) return false;
    lv_qrcode_update(softap_qr, qr_data, (uint32_t)length);

    if(password[0] == '\0') {
        lv_label_set_text_fmt(
            softap_info_label,
            "Option 1: Scan QRCode -> connect Wi-Fi in pop-up browser\n"
            "Option 2: Join Wi-Fi '%s' -> visit '192.168.4.1' in browser",
            ssid
        );
    } else {
        lv_label_set_text_fmt(
            softap_info_label,
            "Option 1: Scan QRCode -> connect Wi-Fi in pop-up browser\n"
            "Option 2: Join Wi-Fi '%s' (password: %s) -> visit '192.168.4.1' in browser",
            ssid, password
        );
    }
    return true;
}
