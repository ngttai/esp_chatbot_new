#include "volume_adapter.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

#include "brookesia/service_helper.hpp"

extern "C" {
#include "speaker_ui.h"
}

namespace host_sim {
namespace {

using AudioPlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
constexpr uint32_t SERVICE_TIMEOUT_MS = 1000;
constexpr uint32_t POLL_PERIOD_MS = 20;
constexpr std::array<int, 3> QUICK_LEVEL_VOLUME{{30, 60, 90}};

lv_obj_t *find_first_slider(lv_obj_t *object)
{
    if (object == nullptr) return nullptr;
    if (lv_obj_check_type(object, &lv_slider_class)) return object;

    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t index = 0; index < child_count; ++index) {
        if (auto *slider = find_first_slider(lv_obj_get_child(object, index))) return slider;
    }
    return nullptr;
}

int quick_level_for_state(int volume, bool mute)
{
    if (mute || volume <= 0) return -1;
    if (volume <= QUICK_LEVEL_VOLUME[0]) return 0;
    if (volume <= QUICK_LEVEL_VOLUME[1]) return 1;
    return 2;
}

lv_obj_t *find_clickable_at(lv_obj_t *object, int32_t x, int32_t y)
{
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN)) return nullptr;

    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t offset = 0; offset < child_count; ++offset) {
        const uint32_t index = child_count - 1U - offset;
        if (auto *match = find_clickable_at(lv_obj_get_child(object, index), x, y)) return match;
    }

    lv_area_t area{};
    lv_obj_get_coords(object, &area);
    if (lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) &&
            x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2) {
        return object;
    }
    return nullptr;
}

} // namespace

bool VolumeAdapter::start()
{
    if (timer_ != nullptr) return true;
    if (!read_service_state()) return false;

    last_quick_level_ = speaker_ui_get_quick_volume_level();
    timer_ = lv_timer_create(timer_callback, POLL_PERIOD_MS, this);
    return timer_ != nullptr;
}

void VolumeAdapter::stop()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    sound_slider_ = nullptr;
    last_slider_value_ = -1;
    quick_synchronized_ = false;
}

void VolumeAdapter::timer_callback(lv_timer_t *timer)
{
    auto *adapter = static_cast<VolumeAdapter *>(lv_timer_get_user_data(timer));
    if (adapter != nullptr) adapter->poll();
}

void VolumeAdapter::poll()
{
    if (speaker_ui_is_screen_active("quick") && !quick_synchronized_) {
        synchronize_quick_level();
        quick_synchronized_ = true;
    }

    const int quick_level = speaker_ui_get_quick_volume_level();
    if (quick_level != last_quick_level_) {
        last_quick_level_ = quick_level;
        if (apply_quick_level(quick_level)) synchronize_slider();
    }

    if (!speaker_ui_is_screen_active("sound")) return;

    auto *slider = find_first_slider(lv_screen_active());
    if (slider == nullptr) return;

    if (slider != sound_slider_) {
        sound_slider_ = slider;
        synchronize_slider();
        return;
    }

    const int value = lv_slider_get_value(slider);
    if (value != last_slider_value_) {
        last_slider_value_ = value;
        if (apply_slider_value(value)) quick_synchronized_ = false;
    }
}

bool VolumeAdapter::read_service_state()
{
    auto volume_result = AudioPlaybackHelper::call_function_sync<double>(
        AudioPlaybackHelper::FunctionId::GetVolume,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!volume_result) {
        std::fprintf(stderr, "host_sim: could not read simulated volume: %s\n",
                     volume_result.error().c_str());
        return false;
    }

    auto mute_result = AudioPlaybackHelper::call_function_sync<bool>(
        AudioPlaybackHelper::FunctionId::GetMute,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!mute_result) {
        std::fprintf(stderr, "host_sim: could not read simulated mute state: %s\n",
                     mute_result.error().c_str());
        return false;
    }

    current_volume_ = std::clamp(static_cast<int>(volume_result.value()), 0, 100);
    current_mute_ = mute_result.value();
    return true;
}

bool VolumeAdapter::apply_quick_level(int level)
{
    if (level < -1 || level >= static_cast<int>(QUICK_LEVEL_VOLUME.size())) return false;

    if (level == -1) {
        auto result = AudioPlaybackHelper::call_function_sync(
            AudioPlaybackHelper::FunctionId::SetMute, true,
            esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
        );
        if (!result) {
            std::fprintf(stderr, "host_sim: could not mute simulated audio: %s\n",
                         result.error().c_str());
            return false;
        }
        current_mute_ = true;
        return true;
    }

    const int volume = QUICK_LEVEL_VOLUME[level];
    auto volume_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::SetVolume, static_cast<double>(volume),
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    auto mute_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::SetMute, false,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!volume_result || !mute_result) {
        std::fprintf(stderr, "host_sim: could not set simulated volume level %d\n", level);
        return false;
    }
    current_volume_ = volume;
    current_mute_ = false;
    return true;
}

bool VolumeAdapter::apply_slider_value(int value)
{
    value = std::clamp(value, 0, 100);
    auto volume_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::SetVolume, static_cast<double>(value),
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    auto mute_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::SetMute, value == 0,
        esp_brookesia::service::helper::Timeout(SERVICE_TIMEOUT_MS)
    );
    if (!volume_result || !mute_result) {
        std::fprintf(stderr, "host_sim: could not set simulated volume to %d%%\n", value);
        return false;
    }
    current_volume_ = value;
    current_mute_ = value == 0;
    return true;
}

void VolumeAdapter::synchronize_quick_level()
{
    const int target = quick_level_for_state(current_volume_, current_mute_);
    int current = speaker_ui_get_quick_volume_level();
    for (int attempt = 0; current != target && attempt < 4; ++attempt) {
        int32_t x = 0;
        int32_t y = 0;
        if (!speaker_ui_get_quick_volume_button_center(&x, &y)) break;

        auto *button = find_clickable_at(lv_layer_top(), x, y);
        if (button == nullptr) break;
        lv_obj_send_event(button, LV_EVENT_CLICKED, nullptr);
        current = speaker_ui_get_quick_volume_level();
    }
    last_quick_level_ = current;
}

void VolumeAdapter::synchronize_slider()
{
    if (sound_slider_ == nullptr || !lv_obj_is_valid(sound_slider_)) return;
    const int value = current_mute_ ? 0 : current_volume_;
    lv_slider_set_value(sound_slider_, value, LV_ANIM_OFF);
    last_slider_value_ = value;
}

} // namespace host_sim
