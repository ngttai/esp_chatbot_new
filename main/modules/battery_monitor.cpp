/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "battery_monitor.hpp"

#include <algorithm>

#include "driver/i2c_master.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "esp_log.h"

namespace {

constexpr char TAG[] = "battery_monitor";
constexpr uint32_t MONITOR_PERIOD_MS = 1000;

// Keep the VoCat v1.0 battery profile byte-for-byte equivalent to esp_speaker.
const ParamCEDV CEDV_PROFILE = {
    .cedv_conf = {
        .gauge_conf = {
            .CCT = 1,
            .CSYNC = 0,
            .RSVD0 = 0,
            .EDV_CMP = 0,
            .SC = 1,
            .FIXED_EDV0 = 0,
            .RSVD1 = 0,
            .FCC_LIM = 1,
            .RSVD2 = 0,
            .FC_FOR_VDQ = 1,
            .IGNORE_SD = 1,
            .SME0 = 0,
            .RSVD3 = 0,
        },
    },
    .full_charge_cap = 650,
    .design_cap = 650,
    .reserve_cap = 0,
    .near_full = 200,
    .self_discharge_rate = 20,
    .EDV0 = 3490,
    .EDV1 = 3511,
    .EDV2 = 3535,
    .EMF = 3670,
    .C0 = 115,
    .R0 = 968,
    .T0 = 4547,
    .R1 = 4764,
    .TC = 11,
    .C1 = 0,
    .DOD0 = 4147,
    .DOD10 = 4002,
    .DOD20 = 3969,
    .DOD30 = 3938,
    .DOD40 = 3880,
    .DOD50 = 3824,
    .DOD60 = 3794,
    .DOD70 = 3753,
    .DOD80 = 3677,
    .DOD90 = 3574,
    .DOD100 = 3490,
};

} // namespace

BatteryMonitor::~BatteryMonitor()
{
    if (timer_ != nullptr) {
        xTimerStop(timer_, 0);
        xTimerDelete(timer_, 0);
    }
    if (gauge_ != nullptr) {
        bq27220_deinit(gauge_);
    }
    if (i2c_bus_referenced_) {
        esp_board_periph_unref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
    }
}

bool BatteryMonitor::init()
{
    if (gauge_ != nullptr) {
        return true;
    }

    void *i2c_bus = nullptr;
    esp_err_t ret = esp_board_periph_ref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER, &i2c_bus);
    if ((ret != ESP_OK) || (i2c_bus == nullptr)) {
        ESP_LOGE(TAG, "Failed to reference Board Manager I2C bus: %s", esp_err_to_name(ret));
        return false;
    }
    i2c_bus_referenced_ = true;

    bq27220_config_t config = {
        .i2c_bus = static_cast<i2c_master_bus_handle_t>(i2c_bus),
        .cedv = const_cast<ParamCEDV *>(&CEDV_PROFILE),
    };
    gauge_ = bq27220_init(&config);
    if (gauge_ == nullptr) {
        ESP_LOGE(TAG, "Failed to initialize BQ27220");
        esp_board_periph_unref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
        i2c_bus_referenced_ = false;
        return false;
    }

    sample();
    timer_ = xTimerCreate(
                 "battery_monitor", pdMS_TO_TICKS(MONITOR_PERIOD_MS), pdTRUE, this, timer_callback
             );
    if (timer_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create battery monitor timer");
        bq27220_deinit(gauge_);
        gauge_ = nullptr;
        esp_board_periph_unref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
        i2c_bus_referenced_ = false;
        return false;
    }
    if (xTimerStart(timer_, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to start battery monitor timer");
        xTimerDelete(timer_, 0);
        timer_ = nullptr;
        bq27220_deinit(gauge_);
        gauge_ = nullptr;
        esp_board_periph_unref_handle(ESP_BOARD_PERIPH_NAME_I2C_MASTER);
        i2c_bus_referenced_ = false;
        return false;
    }

    ESP_LOGI(TAG, "BQ27220 monitor started");
    return true;
}

void BatteryMonitor::timer_callback(TimerHandle_t timer)
{
    auto *monitor = static_cast<BatteryMonitor *>(pvTimerGetTimerID(timer));
    if (monitor != nullptr) {
        monitor->sample();
    }
}

void BatteryMonitor::sample()
{
    if (gauge_ == nullptr) {
        return;
    }

    BatteryStatus status = {};
    if (bq27220_get_battery_status(gauge_, &status) != BQ27220_SUCCESS) {
        valid_.store(false);
        revision_.fetch_add(1);
        return;
    }

    const uint16_t raw_percentage = bq27220_get_state_of_charge(gauge_);
    const uint16_t voltage = bq27220_get_voltage(gauge_);
    const int16_t current = bq27220_get_current(gauge_);
    if (raw_percentage > 100) {
        ESP_LOGW(TAG, "Ignoring invalid battery SOC: %u", raw_percentage);
        valid_.store(false);
        revision_.fetch_add(1);
        return;
    }

    percentage_.store(static_cast<uint8_t>(raw_percentage));
    voltage_mv_.store(voltage);
    current_ma_.store(current);
    charging_.store(status.DSG == 0);
    valid_.store(true);
    revision_.fetch_add(1);
}

BatteryMonitor::Snapshot BatteryMonitor::get_snapshot() const
{
    Snapshot snapshot;
    snapshot.valid = valid_.load();
    snapshot.charging = charging_.load();
    snapshot.percentage = percentage_.load();
    snapshot.voltage_mv = voltage_mv_.load();
    snapshot.current_ma = current_ma_.load();
    snapshot.revision = revision_.load();
    return snapshot;
}
