/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "bmi270.h"

class ImuGesture {
public:
    static ImuGesture &get_instance()
    {
        static ImuGesture instance;
        return instance;
    }

    bool init();
    bool is_initialized() const
    {
        return task_ != nullptr;
    }

private:
    ImuGesture() = default;
    ~ImuGesture();
    ImuGesture(const ImuGesture &) = delete;
    ImuGesture &operator=(const ImuGesture &) = delete;

    static int8_t i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t length, void *context);
    static int8_t i2c_write(uint8_t reg_addr, const uint8_t *data, uint32_t length, void *context);
    static void delay_us(uint32_t period, void *context);
    static void IRAM_ATTR gpio_isr(void *context);
    static void task_entry(void *context);

    bool configure_any_motion();
    void run();
    void cleanup();

    i2c_master_dev_handle_t i2c_device_ = nullptr;
    bmi2_dev device_ = {};
    EventGroupHandle_t event_group_ = nullptr;
    TaskHandle_t task_ = nullptr;
    bool i2c_bus_referenced_ = false;
    bool gpio_handler_added_ = false;
};
