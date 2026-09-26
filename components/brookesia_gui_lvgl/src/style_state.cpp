/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/style_impl.hpp"

namespace esp_brookesia::gui::lvgl::style_detail {



Record::StyleExtrasPayload &ensure_style_extras_payload(Record &record)
{
    if (record.style_extras_payload == nullptr) {
        record.style_extras_payload = std::make_unique<Record::StyleExtrasPayload>();
    }
    return *record.style_extras_payload;
}

void reset_state_styles(Record &record)
{
    auto *style_extras = record.style_extras_payload.get();
    if (style_extras == nullptr) {
        return;
    }

    for (auto &[unused_state, state_style] : style_extras->state_styles) {
        (void)unused_state;
        if (record.object != nullptr) {
            lv_obj_remove_style(record.object, &state_style.style, state_style.selector);
        }
        lv_style_reset(&state_style.style);
    }
    style_extras->state_styles.clear();
}

void reset_part_styles(Record &record)
{
    auto *style_extras = record.style_extras_payload.get();
    if (style_extras == nullptr) {
        return;
    }

    for (auto &[unused_part, part_style] : style_extras->part_styles) {
        (void)unused_part;
        if (record.object != nullptr) {
            lv_obj_remove_style(record.object, &part_style.style, part_style.selector);
        }
        lv_style_reset(&part_style.style);
        for (auto &[unused_state, state_style] : part_style.state_styles) {
            (void)unused_state;
            if (record.object != nullptr) {
                lv_obj_remove_style(record.object, &state_style.style, state_style.selector);
            }
            lv_style_reset(&state_style.style);
        }
    }
    style_extras->part_styles.clear();
    style_extras->arc_gradients.clear();
}

void apply_state_styles(BackendImpl &impl, Record &record, const ResolvedStyle &style)
{
    reset_state_styles(record);
    for (const auto &[state_name, state_style] : style.state_styles) {
        auto selector = style_state_selector(state_name);
        if (!selector.has_value()) {
            BROOKESIA_LOGW(
                "Skipping unsupported style state '%1%' for node '%2%'",
                state_name,
                impl.build_absolute_path(record)
            );
            continue;
        }
        auto &entry = ensure_style_extras_payload(record).state_styles[state_name];
        entry.selector = *selector;
        lv_style_init(&entry.style);
        apply_state_style_fields(entry.style, state_style);
        lv_obj_add_style(record.object, &entry.style, entry.selector);
    }
}

void apply_part_styles(BackendImpl &impl, Record &record, const ResolvedStyle &style)
{
    reset_part_styles(record);
    for (const auto &[part_name, part_style] : style.part_styles) {
        auto part_selector = style_part_selector(part_name);
        if (!part_selector.has_value() || *part_selector == LV_PART_MAIN) {
            BROOKESIA_LOGW(
                "Skipping unsupported style part '%1%' for node '%2%'",
                part_name,
                impl.build_absolute_path(record)
            );
            continue;
        }

        auto &entry = ensure_style_extras_payload(record).part_styles[part_name];
        entry.selector = *part_selector;
        lv_style_init(&entry.style);
        apply_state_style_fields(entry.style, part_style.style);
        lv_obj_add_style(record.object, &entry.style, entry.selector);

        for (const auto &[state_name, state_style] : part_style.state_styles) {
            auto selector = style_state_selector(state_name, *part_selector);
            if (!selector.has_value()) {
                BROOKESIA_LOGW(
                    "Skipping unsupported style state '%1%' for part '%2%' on node '%3%'",
                    state_name,
                    part_name,
                    impl.build_absolute_path(record)
                );
                continue;
            }
            auto &state_entry = entry.state_styles[state_name];
            state_entry.selector = *selector;
            lv_style_init(&state_entry.style);
            apply_state_style_fields(state_entry.style, state_style);
            lv_obj_add_style(record.object, &state_entry.style, state_entry.selector);
        }
    }
}

lv_color_t mix_color(uint32_t start_color, uint32_t end_color, int32_t position, int32_t max_position)
{
    if (max_position <= 0) {
        return lv_color_hex(start_color);
    }
    const auto mix_channel = [position, max_position](uint32_t start, uint32_t end) -> uint32_t {
        return (start * static_cast<uint32_t>(max_position - position) +
                end * static_cast<uint32_t>(position)) /
        static_cast<uint32_t>(max_position);
    };
    const auto sr = (start_color >> 16) & 0xff;
    const auto sg = (start_color >> 8) & 0xff;
    const auto sb = start_color & 0xff;
    const auto er = (end_color >> 16) & 0xff;
    const auto eg = (end_color >> 8) & 0xff;
    const auto eb = end_color & 0xff;
    return lv_color_hex((mix_channel(sr, er) << 16) | (mix_channel(sg, eg) << 8) | mix_channel(sb, eb));
}
}
