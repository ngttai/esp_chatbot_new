/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "imu_gesture.hpp"
#include "lift_to_wake_detector.hpp"
#include "power_manager.hpp"
#include "sdkconfig.h"

#include <cstring>

#include "driver/gpio.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "brookesia/service_helper.hpp"

namespace {

constexpr char TAG[] = "imu_gesture";
constexpr uint8_t BMI270_CHIP_ID_REGISTER = 0x00;
constexpr gpio_num_t BMI270_INTERRUPT_GPIO = GPIO_NUM_21;
constexpr uint32_t I2C_TIMEOUT_MS = 1000;
constexpr uint32_t ANY_MOTION_EMOTE_DURATION_MS = 2500;
constexpr char ANY_MOTION_EMOTE[] = "confused";
constexpr EventBits_t INTERRUPT_EVENT = BIT0;
constexpr size_t MAX_WRITE_LENGTH = 64;
constexpr uint32_t LIFT_SAMPLE_PERIOD_MS = 50;
constexpr float ACCEL_COUNTS_PER_G = 8192.0F;
constexpr uint32_t SETTINGS_STORAGE_TIMEOUT_MS = 3000;
constexpr char SETTINGS_NAMESPACE[] = "imu_gesture";
constexpr char LIFT_SETTING_KEY[] = "lift_enabled";

using EmoteHelper = esp_brookesia::service::helper::ExpressionEmote;
using StorageHelper = esp_brookesia::service::helper::Storage;

} // namespace

ImuGesture::~ImuGesture()
{
    cleanup();
}

int8_t ImuGesture::i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t length, void *context)
{
    auto *self = static_cast<ImuGesture *>(context);
    if ((self == nullptr) || (self->i2c_device_ == nullptr) || (data == nullptr)) {
        return BMI2_E_NULL_PTR;
    }

    const esp_err_t ret = i2c_master_transmit_receive(
                              self->i2c_device_, &reg_addr, sizeof(reg_addr), data, length, I2C_TIMEOUT_MS
                          );
    return (ret == ESP_OK) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

int8_t ImuGesture::i2c_write(uint8_t reg_addr, const uint8_t *data, uint32_t length, void *context)
{
    auto *self = static_cast<ImuGesture *>(context);
    if ((self == nullptr) || (self->i2c_device_ == nullptr) || ((data == nullptr) && (length > 0)) ||
        (length > MAX_WRITE_LENGTH)) {
        return BMI2_E_COM_FAIL;
    }

    uint8_t buffer[MAX_WRITE_LENGTH + 1] = {};
    buffer[0] = reg_addr;
    if (length > 0) {
        std::memcpy(&buffer[1], data, length);
    }
    const esp_err_t ret = i2c_master_transmit(self->i2c_device_, buffer, length + 1, I2C_TIMEOUT_MS);
    return (ret == ESP_OK) ? BMI2_INTF_RET_SUCCESS : BMI2_E_COM_FAIL;
}

void ImuGesture::delay_us(uint32_t period, void *)
{
    esp_rom_delay_us(period);
}

void IRAM_ATTR ImuGesture::gpio_isr(void *context)
{
    auto *self = static_cast<ImuGesture *>(context);
    if ((self == nullptr) || (self->event_group_ == nullptr)) {
        return;
    }

    BaseType_t higher_priority_task_woken = pdFALSE;
    xEventGroupSetBitsFromISR(self->event_group_, INTERRUPT_EVENT, &higher_priority_task_woken);
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void ImuGesture::task_entry(void *context)
{
    static_cast<ImuGesture *>(context)->run();
}

bool ImuGesture::configure_any_motion()
{
    bmi2_sens_config accel_config = {};
    accel_config.type = BMI2_ACCEL;
    int8_t result = bmi270_get_sensor_config(&accel_config, 1, &device_);
    if (result != BMI2_OK) return false;
    accel_config.cfg.acc.range = BMI2_ACC_RANGE_4G;
    accel_config.cfg.acc.odr = BMI2_ACC_ODR_50HZ;
    accel_config.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    accel_config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    result = bmi270_set_sensor_config(&accel_config, 1, &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to configure lift-to-wake accelerometer: %d", result);
        return false;
    }

    bmi2_sens_config motion_config = {};
    motion_config.type = BMI2_ANY_MOTION;
    result = bmi270_get_sensor_config(&motion_config, 1, &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to read BMI270 any-motion config: %d", result);
        return false;
    }

    // Keep the esp_speaker thresholds exactly: 50 * 20 ms and 1000 * 0.48 mg.
    motion_config.cfg.any_motion.duration = 50;
    motion_config.cfg.any_motion.threshold = 1000;
    result = bmi270_set_sensor_config(&motion_config, 1, &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to write BMI270 any-motion config: %d", result);
        return false;
    }

    bmi2_int_pin_config pin_config = {};
    result = bmi2_get_int_pin_config(&pin_config, &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to read BMI270 interrupt config: %d", result);
        return false;
    }
    pin_config.pin_type = BMI2_INT1;
    pin_config.pin_cfg[0].input_en = BMI2_INT_INPUT_DISABLE;
    pin_config.pin_cfg[0].lvl = BMI2_INT_ACTIVE_LOW;
    pin_config.pin_cfg[0].od = BMI2_INT_PUSH_PULL;
    pin_config.pin_cfg[0].output_en = BMI2_INT_OUTPUT_ENABLE;
    pin_config.int_latch = BMI2_INT_NON_LATCH;
    result = bmi2_set_int_pin_config(&pin_config, &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to write BMI270 interrupt config: %d", result);
        return false;
    }

    const bmi2_sens_int_config interrupt_config = {
        .type = BMI2_ANY_MOTION,
        .hw_int_pin = BMI2_INT1,
    };
    result = bmi270_map_feat_int(&interrupt_config, 1, &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to map BMI270 any-motion interrupt: %d", result);
        return false;
    }

    // Enable detection only after the accelerometer and interrupt path are configured.
    uint8_t sensors[] = {BMI2_ACCEL, BMI2_ANY_MOTION};
    result = bmi270_sensor_enable(sensors, sizeof(sensors), &device_);
    if (result != BMI2_OK) {
        ESP_LOGE(TAG, "Failed to enable BMI270 accelerometer/any-motion: %d", result);
        return false;
    }
    return true;
}

bool ImuGesture::init()
{
    if (task_ != nullptr) {
        return true;
    }

    load_settings();
    void *i2c_bus = nullptr;
    esp_err_t ret = esp_board_periph_ref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER, &i2c_bus);
    if ((ret != ESP_OK) || (i2c_bus == nullptr)) {
        ESP_LOGE(TAG, "Failed to reference Board Manager I2C bus: %s", esp_err_to_name(ret));
        return false;
    }
    i2c_bus_referenced_ = true;

    i2c_device_config_t i2c_config = {};
    i2c_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    i2c_config.device_address = BMI270_I2C_ADDRESS;
    i2c_config.scl_speed_hz = 400000;
    ret = i2c_master_bus_add_device(
              static_cast<i2c_master_bus_handle_t>(i2c_bus), &i2c_config, &i2c_device_
          );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BMI270 I2C device: %s", esp_err_to_name(ret));
        cleanup();
        return false;
    }

    uint8_t chip_id = 0;
    if ((i2c_read(BMI270_CHIP_ID_REGISTER, &chip_id, 1, this) != BMI2_OK) ||
        (chip_id != BMI270_CHIP_ID)) {
        ESP_LOGW(TAG, "BMI270 not detected at 0x%02X (chip ID 0x%02X, expected 0x%02X)",
                 BMI270_I2C_ADDRESS, chip_id, BMI270_CHIP_ID);
        cleanup();
        return false;
    }
    ESP_LOGI(TAG, "Detected BMI270 at 0x%02X (chip ID 0x%02X)", BMI270_I2C_ADDRESS, chip_id);

    device_.intf = BMI2_I2C_INTF;
    device_.intf_ptr = this;
    device_.read = i2c_read;
    device_.write = i2c_write;
    device_.delay_us = delay_us;
    device_.read_write_len = 46;
    device_.config_file_ptr = nullptr;

    const int8_t init_result = bmi270_init(&device_);
    if (init_result != BMI2_OK) {
        ESP_LOGE(TAG, "BMI270 initialization failed: %d", init_result);
        cleanup();
        return false;
    }

    event_group_ = xEventGroupCreate();
    if (event_group_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create BMI270 event group");
        cleanup();
        return false;
    }

    const gpio_config_t interrupt_gpio_config = {
        .pin_bit_mask = (1ULL << BMI270_INTERRUPT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    ret = gpio_config(&interrupt_gpio_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure BMI270 interrupt GPIO: %s", esp_err_to_name(ret));
        cleanup();
        return false;
    }

    // Display/touch normally owns the shared GPIO ISR service and starts before
    // the IMU. Reuse it first to avoid a misleading "already installed" error.
    ret = gpio_isr_handler_add(BMI270_INTERRUPT_GPIO, gpio_isr, this);
    if (ret == ESP_ERR_INVALID_STATE) {
        ret = gpio_install_isr_service(0);
        if (ret == ESP_OK) {
            ret = gpio_isr_handler_add(BMI270_INTERRUPT_GPIO, gpio_isr, this);
        }
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add BMI270 GPIO ISR: %s", esp_err_to_name(ret));
        cleanup();
        return false;
    }
    gpio_handler_added_ = true;

    if (!configure_any_motion()) {
        cleanup();
        return false;
    }

    if (xTaskCreate(task_entry, "imu_gesture", 5 * 1024, this, 5, &task_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create BMI270 gesture task");
        cleanup();
        return false;
    }

    ESP_LOGI(TAG, "BMI270 any-motion detection started on GPIO%d", BMI270_INTERRUPT_GPIO);
    ESP_LOGI(TAG, "Lift to wake: %s, tilt=%d deg, stable=%d ms, cooldown=%d ms",
             lift_enabled_.load() ? "enabled" : "disabled", CONFIG_EXAMPLE_IMU_LIFT_TILT_DEGREES,
             CONFIG_EXAMPLE_IMU_LIFT_STABLE_MS, CONFIG_EXAMPLE_IMU_LIFT_COOLDOWN_MS);
    initialized_ = true;
    return true;
}

void ImuGesture::load_settings()
{
    auto result = StorageHelper::get_key_value<bool>(
        SETTINGS_NAMESPACE, LIFT_SETTING_KEY, SETTINGS_STORAGE_TIMEOUT_MS
    );
    if (result) {
        lift_enabled_ = result.value();
    } else {
        lift_enabled_ = true;
        ESP_LOGI(TAG, "Lift setting not loaded; using default enabled: %s", result.error().c_str());
    }
}

bool ImuGesture::set_lift_enabled(bool enabled)
{
    std::lock_guard lock(settings_mutex_);
    auto result = StorageHelper::save_key_value(
        SETTINGS_NAMESPACE, LIFT_SETTING_KEY, enabled, SETTINGS_STORAGE_TIMEOUT_MS
    );
    if (!result) {
        ESP_LOGE(TAG, "Cannot save lift setting: %s", result.error().c_str());
        return false;
    }
    lift_enabled_ = enabled;
    ++settings_revision_;
    ESP_LOGI(TAG, "Lift to wake: %s", enabled ? "enabled" : "disabled");
    return true;
}

void ImuGesture::run()
{
    LiftToWakeDetector detector(CONFIG_EXAMPLE_IMU_LIFT_TILT_DEGREES,
                                CONFIG_EXAMPLE_IMU_LIFT_STABLE_MS,
                                CONFIG_EXAMPLE_IMU_LIFT_COOLDOWN_MS);
    uint32_t revision = settings_revision_.load();
    bool sample_logged = false;
    while (true) {
        const EventBits_t events = xEventGroupWaitBits(
            event_group_, INTERRUPT_EVENT, pdTRUE, pdTRUE, pdMS_TO_TICKS(LIFT_SAMPLE_PERIOD_MS)
        );
        const uint32_t current_revision = settings_revision_.load();
        if (revision != current_revision) {
            revision = current_revision;
            detector.reset();
        }
        bmi2_sens_data sample = {};
        const int8_t sample_result = bmi2_get_sensor_data(&sample, &device_);
        if (sample_result == BMI2_OK && !sample_logged &&
                (sample.acc.x != 0 || sample.acc.y != 0 || sample.acc.z != 0)) {
            ESP_LOGI(TAG, "Accelerometer sample (4g range): x=%d y=%d z=%d",
                     sample.acc.x, sample.acc.y, sample.acc.z);
            sample_logged = true;
        }
        if (sample_result == BMI2_OK && lift_enabled_.load()) {
            const uint32_t now = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
            if (detector.update(now, {sample.acc.x / ACCEL_COUNTS_PER_G,
                                     sample.acc.y / ACCEL_COUNTS_PER_G,
                                     sample.acc.z / ACCEL_COUNTS_PER_G})) {
                PowerManager::get_instance().notify_user_activity();
                ESP_LOGI(TAG, "Lift gesture: wake display only");
            }
        } else {
            detector.reset();
        }
        if ((events & INTERRUPT_EVENT) == 0) continue;

        uint16_t interrupt_status = 0;
        const int8_t result = bmi2_get_int_status(&interrupt_status, &device_);
        if (result != BMI2_OK) {
            ESP_LOGW(TAG, "Failed to read BMI270 interrupt status: %d", result);
            continue;
        }
        if ((interrupt_status & BMI270_ANY_MOT_STATUS_MASK) == 0) {
            continue;
        }

        ESP_LOGI(TAG, "Any-motion detected; show %s emote for %lu ms", ANY_MOTION_EMOTE,
                 static_cast<unsigned long>(ANY_MOTION_EMOTE_DURATION_MS));
        EmoteHelper::call_function_async(
            EmoteHelper::FunctionId::InsertAnimation, ANY_MOTION_EMOTE, ANY_MOTION_EMOTE_DURATION_MS
        );
    }
}

void ImuGesture::cleanup()
{
    initialized_ = false;
    if (task_ != nullptr) {
        vTaskDelete(task_);
        task_ = nullptr;
    }
    if (gpio_handler_added_) {
        gpio_isr_handler_remove(BMI270_INTERRUPT_GPIO);
        gpio_handler_added_ = false;
    }
    if (event_group_ != nullptr) {
        vEventGroupDelete(event_group_);
        event_group_ = nullptr;
    }
    if (i2c_device_ != nullptr) {
        i2c_master_bus_rm_device(i2c_device_);
        i2c_device_ = nullptr;
    }
    if (i2c_bus_referenced_) {
        esp_board_periph_unref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
        i2c_bus_referenced_ = false;
    }
}
