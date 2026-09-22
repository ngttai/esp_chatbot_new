#include "keyboard_adapter.hpp"

#include <cstring>

#include "lvgl.h"

namespace host_sim {
namespace {

#define WLAN_KB_BTN(width) \
    ((lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_POPOVER | (width)))
#define WLAN_KB_PHR(width) ((lv_buttonmatrix_ctrl_t)(width))

constexpr const char *WLAN_KB_PHR_STR = "  ";
constexpr const char *WLAN_KB_SPACE_STR = "Space";
constexpr const char *WLAN_KB_LOWER_STR = "abc";
constexpr const char *WLAN_KB_NUMBER_STR = "123";
constexpr const char *WLAN_KB_SPEC_STR = ",.?!";

lv_obj_t *keyboard = nullptr;

const char *const special_map[] = {
    "+", "|", "\\", "\"", "<", ">", "{", "}", "[", "]", "\n",
    WLAN_KB_PHR_STR, "~", "@", "#", "!", "%", "&", "*", "(", ")", WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_LOWER_STR, "'", "/", "-", "_", ":", ";", "?",
    WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, WLAN_KB_NUMBER_STR, ",", ".", WLAN_KB_SPACE_STR,
    LV_SYMBOL_BACKSPACE, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, LV_SYMBOL_LEFT, LV_SYMBOL_OK, LV_SYMBOL_RIGHT, WLAN_KB_PHR_STR, ""
};

const lv_buttonmatrix_ctrl_t special_ctrl[] = {
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_PHR(1), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(1),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2),
    WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(3), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(5),
    WLAN_KB_BTN(4), WLAN_KB_PHR(2),
    WLAN_KB_PHR(3), WLAN_KB_BTN(4), WLAN_KB_BTN(6), WLAN_KB_BTN(4), WLAN_KB_PHR(3)
};

const char *const number_map[] = {
    WLAN_KB_PHR_STR, "1", "2", "3", LV_SYMBOL_BACKSPACE, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, "4", "5", "6", WLAN_KB_LOWER_STR, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, "7", "8", "9", WLAN_KB_SPEC_STR, WLAN_KB_PHR_STR, "\n",
    WLAN_KB_PHR_STR, LV_SYMBOL_LEFT, "0", LV_SYMBOL_RIGHT, LV_SYMBOL_OK, WLAN_KB_PHR_STR, ""
};

const lv_buttonmatrix_ctrl_t number_ctrl[] = {
    WLAN_KB_PHR(1), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(3),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2),
    WLAN_KB_PHR(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_BTN(2), WLAN_KB_PHR(2)
};

lv_obj_t *find_keyboard(lv_obj_t *object)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_keyboard_class)) return object;
    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *match = find_keyboard(lv_obj_get_child(object, index))) return match;
    }
    return nullptr;
}

bool map_contains(const char *const *map, const char *text)
{
    if (map == nullptr) return false;
    for (size_t index = 0; map[index][0] != '\0'; ++index) {
        if (std::strcmp(map[index], text) == 0) return true;
    }
    return false;
}

bool active_map_contains(lv_keyboard_mode_t mode, const char *text)
{
    lv_keyboard_set_mode(keyboard, mode);
    return map_contains(lv_keyboard_get_map_array(keyboard), text);
}

} // namespace

bool apply_keyboard_layout_tweaks()
{
    keyboard = find_keyboard(lv_layer_top());
    if (keyboard == nullptr) return false;
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL, special_map, special_ctrl);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_NUMBER, number_map, number_ctrl);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    return true;
}

bool keyboard_layout_tweaks_are_active()
{
    if (keyboard == nullptr) return false;
    const lv_keyboard_mode_t original_mode = lv_keyboard_get_mode(keyboard);
    const bool special_is_valid = active_map_contains(LV_KEYBOARD_MODE_SPECIAL, ",") &&
                                  active_map_contains(LV_KEYBOARD_MODE_SPECIAL, ".") &&
                                  !active_map_contains(LV_KEYBOARD_MODE_SPECIAL, LV_SYMBOL_KEYBOARD);
    const bool number_is_valid = !active_map_contains(LV_KEYBOARD_MODE_NUMBER, LV_SYMBOL_KEYBOARD);
    const bool text_modes_unchanged = active_map_contains(LV_KEYBOARD_MODE_TEXT_LOWER,
                                                          LV_SYMBOL_KEYBOARD) &&
                                      active_map_contains(LV_KEYBOARD_MODE_TEXT_UPPER,
                                                          LV_SYMBOL_KEYBOARD);
    lv_keyboard_set_mode(keyboard, original_mode);
    return special_is_valid && number_is_valid && text_modes_unchanged;
}

} // namespace host_sim
