/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "weather_service.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>

#include "boost/json.hpp"
#include "brookesia/hal_adaptor.hpp"
#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/interfaces/network/http_client.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/idf_additions.h"

#include "weather_config.hpp"

namespace {

constexpr char TAG[] = "weather_service";
constexpr char FORECAST_ENDPOINT[] = "https://api.openweathermap.org/data/2.5/forecast";
constexpr char GEOCODING_ENDPOINT[] = "https://api.openweathermap.org/geo/1.0/direct";
constexpr uint32_t HTTP_TIMEOUT_MS = 10000;
constexpr size_t MAX_FORECAST_SIZE = 128 * 1024;
constexpr size_t MAX_GEOCODING_SIZE = 8 * 1024;
constexpr uint32_t NETWORK_POLL_SECONDS = 5;
constexpr uint32_t INITIAL_RETRY_SECONDS = 15;
constexpr uint32_t MAX_RETRY_SECONDS = 5 * 60;

using HttpClient = esp_brookesia::hal::network::HttpClientIface;

void log_heap(const char *stage)
{
    constexpr uint32_t INTERNAL_CAPS = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    ESP_LOGI(TAG,
             "Heap %s: internal_free=%zu, internal_largest=%zu, internal_min=%zu, psram_free=%zu",
             stage,
             heap_caps_get_free_size(INTERNAL_CAPS),
             heap_caps_get_largest_free_block(INTERNAL_CAPS),
             heap_caps_get_minimum_free_size(INTERNAL_CAPS),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void copy_text(char *destination, size_t size, std::string_view value)
{
    if (size == 0) return;
    const size_t count = std::min(size - 1, value.size());
    std::copy_n(value.data(), count, destination);
    destination[count] = '\0';
}

const boost::json::value *member(const boost::json::object &object, std::string_view key)
{
    const auto iterator = object.find(key);
    return iterator == object.end() ? nullptr : &iterator->value();
}

const boost::json::object *object_member(const boost::json::object &object, std::string_view key)
{
    const auto *value = member(object, key);
    return value == nullptr ? nullptr : value->if_object();
}

const boost::json::array *array_member(const boost::json::object &object, std::string_view key)
{
    const auto *value = member(object, key);
    return value == nullptr ? nullptr : value->if_array();
}

bool number_value(const boost::json::value *value, double &output)
{
    if (value == nullptr) return false;
    if (value->is_double()) output = value->as_double();
    else if (value->is_int64()) output = static_cast<double>(value->as_int64());
    else if (value->is_uint64()) output = static_cast<double>(value->as_uint64());
    else return false;
    return std::isfinite(output);
}

bool integer_value(const boost::json::value *value, int64_t &output)
{
    double number = 0;
    if (!number_value(value, number)) return false;
    output = static_cast<int64_t>(number);
    return true;
}

std::string string_value(const boost::json::value *value)
{
    if (value == nullptr || !value->is_string()) return {};
    const auto &text = value->as_string();
    return std::string(text.data(), text.size());
}

std::string url_encode(std::string_view value)
{
    constexpr char HEX[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3);
    for (const unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            result.push_back(static_cast<char>(character));
        } else {
            result.push_back('%');
            result.push_back(HEX[character >> 4]);
            result.push_back(HEX[character & 0x0f]);
        }
    }
    return result;
}

weather_icon_type_t map_weather_icon(int id, std::string_view code)
{
    const bool night = !code.empty() && code.back() == 'n';
    if (id >= 200 && id < 300) return WEATHER_ICON_STORM;
    if (id >= 300 && id < 600) return WEATHER_ICON_RAIN;
    if (id >= 600 && id < 700) return WEATHER_ICON_SNOW;
    if (id >= 700 && id < 800) return WEATHER_ICON_FOG;
    if (id == 800) return night ? WEATHER_ICON_CLEAR_NIGHT : WEATHER_ICON_SUNNY;
    if (id == 801 || id == 802) {
        return night ? WEATHER_ICON_PARTLY_CLOUDY_NIGHT : WEATHER_ICON_PARTLY_CLOUDY;
    }
    return WEATHER_ICON_CLOUDY;
}

bool weather_entry(const boost::json::object &entry, int &id, std::string &description,
                   std::string &icon_code)
{
    const auto *items = array_member(entry, "weather");
    if (items == nullptr || items->empty() || !items->front().is_object()) return false;
    const auto &weather = items->front().as_object();
    int64_t parsed_id = 0;
    if (!integer_value(member(weather, "id"), parsed_id)) return false;
    id = static_cast<int>(parsed_id);
    description = string_value(member(weather, "description"));
    icon_code = string_value(member(weather, "icon"));
    if (!description.empty()) {
        description.front() = static_cast<char>(
            std::toupper(static_cast<unsigned char>(description.front()))
        );
    }
    return true;
}

std::string format_clock(int64_t timestamp, int64_t timezone_offset)
{
    const std::time_t shifted = static_cast<std::time_t>(timestamp + timezone_offset);
    std::tm value{};
    gmtime_r(&shifted, &value);
    char buffer[6]{};
    std::strftime(buffer, sizeof(buffer), "%H:%M", &value);
    return buffer;
}

std::string format_day_key(int64_t timestamp, int64_t timezone_offset)
{
    const std::time_t shifted = static_cast<std::time_t>(timestamp + timezone_offset);
    std::tm value{};
    gmtime_r(&shifted, &value);
    char buffer[11]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &value);
    return buffer;
}

std::string format_day_name(int64_t timestamp, int64_t timezone_offset)
{
    const std::time_t shifted = static_cast<std::time_t>(timestamp + timezone_offset);
    std::tm value{};
    gmtime_r(&shifted, &value);
    char buffer[4]{};
    std::strftime(buffer, sizeof(buffer), "%a", &value);
    return buffer;
}

std::string wind_direction(double degrees)
{
    static constexpr std::array<std::string_view, 8> DIRECTIONS{
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    const int index = static_cast<int>(std::lround(degrees / 45.0)) & 7;
    return std::string(DIRECTIONS[static_cast<size_t>(index)]);
}

bool wifi_connected()
{
    wifi_ap_record_t access_point{};
    return esp_wifi_sta_get_ap_info(&access_point) == ESP_OK;
}

bool http_get(const std::string &url, size_t maximum_size, std::string &payload,
              std::string &error)
{
    auto http = esp_brookesia::hal::acquire_interface<HttpClient>(
        esp_brookesia::hal::NetworkDevice::HTTP_CLIENT_IFACE_NAME
    );
    if (!http) {
        error = "HTTP client is unavailable";
        return false;
    }
    auto transaction = http->create_transaction();
    if (!transaction) {
        error = "Could not create HTTP transaction";
        return false;
    }

    HttpClient::Request request{
        .url = url,
        .method = HttpClient::Method::Get,
        .headers = {},
        .body = {},
        .timeout_ms = HTTP_TIMEOUT_MS,
        .tls_verify = HttpClient::TlsVerifyMode::Verify,
        .cert_pem = {},
        .use_crt_bundle = true,
        .download_path = {},
        .max_response_size = static_cast<uint32_t>(maximum_size),
        .max_file_size = 0,
        .retry_count = 0,
        .retry_on_status_codes = {},
    };
    HttpClient::Response response;
    uint32_t content_length = 0;
    const auto result = transaction->open(request, response, content_length);
    request.url.clear();
    if (result != HttpClient::ErrorCode::Ok) {
        error = response.error_message.empty() ? "OpenWeather request failed" : response.error_message;
        return false;
    }
    if (response.status_code != 200) {
        error = "OpenWeather returned HTTP " + std::to_string(response.status_code);
        return false;
    }
    if (content_length > maximum_size) {
        error = "OpenWeather response exceeds the size limit";
        return false;
    }

    payload.clear();
    payload.reserve(content_length);
    std::array<char, 2048> buffer{};
    while (payload.size() <= maximum_size) {
        std::string read_error;
        const int count = transaction->read(buffer.data(), buffer.size(), read_error);
        if (count < 0) {
            error = read_error.empty() ? "OpenWeather response read failed" : read_error;
            return false;
        }
        if (count == 0) break;
        payload.append(buffer.data(), static_cast<size_t>(count));
    }
    if (payload.size() > maximum_size) {
        error = "OpenWeather response exceeds the size limit";
        return false;
    }
    return true;
}

bool resolve_city(const WeatherConfig::Data &config, double &latitude, double &longitude,
                  std::string &error)
{
    std::ostringstream url;
    url << GEOCODING_ENDPOINT << "?q=" << url_encode(config.city) << "%2C"
        << url_encode(config.country) << "&limit=1&appid=" << config.api_key;
    std::string payload;
    if (!http_get(url.str(), MAX_GEOCODING_SIZE, payload, error)) return false;

    boost::system::error_code parse_error;
    const boost::json::value root = boost::json::parse(payload, parse_error);
    if (parse_error || !root.is_array() || root.as_array().empty() ||
        !root.as_array().front().is_object()) {
        error = "City was not found by OpenWeather";
        return false;
    }
    const auto &location = root.as_array().front().as_object();
    if (!number_value(member(location, "lat"), latitude) ||
        !number_value(member(location, "lon"), longitude) ||
        latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180) {
        error = "OpenWeather returned invalid city coordinates";
        return false;
    }
    return true;
}

bool parse_forecast(const std::string &payload, std::string_view display_name,
                    weather_data_t &parsed, std::string &error)
{
    boost::system::error_code parse_error;
    const boost::json::value root_value = boost::json::parse(payload, parse_error);
    if (parse_error || !root_value.is_object()) {
        error = "OpenWeather response is not valid JSON";
        return false;
    }
    const auto &root = root_value.as_object();
    const auto *city = object_member(root, "city");
    const auto *entries = array_member(root, "list");
    if (city == nullptr || entries == nullptr || entries->empty() ||
        !entries->front().is_object()) {
        error = "OpenWeather response is missing forecast data";
        return false;
    }

    int64_t timezone_offset = 0;
    (void)integer_value(member(*city, "timezone"), timezone_offset);
    const auto &current = entries->front().as_object();
    const auto *current_main = object_member(current, "main");
    const auto *current_wind = object_member(current, "wind");
    int64_t current_timestamp = 0;
    double temperature = 0;
    double high = 0;
    double low = 0;
    if (current_main == nullptr || !integer_value(member(current, "dt"), current_timestamp) ||
        !number_value(member(*current_main, "temp"), temperature) ||
        !number_value(member(*current_main, "temp_max"), high) ||
        !number_value(member(*current_main, "temp_min"), low)) {
        error = "OpenWeather response has invalid current conditions";
        return false;
    }
    int current_id = 0;
    std::string description;
    std::string icon_code;
    if (!weather_entry(current, current_id, description, icon_code)) {
        error = "OpenWeather response has invalid weather details";
        return false;
    }

    parsed = {};
    parsed.available = true;
    const std::string api_city = string_value(member(*city, "name"));
    copy_text(parsed.city, sizeof(parsed.city),
              display_name.empty() ? std::string_view(api_city) : display_name);
    if (parsed.city[0] == '\0') copy_text(parsed.city, sizeof(parsed.city), "Current location");
    copy_text(parsed.condition, sizeof(parsed.condition), description);
    parsed.icon = map_weather_icon(current_id, icon_code);
    parsed.current_temp = static_cast<int>(std::lround(temperature));
    parsed.hi = static_cast<int>(std::ceil(high));
    parsed.lo = static_cast<int>(std::floor(low));

    int64_t sunrise = 0;
    int64_t sunset = 0;
    copy_text(parsed.sunrise, sizeof(parsed.sunrise),
              integer_value(member(*city, "sunrise"), sunrise) ?
              format_clock(sunrise, timezone_offset) : "--:--");
    copy_text(parsed.sunset, sizeof(parsed.sunset),
              integer_value(member(*city, "sunset"), sunset) ?
              format_clock(sunset, timezone_offset) : "--:--");

    double speed = 0;
    double degrees = 0;
    if (current_wind != nullptr && number_value(member(*current_wind, "speed"), speed)) {
        parsed.wind_kmh = static_cast<int>(std::lround(speed * 3.6));
    }
    copy_text(parsed.wind_dir, sizeof(parsed.wind_dir),
              current_wind != nullptr && number_value(member(*current_wind, "deg"), degrees) ?
              wind_direction(degrees) : "--");

    struct DaySummary {
        int64_t timestamp = 0;
        double high = -std::numeric_limits<double>::infinity();
        double low = std::numeric_limits<double>::infinity();
        weather_icon_type_t icon = WEATHER_ICON_CLOUDY;
        int best_hour_distance = std::numeric_limits<int>::max();
    };
    std::map<std::string, DaySummary> days;
    const std::string current_day = format_day_key(current_timestamp, timezone_offset);
    for (const auto &value : *entries) {
        if (!value.is_object()) continue;
        const auto &entry = value.as_object();
        const auto *main = object_member(entry, "main");
        int64_t timestamp = 0;
        double entry_high = 0;
        double entry_low = 0;
        if (main == nullptr || !integer_value(member(entry, "dt"), timestamp) ||
            !number_value(member(*main, "temp_max"), entry_high) ||
            !number_value(member(*main, "temp_min"), entry_low)) continue;
        const std::string day_key = format_day_key(timestamp, timezone_offset);
        if (day_key == current_day) continue;
        auto &day = days[day_key];
        day.timestamp = timestamp;
        day.high = std::max(day.high, entry_high);
        day.low = std::min(day.low, entry_low);
        const std::time_t shifted = static_cast<std::time_t>(timestamp + timezone_offset);
        std::tm local{};
        gmtime_r(&shifted, &local);
        const int distance = std::abs(local.tm_hour - 12);
        int id = 0;
        std::string ignored;
        std::string daily_icon;
        if (distance < day.best_hour_distance && weather_entry(entry, id, ignored, daily_icon)) {
            day.icon = map_weather_icon(id, daily_icon);
            day.best_hour_distance = distance;
        }
    }
    int forecast_index = 0;
    for (const auto &[key, day] : days) {
        (void)key;
        if (forecast_index >= WEATHER_FORECAST_DAYS) break;
        auto &forecast = parsed.forecast[forecast_index++];
        copy_text(forecast.dayname, sizeof(forecast.dayname),
                  format_day_name(day.timestamp, timezone_offset));
        forecast.icon = day.icon;
        forecast.hi = static_cast<int>(std::ceil(day.high));
        forecast.lo = static_cast<int>(std::floor(day.low));
    }
    if (forecast_index < WEATHER_FORECAST_DAYS) {
        error = "OpenWeather returned fewer than four forecast days";
        return false;
    }
    return true;
}

} // namespace

bool WeatherService::init()
{
    const auto config = WeatherConfig::get_instance().get();
    std::lock_guard lock(mutex_);
    data_ = {};
    data_.icon = WEATHER_ICON_CLOUDY;
    if (!config.valid) {
        copy_text(data_.city, sizeof(data_.city), "Weather");
        copy_text(data_.condition, sizeof(data_.condition), "Not configured");
        state_ = State::NotConfigured;
        return true;
    }
    copy_text(data_.city, sizeof(data_.city), config.display_name);
    copy_text(data_.condition, sizeof(data_.condition), "Waiting for Wi-Fi");
    state_ = State::WaitingForNetwork;
    return true;
}

bool WeatherService::start()
{
    if (!WeatherConfig::get_instance().get().valid) return true;
    if (task_ != nullptr) return true;
    if (xTaskCreateWithCaps(
            task_entry, "weather", 12 * 1024, this, 3, &task_,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        ) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create weather task");
        return false;
    }
    return true;
}

weather_data_t WeatherService::snapshot() const
{
    std::lock_guard lock(mutex_);
    return data_;
}

WeatherService::State WeatherService::state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

void WeatherService::task_entry(void *context)
{
    static_cast<WeatherService *>(context)->run();
}

void WeatherService::run()
{
    uint32_t retry_seconds = INITIAL_RETRY_SECONDS;
    while (true) {
        if (!wifi_connected()) {
            set_pending(State::WaitingForNetwork, "Waiting for Wi-Fi");
            vTaskDelay(pdMS_TO_TICKS(NETWORK_POLL_SECONDS * 1000));
            continue;
        }

        set_pending(State::Loading, "Updating...");
        std::string error;
        if (refresh(error)) {
            retry_seconds = INITIAL_RETRY_SECONDS;
            const auto minutes = WeatherConfig::get_instance().get().refresh_minutes;
            vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(minutes) * 60 * 1000));
        } else {
            set_failure(error);
            vTaskDelay(pdMS_TO_TICKS(retry_seconds * 1000));
            retry_seconds = std::min(retry_seconds * 2, MAX_RETRY_SECONDS);
        }
    }
}

bool WeatherService::refresh(std::string &error)
{
    log_heap("before refresh");
    auto config = WeatherConfig::get_instance().get();
    if (!config.valid) {
        error = "Weather is not configured";
        return false;
    }
    if (config.location_mode == WeatherConfig::LocationMode::City) {
        double latitude = 0;
        double longitude = 0;
        if (!resolve_city(config, latitude, longitude, error)) return false;
        if (!WeatherConfig::get_instance().store_resolved_coordinates(latitude, longitude)) {
            error = "Could not persist resolved city coordinates";
            return false;
        }
        config = WeatherConfig::get_instance().get();
        ESP_LOGI(TAG, "Resolved configured city and saved its coordinates");
    }

    std::ostringstream url;
    url.precision(8);
    url << FORECAST_ENDPOINT << "?lat=" << config.latitude << "&lon=" << config.longitude
        << "&appid=" << config.api_key << "&units=metric&lang=" << url_encode(config.language);
    std::string payload;
    if (!http_get(url.str(), MAX_FORECAST_SIZE, payload, error)) return false;
    log_heap("after HTTP");

    weather_data_t parsed{};
    if (!parse_forecast(payload, config.display_name, parsed, error)) return false;
    log_heap("after JSON");
    {
        std::lock_guard lock(mutex_);
        data_ = parsed;
        state_ = State::Live;
        has_live_data_ = true;
    }
    ESP_LOGI(TAG, "Weather updated for %s", parsed.city);
    return true;
}

void WeatherService::set_pending(State state, const char *message)
{
    std::lock_guard lock(mutex_);
    state_ = has_live_data_ ? State::Cached : state;
    if (!has_live_data_) {
        const auto config = WeatherConfig::get_instance().get();
        data_ = {};
        data_.icon = WEATHER_ICON_CLOUDY;
        copy_text(data_.city, sizeof(data_.city),
                  config.display_name.empty() ? std::string_view("Weather") :
                  std::string_view(config.display_name));
        copy_text(data_.condition, sizeof(data_.condition), message);
    }
}

void WeatherService::set_failure(const std::string &error)
{
    {
        std::lock_guard lock(mutex_);
        state_ = has_live_data_ ? State::Cached : State::Offline;
        if (!has_live_data_) {
            copy_text(data_.condition, sizeof(data_.condition), "Unavailable");
        }
    }
    ESP_LOGW(TAG, "Weather update failed: %s", error.c_str());
}

extern "C" bool weather_service_get(weather_data_t *output)
{
    if (output == nullptr) return false;
    *output = WeatherService::get_instance().snapshot();
    return true;
}
