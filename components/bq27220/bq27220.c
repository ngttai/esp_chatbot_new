/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "bq27220.h"
#include "bq27220_reg.h"

static const char *TAG = "bq27220";

// device addr
#define BQ27220_I2C_ADDRESS 0x55
// device id
#define BQ27220_DEVICE_ID 0x0220
#define delay_ms(x) vTaskDelay(pdMS_TO_TICKS(x))

#define WAIT_CFG_UPDATE(handle, expected) do { \
    OperationStatus status = {}; \
    ret = false; \
    for (uint32_t attempt = 0; attempt < 20; ++attempt) { \
        if (bq27220_get_operation_status(handle, &status) == BQ27220_SUCCESS && \
                status.CFGUPDATE == (expected)) { \
            ret = true; \
            break; \
        } \
        delay_ms(100); \
    } \
    if (!ret) { \
        ESP_LOGE(TAG, "Timed out waiting for CFGUPDATE=%d", (expected)); \
    } \
} while(0)


typedef struct {
    i2c_master_dev_handle_t i2c_device_handle;
} bq27220_data_t;


static uint16_t bq27220_read_word(bq27220_handle_t bq_handle, uint8_t address)
{
    bq27220_data_t *bq_data = (bq27220_data_t *)bq_handle;
    uint8_t data[2] = {0};
    esp_err_t ret = i2c_master_transmit_receive(
                        bq_data->i2c_device_handle, &address, sizeof(address), data, sizeof(data),
                        BQ27220_I2C_TIMEOUT
                    );
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read register 0x%02x failed: %s", address, esp_err_to_name(ret));
        return BQ27220_ERROR;
    }
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static bool bq27220_control(bq27220_handle_t bq_handle, uint16_t control)
{
    bq27220_data_t *bq_data = (bq27220_data_t *)bq_handle;
    uint8_t data[] = {CommandControl, control & 0xFF, control >> 8};
    return i2c_master_transmit(
               bq_data->i2c_device_handle, data, sizeof(data), BQ27220_I2C_TIMEOUT
           ) == ESP_OK;
}

static bool bq27220_write_bytes(
    bq27220_handle_t bq_handle, uint8_t address, const uint8_t *data, size_t data_len
)
{
    bq27220_data_t *bq_data = (bq27220_data_t *)bq_handle;
    uint8_t buffer[8] = {0};
    if ((data == NULL) || (data_len > (sizeof(buffer) - 1))) {
        return false;
    }
    buffer[0] = address;
    memcpy(&buffer[1], data, data_len);
    return i2c_master_transmit(
               bq_data->i2c_device_handle, buffer, data_len + 1, BQ27220_I2C_TIMEOUT
           ) == ESP_OK;
}

static uint8_t bq27220_get_checksum(uint8_t *data, uint16_t len)
{
    uint8_t ret = 0;
    for (uint16_t i = 0; i < len; i++) {
        ret += data[i];
    }
    return 0xFF - ret;
}

static uint16_t getDeviceNumber(bq27220_handle_t bq_handle)
{
    uint16_t devid = 0;
    // Request device number(chip PN)
    bq27220_control(bq_handle, Control_DEVICE_NUMBER);
    delay_ms(15); // delay_ms(15);
    // Read id data from MAC scratch space
    devid = bq27220_read_word(bq_handle, CommandMACData);
    return devid;
}

static bool bq27220_set_parameter_u16(bq27220_handle_t bq_handle, uint16_t address, uint16_t value)
{
    bool ret;
    uint8_t buffer[4];

    buffer[0] = address & 0xFF;
    buffer[1] = (address >> 8) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
    ret = bq27220_write_bytes(bq_handle, CommandSelectSubclass, buffer, 4);
    delay_ms(10);

    uint8_t checksum = bq27220_get_checksum(buffer, 4);
    buffer[0] = checksum;
    buffer[1] = 6;
    ret = bq27220_write_bytes(bq_handle, CommandMACDataSum, buffer, 2);

    delay_ms(10);
    return ret;
}

static uint16_t bq27220_get_parameter_u16(bq27220_handle_t bq_handle, uint16_t address)
{
    bq27220_data_t *bq_data = (bq27220_data_t *)bq_handle;
    uint8_t buffer[4];

    buffer[0] = address & 0xFF;
    buffer[1] = (address >> 8) & 0xFF;
    bq27220_write_bytes(bq_handle, CommandSelectSubclass, buffer, 2);
    delay_ms(10);

    uint8_t mac_register = CommandMACData;
    if (i2c_master_transmit_receive(
            bq_data->i2c_device_handle, &mac_register, sizeof(mac_register), buffer, 2,
            BQ27220_I2C_TIMEOUT
        ) != ESP_OK) {
        return BQ27220_ERROR;
    }
    delay_ms(10);
    return ((uint16_t)buffer[0] << 8) | buffer[1];
}

static bool bq27220_seal(bq27220_handle_t bq_handle)
{
    OperationStatus status = {};
    bq27220_get_operation_status(bq_handle, &status);
    if (status.SEC == Bq27220OperationStatusSecSealed) {
        return true; // Already sealed
    }
    bq27220_control(bq_handle, Control_SEALED);
    delay_ms(10);
    bq27220_get_operation_status(bq_handle, &status);
    if (status.SEC == Bq27220OperationStatusSecSealed) {
        return true;
    }
    return false;
}

static bool bq27220_unseal(bq27220_handle_t bq_handle)
{
    OperationStatus status = {};
    bq27220_get_operation_status(bq_handle, &status);
    if (status.SEC == Bq27220OperationStatusSecUnsealed) {
        return true; // Already unsealed
    }
    bq27220_control(bq_handle, UnsealKey1);
    delay_ms(10);
    bq27220_control(bq_handle, UnsealKey2);
    delay_ms(10);
    bq27220_get_operation_status(bq_handle, &status);
    if (status.SEC == Bq27220OperationStatusSecUnsealed) {
        return true;
    }
    return false;
}

bq27220_handle_t bq27220_init(const bq27220_config_t *config)
{
    int ret = 0;
    bq27220_data_t *handle = (bq27220_data_t *)calloc(1, sizeof(bq27220_data_t));
    if (!handle) {
        ESP_LOGE(TAG, "Memory allocation failed");
        return NULL;
    }
    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BQ27220_I2C_ADDRESS,
        .scl_speed_hz = 400000,
    };
    esp_err_t add_device_ret = i2c_master_bus_add_device(
                                   config->i2c_bus, &device_config, &handle->i2c_device_handle
                               );
    ESP_GOTO_ON_FALSE(
        add_device_ret == ESP_OK, 0, err, TAG, "i2c_master_bus_add_device failed: %s",
        esp_err_to_name(add_device_ret)
    );

    uint16_t data = getDeviceNumber(handle);
    ESP_GOTO_ON_FALSE(data == BQ27220_DEVICE_ID, 0, err, TAG, "Invalid Device Number %04x != 0x0220", data);

    data = bq27220_get_fw_version(handle);
    ESP_LOGI(TAG, "Firmware Version %04x", data);
    data = bq27220_get_hw_version(handle);
    ESP_LOGI(TAG, "Hardware Version %04x", data);

    ESP_GOTO_ON_FALSE(bq27220_unseal(handle), 0, err, TAG, "Failed to unseal");

    uint16_t design_cap = bq27220_get_design_capacity(handle);
    uint16_t EMF = bq27220_get_parameter_u16(handle, AddressEMF);
    uint16_t t0 = bq27220_get_parameter_u16(handle, AddressT0);
    uint16_t dod20 = bq27220_get_parameter_u16(handle, AddressStartDOD20);
    ESP_LOGI(TAG, "Design Capacity: %d, EMF: %d, T0: %d, DOD20: %d", design_cap, EMF, t0, dod20);
    ParamCEDV *cedv = config->cedv;
    if (cedv->design_cap == design_cap && cedv->EMF == EMF && cedv->T0 == t0 && cedv->DOD20 == dod20) {
        ESP_LOGI(TAG, "Skip battery profile update");
        return handle;
    }
    ESP_LOGW(TAG, "Start updating battery profile");
    delay_ms(10);
    bq27220_control(handle, Control_ENTER_CFG_UPDATE);
    WAIT_CFG_UPDATE(handle, true);
    ESP_GOTO_ON_FALSE(ret == true, 0, err, TAG, "Battery profile update failed");

    bq27220_set_parameter_u16(handle, AddressGaugingConfig, cedv->cedv_conf.gauge_conf_raw);
    bq27220_set_parameter_u16(handle, AddressFullChargeCapacity, cedv->full_charge_cap);
    bq27220_set_parameter_u16(handle, AddressDesignCapacity, cedv->design_cap);
    bq27220_set_parameter_u16(handle, AddressNearFull, cedv->near_full);
    bq27220_set_parameter_u16(handle, AddressSelfDischargeRate, cedv->self_discharge_rate);
    bq27220_set_parameter_u16(handle, AddressReserveCapacity, cedv->reserve_cap);
    bq27220_set_parameter_u16(handle, AddressEMF, cedv->EMF);
    bq27220_set_parameter_u16(handle, AddressC0, cedv->C0);
    bq27220_set_parameter_u16(handle, AddressR0, cedv->R0);
    bq27220_set_parameter_u16(handle, AddressT0, cedv->T0);
    bq27220_set_parameter_u16(handle, AddressR1, cedv->R1);
    bq27220_set_parameter_u16(handle, AddressTC, (cedv->TC) << 8 | cedv->C1);
    bq27220_set_parameter_u16(handle, AddressStartDOD0, cedv->DOD0);
    bq27220_set_parameter_u16(handle, AddressStartDOD10, cedv->DOD10);
    bq27220_set_parameter_u16(handle, AddressStartDOD20, cedv->DOD20);
    bq27220_set_parameter_u16(handle, AddressStartDOD30, cedv->DOD30);
    bq27220_set_parameter_u16(handle, AddressStartDOD40, cedv->DOD40);
    bq27220_set_parameter_u16(handle, AddressStartDOD50, cedv->DOD40);
    bq27220_set_parameter_u16(handle, AddressStartDOD60, cedv->DOD60);
    bq27220_set_parameter_u16(handle, AddressStartDOD70, cedv->DOD70);
    bq27220_set_parameter_u16(handle, AddressStartDOD80, cedv->DOD80);
    bq27220_set_parameter_u16(handle, AddressStartDOD90, cedv->DOD90);
    bq27220_set_parameter_u16(handle, AddressStartDOD100, cedv->DOD100);
    bq27220_set_parameter_u16(handle, AddressEDV0, cedv->EDV0);
    bq27220_set_parameter_u16(handle, AddressEDV1, cedv->EDV1);
    bq27220_set_parameter_u16(handle, AddressEDV2, cedv->EDV2);
    bq27220_control(handle, Control_EXIT_CFG_UPDATE_REINIT);
    delay_ms(10);
    WAIT_CFG_UPDATE(handle, false);
    ESP_GOTO_ON_FALSE(ret == true, 0, err, TAG, "Battery profile update failed");
    delay_ms(10);
    design_cap = bq27220_get_design_capacity(handle);
    if (cedv->design_cap == design_cap) {
        ESP_LOGI(TAG, "Battery profile update success");
    } else {
        ESP_LOGE(TAG, "Battery profile update failed");
        goto err;
    }
    bq27220_seal(handle);
    return handle;
err:
    if (handle->i2c_device_handle) {
        i2c_master_bus_rm_device(handle->i2c_device_handle);
    }
    free(handle);
    return NULL;
}

bool bq27220_deinit(bq27220_handle_t bq_handle)
{
    ESP_RETURN_ON_FALSE(bq_handle, false, TAG, "Invalid handle");
    bq27220_data_t *bq_data = (bq27220_data_t *)bq_handle;

    if (bq_data->i2c_device_handle) {
        i2c_master_bus_rm_device(bq_data->i2c_device_handle);
    }
    free(bq_handle);
    return true;
}

uint16_t bq27220_get_voltage(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandVoltage);
}

int16_t bq27220_get_current(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandCurrent);
}

int16_t bq27220_get_avgcurrent(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandAverageCurrent);
}

uint8_t bq27220_get_battery_status(bq27220_handle_t bq_handle, BatteryStatus *battery_status)
{
    uint16_t data = bq27220_read_word(bq_handle, CommandBatteryStatus);
    if (data == BQ27220_ERROR) {
        return BQ27220_ERROR;
    } else {
        *(uint16_t *)battery_status = data;
        return BQ27220_SUCCESS;
    }
}

uint8_t bq27220_get_operation_status(bq27220_handle_t bq_handle, OperationStatus *operation_status)
{
    uint16_t data = bq27220_read_word(bq_handle, CommandOperationStatus);
    if (data == BQ27220_ERROR) {
        return BQ27220_ERROR;
    } else {
        *(uint16_t *)operation_status = data;
        return BQ27220_SUCCESS;
    }
}

uint16_t bq27220_get_fw_version(bq27220_handle_t bq_handle)
{
    uint16_t ret = 0;
    // Request device number(chip PN)
    bq27220_control(bq_handle, Control_FW_VERSION);
    delay_ms(15); // delay_ms(15);
    // Read id data from MAC scratch space
    ret = bq27220_read_word(bq_handle, CommandMACData);
    return ret;
}

uint16_t bq27220_get_hw_version(bq27220_handle_t bq_handle)
{
    uint16_t ret = 0;
    // Request device number(chip PN)
    bq27220_control(bq_handle, Control_HW_VERSION);
    delay_ms(15); // delay_ms(15);
    // Read id data from MAC scratch space
    ret = bq27220_read_word(bq_handle, CommandMACData);
    return ret;
}

uint16_t bq27220_get_temperature(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandTemperature);
}

uint16_t bq27220_get_cycle_count(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandCycleCount);
}

uint16_t bq27220_get_full_charge_capacity(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandFullChargeCapacity);
}

uint16_t bq27220_get_design_capacity(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandDesignCapacity);
}

uint16_t bq27220_get_remaining_capacity(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandRemainingCapacity);
}

uint16_t bq27220_get_state_of_charge(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandStateOfCharge);
}

uint16_t bq27220_get_state_of_health(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandStateOfHealth);
}

uint16_t bq27220_get_charge_voltage(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandChargeVoltage);
}

uint16_t bq27220_get_charge_current(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandChargeCurrent);
}

int16_t bq27220_get_average_power(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandAveragePower);
}

uint16_t bq27220_get_time_to_empty(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandTimeToEmpty);
}

uint16_t bq27220_get_time_to_full(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandTimeToFull);
}

int16_t bq27220_get_maxload_current(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandMaxLoadCurrent);
}

int16_t bq27220_get_standby_current(bq27220_handle_t bq_handle)
{
    return bq27220_read_word(bq_handle, CommandStandbyCurrent);
}
