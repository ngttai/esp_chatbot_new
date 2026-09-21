#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "brookesia/hal_interface.hpp"
#include "brookesia/hal_interface/interfaces/network/http_client.hpp"

extern "C" {
#include "flip_clock/weather_source.h"
}

namespace host_sim {

class WeatherAdapter {
public:
    enum class State {
        Mock,
        Loading,
        Live,
        Cached,
        Offline,
        Stopped,
    };

    WeatherAdapter() = default;
    ~WeatherAdapter();

    WeatherAdapter(const WeatherAdapter &) = delete;
    WeatherAdapter &operator=(const WeatherAdapter &) = delete;

    bool start(bool allow_real_backend = true);
    void stop();

    weather_data_t snapshot() const;
    State state() const;
    std::string last_error() const;
    bool wait_for_state(State expected, std::chrono::milliseconds timeout);

    bool apply_forecast_payload_for_test(const std::string &payload);
    void mark_offline_for_test(std::string error);

private:
    void worker_loop();
    bool fetch_once(std::string &payload, std::string &error);
    bool apply_forecast_payload(const std::string &payload, std::string &error);
    void set_fetch_failure(std::string error);

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    weather_data_t data_{};
    State state_ = State::Stopped;
    std::string last_error_;
    std::string api_key_;
    std::string latitude_;
    std::string longitude_;
    bool has_live_cache_ = false;
    bool stop_requested_ = false;
    std::thread worker_;
    esp_brookesia::hal::InterfaceHandle<esp_brookesia::hal::network::HttpClientIface> http_;
};

bool sync_sntp_once(uint32_t timeout_ms, std::string &error);

} // namespace host_sim
