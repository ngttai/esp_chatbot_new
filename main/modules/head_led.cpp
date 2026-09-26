/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "head_led.hpp"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "led_indicator_ledc.h"

namespace {

constexpr char TAG[] = "head_led";
constexpr gpio_num_t HEAD_LED_GPIO = GPIO_NUM_43;

enum BlinkType {
    BLINK_TOUCH_PRESS_DOWN = 0,
    BLINK_WIFI_CONNECTED,
    BLINK_WIFI_DISCONNECTED,
    BLINK_DEVELOPER_MODE,
    BLINK_MAX,
};

const blink_step_t TOUCH_PRESS_DOWN_PATTERN[] = {
    {LED_BLINK_BRIGHTNESS, LED_STATE_25_PERCENT, 200},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t WIFI_CONNECTED_PATTERN[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 1000},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t WIFI_DISCONNECTED_PATTERN[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 100},
    {LED_BLINK_HOLD, LED_STATE_OFF, 200},
    {LED_BLINK_LOOP, 0, 0},
};

const blink_step_t DEVELOPER_MODE_PATTERN[] = {
    {LED_BLINK_BREATHE, LED_STATE_ON, 1000},
    {LED_BLINK_BRIGHTNESS, LED_STATE_ON, 500},
    {LED_BLINK_BREATHE, LED_STATE_OFF, 1000},
    {LED_BLINK_BRIGHTNESS, LED_STATE_OFF, 500},
    {LED_BLINK_LOOP, 0, 0},
};

blink_step_t const *BLINK_LISTS[] = {
    [BLINK_TOUCH_PRESS_DOWN] = TOUCH_PRESS_DOWN_PATTERN,
    [BLINK_WIFI_CONNECTED] = WIFI_CONNECTED_PATTERN,
    [BLINK_WIFI_DISCONNECTED] = WIFI_DISCONNECTED_PATTERN,
    [BLINK_DEVELOPER_MODE] = DEVELOPER_MODE_PATTERN,
    [BLINK_MAX] = nullptr,
};

void wifi_event_handler(void *, esp_event_base_t event_base, int32_t event_id, void *)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        HeadLed::get_instance().set_wifi_connected(false);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        HeadLed::get_instance().set_wifi_connected(true);
    }
}

} // namespace

bool HeadLed::init()
{
    if (initialized_.load()) {
        return true;
    }

    led_indicator_ledc_config_t ledc_config = {
        .is_active_level_high = false,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_1,
        .gpio_num = HEAD_LED_GPIO,
        .channel = LEDC_CHANNEL_2,
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 4, 0)
        .sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD,
#endif
    };
    const led_indicator_config_t config = {
        .blink_lists = BLINK_LISTS,
        .blink_list_num = BLINK_MAX,
    };

    esp_err_t ret = led_indicator_new_ledc_device(&config, &ledc_config, &handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create GPIO43 LED indicator: %s", esp_err_to_name(ret));
        return false;
    }

    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register Wi-Fi LED event: %s", esp_err_to_name(ret));
        return false;
    }
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, nullptr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP LED event: %s", esp_err_to_name(ret));
        return false;
    }

    initialized_.store(true);
    set_wifi_connected(false);
    ESP_LOGI(TAG, "VoCat head LED started on GPIO%d", HEAD_LED_GPIO);
    return true;
}

void HeadLed::set_touch_pressed(bool pressed)
{
    if (!initialized_.load()) {
        return;
    }
    if (pressed) {
        led_indicator_start(handle_, BLINK_TOUCH_PRESS_DOWN);
    } else {
        led_indicator_stop(handle_, BLINK_TOUCH_PRESS_DOWN);
    }
}

void HeadLed::set_wifi_connected(bool connected)
{
    if (!initialized_.load()) {
        return;
    }
    led_indicator_stop(handle_, connected ? BLINK_WIFI_DISCONNECTED : BLINK_WIFI_CONNECTED);
    led_indicator_start(handle_, connected ? BLINK_WIFI_CONNECTED : BLINK_WIFI_DISCONNECTED);
    ESP_LOGI(TAG, "Wi-Fi LED state: %s", connected ? "connected" : "disconnected");
}

void HeadLed::set_developer_mode()
{
    if (!initialized_.load()) {
        return;
    }
    led_indicator_stop(handle_, BLINK_WIFI_CONNECTED);
    led_indicator_stop(handle_, BLINK_WIFI_DISCONNECTED);
    led_indicator_start(handle_, BLINK_DEVELOPER_MODE);
    ESP_LOGI(TAG, "Developer-mode LED pattern started");
}
