#pragma once

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/types.hpp"
#include "brookesia/gui_lvgl/macro_configs.h"
#if !BROOKESIA_GUI_LVGL_STYLE_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#if LV_USE_FREETYPE
#   if __has_include("lvgl/font/lv_freetype.h")
#       include "lvgl/font/lv_freetype.h"
#   elif __has_include("font/lv_freetype.h")
#       include "font/lv_freetype.h"
#   elif __has_include("src/libs/freetype/lv_freetype.h")
#       include "src/libs/freetype/lv_freetype.h"
#   elif __has_include("lvgl/src/libs/freetype/lv_freetype.h")
#       include "lvgl/src/libs/freetype/lv_freetype.h"
#   else
#       error "LVGL FreeType header not found"
#   endif
#   if defined(LV_USE_FS_MEMFS) && LV_USE_FS_MEMFS && defined(LV_FREETYPE_USE_LVGL_PORT) && LV_FREETYPE_USE_LVGL_PORT
#       define BROOKESIA_GUI_LVGL_USE_FREETYPE_MEMFS_PORT (1)
#   else
#       define BROOKESIA_GUI_LVGL_USE_FREETYPE_MEMFS_PORT (0)
#   endif
#   if defined(ESP_PLATFORM) && (!defined(CONFIG_SPIRAM_XIP_FROM_PSRAM) || !CONFIG_SPIRAM_XIP_FROM_PSRAM) && \
        !BROOKESIA_GUI_LVGL_USE_FREETYPE_MEMFS_PORT
#       error "Enable LV_USE_FS_MEMFS and LV_FREETYPE_USE_LVGL_PORT when CONFIG_SPIRAM_XIP_FROM_PSRAM is disabled"
#   endif
#endif

#if LV_USE_IMGFONT
#   if __has_include("lvgl/font/lv_imgfont.h")
#       include "lvgl/font/lv_imgfont.h"
#       define BROOKESIA_GUI_LVGL_HAS_IMGFONT (1)
#   elif __has_include("font/lv_imgfont.h")
#       include "font/lv_imgfont.h"
#       define BROOKESIA_GUI_LVGL_HAS_IMGFONT (1)
#   elif __has_include("src/font/imgfont/lv_imgfont.h")
#       include "src/font/imgfont/lv_imgfont.h"
#       define BROOKESIA_GUI_LVGL_HAS_IMGFONT (1)
#   elif __has_include("lvgl/src/font/imgfont/lv_imgfont.h")
#       include "lvgl/src/font/imgfont/lv_imgfont.h"
#       define BROOKESIA_GUI_LVGL_HAS_IMGFONT (1)
#   else
#       define BROOKESIA_GUI_LVGL_HAS_IMGFONT (0)
#   endif
#else
#   define BROOKESIA_GUI_LVGL_HAS_IMGFONT (0)
#endif

#include "brookesia/service_helper/system/storage.hpp"

namespace esp_brookesia::gui::lvgl {
std::optional<uint32_t> style_part_selector(std::string_view part);
std::optional<uint32_t> style_state_selector(std::string_view state, uint32_t part = LV_PART_MAIN);
}

namespace esp_brookesia::gui::lvgl {

namespace style_detail {

using StorageHelper = service::helper::Storage;

constexpr lv_opa_t DEBUG_OUTLINE_OPACITY = LV_OPA_COVER;
constexpr int32_t DEBUG_OUTLINE_WIDTH = 2;
constexpr int32_t DEBUG_OUTLINE_PAD = 2;

lv_color_t get_debug_outline_color(uint32_t depth)
;

#if LV_USE_FREETYPE && !BROOKESIA_GUI_LVGL_HAS_ESP_FONT_BACKEND
bool file_exists(std::string_view path)
;
#endif

#if LV_USE_FREETYPE && BROOKESIA_GUI_LVGL_USE_FREETYPE_MEMFS_PORT
std::expected<std::shared_ptr<FontCacheEntry::FontSource>, std::string> load_font_source_from_storage(
    BackendImpl &impl, std::string_view font_path)
;
#endif

void log_font_warning_once(std::string_view message)
;

void log_font_info_once(std::string_view message)
;

const lv_font_t *get_builtin_font(int32_t font_size)
;

bool node_type_uses_text_font(NodeType type)
;

const NativeFontVariant *select_native_font_variant(const ResolvedFontSpec &font_spec, int32_t font_size)
;

std::vector<ImageFontSize> normalized_image_font_sizes(const ResolvedFontSpec &font_spec)
;

std::optional<ImageFontSize> select_image_font_size(const ResolvedFontSpec &font_spec, int32_t requested_size)
;

std::string make_font_cache_key(const ResolvedFontSpec &font_spec, int32_t font_size, int32_t image_font_size = 0)
;

#if LV_USE_FREETYPE
FontCacheEntry *create_font_cache_entry(BackendImpl &impl, const ResolvedFontSpec &font_spec, int32_t font_size)
;
#endif

#if BROOKESIA_GUI_LVGL_HAS_IMGFONT
const void *image_font_get_source_cb(
    const lv_font_t *font,
    uint32_t unicode,
    uint32_t unicode_next,
    int32_t *offset_y,
    void *user_data)
;

FontCacheEntry *create_image_font_cache_entry(
    BackendImpl &impl,
    const ResolvedFontSpec &font_spec,
    int32_t font_size,
    int32_t image_font_size)
;
#endif


std::optional<lv_grad_dir_t> parse_gradient_direction(std::string_view direction);

void apply_style_colors(Record &record, const ResolvedStyle &style)
;

void apply_style_border(Record &record, const ResolvedStyle &style)
;

void apply_style_radius(Record &record, const ResolvedStyle &style)
;

void apply_style_padding(Record &record, const ResolvedStyle &style)
;

void apply_style_margin(Record &record, const ResolvedStyle &style)
;

void apply_style_shadow(Record &record, const ResolvedStyle &style)
;

void apply_style_opacity(Record &record, const ResolvedStyle &style)
;

void apply_style_line(Record &record, const ResolvedStyle &style)
;

void apply_style_image_opacity(Record &record, const ResolvedStyle &style)
;

void apply_style_image_recolor(Record &record, const ResolvedStyle &style)
;

void apply_style_font(BackendImpl &impl, Record &record, const ResolvedStyle &style)
;

void apply_optional_color(lv_style_t &lv_style, const std::optional<std::string> &color, lv_style_prop_t prop)
;

std::optional<lv_grad_dir_t> parse_gradient_direction(std::string_view direction)
;

void apply_state_style_fields(lv_style_t &lv_style, const Style &style)
;

Record::StyleExtrasPayload &ensure_style_extras_payload(Record &record)
;

void reset_state_styles(Record &record)
;

void reset_part_styles(Record &record)
;

void apply_state_styles(BackendImpl &impl, Record &record, const ResolvedStyle &style)
;

void apply_part_styles(BackendImpl &impl, Record &record, const ResolvedStyle &style)
;

lv_color_t mix_color(uint32_t start_color, uint32_t end_color, int32_t position, int32_t max_position)
;

void draw_arc_gradient_segmented(
    const lv_draw_arc_dsc_t &source,
    const Record::ArcGradientRecord &gradient)
;

void on_arc_gradient_draw_task_added(lv_event_t *event)
;

std::optional<Record::ArcGradientRecord> make_arc_gradient_record(const Style &style)
;

void apply_arc_gradient(BackendImpl &impl, Record &record, const ResolvedStyle &style)
;


} // namespace style_detail

} // namespace esp_brookesia::gui::lvgl
