/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <cstdint>
#include <mutex>
#include <string>

class WeatherConfig {
public:
    enum class LocationMode {
        Coordinates,
        City,
    };

    struct Data {
        bool valid = false;
        std::string api_key;
        LocationMode location_mode = LocationMode::Coordinates;
        double latitude = 0;
        double longitude = 0;
        std::string city;
        std::string country;
        std::string display_name;
        std::string language = "en";
        uint16_t refresh_minutes = 10;
    };

    static WeatherConfig &get_instance()
    {
        static WeatherConfig instance;
        return instance;
    }

    bool init();
    bool clear();
    bool store_resolved_coordinates(double latitude, double longitude);
    Data get() const;

private:
    WeatherConfig() = default;
    WeatherConfig(const WeatherConfig &) = delete;
    WeatherConfig &operator=(const WeatherConfig &) = delete;

    bool import_from_sd_card();
    bool load_from_nvs();
    bool save_to_nvs(const Data &config);
    static bool parse(const std::string &json, Data &config, std::string &error);
    static std::string serialize(const Data &config);

    mutable std::mutex mutex_;
    Data data_;
};
