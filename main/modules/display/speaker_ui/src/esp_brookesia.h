#pragma once

/* Minimal desktop shim for repository-generated SquareLine UI sources. */
#include "lvgl.h"

LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_12)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_14)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_16)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_18)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_20)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_22)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_24)
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_26)

lv_event_code_t ui_comp_get_event_code(void);
#define LV_EVENT_GET_COMP_CHILD ui_comp_get_event_code()
void esp_brookesia_squareline_ui_comp_init(void);
lv_obj_t *ui_comp_get_child(lv_obj_t *comp, uint32_t child_idx);

void get_component_child_event_cb(lv_event_t *e);
void del_component_child_event_cb(lv_event_t *e);
void _ui_arc_set_text_value(lv_obj_t *target, lv_obj_t *source,
                            const char *prefix, const char *postfix);
