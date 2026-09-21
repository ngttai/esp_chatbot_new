#include "wifi_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <span>
#include <string_view>

#include "brookesia/hal_linux/wifi/device.hpp"
#include "host_capabilities_config.hpp"

extern "C" {
#include "speaker_ui.h"
}

namespace host_sim {
namespace {

using namespace esp_brookesia;
constexpr uint32_t POLL_PERIOD_MS = 50;
constexpr int RETRY_DELAY_MS = 1000;
constexpr std::string_view LOCKED_PASSWORD = "esp123456";
constexpr std::string_view TIMEOUT_PASSWORD = "ntt123456";

lv_obj_t *find_label(lv_obj_t *object, std::string_view text)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_label_class)) {
        const char *value = lv_label_get_text(object);
        if (value != nullptr && text == value) return object;
    }
    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *match = find_label(lv_obj_get_child(object, index), text)) return match;
    }
    return nullptr;
}

lv_obj_t *find_first_textarea(lv_obj_t *object)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_textarea_class)) return object;
    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *match = find_first_textarea(lv_obj_get_child(object, index))) return match;
    }
    return nullptr;
}

hal::wifi::ScanApInfo make_ap(
    std::string ssid, bool locked, int rssi, uint8_t channel
)
{
    return hal::wifi::ScanApInfo{
        .ssid = std::move(ssid),
        .is_locked = locked,
        .rssi = rssi,
        .signal_level = hal::wifi::ScanApInfo::get_signal_level(rssi),
        .channel = channel,
    };
}

} // namespace

bool WifiAdapter::start()
{
    if (timer_ != nullptr) return true;

    (void)hal::WifiLinuxDevice::get_instance();
    basic_ = hal::acquire_interface<hal::wifi::BasicIface>(
        hal::WifiLinuxDevice::BASIC_IFACE_NAME
    );
    station_ = hal::acquire_interface<hal::wifi::StationIface>(
        hal::WifiLinuxDevice::STATION_IFACE_NAME
    );
    soft_ap_ = hal::acquire_interface<hal::wifi::SoftApIface>(
        hal::WifiLinuxDevice::SOFTAP_IFACE_NAME
    );
    connectivity_ = hal::acquire_interface<hal::network::ConnectivityIface>(
        hal::WifiLinuxDevice::CONNECTIVITY_IFACE_NAME
    );
    if (!basic_ || !station_ || !soft_ap_ || !connectivity_) {
        std::fprintf(stderr, "host_sim: Brookesia Linux Wi-Fi interfaces are unavailable\n");
        stop();
        return false;
    }

    if (!basic_->configure({}, {}) || !station_->configure({
            .on_scan_ap_infos_updated = [this](std::span<const hal::wifi::ScanApInfo> aps) {
                scan_results_.assign(aps.begin(), aps.end());
            },
        }) || !soft_ap_->configure({
            .on_event = [this](hal::wifi::SoftApEvent event) {
                soft_ap_started_ = event == hal::wifi::SoftApEvent::Started;
            },
        }) || !basic_->init() || !basic_->start()) {
        std::fprintf(stderr, "host_sim: could not initialize Brookesia Linux Wi-Fi\n");
        stop();
        return false;
    }

    if (!set_enabled(speaker_ui_is_wlan_on())) {
        stop();
        return false;
    }
    timer_ = lv_timer_create(timer_callback, POLL_PERIOD_MS, this);
    return timer_ != nullptr;
}

void WifiAdapter::stop()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    if (soft_ap_) soft_ap_->stop();
    if (station_) {
        station_->do_action(hal::wifi::StationAction::Disconnect, true);
        station_->clear_callbacks();
    }
    if (soft_ap_) soft_ap_->clear_callbacks();
    if (basic_) {
        basic_->stop();
        basic_->deinit();
        basic_->clear_callbacks();
    }
    connectivity_.reset();
    soft_ap_.reset();
    station_.reset();
    basic_.reset();
    enabled_ = false;
    connected_ = false;
    soft_ap_started_ = false;
}

bool WifiAdapter::set_enabled(bool enabled)
{
    if (!basic_ || !station_) return false;
    if (!enabled) {
        (void)disconnect();
        basic_->do_action(hal::wifi::BasicAction::Stop, true);
        enabled_ = false;
        last_result_ = ConnectionResult::Offline;
        retry_countdown_ms_ = 0;
        return true;
    }

    if (!basic_->do_action(hal::wifi::BasicAction::Init, true) ||
            !basic_->do_action(hal::wifi::BasicAction::Start, true)) {
        return false;
    }
    enabled_ = true;
    if (!scan()) return false;

    // The unchanged UI starts with its persisted Studio-WiFi row connected. Keep
    // only the deterministic stub aligned; a real backend never auto-connects.
    if (deterministic_stub()) {
        connected_ = connect_backend("Studio-WiFi", "stored-profile");
        last_result_ = connected_ ? ConnectionResult::Connected : ConnectionResult::BackendError;
    }
    return true;
}

bool WifiAdapter::scan()
{
    if (!enabled_ || !station_) return false;
    if (!station_->set_scan_params({.ap_count = 20, .interval_ms = 10000, .timeout_ms = 60000}) ||
            !station_->start_scan()) {
        return false;
    }
    if (deterministic_stub()) {
        scan_results_ = {
            make_ap("ESP-Lab", true, -42, 6),
            make_ap("NTT_Office", true, -63, 11),
            make_ap("Guest", false, -76, 1),
        };
    }
    return true;
}

WifiAdapter::ConnectionResult WifiAdapter::connect(
    const std::string &ssid, const std::string &password
)
{
    return connect_internal(ssid, password, false);
}

WifiAdapter::ConnectionResult WifiAdapter::retry()
{
    if (pending_ssid_.empty()) return last_result_;
    retry_countdown_ms_ = 0;
    const std::string ssid = pending_ssid_;
    const std::string password = pending_password_;
    return connect_internal(ssid, password, true);
}

WifiAdapter::ConnectionResult WifiAdapter::connect_internal(
    const std::string &ssid, const std::string &password, bool retrying
)
{
    if (!enabled_) return last_result_ = ConnectionResult::Offline;

    if (deterministic_stub()) {
        const auto ap = std::find_if(scan_results_.begin(), scan_results_.end(), [&](const auto &item) {
            return item.ssid == ssid;
        });
        if (ap == scan_results_.end()) {
            return last_result_ = ConnectionResult::NetworkNotFound;
        }
        if (ap->is_locked) {
            const std::string_view expected = ssid == "NTT_Office" ? TIMEOUT_PASSWORD : LOCKED_PASSWORD;
            if (password != expected) {
                pending_ssid_.clear();
                pending_password_.clear();
                return last_result_ = ConnectionResult::AuthenticationFailed;
            }
        }
        if (ssid == "NTT_Office" && !retrying) {
            pending_ssid_ = ssid;
            pending_password_ = password;
            retry_countdown_ms_ = RETRY_DELAY_MS;
            return last_result_ = ConnectionResult::TimedOut;
        }
    }

    pending_ssid_.clear();
    pending_password_.clear();
    connected_ = connect_backend(ssid, password);
    return last_result_ = connected_ ? ConnectionResult::Connected : ConnectionResult::BackendError;
}

bool WifiAdapter::connect_backend(const std::string &ssid, const std::string &password)
{
    if (!station_->set_target_connect_ap_info(hal::wifi::ConnectApInfo(ssid, password)) ||
            !station_->do_action(hal::wifi::StationAction::Connect, true)) {
        return false;
    }
    return station_->is_event_ready(hal::wifi::StationEvent::Connected);
}

bool WifiAdapter::disconnect()
{
    if (!station_) return false;
    const bool result = station_->do_action(hal::wifi::StationAction::Disconnect, true);
    connected_ = false;
    last_result_ = enabled_ ? ConnectionResult::Disconnected : ConnectionResult::Offline;
    return result;
}

bool WifiAdapter::start_soft_ap()
{
    if (!enabled_ || !soft_ap_) return false;
    const hal::wifi::SoftApParams params{
        .ssid = "ESP-Speaker-Setup",
        .password = "esp123456",
        .max_connection = 4,
        .channel = 6,
    };
    if (!soft_ap_->set_params(params) || !soft_ap_->start()) return false;
    soft_ap_started_ = true;
    return true;
}

void WifiAdapter::stop_soft_ap()
{
    if (soft_ap_) soft_ap_->stop();
    soft_ap_started_ = false;
}

void WifiAdapter::timer_callback(lv_timer_t *timer)
{
    auto *adapter = static_cast<WifiAdapter *>(lv_timer_get_user_data(timer));
    if (adapter != nullptr) adapter->poll();
}

void WifiAdapter::network_selected_callback(lv_event_t *event)
{
    auto *adapter = static_cast<WifiAdapter *>(lv_event_get_user_data(event));
    auto *row = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (adapter == nullptr || row == nullptr || lv_obj_get_child_count(row) == 0) return;
    auto *label = lv_obj_get_child(row, 0);
    if (!lv_obj_check_type(label, &lv_label_class)) return;
    adapter->selected_ssid_ = lv_label_get_text(label);
}

void WifiAdapter::poll()
{
    const bool ui_enabled = speaker_ui_is_wlan_on();
    if (ui_enabled != enabled_) (void)set_enabled(ui_enabled);

    const bool wlan_active = speaker_ui_is_screen_active("wlan");
    if (wlan_active) {
        attach_ui_handlers();
        update_ui_status();
    }

    const bool password_active = speaker_ui_is_screen_active("wlan-connect");
    if (password_active) {
        password_field_ = find_first_textarea(lv_screen_active());
    } else if (password_screen_was_active_ && wlan_active &&
               password_field_ != nullptr && !selected_ssid_.empty()) {
        const char *password = lv_textarea_get_text(password_field_);
        (void)connect(selected_ssid_, password == nullptr ? "" : password);
        update_ui_status();
    }
    password_screen_was_active_ = password_active;

    const bool softap_active = speaker_ui_is_screen_active("softap");
    if (softap_active && !soft_ap_started_) (void)start_soft_ap();
    else if (!softap_active && soft_ap_started_) stop_soft_ap();

    if (retry_countdown_ms_ > 0) {
        retry_countdown_ms_ -= static_cast<int>(POLL_PERIOD_MS);
        if (retry_countdown_ms_ <= 0) {
            (void)retry();
            update_ui_status();
        }
    }
}

void WifiAdapter::attach_ui_handlers()
{
    if (handlers_attached_) return;
    for (const auto ssid : {"ESP-Lab", "NTT_Office", "Guest"}) {
        auto *label = find_label(lv_screen_active(), ssid);
        if (label == nullptr) return;
        auto *row = lv_obj_get_parent(label);
        if (row == nullptr) return;
        lv_obj_add_event_cb(row, network_selected_callback, LV_EVENT_CLICKED, this);
    }
    status_label_ = find_label(lv_screen_active(), "Connected");
    handlers_attached_ = status_label_ != nullptr;
}

void WifiAdapter::update_ui_status()
{
    if (status_label_ == nullptr) return;
    switch (last_result_) {
    case ConnectionResult::Connected:
        lv_label_set_text(status_label_, "Connected");
        break;
    case ConnectionResult::Disconnected:
    case ConnectionResult::Offline:
        lv_label_set_text(status_label_, "Disconnected");
        break;
    case ConnectionResult::AuthenticationFailed:
        lv_label_set_text(status_label_, "Authentication failed");
        break;
    case ConnectionResult::TimedOut:
        lv_label_set_text(status_label_, "Connection timed out");
        break;
    case ConnectionResult::NetworkNotFound:
        lv_label_set_text(status_label_, "Network not found");
        break;
    case ConnectionResult::BackendError:
        lv_label_set_text(status_label_, "Connection failed");
        break;
    }
}

bool WifiAdapter::deterministic_stub() const
{
    return HOST_SIM_WIFI_BACKEND_NETWORKMANAGER_ENABLED == 0;
}

} // namespace host_sim
