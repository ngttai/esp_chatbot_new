#include "wifi_control.hpp"
#include "wifi_preferences.hpp"
#include "head_led.hpp"
#include "brookesia/service_helper.hpp"
#include "brookesia/lib_utils.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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
    enabled_ = wifi_preferences::load_enabled();
}

bool WifiControl::reset_preferences()
{
    std::lock_guard lock(mutex_);
    auto result = wifi_preferences::save_enabled(true);
    if (!result) BROOKESIA_LOGE("Failed to reset WLAN preference: %1%", result.error());
    return result.has_value();
}

bool WifiControl::set_enabled(bool enabled)
{
    std::lock_guard lock(mutex_);
    if (enabled == enabled_.load()) return true;
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
    enabled_ = enabled;
    HeadLed::get_instance().set_wifi_enabled(enabled);
    auto saved = wifi_preferences::save_enabled(enabled);
    if (!saved) {
        BROOKESIA_LOGE("WLAN changed but preference save failed: %1%", saved.error());
        return false;
    }
    return true;
}
