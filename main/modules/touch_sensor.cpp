/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "touch_sensor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <inttypes.h>

#include "esp_log.h"
#include "soc/soc_caps.h"
#include "touch_sensor_lowlevel.h"
#include "modules/ai_agents.hpp"
#include "modules/head_led.hpp"

namespace {

constexpr char TAG[] = "touch_sensor";
constexpr uint32_t VOCAT_V1_0_TOUCH_GPIO = 7;
constexpr float TOUCH_THRESHOLD = 0.05F;
constexpr uint32_t POLL_INTERVAL_MS = 20;
constexpr uint32_t DEBOUNCE_MS = 60;
constexpr uint32_t BASELINE_SAMPLES = 25;
constexpr uint32_t ENABLE_SETTLE_SAMPLES = 10;
constexpr uint32_t SHORT_PRESS_TIME_MS = 245;
constexpr uint32_t LONG_PRESS_TIME_MS = 1500;

} // namespace

bool TouchSensor::init()
{
    if (task_ != nullptr) {
        return true;
    }

    uint32_t channel_list[] = {VOCAT_V1_0_TOUCH_GPIO};
    touch_lowlevel_type_t channel_type[] = {TOUCH_LOWLEVEL_TYPE_TOUCH};
    touch_lowlevel_config_t lowlevel_config = {
        .channel_num = 1,
        .channel_list = channel_list,
        .channel_type = channel_type,
        .sample_period_ms = POLL_INTERVAL_MS,
        .sample_cfg_num = 0,
#if SOC_TOUCH_SUPPORT_PROX_SENSING
        .proximity_count = 0,
#endif
    };
    esp_err_t ret = touch_sensor_lowlevel_create(&lowlevel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create touch sensor low level: %s", esp_err_to_name(ret));
        return false;
    }

    ret = touch_sensor_lowlevel_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start touch sensor low level: %s", esp_err_to_name(ret));
        touch_sensor_lowlevel_delete();
        return false;
    }

    if (xTaskCreate(task_entry, "touch_sensor", 4096, this, 5, &task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create touch sensor task");
        touch_sensor_lowlevel_stop();
        touch_sensor_lowlevel_delete();
        return false;
    }

    enabled_.store(true);

    ESP_LOGI(TAG, "VoCat v1.0 touch sensor started on GPIO%u", VOCAT_V1_0_TOUCH_GPIO);
    return true;
}

bool TouchSensor::set_enabled(bool enabled)
{
    if (task_ == nullptr) {
        return false;
    }
    enabled_.store(enabled);
    if (!enabled) {
        HeadLed::get_instance().set_touch_pressed(false);
    }
    ESP_LOGI(TAG, "Touch sensor %s", enabled ? "enabled" : "disabled");
    return true;
}

void TouchSensor::task_entry(void *arg)
{
    static_cast<TouchSensor *>(arg)->run();
}

uint32_t TouchSensor::read_filtered_value() const
{
    std::array<uint32_t, SOC_TOUCH_SAMPLE_CFG_NUM> values{};
    if (touch_sensor_lowlevel_get_data(VOCAT_V1_0_TOUCH_GPIO, values.data()) != ESP_OK) {
        return 0;
    }
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

void TouchSensor::run()
{
    uint64_t baseline_sum = 0;
    uint32_t valid_samples = 0;
    while (valid_samples < BASELINE_SAMPLES) {
        const uint32_t value = read_filtered_value();
        if (value != 0) {
            baseline_sum += value;
            ++valid_samples;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }

    uint32_t baseline = static_cast<uint32_t>(baseline_sum / BASELINE_SAMPLES);
    bool pressed = false;
    bool long_press_sent = false;
    bool was_enabled = enabled_.load();
    uint32_t settle_samples = 0;
    TickType_t candidate_started = 0;
    TickType_t press_started = 0;
    TickType_t click_deadline = 0;
    uint8_t click_count = 0;

    while (true) {
        const uint32_t value = read_filtered_value();
        if (value == 0) {
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }

        const bool enabled = enabled_.load();
        if (!enabled) {
            if (pressed) {
                HeadLed::get_instance().set_touch_pressed(false);
            }
            pressed = false;
            long_press_sent = false;
            candidate_started = 0;
            press_started = 0;
            click_deadline = 0;
            click_count = 0;
            settle_samples = 0;
            was_enabled = false;
            // Follow environmental drift while the Home touch action is disabled.
            baseline = static_cast<uint32_t>((static_cast<uint64_t>(baseline) * 7U + value) / 8U);
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }

        if (!was_enabled) {
            was_enabled = true;
            settle_samples = ENABLE_SETTLE_SAMPLES;
            baseline = value;
            ESP_LOGI(TAG, "Recalibrating touch baseline after entering Home");
        }
        if (settle_samples > 0) {
            baseline = static_cast<uint32_t>((static_cast<uint64_t>(baseline) * 3U + value) / 4U);
            --settle_samples;
            vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
            continue;
        }

        const uint32_t threshold = static_cast<uint32_t>(baseline * TOUCH_THRESHOLD);
        // ESP32-S3 touch data rises while the pad is touched. Using an absolute
        // delta can mistake release or downward environmental drift for a press
        // and leave the state permanently latched.
        const bool touching = value > (baseline + threshold);
        const TickType_t now = xTaskGetTickCount();

        if ((click_deadline != 0) && (now >= click_deadline) && !pressed) {
            if (click_count == 1) {
                AI_Agents::get_instance().handle_touch_sensor_click();
            }
            click_deadline = 0;
            click_count = 0;
        }

        if (touching) {
            if (candidate_started == 0) {
                candidate_started = now;
            }
            if (!pressed && (now - candidate_started) >= pdMS_TO_TICKS(DEBOUNCE_MS)) {
                pressed = true;
                long_press_sent = false;
                press_started = now;
                ESP_LOGI(TAG, "Touch press detected (value=%" PRIu32 ", baseline=%" PRIu32 ")", value, baseline);
                HeadLed::get_instance().set_touch_pressed(true);
            } else if (pressed && !long_press_sent &&
                       ((now - press_started) >= pdMS_TO_TICKS(LONG_PRESS_TIME_MS))) {
                long_press_sent = true;
                click_deadline = 0;
                click_count = 0;
                ESP_LOGI(TAG, "Touch long press detected");
                AI_Agents::get_instance().handle_touch_sensor_long_press();
            }
        } else {
            candidate_started = 0;
            if (pressed) {
                HeadLed::get_instance().set_touch_pressed(false);
                pressed = false;
                if (!long_press_sent) {
                    ++click_count;
                    click_deadline = now + pdMS_TO_TICKS(SHORT_PRESS_TIME_MS);
                }
                long_press_sent = false;
                press_started = 0;
            }
            // Track slow environmental drift only while the pad is untouched.
            baseline = static_cast<uint32_t>((static_cast<uint64_t>(baseline) * 99U + value) / 100U);
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
