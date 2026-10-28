#include "music_ui.h"
#include "music_ui_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "modules/music/music_data.h"

#define MUSIC_WHITE  lv_color_hex(0xF5F7FA)
#define MUSIC_MUTED  lv_color_hex(0x9BA3B3)
#define MUSIC_CARD   lv_color_hex(0x242936)
#define MUSIC_ACCENT lv_color_hex(0x5AAFF0)

#define MUSIC_ROW_POOL      7
#define MUSIC_MAX_TRACKS    5000
#define MUSIC_SYNC_PERIOD   100
#define MUSIC_NO_INDEX      ((size_t)-1)

LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_14)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_16)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_20)
LV_IMAGE_DECLARE(music_heart_outline_24_24);

typedef struct {
    uint32_t color_start;
    uint32_t color_end;
    uint32_t shape_primary;
    uint32_t shape_secondary;
} music_palette_t;

static const music_palette_t palettes[] = {
    {0xFF6B35, 0xFFB347, 0xFFD166, 0xEF476F},
    {0x253C78, 0x6A4C93, 0x4CC9F0, 0xF72585},
    {0x111827, 0x30475E, 0x8B5CF6, 0x22D3EE},
    {0x075985, 0x0E7490, 0x67E8F9, 0x38BDF8},
    {0x92400E, 0xF59E0B, 0xFDE68A, 0xFB7185},
    {0x312E81, 0x7C3AED, 0xC4B5FD, 0xF472B6},
    {0x14532D, 0x15803D, 0x86EFAC, 0xFDE047},
    {0x7F1D1D, 0xBE123C, 0xFDA4AF, 0xFDBA74},
};
#define PALETTE_COUNT (sizeof(palettes) / sizeof(palettes[0]))

static lv_obj_t *album_card;
static lv_obj_t *album_shape_primary;
static lv_obj_t *album_shape_secondary;
static lv_obj_t *album_mark;
static lv_obj_t *album_cover_image;
static lv_obj_t *title_label;
static lv_obj_t *artist_label;
static lv_obj_t *elapsed_label;
static lv_obj_t *duration_label;
static lv_obj_t *progress_bar;
static lv_obj_t *play_label;
static lv_obj_t *repeat_label;
static lv_obj_t *favorite_icon;
static lv_obj_t *now_playing_page;
static lv_obj_t *playlist_page;
static lv_obj_t *playlist_track_view;
static lv_obj_t *playlist_spacer;
static lv_obj_t *playlist_empty_label;
static lv_obj_t *track_rows[MUSIC_ROW_POOL];
static lv_obj_t *track_row_titles[MUSIC_ROW_POOL];
static lv_obj_t *track_row_artists[MUSIC_ROW_POOL];
static lv_obj_t *track_row_durations[MUSIC_ROW_POOL];
static lv_obj_t *track_row_numbers[MUSIC_ROW_POOL];
static lv_obj_t *track_row_equalizers[MUSIC_ROW_POOL];
static size_t row_index[MUSIC_ROW_POOL];
static bool row_selected[MUSIC_ROW_POOL];
static music_data_track_t row_data[MUSIC_ROW_POOL];
static music_data_track_t shown_track;
static bool shown_valid;
static lv_timer_t *sync_timer;
static uint32_t seen_version;
static size_t track_total;
static size_t track_index;
static bool playing;
static bool repeat_enabled;
static bool data_idle = true;
static uint8_t liked[(MUSIC_MAX_TRACKS + 7) / 8];

static lv_obj_t *make_label(lv_obj_t *parent, const char *value, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, value);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, MUSIC_UI_PROFILE_SIZE, MUSIC_UI_PROFILE_SIZE);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static lv_obj_t *make_control(lv_obj_t *parent, int32_t size, const char *symbol,
                              lv_color_t background, lv_opa_t opacity, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(button, background, 0);
    lv_obj_set_style_bg_opa(button, opacity, 0);
    lv_obj_set_style_bg_color(button, lv_color_lighten(background, 24), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    if(callback != NULL) lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, MUSIC_WHITE, 0);
    lv_obj_center(label);
    return button;
}

static bool is_liked(size_t index)
{
    return index < MUSIC_MAX_TRACKS && (liked[index / 8] & (1u << (index % 8))) != 0;
}

static void toggle_liked(size_t index)
{
    if(index < MUSIC_MAX_TRACKS) liked[index / 8] ^= (uint8_t)(1u << (index % 8));
}

static void format_duration(char *buffer, size_t size, uint32_t duration_ms)
{
    const uint32_t seconds = duration_ms / 1000;
    snprintf(buffer, size, "%lu:%02lu", (unsigned long)(seconds / 60), (unsigned long)(seconds % 60));
}

static void set_data_idle(bool idle)
{
    if(data_idle == idle) return;
    data_idle = idle;
    music_data_set_ui_idle(idle);
}

static int32_t row_step(void)
{
    return MUSIC_UI_TRACK_ROW_HEIGHT + MUSIC_UI_TRACK_ROW_GAP;
}

static size_t first_visible_index(void)
{
    const int32_t offset = lv_obj_get_scroll_y(playlist_track_view) - MUSIC_UI_TRACK_ROW_FIRST_Y;
    return offset <= 0 ? 0 : (size_t)(offset / row_step());
}

static void update_spacer(void)
{
    const int32_t end_y = MUSIC_UI_TRACK_ROW_FIRST_Y + (int32_t)track_total * row_step() - MUSIC_UI_TRACK_ROW_GAP;
    lv_obj_set_pos(playlist_spacer, 0, end_y > 0 ? end_y - 1 : 0);
    if(track_total == 0) lv_obj_remove_flag(playlist_empty_label, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(playlist_empty_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(playlist_empty_label, music_data_scan_done() ? "No music found" : "Scanning...");
}

static void hide_row(size_t slot)
{
    lv_obj_add_flag(track_rows[slot], LV_OBJ_FLAG_HIDDEN);
    row_index[slot] = MUSIC_NO_INDEX;
}

static void style_row(size_t slot, bool selected)
{
    lv_obj_set_style_bg_color(track_rows[slot], selected ? MUSIC_ACCENT : MUSIC_CARD, 0);
    lv_obj_set_style_bg_opa(track_rows[slot], selected ? LV_OPA_30 : LV_OPA_COVER, 0);
    lv_obj_set_style_border_opa(track_rows[slot], selected ? LV_OPA_70 : LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(track_row_titles[slot], selected ? MUSIC_ACCENT : MUSIC_WHITE, 0);
    lv_obj_set_style_text_color(track_row_artists[slot], selected ? lv_color_hex(0xB9DCFA) : MUSIC_MUTED, 0);
    lv_obj_set_style_text_color(track_row_durations[slot], selected ? MUSIC_ACCENT : MUSIC_MUTED, 0);
    lv_obj_set_style_text_color(track_row_numbers[slot], selected ? MUSIC_ACCENT : MUSIC_MUTED, 0);
    if(selected) {
        lv_obj_add_flag(track_row_numbers[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(track_row_equalizers[slot], LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(track_row_numbers[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(track_row_equalizers[slot], LV_OBJ_FLAG_HIDDEN);
    }
}

static void bind_row(size_t slot, size_t index)
{
    music_data_track_t data;
    if(!music_data_get(index, &data)) {
        hide_row(slot);
        return;
    }
    const bool selected = index == track_index;
    const bool same_index = row_index[slot] == index;
    if(same_index && row_selected[slot] == selected && memcmp(&row_data[slot], &data, sizeof(data)) == 0) return;

    char text[16];
    if(!same_index) {
        lv_obj_remove_flag(track_rows[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(track_rows[slot], MUSIC_UI_TRACK_ROW_FIRST_Y + (int32_t)index * row_step());
        snprintf(text, sizeof(text), "%02lu", (unsigned long)(index + 1));
        lv_label_set_text(track_row_numbers[slot], text);
    }
    if(!same_index || strcmp(row_data[slot].title, data.title) != 0) lv_label_set_text(track_row_titles[slot], data.title);
    const char *artist = data.state == MUSIC_DATA_READY ? data.artist : "";
    const char *old_artist = row_data[slot].state == MUSIC_DATA_READY ? row_data[slot].artist : "";
    if(!same_index || strcmp(old_artist, artist) != 0) lv_label_set_text(track_row_artists[slot], artist);
    if(!same_index || row_data[slot].duration_ms != data.duration_ms || row_data[slot].state != data.state) {
        text[0] = '\0';
        if(data.state == MUSIC_DATA_READY && data.duration_ms > 0) format_duration(text, sizeof(text), data.duration_ms);
        lv_label_set_text(track_row_durations[slot], text);
    }
    if(!same_index || row_selected[slot] != selected) style_row(slot, selected);

    row_index[slot] = index;
    row_selected[slot] = selected;
    row_data[slot] = data;
}

static void sync_rows(void)
{
    const size_t base = first_visible_index();
    for(size_t k = 0; k < MUSIC_ROW_POOL; k++) {
        const size_t index = base + k;
        const size_t slot = index % MUSIC_ROW_POOL;
        if(index < track_total) bind_row(slot, index);
        else if(row_index[slot] != MUSIC_NO_INDEX) hide_row(slot);
    }
}

static void update_wanted(void)
{
    size_t wanted[MUSIC_ROW_POOL];
    size_t count = 0;
    const int32_t view_bottom = lv_obj_get_scroll_y(playlist_track_view) + MUSIC_UI_TRACK_VIEW_HEIGHT;
    for(size_t index = first_visible_index(); index < track_total && count < MUSIC_ROW_POOL; index++) {
        if(MUSIC_UI_TRACK_ROW_FIRST_Y + (int32_t)index * row_step() >= view_bottom) break;
        wanted[count++] = index;
    }
    music_data_set_wanted(wanted, count);
}

static void refresh_cover(size_t index)
{
    const music_data_cover_t cover = music_data_get_cover(index);
    if(cover == NULL) {
        lv_obj_add_flag(album_cover_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(album_shape_primary, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(album_shape_secondary, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(album_mark, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_image_set_src(album_cover_image, cover);
    lv_obj_center(album_cover_image);
    lv_obj_remove_flag(album_cover_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(album_shape_primary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(album_shape_secondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(album_mark, LV_OBJ_FLAG_HIDDEN);
}

static void refresh_track(void)
{
    char text[16];
    music_data_track_t data;
    const bool have = track_total > 0 && music_data_get(track_index, &data);
    if(have) {
        const music_palette_t *palette = &palettes[track_index % PALETTE_COUNT];
        lv_label_set_text(title_label, data.title);
        lv_label_set_text(artist_label, data.state == MUSIC_DATA_READY ? data.artist : "");
        format_duration(text, sizeof(text), data.state == MUSIC_DATA_READY ? data.duration_ms : 0);
        lv_label_set_text(duration_label, text);
        lv_obj_set_style_bg_color(album_card, lv_color_hex(palette->color_start), 0);
        lv_obj_set_style_bg_grad_color(album_card, lv_color_hex(palette->color_end), 0);
        lv_obj_set_style_bg_color(album_shape_primary, lv_color_hex(palette->shape_primary), 0);
        lv_obj_set_style_bg_color(album_shape_secondary, lv_color_hex(palette->shape_secondary), 0);
        shown_track = data;
        shown_valid = true;
    } else {
        lv_label_set_text(title_label, "No music");
        lv_label_set_text(artist_label, music_data_scan_done() ? "Add MP3 files to /Music" : "Scanning...");
        lv_label_set_text(duration_label, "0:00");
        lv_obj_set_style_bg_color(album_card, lv_color_hex(0x242936), 0);
        lv_obj_set_style_bg_grad_color(album_card, lv_color_hex(0x30475E), 0);
        lv_obj_set_style_bg_color(album_shape_primary, lv_color_hex(0x3A4660), 0);
        lv_obj_set_style_bg_color(album_shape_secondary, lv_color_hex(0x2C3548), 0);
        shown_valid = false;
    }
    refresh_cover(have ? track_index : MUSIC_NO_INDEX);
    lv_label_set_text(elapsed_label, "0:00");
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_text_color(repeat_label, repeat_enabled ? MUSIC_ACCENT : MUSIC_WHITE, 0);
    lv_obj_set_style_image_recolor(favorite_icon, is_liked(track_index) ? MUSIC_ACCENT : MUSIC_WHITE, 0);
    if(playlist_page != NULL && !lv_obj_has_flag(playlist_page, LV_OBJ_FLAG_HIDDEN)) sync_rows();
}

static void select_index(size_t index)
{
    track_index = index;
    music_data_set_pinned(index);
    refresh_track();
}

static void sync_total(void)
{
    const size_t total = music_data_count();
    if(total == track_total) return;
    track_total = total;
    if(track_index >= track_total) track_index = 0;
    update_spacer();
}

static void sync_timer_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    /* Safety net: never leave the loader blocked when a scroll ended unnoticed. */
    if(!data_idle && !lv_obj_is_scrolling(playlist_track_view) &&
       lv_anim_get(playlist_track_view, NULL) == NULL) {
        set_data_idle(true);
    }

    const uint32_t version = music_data_version();
    if(version == seen_version) return;
    seen_version = version;

    sync_total();

    music_data_track_t data;
    const bool have = track_total > 0 && music_data_get(track_index, &data);
    if(have != shown_valid || (have && memcmp(&data, &shown_track, sizeof(data)) != 0)) refresh_track();
    if(!lv_obj_has_flag(playlist_page, LV_OBJ_FLAG_HIDDEN)) sync_rows();
}

static void play_pause_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    playing = !playing;
    lv_label_set_text(play_label, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void previous_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if(track_total == 0) return;
    select_index(track_index == 0 ? track_total - 1 : track_index - 1);
}

static void next_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if(track_total == 0) return;
    select_index((track_index + 1) % track_total);
}

static void repeat_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    repeat_enabled = !repeat_enabled;
    lv_obj_set_style_text_color(repeat_label, repeat_enabled ? MUSIC_ACCENT : MUSIC_WHITE, 0);
}

static void favorite_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    if(track_total == 0) return;
    toggle_liked(track_index);
    lv_obj_set_style_image_recolor(favorite_icon, is_liked(track_index) ? MUSIC_ACCENT : MUSIC_WHITE, 0);
}

static void show_now_playing_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    music_ui_show_now_playing();
}

void music_ui_show_now_playing(void)
{
    lv_obj_add_flag(playlist_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(now_playing_page, LV_OBJ_FLAG_HIDDEN);
    set_data_idle(true);
    music_data_set_wanted(NULL, 0);
}

static void show_playlist_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    music_ui_show_playlist();
}

void music_ui_show_playlist(void)
{
    lv_obj_add_flag(now_playing_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(playlist_page, LV_OBJ_FLAG_HIDDEN);
    sync_total();
    sync_rows();
    set_data_idle(true);
    update_wanted();
}

static void select_track_cb(lv_event_t *event)
{
    const size_t slot = (size_t)(uintptr_t)lv_event_get_user_data(event);
    const size_t selected = slot < MUSIC_ROW_POOL ? row_index[slot] : MUSIC_NO_INDEX;
    if(selected < track_total) select_index(selected);
    show_now_playing_cb(event);
}

static void playlist_scroll_begin_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    set_data_idle(false);
}

static void playlist_scroll_cb(lv_event_t *event)
{
    LV_UNUSED(event);
    sync_rows();
}

static void playlist_scroll_end_cb(lv_event_t *event)
{
    lv_obj_t *track_view = lv_event_get_target_obj(event);
    const int32_t scroll_y = lv_obj_get_scroll_y(track_view);
    const int32_t max_scroll_y = scroll_y + lv_obj_get_scroll_bottom(track_view);
    const int32_t step = row_step();
    const int32_t half_row_offset = MUSIC_UI_TRACK_ROW_FIRST_Y + (MUSIC_UI_TRACK_ROW_HEIGHT + 1) / 2;
    int32_t target_y;

    if(scroll_y <= half_row_offset / 2) {
        target_y = 0;
    } else {
        const int32_t relative_y = scroll_y - half_row_offset;
        const int32_t row_number = relative_y > 0 ? (relative_y + step / 2) / step : 0;
        target_y = half_row_offset + row_number * step;
    }

    if(target_y > max_scroll_y) target_y = max_scroll_y;
    if(target_y < 0) target_y = 0;
    if(scroll_y != target_y) {
        lv_obj_scroll_to_y(track_view, target_y, LV_ANIM_ON);
        return;
    }

    sync_rows();
    set_data_idle(true);
    update_wanted();
}

static void build_playlist_page(lv_obj_t *parent)
{
    lv_obj_t *navigation = lv_obj_create(parent);
    lv_obj_remove_style_all(navigation);
    lv_obj_set_size(navigation, MUSIC_UI_LIST_NAV_WIDTH, MUSIC_UI_LIST_NAV_HEIGHT);
    lv_obj_set_pos(navigation, MUSIC_UI_CENTER_X(MUSIC_UI_LIST_NAV_WIDTH), MUSIC_UI_LIST_NAV_Y);
    lv_obj_set_flex_flow(navigation, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(navigation, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(navigation, MUSIC_UI_SCALE(7), 0);
    lv_obj_set_style_opa(navigation, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_add_flag(navigation, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(navigation, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(navigation, show_now_playing_cb, LV_EVENT_CLICKED, NULL);

    make_label(navigation, LV_SYMBOL_LEFT, &lv_font_montserrat_24, MUSIC_ACCENT);
    make_label(navigation, "Now Playing", &esp_brookesia_font_maison_neue_book_20, MUSIC_ACCENT);

    lv_obj_t *track_view = lv_obj_create(parent);
    lv_obj_remove_style_all(track_view);
    lv_obj_set_size(track_view, MUSIC_UI_TRACK_VIEW_WIDTH, MUSIC_UI_TRACK_VIEW_HEIGHT);
    lv_obj_set_pos(track_view, MUSIC_UI_CENTER_X(MUSIC_UI_TRACK_VIEW_WIDTH), MUSIC_UI_TRACK_VIEW_Y);
    lv_obj_set_scroll_dir(track_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(track_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(track_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_bottom(track_view, MUSIC_UI_SCALE(12), 0);
    lv_obj_add_event_cb(track_view, playlist_scroll_begin_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(track_view, playlist_scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(track_view, playlist_scroll_end_cb, LV_EVENT_SCROLL_END, NULL);
    playlist_track_view = track_view;

    playlist_spacer = lv_obj_create(track_view);
    lv_obj_remove_style_all(playlist_spacer);
    lv_obj_set_size(playlist_spacer, 1, 1);
    lv_obj_remove_flag(playlist_spacer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(playlist_spacer, LV_OBJ_FLAG_SCROLLABLE);

    playlist_empty_label = make_label(track_view, "", &esp_brookesia_font_maison_neue_book_16, MUSIC_MUTED);
    lv_obj_set_width(playlist_empty_label, MUSIC_UI_TRACK_LIST_WIDTH);
    lv_obj_set_style_text_align(playlist_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(playlist_empty_label, (MUSIC_UI_TRACK_VIEW_WIDTH - MUSIC_UI_TRACK_LIST_WIDTH) / 2, MUSIC_UI_SCALE(90));

    for(size_t i = 0; i < MUSIC_ROW_POOL; i++) {
        lv_obj_t *row = lv_obj_create(track_view);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, MUSIC_UI_TRACK_LIST_WIDTH, MUSIC_UI_TRACK_ROW_HEIGHT);
        lv_obj_set_pos(row, (MUSIC_UI_TRACK_VIEW_WIDTH - MUSIC_UI_TRACK_LIST_WIDTH) / 2, MUSIC_UI_TRACK_ROW_FIRST_Y);
        lv_obj_set_style_radius(row, MUSIC_UI_SCALE(12), 0);
        lv_obj_set_style_bg_color(row, MUSIC_CARD, 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, MUSIC_UI_SCALE(1), 0);
        lv_obj_set_style_border_color(row, MUSIC_ACCENT, 0);
        lv_obj_set_style_border_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x303746), LV_STATE_PRESSED);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(row, select_track_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        track_rows[i] = row;
        row_index[i] = MUSIC_NO_INDEX;

        lv_obj_t *number = make_label(row, "", &esp_brookesia_font_maison_neue_book_14, MUSIC_MUTED);
        lv_obj_set_width(number, MUSIC_UI_SCALE(28));
        lv_obj_set_style_text_align(number, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(number, MUSIC_UI_SCALE(8), MUSIC_UI_SCALE(17));
        track_row_numbers[i] = number;

        lv_obj_t *equalizer = lv_obj_create(row);
        lv_obj_remove_style_all(equalizer);
        lv_obj_set_size(equalizer, MUSIC_UI_SCALE(28), MUSIC_UI_SCALE(24));
        lv_obj_set_pos(equalizer, MUSIC_UI_SCALE(8), MUSIC_UI_SCALE(13));
        lv_obj_remove_flag(equalizer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(equalizer, LV_OBJ_FLAG_HIDDEN);
        for(int32_t bar = 0; bar < 3; bar++) {
            static const int32_t bar_y[] = {10, 4, 7};
            static const int32_t bar_height[] = {9, 15, 12};
            lv_obj_t *segment = lv_obj_create(equalizer);
            lv_obj_remove_style_all(segment);
            lv_obj_set_size(segment, MUSIC_UI_SCALE(4), MUSIC_UI_SCALE(bar_height[bar]));
            lv_obj_set_pos(segment, MUSIC_UI_SCALE(3 + bar * 7), MUSIC_UI_SCALE(bar_y[bar]));
            lv_obj_set_style_radius(segment, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(segment, MUSIC_ACCENT, 0);
            lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
        }
        track_row_equalizers[i] = equalizer;

        lv_obj_t *title = make_label(row, "", &esp_brookesia_font_maison_neue_book_16, MUSIC_WHITE);
        lv_obj_set_size(title, MUSIC_UI_SCALE(140), lv_font_get_line_height(&esp_brookesia_font_maison_neue_book_16));
        lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(title, MUSIC_UI_SCALE(45), MUSIC_UI_SCALE(5));
        track_row_titles[i] = title;

        lv_obj_t *artist = make_label(row, "", &esp_brookesia_font_maison_neue_book_14, MUSIC_MUTED);
        lv_obj_set_size(artist, MUSIC_UI_SCALE(140), lv_font_get_line_height(&esp_brookesia_font_maison_neue_book_14));
        lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
        lv_obj_set_pos(artist, MUSIC_UI_SCALE(45), MUSIC_UI_SCALE(27));
        track_row_artists[i] = artist;

        lv_obj_t *duration = make_label(row, "", &esp_brookesia_font_maison_neue_book_14, MUSIC_MUTED);
        lv_obj_set_width(duration, MUSIC_UI_SCALE(42));
        lv_obj_set_style_text_align(duration, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(duration, MUSIC_UI_SCALE(194), MUSIC_UI_SCALE(8));
        track_row_durations[i] = duration;
    }
}

static void build_now_playing_page(lv_obj_t *parent)
{
    album_card = lv_obj_create(parent);
    lv_obj_remove_style_all(album_card);
    lv_obj_set_size(album_card, MUSIC_UI_ALBUM_SIZE, MUSIC_UI_ALBUM_SIZE);
    lv_obj_set_pos(album_card, MUSIC_UI_CENTER_X(MUSIC_UI_ALBUM_SIZE), MUSIC_UI_ALBUM_Y);
    lv_obj_set_style_radius(album_card, MUSIC_UI_ALBUM_RADIUS, 0);
    lv_obj_set_style_clip_corner(album_card, true, 0);
    lv_obj_set_style_bg_opa(album_card, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_grad_dir(album_card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(album_card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(album_card, LV_OPA_30, 0);
    lv_obj_set_style_shadow_width(album_card, MUSIC_UI_SCALE(16), 0);
    lv_obj_set_style_shadow_offset_y(album_card, MUSIC_UI_SCALE(6), 0);
    lv_obj_remove_flag(album_card, LV_OBJ_FLAG_SCROLLABLE);

    album_shape_primary = lv_obj_create(album_card);
    lv_obj_remove_style_all(album_shape_primary);
    lv_obj_set_size(album_shape_primary, MUSIC_UI_SCALE(90), MUSIC_UI_SCALE(90));
    lv_obj_set_pos(album_shape_primary, -MUSIC_UI_SCALE(22), MUSIC_UI_SCALE(66));
    lv_obj_set_style_radius(album_shape_primary, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(album_shape_primary, LV_OPA_70, 0);

    album_shape_secondary = lv_obj_create(album_card);
    lv_obj_remove_style_all(album_shape_secondary);
    lv_obj_set_size(album_shape_secondary, MUSIC_UI_SCALE(72), MUSIC_UI_SCALE(72));
    lv_obj_set_pos(album_shape_secondary, MUSIC_UI_SCALE(76), -MUSIC_UI_SCALE(18));
    lv_obj_set_style_radius(album_shape_secondary, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(album_shape_secondary, LV_OPA_60, 0);

    album_mark = make_label(album_card, "V", &esp_brookesia_font_maison_neue_book_20, MUSIC_WHITE);
    lv_obj_set_style_text_opa(album_mark, LV_OPA_80, 0);
    lv_obj_align(album_mark, LV_ALIGN_BOTTOM_RIGHT, -MUSIC_UI_SCALE(14), -MUSIC_UI_SCALE(10));

    album_cover_image = lv_image_create(album_card);
    lv_obj_add_flag(album_cover_image, LV_OBJ_FLAG_HIDDEN);

    title_label = make_label(parent, "", &esp_brookesia_font_maison_neue_book_20, MUSIC_WHITE);
    lv_obj_set_width(title_label, MUSIC_UI_TITLE_WIDTH);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_pos(title_label, MUSIC_UI_CENTER_X(MUSIC_UI_TITLE_WIDTH), MUSIC_UI_TITLE_Y);

    artist_label = make_label(parent, "", &esp_brookesia_font_maison_neue_book_14, MUSIC_MUTED);
    lv_obj_set_size(artist_label, MUSIC_UI_ARTIST_WIDTH, lv_font_get_line_height(&esp_brookesia_font_maison_neue_book_14));
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(artist_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(artist_label, MUSIC_UI_CENTER_X(MUSIC_UI_ARTIST_WIDTH), MUSIC_UI_ARTIST_Y);

    progress_bar = lv_bar_create(parent);
    lv_obj_set_size(progress_bar, MUSIC_UI_PROGRESS_WIDTH, MUSIC_UI_PROGRESS_HEIGHT);
    lv_obj_set_pos(progress_bar, MUSIC_UI_PROGRESS_X, MUSIC_UI_PROGRESS_Y);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar, MUSIC_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, MUSIC_ACCENT, LV_PART_INDICATOR);

    elapsed_label = make_label(parent, "", &esp_brookesia_font_maison_neue_book_14, MUSIC_MUTED);
    lv_obj_set_width(elapsed_label, MUSIC_UI_TIME_WIDTH);
    lv_obj_set_style_text_align(elapsed_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(elapsed_label, MUSIC_UI_ELAPSED_X, MUSIC_UI_TIME_Y);

    duration_label = make_label(parent, "", &esp_brookesia_font_maison_neue_book_14, MUSIC_MUTED);
    lv_obj_set_width(duration_label, MUSIC_UI_TIME_WIDTH);
    lv_obj_set_style_text_align(duration_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(duration_label, MUSIC_UI_DURATION_X, MUSIC_UI_TIME_Y);

    lv_obj_t *repeat = make_control(parent, MUSIC_UI_SIDE_BUTTON_SIZE, LV_SYMBOL_LOOP,
                                    lv_color_hex(0x14161D), LV_OPA_TRANSP, repeat_cb);
    lv_obj_set_pos(repeat, MUSIC_UI_REPEAT_X, MUSIC_UI_SIDE_BUTTON_Y);
    repeat_label = lv_obj_get_child(repeat, 0);

    lv_obj_t *previous = make_control(parent, MUSIC_UI_TRACK_BUTTON_SIZE, LV_SYMBOL_PREV,
                                      MUSIC_CARD, LV_OPA_COVER, previous_cb);
    lv_obj_set_pos(previous, MUSIC_UI_PREVIOUS_X, MUSIC_UI_TRACK_BUTTON_Y);

    lv_obj_t *play = make_control(parent, MUSIC_UI_PLAY_BUTTON_SIZE, LV_SYMBOL_PLAY,
                                  MUSIC_ACCENT, LV_OPA_COVER, play_pause_cb);
    lv_obj_set_pos(play, MUSIC_UI_PLAY_X, MUSIC_UI_PLAY_BUTTON_Y);
    play_label = lv_obj_get_child(play, 0);

    lv_obj_t *next = make_control(parent, MUSIC_UI_TRACK_BUTTON_SIZE, LV_SYMBOL_NEXT,
                                  MUSIC_CARD, LV_OPA_COVER, next_cb);
    lv_obj_set_pos(next, MUSIC_UI_NEXT_X, MUSIC_UI_TRACK_BUTTON_Y);

    lv_obj_t *favorite = make_control(parent, MUSIC_UI_SIDE_BUTTON_SIZE, "",
                                      lv_color_hex(0x14161D), LV_OPA_TRANSP, favorite_cb);
    lv_obj_set_pos(favorite, MUSIC_UI_FAVORITE_X, MUSIC_UI_SIDE_BUTTON_Y);
    lv_obj_delete(lv_obj_get_child(favorite, 0));
    favorite_icon = lv_image_create(favorite);
    lv_image_set_src(favorite_icon, &music_heart_outline_24_24);
    lv_obj_set_style_image_recolor_opa(favorite_icon, LV_OPA_COVER, 0);
    lv_obj_center(favorite_icon);

    lv_obj_t *playlist = lv_obj_create(parent);
    lv_obj_remove_style_all(playlist);
    lv_obj_set_size(playlist, MUSIC_UI_PLAYLIST_WIDTH, MUSIC_UI_PLAYLIST_HEIGHT);
    lv_obj_set_pos(playlist, MUSIC_UI_CENTER_X(MUSIC_UI_PLAYLIST_WIDTH), MUSIC_UI_PLAYLIST_Y);
    lv_obj_set_style_radius(playlist, MUSIC_UI_PLAYLIST_HEIGHT / 2, 0);
    lv_obj_set_style_bg_color(playlist, MUSIC_CARD, 0);
    lv_obj_set_style_bg_opa(playlist, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(playlist, lv_color_hex(0x303746), LV_STATE_PRESSED);
    lv_obj_add_flag(playlist, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(playlist, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(playlist, show_playlist_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *playlist_label = make_label(playlist, "Playlist  " LV_SYMBOL_LIST, &lv_font_montserrat_14, MUSIC_MUTED);
    lv_obj_center(playlist_label);
}

void music_ui_create(lv_obj_t *parent)
{
    track_index = 0;
    playing = false;
    repeat_enabled = false;
    data_idle = true;
    memset(liked, 0, sizeof(liked));
    memset(row_data, 0, sizeof(row_data));
    memset(row_selected, 0, sizeof(row_selected));

    music_data_start();
    track_total = music_data_count();
    seen_version = music_data_version();

    now_playing_page = make_page(parent);
    playlist_page = make_page(parent);
    build_now_playing_page(now_playing_page);
    build_playlist_page(playlist_page);
    lv_obj_add_flag(playlist_page, LV_OBJ_FLAG_HIDDEN);
    update_spacer();
    refresh_track();
    sync_timer = lv_timer_create(sync_timer_cb, MUSIC_SYNC_PERIOD, NULL);
}

size_t music_ui_get_track_index(void)
{
    return track_index;
}

bool music_ui_is_playing(void)
{
    return playing;
}

bool music_ui_is_playlist_visible(void)
{
    return playlist_page != NULL && !lv_obj_has_flag(playlist_page, LV_OBJ_FLAG_HIDDEN);
}

int32_t music_ui_get_playlist_scroll_y(void)
{
    return playlist_track_view != NULL ? lv_obj_get_scroll_y(playlist_track_view) : 0;
}
