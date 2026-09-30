#include "weather_adapter.hpp"

#include "host_capabilities_config.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>

#include "boost/json.hpp"
#include "brookesia/hal_linux.hpp"

extern "C" {
#include "flip_clock/mock_weather.h"
}

namespace host_sim {
namespace {

constexpr std::chrono::minutes WEATHER_REFRESH_INTERVAL{10};
constexpr uint32_t WEATHER_HTTP_TIMEOUT_MS = 5000;
constexpr size_t WEATHER_MAX_RESPONSE_SIZE = 1024U * 1024U;
std::atomic<WeatherAdapter *> active_adapter{nullptr};

void copy_text(char *destination, size_t size, std::string_view value)
{
    if (size == 0) return;
    const size_t count = std::min(size - 1, value.size());
    std::copy_n(value.data(), count, destination);
    destination[count] = '\0';
}

weather_data_t mock_snapshot()
{
    const mock_weather_t *mock = mock_weather_get();
    weather_data_t output{};
    output.available = true;
    copy_text(output.city, sizeof(output.city), mock->city);
    copy_text(output.condition, sizeof(output.condition), mock->condition);
    output.icon = mock->icon;
    output.current_temp = mock->current_temp;
    output.hi = mock->hi;
    output.lo = mock->lo;
    copy_text(output.sunrise, sizeof(output.sunrise), mock->sunrise);
    copy_text(output.sunset, sizeof(output.sunset), mock->sunset);
    output.wind_kmh = mock->wind_kmh;
    copy_text(output.wind_dir, sizeof(output.wind_dir), mock->wind_dir);
    for (int index = 0; index < WEATHER_FORECAST_DAYS; ++index) {
        copy_text(output.forecast[index].dayname, sizeof(output.forecast[index].dayname),
                  mock->forecast[index].dayname);
        output.forecast[index].icon = mock->forecast[index].icon;
        output.forecast[index].hi = mock->forecast[index].hi;
        output.forecast[index].lo = mock->forecast[index].lo;
    }
    return output;
}

weather_data_t offline_snapshot()
{
    weather_data_t output{};
    copy_text(output.city, sizeof(output.city), "Weather Offline");
    copy_text(output.condition, sizeof(output.condition), "Unavailable");
    output.icon = WEATHER_ICON_CLOUDY;
    copy_text(output.sunrise, sizeof(output.sunrise), "--:--");
    copy_text(output.sunset, sizeof(output.sunset), "--:--");
    copy_text(output.wind_dir, sizeof(output.wind_dir), "--");
    return output;
}

const boost::json::value *member(const boost::json::object &object, std::string_view key)
{
    auto iterator = object.find(key);
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
    return true;
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

weather_icon_type_t map_weather_icon(int id, std::string_view code)
{
    const bool night = code.ends_with('n');
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
    static constexpr std::array<std::string_view, 8> directions{
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    const int index = static_cast<int>(std::lround(degrees / 45.0)) & 7;
    return std::string(directions[static_cast<size_t>(index)]);
}

std::string environment_or(const char *name, std::string fallback)
{
    const char *value = std::getenv(name);
    return value == nullptr || *value == '\0' ? std::move(fallback) : std::string(value);
}

bool valid_coordinate(const std::string &text, double minimum, double maximum)
{
    char *end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && std::isfinite(value) &&
           value >= minimum && value <= maximum;
}

} // namespace

WeatherAdapter::~WeatherAdapter()
{
    stop();
}

bool WeatherAdapter::start(bool allow_real_backend)
{
    stop();
    active_adapter.store(this);
    if (!allow_real_backend || std::string_view(HOST_SIM_WEATHER_BACKEND) == "mock") {
        std::lock_guard lock(mutex_);
        data_ = mock_snapshot();
        state_ = State::Mock;
        return true;
    }

    api_key_ = environment_or("OPENWEATHER_API_KEY", "");
    latitude_ = environment_or("HOST_SIM_WEATHER_LATITUDE", "10.8231");
    longitude_ = environment_or("HOST_SIM_WEATHER_LONGITUDE", "106.6297");
    {
        std::lock_guard lock(mutex_);
        data_ = offline_snapshot();
        state_ = State::Loading;
        stop_requested_ = false;
        has_live_cache_ = false;
        last_error_.clear();
    }
    if (api_key_.empty()) {
        set_fetch_failure("OPENWEATHER_API_KEY is not set");
        return true;
    }
    if (!valid_coordinate(latitude_, -90.0, 90.0) ||
        !valid_coordinate(longitude_, -180.0, 180.0)) {
        set_fetch_failure("Weather latitude/longitude is invalid");
        return true;
    }
    http_ = esp_brookesia::hal::acquire_interface<
        esp_brookesia::hal::network::HttpClientIface
    >(esp_brookesia::hal::NetworkLinuxDevice::HTTP_CLIENT_IFACE_NAME);
    if (!http_) {
        set_fetch_failure("Brookesia HTTP client is unavailable");
        return true;
    }
    std::string payload;
    std::string error;
    if (fetch_once(payload, error)) {
        if (!apply_forecast_payload(payload, error)) set_fetch_failure(std::move(error));
    } else {
        set_fetch_failure(std::move(error));
    }
    worker_ = std::thread(&WeatherAdapter::worker_loop, this);
    return true;
}

void WeatherAdapter::stop()
{
    {
        std::lock_guard lock(mutex_);
        stop_requested_ = true;
    }
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
    http_.reset();
    if (active_adapter.load() == this) active_adapter.store(nullptr);
    std::lock_guard lock(mutex_);
    state_ = State::Stopped;
    api_key_.clear();
}

weather_data_t WeatherAdapter::snapshot() const
{
    std::lock_guard lock(mutex_);
    return data_;
}

WeatherAdapter::State WeatherAdapter::state() const
{
    std::lock_guard lock(mutex_);
    return state_;
}

std::string WeatherAdapter::last_error() const
{
    std::lock_guard lock(mutex_);
    return last_error_;
}

bool WeatherAdapter::wait_for_state(State expected, std::chrono::milliseconds timeout)
{
    std::unique_lock lock(mutex_);
    return condition_.wait_for(lock, timeout, [this, expected]() {
        return state_ == expected || (expected == State::Live && state_ == State::Offline);
    }) && state_ == expected;
}

void WeatherAdapter::worker_loop()
{
    while (true) {
        {
            std::unique_lock lock(mutex_);
            if (condition_.wait_for(lock, WEATHER_REFRESH_INTERVAL, [this]() {
                    return stop_requested_;
                })) {
                break;
            }
        }
        std::string payload;
        std::string error;
        if (fetch_once(payload, error)) {
            if (!apply_forecast_payload(payload, error)) set_fetch_failure(std::move(error));
        } else {
            set_fetch_failure(std::move(error));
        }

    }
}

bool WeatherAdapter::fetch_once(std::string &payload, std::string &error)
{
    auto transaction = http_ ? http_->create_transaction() : nullptr;
    if (!transaction) {
        error = "Could not create Brookesia HTTP transaction";
        return false;
    }
    std::ostringstream url;
    url << "https://api.openweathermap.org/data/2.5/forecast?lat=" << latitude_
        << "&lon=" << longitude_ << "&appid=" << api_key_ << "&units=metric";
    esp_brookesia::hal::network::HttpClientIface::Request request{
        .url = url.str(),
        .method = esp_brookesia::hal::network::HttpClientIface::Method::Get,
        .timeout_ms = WEATHER_HTTP_TIMEOUT_MS,
        .tls_verify = esp_brookesia::hal::network::HttpClientIface::TlsVerifyMode::Verify,
        .max_response_size = WEATHER_MAX_RESPONSE_SIZE,
    };
    esp_brookesia::hal::network::HttpClientIface::Response response;
    uint32_t content_length = 0;
    const auto result = transaction->open(request, response, content_length);
    request.url.clear();
    if (result != esp_brookesia::hal::network::HttpClientIface::ErrorCode::Ok) {
        error = "OpenWeather request failed: " + response.error_message;
        return false;
    }
    if (response.status_code != 200) {
        error = "OpenWeather returned HTTP " + std::to_string(response.status_code);
        return false;
    }
    if (content_length > WEATHER_MAX_RESPONSE_SIZE) {
        error = "OpenWeather response exceeds the size limit";
        return false;
    }
    payload.clear();
    payload.reserve(content_length);
    std::array<char, 4096> buffer{};
    while (payload.size() <= WEATHER_MAX_RESPONSE_SIZE) {
        std::string read_error;
        const int read = transaction->read(buffer.data(), buffer.size(), read_error);
        if (read < 0) {
            error = "OpenWeather response read failed: " + read_error;
            return false;
        }
        if (read == 0) break;
        payload.append(buffer.data(), static_cast<size_t>(read));
    }
    if (payload.size() > WEATHER_MAX_RESPONSE_SIZE) {
        error = "OpenWeather response exceeds the size limit";
        return false;
    }
    return true;
}

bool WeatherAdapter::apply_forecast_payload(const std::string &payload, std::string &error)
{
    boost::system::error_code parse_error;
    boost::json::value root_value = boost::json::parse(payload, parse_error);
    if (parse_error || !root_value.is_object()) {
        error = "OpenWeather response is not valid JSON";
        return false;
    }
    const auto &root = root_value.as_object();
    const auto *city = object_member(root, "city");
    const auto *entries = array_member(root, "list");
    if (city == nullptr || entries == nullptr || entries->empty() ||
        !entries->front().is_object()) {
        error = "OpenWeather response is missing city or forecast data";
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
    std::string current_icon_code;
    if (!weather_entry(current, current_id, description, current_icon_code)) {
        error = "OpenWeather response has invalid weather details";
        return false;
    }

    weather_data_t parsed{};
    parsed.available = true;
    std::string city_name = string_value(member(*city, "name"));
    if (city_name.empty()) city_name = "Current location";
    copy_text(parsed.city, sizeof(parsed.city), city_name);
    copy_text(parsed.condition, sizeof(parsed.condition), description);
    parsed.icon = map_weather_icon(current_id, current_icon_code);
    parsed.current_temp = static_cast<int>(std::lround(temperature));
    parsed.hi = static_cast<int>(std::ceil(high));
    parsed.lo = static_cast<int>(std::floor(low));

    int64_t sunrise = 0;
    int64_t sunset = 0;
    if (integer_value(member(*city, "sunrise"), sunrise)) {
        copy_text(parsed.sunrise, sizeof(parsed.sunrise), format_clock(sunrise, timezone_offset));
    } else copy_text(parsed.sunrise, sizeof(parsed.sunrise), "--:--");
    if (integer_value(member(*city, "sunset"), sunset)) {
        copy_text(parsed.sunset, sizeof(parsed.sunset), format_clock(sunset, timezone_offset));
    } else copy_text(parsed.sunset, sizeof(parsed.sunset), "--:--");

    double wind_speed = 0;
    double wind_degrees = 0;
    if (current_wind != nullptr && number_value(member(*current_wind, "speed"), wind_speed)) {
        parsed.wind_kmh = static_cast<int>(std::lround(wind_speed * 3.6));
    }
    if (current_wind != nullptr && number_value(member(*current_wind, "deg"), wind_degrees)) {
        copy_text(parsed.wind_dir, sizeof(parsed.wind_dir), wind_direction(wind_degrees));
    } else copy_text(parsed.wind_dir, sizeof(parsed.wind_dir), "--");

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
        const std::string key = format_day_key(timestamp, timezone_offset);
        if (key == current_day) continue;
        auto &day = days[key];
        day.timestamp = timestamp;
        day.high = std::max(day.high, entry_high);
        day.low = std::min(day.low, entry_low);
        const std::time_t shifted = static_cast<std::time_t>(timestamp + timezone_offset);
        std::tm local{};
        gmtime_r(&shifted, &local);
        const int distance = std::abs(local.tm_hour - 12);
        int id = 0;
        std::string ignored_description;
        std::string icon_code;
        if (distance < day.best_hour_distance &&
            weather_entry(entry, id, ignored_description, icon_code)) {
            day.icon = map_weather_icon(id, icon_code);
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
        error = "OpenWeather response has fewer than four forecast days";
        return false;
    }

    {
        std::lock_guard lock(mutex_);
        data_ = parsed;
        state_ = State::Live;
        has_live_cache_ = true;
        last_error_.clear();
    }
    condition_.notify_all();
    return true;
}

bool WeatherAdapter::apply_forecast_payload_for_test(const std::string &payload)
{
    std::string error;
    const bool result = apply_forecast_payload(payload, error);
    if (!result) set_fetch_failure(std::move(error));
    return result;
}

void WeatherAdapter::set_fetch_failure(std::string error)
{
    {
        std::lock_guard lock(mutex_);
        last_error_ = std::move(error);
        state_ = has_live_cache_ ? State::Cached : State::Offline;
        if (!has_live_cache_) data_ = offline_snapshot();
    }
    condition_.notify_all();
}

void WeatherAdapter::mark_offline_for_test(std::string error)
{
    set_fetch_failure(std::move(error));
}

bool sync_sntp_once(uint32_t timeout_ms, std::string &error)
{
    auto sntp = esp_brookesia::hal::acquire_interface<
        esp_brookesia::hal::network::SntpClientIface
    >(esp_brookesia::hal::NetworkLinuxDevice::SNTP_CLIENT_IFACE_NAME);
    if (!sntp) {
        error = "Brookesia SNTP client is unavailable";
        return false;
    }
    const std::string server = environment_or("HOST_SIM_SNTP_SERVER", "pool.ntp.org");
    const std::string timezone = environment_or("HOST_SIM_TIMEZONE",
        environment_or("TZ", "UTC"));
    if (!sntp->init({.servers = {server}, .timezone = timezone, .use_dhcp = false})) {
        error = sntp->get_last_error();
        return false;
    }
    if (!sntp->start()) {
        error = sntp->get_last_error();
        sntp->deinit();
        return false;
    }
    const bool synced = sntp->wait_time_sync(timeout_ms);
    if (!synced) error = sntp->get_last_error();
    sntp->stop();
    sntp->deinit();
    return synced;
}

} // namespace host_sim

extern "C" void weather_client_get(weather_data_t *output)
{
    if (output == nullptr) return;
    auto *adapter = host_sim::active_adapter.load();
    *output = adapter == nullptr ? host_sim::mock_snapshot() : adapter->snapshot();
}
