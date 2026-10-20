/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include "mcp_device_tools.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "boost/json/object.hpp"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "brookesia/lib_utils/describe_helpers.hpp"
#include "brookesia/mcp_utils/mcp_utils.hpp"
#include "brookesia/service_helper.hpp"
#include "private/utils.hpp"
#include "modules/battery_monitor.hpp"
#include "modules/display/display.hpp"
#include "modules/power_manager.hpp"
#include "modules/wifi_control.hpp"

using namespace esp_brookesia;

namespace {

using AudioPlaybackHelper = service::helper::AudioPlayback;
using DisplayHelper = service::helper::Display;
using WifiHelper = service::helper::Wifi;
using XiaoZhiHelper = service::helper::XiaoZhi;

constexpr uint32_t SERVICE_TIMEOUT_MS = 1500;
constexpr char PERCENT_PARAMETER[] = "percent";
constexpr char MUTED_PARAMETER[] = "muted";

service::FunctionResult error_result(std::string message)
{
    return {
        .success = false,
        .error_message = std::move(message),
    };
}

const char *power_state_name(PowerPolicyState state)
{
    switch (state) {
    case PowerPolicyState::Boot: return "Boot";
    case PowerPolicyState::Active: return "Active";
    case PowerPolicyState::WaitAgentSleep: return "WaitAgentSleep";
    case PowerPolicyState::Dimmed: return "Dimmed";
    case PowerPolicyState::HardwareSleep: return "HardwareSleep";
    case PowerPolicyState::Waking: return "Waking";
    }
    return "Unknown";
}

bool get_percent(const service::FunctionParameterMap &params, double &percent)
{
    const auto it = params.find(PERCENT_PARAMETER);
    if (it == params.end()) return false;
    const auto *number = std::get_if<double>(&it->second);
    if ((number == nullptr) || !std::isfinite(*number) || (*number < 0.0) || (*number > 100.0)) {
        return false;
    }
    percent = *number;
    return true;
}

service::FunctionResult read_audio_state()
{
    auto volume = AudioPlaybackHelper::call_function_sync<double>(
                      AudioPlaybackHelper::FunctionId::GetVolume,
                      service::helper::Timeout(SERVICE_TIMEOUT_MS)
                  );
    if (!volume) return error_result("Audio volume is unavailable: " + volume.error());

    auto muted = AudioPlaybackHelper::call_function_sync<bool>(
                     AudioPlaybackHelper::FunctionId::GetMute,
                     service::helper::Timeout(SERVICE_TIMEOUT_MS)
                 );
    if (!muted) return error_result("Audio mute state is unavailable: " + muted.error());

    return {
        .success = true,
        .data = boost::json::object{
            {"percent", volume.value()},
            {"muted", muted.value()},
        },
    };
}

service::FunctionResult device_get_status(service::FunctionParameterMap &&)
{
    const auto battery = BatteryMonitor::get_instance().get_snapshot();
    boost::json::object battery_status{
        {"available", battery.valid},
    };
    if (battery.valid) {
        battery_status["percent"] = static_cast<uint64_t>(battery.percentage);
        battery_status["charging"] = battery.charging;
    } else {
        battery_status["percent"] = nullptr;
        battery_status["charging"] = nullptr;
    }

    std::string wifi_state = "Unavailable";
    auto state_result = WifiHelper::call_function_sync<std::string>(
                            WifiHelper::FunctionId::GetGeneralState,
                            service::helper::Timeout(SERVICE_TIMEOUT_MS)
                        );
    if (state_result) wifi_state = state_result.value();

    boost::json::object wifi_status{
        {"enabled", WifiControl::get_instance().is_enabled()},
        {"connected", wifi_state == "Connected"},
        {"state", wifi_state},
        {"ssid", nullptr},
        {"rssi_dbm", nullptr},
    };
    if (wifi_state == "Connected") {
        wifi_ap_record_t access_point{};
        if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
            const auto *ssid = reinterpret_cast<const char *>(access_point.ssid);
            wifi_status["ssid"] = std::string(ssid, strnlen(ssid, sizeof(access_point.ssid)));
            wifi_status["rssi_dbm"] = static_cast<int64_t>(access_point.rssi);
        }
    }

    boost::json::object heap_status{
        {"internal_free_bytes", static_cast<uint64_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))},
        {"internal_min_free_bytes", static_cast<uint64_t>(
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
         )},
        {"psram_free_bytes", static_cast<uint64_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM))},
        {"psram_min_free_bytes", static_cast<uint64_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM))},
    };

    return {
        .success = true,
        .data = boost::json::object{
            {"battery", std::move(battery_status)},
            {"wifi", std::move(wifi_status)},
            {"power_state", power_state_name(PowerManager::get_instance().state())},
            {"heap", std::move(heap_status)},
        },
    };
}

service::FunctionResult display_get_brightness(service::FunctionParameterMap &&)
{
    const auto output_id = Display::get_instance().output_id();
    if (output_id == 0) return error_result("Display output is unavailable");

    auto brightness = DisplayHelper::call_function_sync<double>(
                          DisplayHelper::FunctionId::GetBacklightBrightness,
                          output_id,
                          service::helper::Timeout(SERVICE_TIMEOUT_MS)
                      );
    if (!brightness) return error_result("Display brightness is unavailable: " + brightness.error());

    return {
        .success = true,
        .data = boost::json::object{{"percent", brightness.value()}},
    };
}

service::FunctionResult display_set_brightness(service::FunctionParameterMap &&params)
{
    double percent = 0;
    if (!get_percent(params, percent)) return error_result("percent must be a number in range [0, 100]");

    const auto output_id = Display::get_instance().output_id();
    if (output_id == 0) return error_result("Display output is unavailable");

    auto result = DisplayHelper::call_function_sync(
                      DisplayHelper::FunctionId::SetBacklightBrightness,
                      output_id,
                      percent,
                      service::helper::Timeout(SERVICE_TIMEOUT_MS)
                  );
    if (!result) return error_result("Failed to set display brightness: " + result.error());

    return display_get_brightness(service::FunctionParameterMap{});
}

service::FunctionResult audio_get_volume(service::FunctionParameterMap &&)
{
    return read_audio_state();
}

service::FunctionResult audio_set_volume(service::FunctionParameterMap &&params)
{
    double percent = 0;
    if (!get_percent(params, percent)) return error_result("percent must be a number in range [0, 100]");

    auto volume_result = AudioPlaybackHelper::call_function_sync(
                             AudioPlaybackHelper::FunctionId::SetVolume,
                             percent,
                             service::helper::Timeout(SERVICE_TIMEOUT_MS)
                         );
    if (!volume_result) return error_result("Failed to set audio volume: " + volume_result.error());

    auto mute_result = AudioPlaybackHelper::call_function_sync(
                           AudioPlaybackHelper::FunctionId::SetMute,
                           percent == 0.0,
                           service::helper::Timeout(SERVICE_TIMEOUT_MS)
                       );
    if (!mute_result) return error_result("Volume changed but mute state update failed: " + mute_result.error());

    return read_audio_state();
}

service::FunctionResult audio_set_mute(service::FunctionParameterMap &&params)
{
    const auto it = params.find(MUTED_PARAMETER);
    if (it == params.end()) return error_result("muted must be a boolean");
    const auto *muted = std::get_if<bool>(&it->second);
    if (muted == nullptr) return error_result("muted must be a boolean");

    auto result = AudioPlaybackHelper::call_function_sync(
                      AudioPlaybackHelper::FunctionId::SetMute,
                      *muted,
                      service::helper::Timeout(SERVICE_TIMEOUT_MS)
                  );
    if (!result) return error_result("Failed to set audio mute state: " + result.error());

    return read_audio_state();
}

service::FunctionResult app_show_clock(service::FunctionParameterMap &&)
{
    if (!Display::get_instance().request_show_clock()) {
        return error_result("Clock app is unavailable");
    }
    return {
        .success = true,
        .data = boost::json::object{
            {"screen", "Clock"},
            {"requested", true},
        },
    };
}

service::FunctionResult app_show_home(service::FunctionParameterMap &&)
{
    if (!Display::get_instance().request_show_home()) {
        return error_result("Home screen is unavailable");
    }
    return {
        .success = true,
        .data = boost::json::object{
            {"screen", "Home"},
            {"requested", true},
        },
    };
}

mcp_utils::CustomTool make_tool(
    std::string name, std::string description, std::vector<service::FunctionParameterSchema> parameters,
    mcp_utils::CustomToolCallback callback
)
{
    return {
        .schema = {
            .name = std::move(name),
            .description = std::move(description),
            .parameters = std::move(parameters),
            .require_scheduler = false,
        },
        .callback = callback,
    };
}

} // namespace

bool register_vocat_mcp_tools()
{
    std::vector<mcp_utils::CustomTool> tools;
    tools.reserve(8);
    tools.push_back(make_tool(
        "Device.GetStatus", "Get VoCat battery, Wi-Fi, power and memory status.", {}, device_get_status
    ));
    tools.push_back(make_tool(
        "Display.GetBrightness", "Get the current display brightness percentage.", {}, display_get_brightness
    ));
    tools.push_back(make_tool(
        "Display.SetBrightness", "Set the display brightness percentage.",
        {{PERCENT_PARAMETER, "Brightness percentage in range [0, 100].", service::FunctionValueType::Number}},
        display_set_brightness
    ));
    tools.push_back(make_tool(
        "Audio.GetVolume", "Get the playback volume and mute state.", {}, audio_get_volume
    ));
    tools.push_back(make_tool(
        "Audio.SetVolume", "Set playback volume; zero mutes and a positive value unmutes.",
        {{PERCENT_PARAMETER, "Volume percentage in range [0, 100].", service::FunctionValueType::Number}},
        audio_set_volume
    ));
    tools.push_back(make_tool(
        "Audio.SetMute", "Mute or unmute playback without changing the saved volume.",
        {{MUTED_PARAMETER, "True to mute or false to unmute.", service::FunctionValueType::Boolean}},
        audio_set_mute
    ));
    tools.push_back(make_tool(
        "App.ShowClock",
        "Show the Clock app on the VoCat screen. Use only when the user explicitly asks to open, show, or switch "
        "to the Clock app. Do not use when the user only asks for the current time or date.",
        {}, app_show_clock
    ));
    tools.push_back(make_tool(
        "App.ShowHome",
        "Show the VoCat Home face screen. Use only when the user explicitly requests navigation to Home.",
        {}, app_show_home
    ));

    auto result = XiaoZhiHelper::call_function_sync<boost::json::array>(
                      XiaoZhiHelper::FunctionId::AddMCP_ToolsWithCustomFunction,
                      BROOKESIA_DESCRIBE_TO_JSON(tools).as_array()
                  );
    if (!result) {
        BROOKESIA_LOGE("Failed to register VoCat MCP tools: %1%", result.error());
        return false;
    }
    if (result->size() != tools.size()) {
        BROOKESIA_LOGE("Expected %1% VoCat MCP tools, registered %2%", tools.size(), result->size());
        return false;
    }
    for (size_t index = 0; index < tools.size(); ++index) {
        const auto &registered_name = result->at(index);
        const auto expected_name = std::string("Custom.") + tools[index].schema.name;
        if (!registered_name.is_string() || (registered_name.as_string() != expected_name)) {
            BROOKESIA_LOGE(
                "VoCat MCP tool registration mismatch at index %1%: expected '%2%', got '%3%'",
                index, expected_name, registered_name
            );
            return false;
        }
    }

    BROOKESIA_LOGI("Registered VoCat MCP tools: %1%", result.value());
    return true;
}
