#pragma once

#include "brookesia/service_helper/system/storage.hpp"

namespace wifi_preferences {
inline bool load_enabled()
{
    auto result = esp_brookesia::service::helper::Storage::get_key_value<bool>(
        "wifi_ui", "enabled", 3000);
    return result ? result.value() : true;
}

inline auto save_enabled(bool enabled)
{
    return esp_brookesia::service::helper::Storage::save_key_value(
        "wifi_ui", "enabled", enabled, 3000);
}
} // namespace wifi_preferences
