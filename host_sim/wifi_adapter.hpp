#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "brookesia/hal_interface/interfaces/network/connectivity.hpp"
#include "brookesia/hal_interface/interfaces/wifi/basic.hpp"
#include "brookesia/hal_interface/interfaces/wifi/softap.hpp"
#include "brookesia/hal_interface/interfaces/wifi/station.hpp"
#include "lvgl.h"

namespace host_sim {

class WifiAdapter {
public:
    enum class ConnectionResult {
        Offline,
        Connected,
        Disconnected,
        AuthenticationFailed,
        TimedOut,
        NetworkNotFound,
        BackendError,
    };

    bool start();
    void stop();

    bool set_enabled(bool enabled);
    bool scan();
    ConnectionResult connect(const std::string &ssid, const std::string &password);
    ConnectionResult retry();
    bool disconnect();
    bool start_soft_ap();
    void stop_soft_ap();

    bool enabled() const { return enabled_; }
    bool connected() const { return connected_; }
    bool soft_ap_started() const { return soft_ap_started_; }
    bool real_backend_enabled() const { return !deterministic_stub(); }
    ConnectionResult last_result() const { return last_result_; }
    const std::vector<esp_brookesia::hal::wifi::ScanApInfo> &scan_results() const
    {
        return scan_results_;
    }

private:
    static void timer_callback(lv_timer_t *timer);
    static void network_selected_callback(lv_event_t *event);

    void poll();
    void attach_ui_handlers();
    void update_real_scan_ui();
    void refresh_real_status();
    void update_ui_status();
    ConnectionResult connect_internal(
        const std::string &ssid, const std::string &password, bool retrying
    );
    bool connect_backend(const std::string &ssid, const std::string &password);
    bool deterministic_stub() const;

    esp_brookesia::hal::InterfaceHandle<esp_brookesia::hal::wifi::BasicIface> basic_;
    esp_brookesia::hal::InterfaceHandle<esp_brookesia::hal::wifi::StationIface> station_;
    esp_brookesia::hal::InterfaceHandle<esp_brookesia::hal::wifi::SoftApIface> soft_ap_;
    esp_brookesia::hal::InterfaceHandle<esp_brookesia::hal::network::ConnectivityIface> connectivity_;
    lv_timer_t *timer_ = nullptr;
    lv_obj_t *password_field_ = nullptr;
    lv_obj_t *status_label_ = nullptr;
    lv_obj_t *connected_name_label_ = nullptr;
    std::array<lv_obj_t *, 3> network_rows_{};
    std::array<lv_obj_t *, 3> network_labels_{};
    std::vector<esp_brookesia::hal::wifi::ScanApInfo> scan_results_;
    std::string selected_ssid_;
    std::string pending_ssid_;
    std::string pending_password_;
    bool enabled_ = false;
    bool connected_ = false;
    bool soft_ap_started_ = false;
    bool handlers_attached_ = false;
    bool scan_ui_dirty_ = true;
    bool open_ap_pending_ = false;
    bool password_screen_was_active_ = false;
    int retry_countdown_ms_ = 0;
    int open_ap_countdown_ms_ = 0;
    int status_poll_countdown_ms_ = 0;
    ConnectionResult last_result_ = ConnectionResult::Offline;
};

} // namespace host_sim
