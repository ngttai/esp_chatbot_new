/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "weather_config.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <vector>

#include "boost/json.hpp"
#include "brookesia/service_helper.hpp"
#include "esp_log.h"
#include "nvs.h"

namespace {

constexpr char TAG[] = "weather_config";
constexpr char CONFIG_DIRECTORY[] = "/sdcard/config";
constexpr char IMPORT_PATH[] = "/sdcard/config/weather-import.json";
constexpr char IMPORTED_PATH[] = "/sdcard/config/weather-imported.json";
constexpr char NVS_NAMESPACE[] = "weather_cfg";
constexpr char NVS_CONFIG_KEY[] = "config";
constexpr uint32_t STORAGE_TIMEOUT_MS = 3000;
constexpr uint64_t MAX_CONFIG_SIZE = 4096;
constexpr uint16_t MIN_REFRESH_MINUTES = 10;
constexpr uint16_t MAX_REFRESH_MINUTES = 24 * 60;

using StorageHelper = esp_brookesia::service::helper::Storage;

const boost::json::value *member(const boost::json::object &object, std::string_view name)
{
    const auto iterator = object.find(name);
    return iterator == object.end() ? nullptr : &iterator->value();
}

bool read_string(const boost::json::object &object, std::string_view name, std::string &output)
{
    const auto *value = member(object, name);
    if ((value == nullptr) || !value->is_string()) {
        return false;
    }
    output = value->as_string().c_str();
    return true;
}

bool read_number(const boost::json::object &object, std::string_view name, double &output)
{
    const auto *value = member(object, name);
    if (value == nullptr || !value->is_number()) {
        return false;
    }
    if (value->is_double()) {
        output = value->as_double();
    } else if (value->is_int64()) {
        output = static_cast<double>(value->as_int64());
    } else {
        output = static_cast<double>(value->as_uint64());
    }
    return std::isfinite(output);
}

bool is_api_key_valid(const std::string &key)
{
    return key != "YOUR_OPENWEATHER_API_KEY" && key.size() >= 16 && key.size() <= 128 &&
           std::all_of(key.begin(), key.end(), [](unsigned char character) {
               return std::isalnum(character) || character == '_' || character == '-';
           });
}

bool is_language_valid(const std::string &language)
{
    return language.size() >= 2 && language.size() <= 8 &&
           std::all_of(language.begin(), language.end(), [](unsigned char character) {
               return std::isalpha(character) || character == '-';
           });
}

bool is_country_valid(const std::string &country)
{
    return country.size() == 2 &&
           std::all_of(country.begin(), country.end(), [](unsigned char character) {
               return std::isalpha(character);
           });
}

} // namespace

bool WeatherConfig::init()
{
    const bool imported = import_from_sd_card();
    if (!imported) {
        (void)load_from_nvs();
    }

    const auto config = get();
    if (config.valid) {
        ESP_LOGI(
            TAG, "Weather configuration ready (location=%s, refresh=%u min)",
            config.location_mode == LocationMode::Coordinates ? "coordinates" : "city",
            static_cast<unsigned>(config.refresh_minutes)
        );
    } else {
        ESP_LOGI(TAG, "Weather is not configured; add %s", IMPORT_PATH);
    }
    return true;
}

bool WeatherConfig::import_from_sd_card()
{
    auto sd_card = StorageHelper::fs_stat("/sdcard", STORAGE_TIMEOUT_MS);
    if (!sd_card || !sd_card->exists || sd_card->type != StorageHelper::FileType::Directory) {
        return false;
    }

    auto directory = StorageHelper::fs_stat(CONFIG_DIRECTORY, STORAGE_TIMEOUT_MS);
    if (directory && !directory->exists) {
        auto create_result = StorageHelper::fs_mkdir(CONFIG_DIRECTORY, STORAGE_TIMEOUT_MS);
        if (!create_result) {
            ESP_LOGW(TAG, "Failed to create weather config directory: %s", create_result.error().c_str());
            return false;
        }
    }

    auto import_file = StorageHelper::fs_stat(IMPORT_PATH, STORAGE_TIMEOUT_MS);
    if (!import_file || !import_file->exists) {
        return false;
    }
    if (import_file->type != StorageHelper::FileType::File || import_file->size == 0 ||
        import_file->size > MAX_CONFIG_SIZE) {
        ESP_LOGE(TAG, "Weather import file must be a non-empty JSON file no larger than %llu bytes",
                 static_cast<unsigned long long>(MAX_CONFIG_SIZE));
        return false;
    }

    auto contents = StorageHelper::fs_read_text(IMPORT_PATH, STORAGE_TIMEOUT_MS);
    if (!contents) {
        ESP_LOGE(TAG, "Failed to read weather import file: %s", contents.error().c_str());
        return false;
    }

    Data imported_config;
    std::string error;
    if (!parse(contents.value(), imported_config, error)) {
        ESP_LOGE(TAG, "Weather import rejected: %s", error.c_str());
        return false;
    }
    if (!save_to_nvs(imported_config)) {
        return false;
    }

    auto old_receipt = StorageHelper::fs_stat(IMPORTED_PATH, STORAGE_TIMEOUT_MS);
    if (old_receipt && old_receipt->exists) {
        auto remove_result = StorageHelper::fs_remove(IMPORTED_PATH, STORAGE_TIMEOUT_MS);
        if (!remove_result) {
            ESP_LOGW(TAG, "Failed to replace previous weather import receipt: %s",
                     remove_result.error().c_str());
        }
    }
    auto rename_result = StorageHelper::fs_rename(IMPORT_PATH, IMPORTED_PATH, STORAGE_TIMEOUT_MS);
    if (!rename_result) {
        ESP_LOGW(TAG, "Weather config was saved, but the import file could not be renamed: %s",
                 rename_result.error().c_str());
    }

    {
        std::lock_guard lock(mutex_);
        data_ = std::move(imported_config);
    }
    ESP_LOGI(TAG, "Imported weather configuration from SD card");
    return true;
}

bool WeatherConfig::load_from_nvs()
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open weather configuration: %s", esp_err_to_name(result));
        return false;
    }

    size_t size = 0;
    result = nvs_get_str(handle, NVS_CONFIG_KEY, nullptr, &size);
    if (result != ESP_OK || size == 0 || size > MAX_CONFIG_SIZE) {
        nvs_close(handle);
        return false;
    }
    std::vector<char> buffer(size);
    result = nvs_get_str(handle, NVS_CONFIG_KEY, buffer.data(), &size);
    nvs_close(handle);
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load weather configuration: %s", esp_err_to_name(result));
        return false;
    }

    Data stored_config;
    std::string error;
    if (!parse(buffer.data(), stored_config, error)) {
        ESP_LOGW(TAG, "Stored weather configuration is invalid: %s", error.c_str());
        return false;
    }
    std::lock_guard lock(mutex_);
    data_ = std::move(stored_config);
    return true;
}

bool WeatherConfig::save_to_nvs(const Data &config)
{
    const std::string normalized = serialize(config);
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_OK) {
        result = nvs_set_str(handle, NVS_CONFIG_KEY, normalized.c_str());
    }
    if (result == ESP_OK) {
        result = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save weather configuration: %s", esp_err_to_name(result));
        return false;
    }
    return true;
}

bool WeatherConfig::clear()
{
    nvs_handle_t handle = 0;
    esp_err_t result = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (result == ESP_ERR_NVS_NOT_FOUND) {
        result = ESP_OK;
    } else if (result == ESP_OK) {
        result = nvs_erase_all(handle);
        if (result == ESP_OK) {
            result = nvs_commit(handle);
        }
        nvs_close(handle);
    }
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear weather configuration: %s", esp_err_to_name(result));
        return false;
    }
    std::lock_guard lock(mutex_);
    data_ = {};
    return true;
}

bool WeatherConfig::store_resolved_coordinates(double latitude, double longitude)
{
    if (!std::isfinite(latitude) || latitude < -90 || latitude > 90 ||
        !std::isfinite(longitude) || longitude < -180 || longitude > 180) {
        return false;
    }

    Data updated;
    {
        std::lock_guard lock(mutex_);
        if (!data_.valid) {
            return false;
        }
        updated = data_;
    }
    updated.location_mode = LocationMode::Coordinates;
    updated.latitude = latitude;
    updated.longitude = longitude;
    updated.city.clear();
    updated.country.clear();
    if (!save_to_nvs(updated)) {
        return false;
    }

    std::lock_guard lock(mutex_);
    data_ = std::move(updated);
    return true;
}

WeatherConfig::Data WeatherConfig::get() const
{
    std::lock_guard lock(mutex_);
    return data_;
}

bool WeatherConfig::parse(const std::string &json, Data &config, std::string &error)
{
    boost::system::error_code parse_error;
    const boost::json::value root_value = boost::json::parse(json, parse_error);
    if (parse_error || !root_value.is_object()) {
        error = "root must be a valid JSON object";
        return false;
    }
    const auto &root = root_value.as_object();

    const auto *version = member(root, "version");
    const bool version_ok = version != nullptr &&
                            ((version->is_int64() && version->as_int64() == 1) ||
                             (version->is_uint64() && version->as_uint64() == 1));
    if (!version_ok) {
        error = "version must be 1";
        return false;
    }

    std::string provider;
    if (!read_string(root, "provider", provider) || provider != "openweathermap") {
        error = "provider must be openweathermap";
        return false;
    }
    if (!read_string(root, "api_key", config.api_key) || !is_api_key_valid(config.api_key)) {
        error = "api_key has an invalid format";
        return false;
    }

    const auto *location_value = member(root, "location");
    if (location_value == nullptr || !location_value->is_object()) {
        error = "location must be an object";
        return false;
    }
    const auto &location = location_value->as_object();
    std::string mode;
    if (!read_string(location, "mode", mode)) {
        error = "location.mode is required";
        return false;
    }
    (void)read_string(location, "display_name", config.display_name);
    if (config.display_name.size() > 64) {
        error = "location.display_name is too long";
        return false;
    }

    if (mode == "coordinates") {
        config.location_mode = LocationMode::Coordinates;
        if (!read_number(location, "latitude", config.latitude) || config.latitude < -90 ||
            config.latitude > 90 || !read_number(location, "longitude", config.longitude) ||
            config.longitude < -180 || config.longitude > 180) {
            error = "latitude or longitude is invalid";
            return false;
        }
        if (config.display_name.empty()) {
            config.display_name = "Current location";
        }
    } else if (mode == "city") {
        config.location_mode = LocationMode::City;
        if (!read_string(location, "city", config.city) || config.city.empty() ||
            config.city.size() > 64) {
            error = "location.city is invalid";
            return false;
        }
        if (!read_string(location, "country", config.country) ||
            !is_country_valid(config.country)) {
            error = "location.country must be a two-letter country code";
            return false;
        }
        std::transform(config.country.begin(), config.country.end(), config.country.begin(),
                       [](unsigned char character) { return std::toupper(character); });
        if (config.display_name.empty()) {
            config.display_name = config.city;
        }
    } else {
        error = "location.mode must be coordinates or city";
        return false;
    }

    const auto *language = member(root, "language");
    if (language != nullptr) {
        if (!language->is_string()) {
            error = "language must be a string";
            return false;
        }
        config.language = language->as_string().c_str();
    }
    if (!is_language_valid(config.language)) {
        error = "language has an invalid format";
        return false;
    }

    const auto *refresh = member(root, "refresh_minutes");
    uint64_t refresh_value = MIN_REFRESH_MINUTES;
    if (refresh != nullptr) {
        if (refresh->is_uint64()) {
            refresh_value = refresh->as_uint64();
        } else if (refresh->is_int64() && refresh->as_int64() >= 0) {
            refresh_value = static_cast<uint64_t>(refresh->as_int64());
        } else {
            error = "refresh_minutes must be an integer";
            return false;
        }
    }
    if (refresh_value < MIN_REFRESH_MINUTES || refresh_value > MAX_REFRESH_MINUTES) {
        error = "refresh_minutes must be between 10 and 1440";
        return false;
    }
    config.refresh_minutes = static_cast<uint16_t>(refresh_value);
    config.valid = true;
    return true;
}

std::string WeatherConfig::serialize(const Data &config)
{
    boost::json::object location;
    if (config.location_mode == LocationMode::Coordinates) {
        location["mode"] = "coordinates";
        location["latitude"] = config.latitude;
        location["longitude"] = config.longitude;
    } else {
        location["mode"] = "city";
        location["city"] = config.city;
        location["country"] = config.country;
    }
    location["display_name"] = config.display_name;

    boost::json::object root;
    root["version"] = 1;
    root["provider"] = "openweathermap";
    root["api_key"] = config.api_key;
    root["location"] = std::move(location);
    root["language"] = config.language;
    root["refresh_minutes"] = config.refresh_minutes;
    return boost::json::serialize(root);
}
