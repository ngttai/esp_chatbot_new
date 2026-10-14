#include "wifi_control.hpp"
#include "wifi_preferences.hpp"
#include "head_led.hpp"
#include "brookesia/service_helper.hpp"
#include "brookesia/lib_utils.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

using WifiHelper = esp_brookesia::service::helper::Wifi;
using AgentHelper = esp_brookesia::service::helper::AgentManager;
using EncoderHelper = esp_brookesia::service::helper::AudioEncoder<0>;
using Timeout = esp_brookesia::service::helper::Timeout;

WifiControl &WifiControl::get_instance()
{
    static WifiControl control;
    return control;
}

void WifiControl::init()
{
    std::lock_guard lock(mutex_);
    state_.init(wifi_preferences::load_enabled());
}

bool WifiControl::reset_preferences()
{
    std::lock_guard lock(mutex_);
    auto result = wifi_preferences::save_enabled(true);
    if (!result) BROOKESIA_LOGE("Failed to reset WLAN preference: %1%", result.error());
    return result.has_value();
}

bool WifiControl::process_pending()
{
    std::lock_guard lock(mutex_);
    bool radio_succeeded = true;
    for (unsigned step = 0; step < 6 && state_.pending(); ++step) {
        auto state = state_.snapshot();
        if (state.requested != state.confirmed) {
            bool actual = state.confirmed;
            radio_succeeded = apply_enabled(state.requested, actual);
            state_.transition_done(state.revision, actual, radio_succeeded);
            HeadLed::get_instance().set_wifi_enabled(actual);
        } else if (state.dirty) {
            auto saved = wifi_preferences::save_enabled(state.confirmed);
            state_.save_done(state.confirmed, saved.has_value());
            if (!saved) {
                BROOKESIA_LOGE("WLAN preference save attempt %1% failed: %2%", state.save_attempts + 1, saved.error());
                if (state.save_attempts < 2) vTaskDelay(pdMS_TO_TICKS(500));
            }
        }
    }
    return radio_succeeded;
}

bool WifiControl::apply_enabled(bool enabled, bool &actual)
{
    if (!enabled) {
        // Drain the encoder and stop the conversation before removing its network.
        auto state = AgentHelper::call_function_sync<std::string>(
            AgentHelper::FunctionId::GetGeneralState, Timeout(1000));
        AgentHelper::GeneralState parsed = AgentHelper::GeneralState::Max;
        if (state) (void)BROOKESIA_DESCRIBE_STR_TO_ENUM(state.value(), parsed);
        if (parsed == AgentHelper::GeneralState::Started || parsed == AgentHelper::GeneralState::Slept) {
            if (EncoderHelper::is_running()) {
                auto paused = EncoderHelper::call_function_sync(EncoderHelper::FunctionId::Pause, Timeout(1000));
                if (!paused) BROOKESIA_LOGW("Failed to pause encoder: %1%", paused.error());
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            auto stopped = AgentHelper::call_function_sync(
                AgentHelper::FunctionId::TriggerGeneralAction,
                BROOKESIA_DESCRIBE_TO_STR(AgentHelper::GeneralAction::Stop), Timeout(5000));
            if (!stopped) BROOKESIA_LOGW("Failed to stop Agent: %1%", stopped.error());
        }
        (void)WifiHelper::call_function_sync(WifiHelper::FunctionId::TriggerScanStop, Timeout(1000));
        (void)WifiHelper::call_function_sync(WifiHelper::FunctionId::TriggerSoftApProvisionStop, Timeout(1000));
    }
    auto result = WifiHelper::call_function_sync(
        WifiHelper::FunctionId::TriggerGeneralAction,
        BROOKESIA_DESCRIBE_TO_STR(enabled ? WifiHelper::GeneralAction::Start : WifiHelper::GeneralAction::Stop),
        Timeout(1000));
    if (!result) {
        BROOKESIA_LOGE("Failed to change WLAN state: %1%", result.error());
        return false;
    }
    // TriggerGeneralAction accepts a queued command; confirm its resulting state separately.
    const int64_t deadline = esp_timer_get_time() + 8000000;
    while (esp_timer_get_time() < deadline) {
        auto state = WifiHelper::call_function_sync<std::string>(WifiHelper::FunctionId::GetGeneralState, Timeout(500));
        auto confirmed = state ? wifi_confirmed_state(state.value()) : std::nullopt;
        if (confirmed) {
            actual = confirmed.value();
            if (actual == enabled) return true;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    BROOKESIA_LOGE("WLAN %1% confirmation timed out", enabled ? "start" : "stop");
    return false;
}
