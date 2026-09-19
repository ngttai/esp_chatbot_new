#include "persistence_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>
#include <vector>

#include <unistd.h>

#include "brookesia/service_helper.hpp"

extern "C" {
#include "speaker_ui.h"
#include "ui.h"
}

namespace host_sim {
namespace {

using AudioPlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
using DisplayHelper = esp_brookesia::service::helper::Display;
using StorageHelper = esp_brookesia::service::helper::Storage;

constexpr uint32_t SERVICE_TIMEOUT_MS = 2000;
constexpr uint32_t POLL_PERIOD_MS = 50;
constexpr const char *STORAGE_NAMESPACE = "HostSimulator";
constexpr const char *KEY_WLAN_ENABLED = "WlanEnabled";
constexpr const char *KEY_AI_PROFILE = "AiProfile";
constexpr std::array<std::string_view, 4> VIRTUAL_MOUNTS{{
    "/spiffs", "/littlefs", "/fatfs", "/sdcard",
}};

std::filesystem::path sandbox_file_system_root()
{
    if (const char *configured = std::getenv("BROOKESIA_HAL_LINUX_FS_ROOT")) {
        if (configured[0] != '\0') return std::filesystem::path(configured);
    }

    std::array<char, 4096> executable_path{};
    const ssize_t length = ::readlink(
        "/proc/self/exe", executable_path.data(), executable_path.size() - 1U
    );
    if (length > 0) {
        executable_path[static_cast<size_t>(length)] = '\0';
        return std::filesystem::path(executable_path.data()).parent_path() / ".brookesia" / "fs";
    }
    return {};
}

std::filesystem::path physical_mount_path(std::string_view mount)
{
    const auto known = std::find(VIRTUAL_MOUNTS.begin(), VIRTUAL_MOUNTS.end(), mount);
    if (known == VIRTUAL_MOUNTS.end()) return {};
    return sandbox_file_system_root() / mount.substr(1);
}

lv_obj_t *find_switch(lv_obj_t *object)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_switch_class)) return object;
    const uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < count; ++index) {
        if (auto *match = find_switch(lv_obj_get_child(object, index))) return match;
    }
    return nullptr;
}

bool contains_label(lv_obj_t *object, std::string_view text)
{
    if (object == nullptr) return false;
    if (lv_obj_check_type(object, &lv_label_class)) {
        const char *label = lv_label_get_text(object);
        return label != nullptr && text == label;
    }
    const uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < count; ++index) {
        if (contains_label(lv_obj_get_child(object, index), text)) return true;
    }
    return false;
}

lv_obj_t *find_button_with_label(lv_obj_t *object, std::string_view text)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_button_class) && contains_label(object, text)) return object;
    const uint32_t count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < count; ++index) {
        if (auto *match = find_button_with_label(lv_obj_get_child(object, index), text)) return match;
    }
    return nullptr;
}

std::expected<void, std::string> clear_virtual_file_systems()
{
    for (const auto mount : VIRTUAL_MOUNTS) {
        const auto physical_mount = physical_mount_path(mount);
        if (physical_mount.empty()) return std::unexpected("Simulator FS sandbox root is unavailable");
        auto entries_result = StorageHelper::fs_list(
            physical_mount.generic_string(), SERVICE_TIMEOUT_MS
        );
        if (!entries_result) return std::unexpected(entries_result.error());
        for (const auto &entry : entries_result.value()) {
            const std::string path = (physical_mount / entry.name).generic_string();
            auto remove_result = StorageHelper::fs_remove(path, SERVICE_TIMEOUT_MS);
            if (!remove_result) return std::unexpected(remove_result.error());
        }
    }
    return {};
}

} // namespace

bool PersistenceAdapter::start(uint32_t backlight_output_id)
{
    if (timer_ != nullptr) return true;
    backlight_output_id_ = backlight_output_id;

    auto state_result = load_ui_state();
    if (!state_result) {
        std::fprintf(stderr, "host_sim: could not load simulator UI state: %s\n",
                     state_result.error().c_str());
        return false;
    }
    state_ = state_result.value();

    apply_ai_profile(state_.ai_profile);
    if (!apply_wlan_state(state_.wlan_enabled) || !attach_restore_handler()) return false;

    if (ui_ScreenAIProfileButtonButtonRole1Select != nullptr) {
        lv_obj_add_event_cb(ui_ScreenAIProfileButtonButtonRole1Select, ai_select_callback,
                            LV_EVENT_CLICKED, this);
    }
    if (ui_ScreenAIProfileButtonButtonRole2Select != nullptr) {
        lv_obj_add_event_cb(ui_ScreenAIProfileButtonButtonRole2Select, ai_select_callback,
                            LV_EVENT_CLICKED, this);
    }

    speaker_ui_show("idle");
    timer_ = lv_timer_create(timer_callback, POLL_PERIOD_MS, this);
    return timer_ != nullptr;
}

void PersistenceAdapter::stop()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
}

const char *PersistenceAdapter::storage_namespace()
{
    return STORAGE_NAMESPACE;
}

std::string PersistenceAdapter::sandbox_file_path(
    std::string_view mount, std::string_view relative_path
)
{
    const std::filesystem::path relative(relative_path);
    if (relative.empty() || relative.is_absolute()) return {};
    for (const auto &part : relative) {
        if (part == "..") return {};
    }
    const auto physical_mount = physical_mount_path(mount);
    if (physical_mount.empty()) return {};
    return (physical_mount / relative).lexically_normal().generic_string();
}

std::expected<SimulatorUiState, std::string> PersistenceAdapter::load_ui_state()
{
    SimulatorUiState state;
    auto wlan_result = StorageHelper::get_key_value<bool>(
        STORAGE_NAMESPACE, KEY_WLAN_ENABLED, SERVICE_TIMEOUT_MS
    );
    if (wlan_result) state.wlan_enabled = wlan_result.value();

    auto profile_result = StorageHelper::get_key_value<int32_t>(
        STORAGE_NAMESPACE, KEY_AI_PROFILE, SERVICE_TIMEOUT_MS
    );
    if (profile_result) state.ai_profile = std::clamp(profile_result.value(), 0, 1);
    return state;
}

std::expected<void, std::string> PersistenceAdapter::save_ui_state(const SimulatorUiState &state)
{
    auto wlan_result = StorageHelper::save_key_value(
        STORAGE_NAMESPACE, KEY_WLAN_ENABLED, state.wlan_enabled, SERVICE_TIMEOUT_MS
    );
    if (!wlan_result) return std::unexpected(wlan_result.error());

    auto profile_result = StorageHelper::save_key_value(
        STORAGE_NAMESPACE, KEY_AI_PROFILE,
        static_cast<int32_t>(std::clamp(state.ai_profile, 0, 1)), SERVICE_TIMEOUT_MS
    );
    if (!profile_result) return std::unexpected(profile_result.error());
    return {};
}

std::expected<void, std::string> PersistenceAdapter::reset_simulator_data(
    uint32_t backlight_output_id
)
{
    auto display_result = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::ResetData, 0.0,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!display_result) return std::unexpected(display_result.error());

    // Display ResetData follows the embedded default and turns the backlight off.
    // Keep the host window visible so the reset UI remains usable in the simulator.
    auto backlight_result = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetBacklightOnOff,
        static_cast<double>(backlight_output_id),
        true,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!backlight_result) return std::unexpected(backlight_result.error());

    auto audio_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::ResetData,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!audio_result) return std::unexpected(audio_result.error());

    auto host_result = StorageHelper::erase_keys(STORAGE_NAMESPACE, {}, SERVICE_TIMEOUT_MS);
    if (!host_result) return std::unexpected(host_result.error());

    return clear_virtual_file_systems();
}

void PersistenceAdapter::timer_callback(lv_timer_t *timer)
{
    auto *adapter = static_cast<PersistenceAdapter *>(lv_timer_get_user_data(timer));
    if (adapter != nullptr) adapter->poll();
}

void PersistenceAdapter::ai_select_callback(lv_event_t *event)
{
    auto *adapter = static_cast<PersistenceAdapter *>(lv_event_get_user_data(event));
    if (adapter == nullptr || ui_ScreenAIProfileTabviewTabView == nullptr) return;
    adapter->state_.ai_profile = static_cast<int>(
        lv_tabview_get_tab_active(ui_ScreenAIProfileTabviewTabView)
    );
    (void)adapter->save_current_state();
}

void PersistenceAdapter::restore_callback(lv_event_t *event)
{
    auto *adapter = static_cast<PersistenceAdapter *>(lv_event_get_user_data(event));
    if (adapter == nullptr) return;
    auto result = reset_simulator_data(adapter->backlight_output_id_);
    if (!result) {
        std::fprintf(stderr, "host_sim: factory reset failed: %s\n", result.error().c_str());
        return;
    }
    adapter->apply_reset_state();
}

void PersistenceAdapter::poll()
{
    const bool wlan_enabled = speaker_ui_is_wlan_on();
    if (wlan_enabled == state_.wlan_enabled) return;
    state_.wlan_enabled = wlan_enabled;
    (void)save_current_state();
}

bool PersistenceAdapter::apply_wlan_state(bool enabled)
{
    if (!speaker_ui_show("wlan")) return false;
    lv_obj_update_layout(lv_screen_active());
    auto *wlan_switch = find_switch(lv_screen_active());
    if (wlan_switch == nullptr) return false;

    if (enabled) lv_obj_add_state(wlan_switch, LV_STATE_CHECKED);
    else lv_obj_remove_state(wlan_switch, LV_STATE_CHECKED);
    lv_obj_send_event(wlan_switch, LV_EVENT_VALUE_CHANGED, nullptr);
    return true;
}

void PersistenceAdapter::apply_ai_profile(int profile)
{
    if (ui_ScreenAIProfileTabviewTabView == nullptr) return;
    lv_tabview_set_active(ui_ScreenAIProfileTabviewTabView,
                          static_cast<uint32_t>(std::clamp(profile, 0, 1)), LV_ANIM_OFF);
}

bool PersistenceAdapter::attach_restore_handler()
{
    if (!speaker_ui_show("restore")) return false;
    lv_obj_update_layout(lv_screen_active());
    restore_button_ = find_button_with_label(lv_screen_active(), "Restore settings");
    if (restore_button_ == nullptr) return false;
    lv_obj_add_event_cb(restore_button_, restore_callback, LV_EVENT_CLICKED, this);
    return true;
}

bool PersistenceAdapter::save_current_state()
{
    auto result = save_ui_state(state_);
    if (result) return true;
    std::fprintf(stderr, "host_sim: could not persist simulator UI state: %s\n",
                 result.error().c_str());
    return false;
}

void PersistenceAdapter::apply_reset_state()
{
    state_ = SimulatorUiState{};
    apply_ai_profile(state_.ai_profile);
    (void)apply_wlan_state(state_.wlan_enabled);
    speaker_ui_show("restore");
}

} // namespace host_sim
