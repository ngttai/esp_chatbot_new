#pragma once

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#if defined(ESP_PLATFORM)
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#include "boost/format.hpp"
#include "brookesia/lib_utils/plugin.hpp"
#include "brookesia/service_display/service_display.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/service/manager.hpp"

#if !BROOKESIA_SERVICE_DISPLAY_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/dataflow_provider.hpp"
#include "private/utils.hpp"

namespace esp_brookesia::service {

namespace {

constexpr uint32_t INVALID_SOURCE_ID = 0;
constexpr uint32_t INVALID_TOUCH_ID = 0;
constexpr float PI = 3.14159265358979323846F;
#if defined(ESP_PLATFORM)
constexpr const char *TOUCH_INTERRUPT_TASK_NAME = "svc_display_touch_irq";
constexpr uint32_t TOUCH_INTERRUPT_TASK_STACK_SIZE = 3 * 1024;
constexpr UBaseType_t TOUCH_INTERRUPT_TASK_PRIORITY = 5;
#endif

bool is_valid_source_name(std::string_view source_name)
{
    return !source_name.empty();
}

template <typename T>
std::expected<boost::json::array, std::string> to_json_array(const T &value)
{
    auto json_value = BROOKESIA_DESCRIBE_TO_JSON(value);
    if (!json_value.is_array()) {
        return std::unexpected("Failed to serialize Display data to JSON array");
    }
    return boost::json::array(json_value.as_array());
}

uint64_t get_current_time_ms()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

uint8_t to_area_mask(Display::TouchGestureArea area)
{
    return static_cast<uint8_t>(area);
}

} // namespace

struct Display::TouchInterruptBridge {
    Display *owner = nullptr;
    uint32_t touch_id = INVALID_TOUCH_ID;
#if defined(ESP_PLATFORM)
    SemaphoreHandle_t interrupt_sem = nullptr;
    SemaphoreHandle_t task_done_sem = nullptr;
    TaskHandle_t task = nullptr;
    std::atomic_bool running = false;
#endif

    bool start(Display *display, uint32_t id, const std::shared_ptr<hal::display::TouchIface> &touch)
    {
        BROOKESIA_CHECK_NULL_RETURN(display, false, "Display touch interrupt owner is null");
        BROOKESIA_CHECK_NULL_RETURN(touch, false, "Display touch interrupt HAL handle is null");
        owner = display;
        touch_id = id;

#if defined(ESP_PLATFORM)
        interrupt_sem = xSemaphoreCreateBinary();
        BROOKESIA_CHECK_NULL_RETURN(interrupt_sem, false, "Failed to create Display touch interrupt semaphore");
        task_done_sem = xSemaphoreCreateBinary();
        if (task_done_sem == nullptr) {
            BROOKESIA_LOGE("Failed to create Display touch task done semaphore");
            stop();
            return false;
        }
        running = true;
        const auto task_ret = xTaskCreate(
                                  TouchInterruptBridge::task_entry, TOUCH_INTERRUPT_TASK_NAME,
                                  TOUCH_INTERRUPT_TASK_STACK_SIZE, this, TOUCH_INTERRUPT_TASK_PRIORITY, &task
                              );
        if (task_ret != pdPASS) {
            running = false;
            BROOKESIA_LOGE("Failed to create Display touch interrupt task");
            stop();
            return false;
        }
#endif

        if (!touch->register_interrupt_handler(TouchInterruptBridge::interrupt_handler, this)) {
            BROOKESIA_LOGE("Failed to register Display touch interrupt handler");
            stop();
            return false;
        }
        return true;
    }

    void stop()
    {
#if defined(ESP_PLATFORM)
        running = false;
        if (interrupt_sem != nullptr) {
            xSemaphoreGive(interrupt_sem);
        }
        if ((task != nullptr) && (task_done_sem != nullptr)) {
            const auto done = xSemaphoreTake(task_done_sem, pdMS_TO_TICKS(200));
            if (done != pdTRUE) {
                BROOKESIA_LOGW("Timeout waiting for Display touch interrupt task to stop");
            }
        }
        task = nullptr;
        if (interrupt_sem != nullptr) {
            vSemaphoreDelete(interrupt_sem);
            interrupt_sem = nullptr;
        }
        if (task_done_sem != nullptr) {
            vSemaphoreDelete(task_done_sem);
            task_done_sem = nullptr;
        }
#endif
        owner = nullptr;
        touch_id = INVALID_TOUCH_ID;
    }

    bool notify()
    {
        if (owner == nullptr) {
            return false;
        }
        if (!owner->schedule_touch_read(touch_id)) {
            BROOKESIA_LOGW("Failed to schedule Display touch read from interrupt");
        }
        return false;
    }

    static bool interrupt_handler(void *ctx)
    {
        auto *bridge = static_cast<TouchInterruptBridge *>(ctx);
        if (bridge == nullptr) {
            return false;
        }
#if defined(ESP_PLATFORM)
        if (bridge->interrupt_sem == nullptr) {
            return false;
        }
        BaseType_t higher_priority_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(bridge->interrupt_sem, &higher_priority_task_woken);
        return higher_priority_task_woken == pdTRUE;
#else
        return bridge->notify();
#endif
    }

#if defined(ESP_PLATFORM)
    void task_loop()
    {
        while (running.load()) {
            if (xSemaphoreTake(interrupt_sem, portMAX_DELAY) != pdTRUE) {
                continue;
            }
            if (!running.load()) {
                break;
            }
            (void)notify();
        }
        if (task_done_sem != nullptr) {
            xSemaphoreGive(task_done_sem);
        }
    }

    static void task_entry(void *arg)
    {
        auto *bridge = static_cast<TouchInterruptBridge *>(arg);
        if (bridge != nullptr) {
            bridge->task_loop();
        }
        vTaskDelete(nullptr);
    }
#endif
};


} // namespace esp_brookesia::service

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
