/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "game_2048.h"

#include <inttypes.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_random.h"
#endif

#define GAME_SIZE 4
#define BOARD_SIZE 216
#define BOARD_X 72
#define BOARD_Y 104
#define GRID_GAP 8
#define CELL_SIZE 44
#define MOVE_ANIMATION_MS 140

#define COLOR_SCREEN lv_color_hex(0xFAF8EF)
#define COLOR_BOARD lv_color_hex(0xBBADA0)
#define COLOR_EMPTY lv_color_hex(0xCDC1B4)
#define COLOR_DARK_TEXT lv_color_hex(0x776E65)
#define COLOR_LIGHT_TEXT lv_color_hex(0xF9F6F2)
#define COLOR_SCORE_BOX lv_color_hex(0x8F7A66)

LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_16)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_18)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_20)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_24)

typedef struct {
    uint8_t row;
    uint8_t column;
} cell_position_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *board;
    lv_obj_t *motion_layer;
    lv_obj_t *game_over_panel;
    lv_obj_t *score_label;
    lv_obj_t *best_label;
    lv_obj_t *cells[GAME_SIZE][GAME_SIZE];
    lv_obj_t *cell_labels[GAME_SIZE][GAME_SIZE];
    game_2048_model_t model;
    uint16_t pending_animations;
    bool animating;
} game_2048_ui_t;

static game_2048_ui_t game;
static uint32_t random_state;

static uint32_t next_random(void)
{
    uint32_t value = random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    random_state = value;
    return value;
}

static cell_position_t line_position(game_2048_direction_t direction, int line, int index)
{
    cell_position_t position = {0};
    switch(direction) {
    case GAME_2048_DIRECTION_LEFT:
        position.row = (uint8_t)line;
        position.column = (uint8_t)index;
        break;
    case GAME_2048_DIRECTION_RIGHT:
        position.row = (uint8_t)line;
        position.column = (uint8_t)(GAME_SIZE - 1 - index);
        break;
    case GAME_2048_DIRECTION_UP:
        position.row = (uint8_t)index;
        position.column = (uint8_t)line;
        break;
    case GAME_2048_DIRECTION_DOWN:
        position.row = (uint8_t)(GAME_SIZE - 1 - index);
        position.column = (uint8_t)line;
        break;
    }
    return position;
}

static bool move_model(
    game_2048_model_t *model,
    game_2048_direction_t direction,
    cell_position_t targets[GAME_SIZE][GAME_SIZE]
)
{
    if(model == NULL) return false;

    game_2048_model_t before = *model;
    uint32_t next[GAME_SIZE][GAME_SIZE] = {0};
    uint32_t score_delta = 0;

    if(targets != NULL) {
        for(int row = 0; row < GAME_SIZE; ++row) {
            for(int column = 0; column < GAME_SIZE; ++column) {
                targets[row][column] = (cell_position_t){(uint8_t)row, (uint8_t)column};
            }
        }
    }

    for(int line = 0; line < GAME_SIZE; ++line) {
        uint32_t values[GAME_SIZE] = {0};
        cell_position_t sources[GAME_SIZE] = {0};
        int value_count = 0;

        for(int index = 0; index < GAME_SIZE; ++index) {
            const cell_position_t source = line_position(direction, line, index);
            const uint32_t value = before.cells[source.row][source.column];
            if(value == 0) continue;
            values[value_count] = value;
            sources[value_count] = source;
            ++value_count;
        }

        int source_index = 0;
        int output_index = 0;
        while(source_index < value_count) {
            const cell_position_t destination = line_position(direction, line, output_index);
            uint32_t output_value = values[source_index];
            if(targets != NULL) {
                targets[sources[source_index].row][sources[source_index].column] = destination;
            }

            if(source_index + 1 < value_count && values[source_index + 1] == output_value) {
                output_value *= 2;
                score_delta += output_value;
                if(targets != NULL) {
                    targets[sources[source_index + 1].row][sources[source_index + 1].column] = destination;
                }
                source_index += 2;
            } else {
                ++source_index;
            }

            next[destination.row][destination.column] = output_value;
            ++output_index;
        }
    }

    if(memcmp(before.cells, next, sizeof(next)) == 0) return false;
    memcpy(model->cells, next, sizeof(next));
    model->score += score_delta;
    if(model->score > model->best_score) model->best_score = model->score;
    return true;
}

void game_2048_model_reset(game_2048_model_t *model)
{
    if(model == NULL) return;
    const uint32_t best_score = model->best_score;
    memset(model, 0, sizeof(*model));
    model->best_score = best_score;
}

bool game_2048_model_move(game_2048_model_t *model, game_2048_direction_t direction)
{
    return move_model(model, direction, NULL);
}

bool game_2048_model_is_game_over(const game_2048_model_t *model)
{
    if(model == NULL) return true;
    for(int row = 0; row < GAME_SIZE; ++row) {
        for(int column = 0; column < GAME_SIZE; ++column) {
            const uint32_t value = model->cells[row][column];
            if(value == 0) return false;
            if(column + 1 < GAME_SIZE && value == model->cells[row][column + 1]) return false;
            if(row + 1 < GAME_SIZE && value == model->cells[row + 1][column]) return false;
        }
    }
    return true;
}

static lv_color_t tile_color(uint32_t value)
{
    switch(value) {
    case 2: return lv_color_hex(0xEEE4DA);
    case 4: return lv_color_hex(0xEDE0C8);
    case 8: return lv_color_hex(0xF2B179);
    case 16: return lv_color_hex(0xF59563);
    case 32: return lv_color_hex(0xF67C5F);
    case 64: return lv_color_hex(0xF65E3B);
    case 128: return lv_color_hex(0xEDCF72);
    case 256: return lv_color_hex(0xEDCC61);
    case 512: return lv_color_hex(0xEDC850);
    case 1024: return lv_color_hex(0xEDC53F);
    default: return value == 0 ? COLOR_EMPTY : lv_color_hex(0xEDC22E);
    }
}

static const lv_font_t *tile_font(uint32_t value)
{
    if(value >= 16384) return &esp_brookesia_font_maison_neue_book_16;
    if(value >= 1024) return &esp_brookesia_font_maison_neue_book_20;
    return &esp_brookesia_font_maison_neue_book_24;
}

static lv_color_t tile_text_color(uint32_t value)
{
    return value <= 4 ? COLOR_DARK_TEXT : COLOR_LIGHT_TEXT;
}

static void style_plain(lv_obj_t *object)
{
    lv_obj_remove_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static void style_tile(lv_obj_t *tile, lv_obj_t *label, uint32_t value)
{
    lv_obj_set_style_bg_color(tile, tile_color(value), 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(tile, 5, 0);
    if(value == 0) {
        lv_label_set_text(label, "");
        return;
    }
    lv_obj_set_style_text_font(label, tile_font(value), 0);
    lv_obj_set_style_text_color(label, tile_text_color(value), 0);
    lv_label_set_text_fmt(label, "%" PRIu32, value);
    lv_obj_center(label);
}

static void render_scores(void)
{
    if(game.score_label != NULL) lv_label_set_text_fmt(game.score_label, "%" PRIu32, game.model.score);
    if(game.best_label != NULL) lv_label_set_text_fmt(game.best_label, "%" PRIu32, game.model.best_score);
}

static void render_board(void)
{
    for(int row = 0; row < GAME_SIZE; ++row) {
        for(int column = 0; column < GAME_SIZE; ++column) {
            style_tile(game.cells[row][column], game.cell_labels[row][column], game.model.cells[row][column]);
        }
    }
    render_scores();
    if(game.game_over_panel != NULL) {
        if(game_2048_model_is_game_over(&game.model)) {
            lv_obj_remove_flag(game.game_over_panel, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(game.game_over_panel);
        } else {
            lv_obj_add_flag(game.game_over_panel, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static bool spawn_tile(void)
{
    cell_position_t empty[GAME_SIZE * GAME_SIZE];
    int empty_count = 0;
    for(int row = 0; row < GAME_SIZE; ++row) {
        for(int column = 0; column < GAME_SIZE; ++column) {
            if(game.model.cells[row][column] == 0) {
                empty[empty_count++] = (cell_position_t){(uint8_t)row, (uint8_t)column};
            }
        }
    }
    if(empty_count == 0) return false;
    const cell_position_t target = empty[next_random() % (uint32_t)empty_count];
    game.model.cells[target.row][target.column] = (next_random() % 10U == 0) ? 4 : 2;
    return true;
}

static void clear_motion_layer(void)
{
    if(game.motion_layer == NULL) return;
    while(lv_obj_get_child_count(game.motion_layer) > 0) {
        lv_obj_t *child = lv_obj_get_child(game.motion_layer, 0);
        lv_anim_delete(child, NULL);
        lv_obj_delete(child);
    }
}

static void finish_move(void)
{
    clear_motion_layer();
    game.pending_animations = 0;
    spawn_tile();
    render_board();
    game.animating = false;
}

static void move_animation_ready(lv_anim_t *animation)
{
    LV_UNUSED(animation);
    if(game.pending_animations > 0) --game.pending_animations;
    if(game.pending_animations == 0 && game.animating) finish_move();
}

static lv_obj_t *create_motion_tile(int row, int column, uint32_t value)
{
    lv_obj_t *tile = lv_obj_create(game.motion_layer);
    style_plain(tile);
    lv_obj_set_size(tile, CELL_SIZE, CELL_SIZE);
    lv_obj_set_pos(tile, GRID_GAP + column * (CELL_SIZE + GRID_GAP),
                   GRID_GAP + row * (CELL_SIZE + GRID_GAP));
    lv_obj_t *label = lv_label_create(tile);
    style_tile(tile, label, value);
    return tile;
}

static void set_tile_x(void *tile, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)tile, value);
}

static void set_tile_y(void *tile, int32_t value)
{
    lv_obj_set_y((lv_obj_t *)tile, value);
}

static void animate_tile(lv_obj_t *tile, int32_t start, int32_t destination, bool horizontal)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, tile);
    lv_anim_set_values(&animation, start, destination);
    lv_anim_set_duration(&animation, MOVE_ANIMATION_MS);
    lv_anim_set_path_cb(&animation, lv_anim_path_custom_bezier3);
    lv_anim_set_bezier3_param(
        &animation,
        LV_BEZIER_VAL_FLOAT(0.20), LV_BEZIER_VAL_FLOAT(0.35),
        LV_BEZIER_VAL_FLOAT(0.55), LV_BEZIER_VAL_FLOAT(1.05)
    );
    lv_anim_set_exec_cb(&animation, horizontal ? set_tile_x : set_tile_y);
    lv_anim_set_ready_cb(&animation, move_animation_ready);
    lv_anim_start(&animation);
    ++game.pending_animations;
}

bool game_2048_handle_swipe(game_2048_direction_t direction)
{
    if(game.root == NULL || game.animating) return false;

    const game_2048_model_t before = game.model;
    cell_position_t targets[GAME_SIZE][GAME_SIZE];
    if(!move_model(&game.model, direction, targets)) return false;

    game.animating = true;
    game.pending_animations = 0;
    render_scores();
    for(int row = 0; row < GAME_SIZE; ++row) {
        for(int column = 0; column < GAME_SIZE; ++column) {
            style_tile(game.cells[row][column], game.cell_labels[row][column], 0);
        }
    }

    for(int row = 0; row < GAME_SIZE; ++row) {
        for(int column = 0; column < GAME_SIZE; ++column) {
            const uint32_t value = before.cells[row][column];
            if(value == 0) continue;
            lv_obj_t *tile = create_motion_tile(row, column, value);
            const cell_position_t target = targets[row][column];
            if(target.row == row && target.column == column) continue;
            const bool horizontal = target.row == row;
            const int32_t start = GRID_GAP +
                (horizontal ? column : row) * (CELL_SIZE + GRID_GAP);
            const int32_t destination = GRID_GAP +
                (horizontal ? target.column : target.row) * (CELL_SIZE + GRID_GAP);
            animate_tile(tile, start, destination, horizontal);
        }
    }

    if(game.pending_animations == 0) finish_move();
    return true;
}

void game_2048_new_game(void)
{
    clear_motion_layer();
    game.animating = false;
    game.pending_animations = 0;
    game_2048_model_reset(&game.model);
    spawn_tile();
    spawn_tile();
    render_board();
}

const game_2048_model_t *game_2048_get_model(void)
{
    return &game.model;
}

static void new_game_clicked(lv_event_t *event)
{
    LV_UNUSED(event);
    game_2048_new_game();
}

static lv_obj_t *create_score_box(lv_obj_t *parent, int32_t x, const char *title, lv_obj_t **value_label)
{
    lv_obj_t *box = lv_obj_create(parent);
    style_plain(box);
    lv_obj_set_size(box, 72, 52);
    lv_obj_set_pos(box, x, 42);
    lv_obj_set_style_radius(box, 5, 0);
    lv_obj_set_style_bg_color(box, COLOR_SCORE_BOX, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);

    lv_obj_t *title_label = lv_label_create(box);
    lv_obj_set_style_text_font(title_label, &esp_brookesia_font_maison_neue_book_16, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(0xEEE4DA), 0);
    lv_label_set_text(title_label, title);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 4);

    *value_label = lv_label_create(box);
    lv_obj_set_style_text_font(*value_label, &esp_brookesia_font_maison_neue_book_18, 0);
    lv_obj_set_style_text_color(*value_label, COLOR_LIGHT_TEXT, 0);
    lv_obj_align(*value_label, LV_ALIGN_BOTTOM_MID, 0, -4);
    return box;
}

void game_2048_create(lv_obj_t *parent)
{
    if(parent == NULL || game.root != NULL) return;
    memset(&game, 0, sizeof(game));
#if defined(ESP_PLATFORM)
    random_state = esp_random();
#else
    random_state = UINT32_C(0x2048C0DE);
#endif
    if(random_state == 0) random_state = UINT32_C(0x2048C0DE);

    game.root = lv_obj_create(parent);
    style_plain(game.root);
    lv_obj_set_size(game.root, 360, 360);
    lv_obj_set_pos(game.root, 0, 0);
    lv_obj_set_style_bg_color(game.root, COLOR_SCREEN, 0);
    lv_obj_set_style_bg_opa(game.root, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(game.root);
    lv_obj_set_style_text_font(title, &esp_brookesia_font_maison_neue_book_24, 0);
    lv_obj_set_style_text_color(title, COLOR_DARK_TEXT, 0);
    lv_label_set_text(title, "2048");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 3);

    create_score_box(game.root, 70, "SCORE", &game.score_label);
    create_score_box(game.root, 218, "BEST", &game.best_label);

    lv_obj_t *new_button = lv_button_create(game.root);
    style_plain(new_button);
    lv_obj_set_size(new_button, 72, 52);
    lv_obj_set_pos(new_button, 144, 42);
    lv_obj_set_style_radius(new_button, 5, 0);
    lv_obj_set_style_bg_color(new_button, COLOR_SCORE_BOX, 0);
    lv_obj_set_style_bg_color(new_button, lv_color_hex(0x9F8A76), LV_STATE_PRESSED);
    lv_obj_add_event_cb(new_button, new_game_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *new_label = lv_label_create(new_button);
    lv_obj_set_style_text_font(new_label, &esp_brookesia_font_maison_neue_book_16, 0);
    lv_obj_set_style_text_color(new_label, COLOR_LIGHT_TEXT, 0);
    lv_obj_set_style_text_align(new_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(new_label, "New\nGame");
    lv_obj_center(new_label);

    game.board = lv_obj_create(game.root);
    style_plain(game.board);
    lv_obj_set_size(game.board, BOARD_SIZE, BOARD_SIZE);
    lv_obj_set_pos(game.board, BOARD_X, BOARD_Y);
    lv_obj_set_style_radius(game.board, 6, 0);
    lv_obj_set_style_bg_color(game.board, COLOR_BOARD, 0);
    lv_obj_set_style_bg_opa(game.board, LV_OPA_COVER, 0);

    for(int row = 0; row < GAME_SIZE; ++row) {
        for(int column = 0; column < GAME_SIZE; ++column) {
            lv_obj_t *cell = lv_obj_create(game.board);
            style_plain(cell);
            lv_obj_set_size(cell, CELL_SIZE, CELL_SIZE);
            lv_obj_set_pos(cell, GRID_GAP + column * (CELL_SIZE + GRID_GAP),
                           GRID_GAP + row * (CELL_SIZE + GRID_GAP));
            lv_obj_t *label = lv_label_create(cell);
            game.cells[row][column] = cell;
            game.cell_labels[row][column] = label;
            style_tile(cell, label, 0);
        }
    }

    game.motion_layer = lv_obj_create(game.board);
    style_plain(game.motion_layer);
    lv_obj_set_size(game.motion_layer, BOARD_SIZE, BOARD_SIZE);
    lv_obj_set_pos(game.motion_layer, 0, 0);
    lv_obj_set_style_bg_opa(game.motion_layer, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(game.motion_layer, LV_OBJ_FLAG_CLICKABLE);

    game.game_over_panel = lv_obj_create(game.board);
    style_plain(game.game_over_panel);
    lv_obj_set_size(game.game_over_panel, 180, 104);
    lv_obj_center(game.game_over_panel);
    lv_obj_set_style_radius(game.game_over_panel, 8, 0);
    lv_obj_set_style_bg_color(game.game_over_panel, COLOR_SCORE_BOX, 0);
    lv_obj_set_style_bg_opa(game.game_over_panel, LV_OPA_90, 0);
    lv_obj_t *game_over_label = lv_label_create(game.game_over_panel);
    lv_obj_set_style_text_font(game_over_label, &esp_brookesia_font_maison_neue_book_20, 0);
    lv_obj_set_style_text_color(game_over_label, COLOR_LIGHT_TEXT, 0);
    lv_label_set_text(game_over_label, "Game Over");
    lv_obj_align(game_over_label, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_t *restart_button = lv_button_create(game.game_over_panel);
    lv_obj_set_size(restart_button, 116, 38);
    lv_obj_align(restart_button, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_color(restart_button, lv_color_hex(0xEDC22E), 0);
    lv_obj_add_event_cb(restart_button, new_game_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *restart_label = lv_label_create(restart_button);
    lv_obj_set_style_text_font(restart_label, &esp_brookesia_font_maison_neue_book_16, 0);
    lv_obj_set_style_text_color(restart_label, COLOR_LIGHT_TEXT, 0);
    lv_label_set_text(restart_label, "New Game");
    lv_obj_center(restart_label);
    lv_obj_add_flag(game.game_over_panel, LV_OBJ_FLAG_HIDDEN);

    game_2048_new_game();
}
