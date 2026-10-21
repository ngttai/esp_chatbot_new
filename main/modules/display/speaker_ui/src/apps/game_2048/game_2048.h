#ifndef GAME_2048_H
#define GAME_2048_H

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

typedef enum {
    GAME_2048_DIRECTION_LEFT,
    GAME_2048_DIRECTION_RIGHT,
    GAME_2048_DIRECTION_UP,
    GAME_2048_DIRECTION_DOWN,
} game_2048_direction_t;

typedef struct {
    uint32_t cells[4][4];
    uint32_t score;
    uint32_t best_score;
} game_2048_model_t;

void game_2048_model_reset(game_2048_model_t *model);
bool game_2048_model_move(game_2048_model_t *model, game_2048_direction_t direction);
bool game_2048_model_is_game_over(const game_2048_model_t *model);

void game_2048_create(lv_obj_t *parent);
bool game_2048_handle_swipe(game_2048_direction_t direction);
void game_2048_new_game(void);
const game_2048_model_t *game_2048_get_model(void);

#endif
