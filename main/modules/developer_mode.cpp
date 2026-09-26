/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "developer_mode.hpp"

#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "soc/usb_serial_jtag_reg.h"
#include "hal/usb_serial_jtag_ll.h"
#endif
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_lv_adapter.h"
#include "lvgl.h"
#include "modules/head_led.hpp"
#include "modules/usb_msc.h"

extern "C" {
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_16);
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_18);
LV_FONT_DECLARE(esp_brookesia_font_maison_neue_book_26);
}

namespace {

constexpr char TAG[] = "developer_mode";
constexpr int DEVELOPER_MODE_KEY = 0x655;
RTC_NOINIT_ATTR int developer_mode_key;

void restore_usb_serial_jtag_phy()
{
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLDOWN);
    vTaskDelay(pdMS_TO_TICKS(10));
#if USB_SERIAL_JTAG_LL_EXT_PHY_SUPPORTED
    usb_serial_jtag_ll_phy_enable_external(false);
    usb_serial_jtag_ll_phy_enable_pad(true);
#else
    usb_serial_jtag_ll_phy_set_defaults();
#endif
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLDOWN);
    SET_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_DP_PULLUP);
    CLEAR_PERI_REG_MASK(USB_SERIAL_JTAG_CONF0_REG, USB_SERIAL_JTAG_PAD_PULL_OVERRIDE);
#endif
}

} // namespace

bool DeveloperMode::is_requested()
{
    return developer_mode_key == DEVELOPER_MODE_KEY;
}

void DeveloperMode::request_and_restart()
{
    ESP_LOGW(TAG, "Entering Developer Mode");
    developer_mode_key = DEVELOPER_MODE_KEY;
    esp_restart();
}

void DeveloperMode::exit_clicked(void *event)
{
    (void)event;
    clear_and_restart();
}

void DeveloperMode::clear_and_restart()
{
    ESP_LOGI(TAG, "Exit Developer Mode");
    developer_mode_key = 0;
    restore_usb_serial_jtag_phy();
    esp_restart();
}

bool DeveloperMode::start()
{
    esp_lv_adapter_lock(-1);

    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_white(), 0);

    lv_obj_t *title_label = lv_label_create(screen);
    lv_obj_set_size(title_label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(title_label, &esp_brookesia_font_maison_neue_book_26, 0);
    lv_label_set_text(title_label, "Developer Mode");
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 60);

    lv_obj_t *content_label = lv_label_create(screen);
    lv_obj_set_size(content_label, LV_PCT(80), LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(content_label, &esp_brookesia_font_maison_neue_book_18, 0);
    lv_obj_set_style_text_align(content_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(content_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(
        content_label,
        "Please connect the device to your computer via USB. A USB drive will appear. "
        "You can create or modify the files in the SD card (like `bot_setting.json` and `private_key.pem`) as needed."
    );
    lv_obj_align_to(content_label, title_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);

    lv_obj_t *exit_button = lv_button_create(screen);
    lv_obj_set_size(exit_button, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(exit_button, LV_ALIGN_BOTTOM_MID, 0, -60);
    lv_obj_add_event_cb(
        exit_button,
        [](lv_event_t *event) {
            DeveloperMode::exit_clicked(event);
        },
        LV_EVENT_CLICKED,
        nullptr
    );

    lv_obj_t *button_label = lv_label_create(exit_button);
    lv_obj_set_style_text_font(button_label, &esp_brookesia_font_maison_neue_book_16, 0);
    lv_label_set_text(button_label, "Exit and reboot");
    lv_obj_center(button_label);

    lv_screen_load(screen);
    esp_lv_adapter_unlock();

    HeadLed::get_instance().set_developer_mode();
    const esp_err_t result = usb_msc_mount();
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start USB MSC: %s", esp_err_to_name(result));
        return false;
    }

    ESP_LOGI(TAG, "Developer Mode started");
    return true;
}
