#include "power_adapter.hpp"

#include <cstdio>
#include <string_view>

#include "brookesia/hal_linux/power/device.hpp"

namespace host_sim {
namespace {

using BatteryIface = esp_brookesia::hal::power::BatteryIface;
constexpr uint32_t POLL_PERIOD_MS = 1000;

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

bool shows_charge_icon(const BatteryIface::State &state)
{
    if (!state.is_present) return false;
    if (state.power_source == BatteryIface::PowerSource::External) return true;

    switch (state.charge_state) {
    case BatteryIface::ChargeState::Charging:
    case BatteryIface::ChargeState::Trickle:
    case BatteryIface::ChargeState::PreCharge:
    case BatteryIface::ChargeState::ConstantCurrent:
    case BatteryIface::ChargeState::ConstantVoltage:
    case BatteryIface::ChargeState::Full:
        return true;
    default:
        return false;
    }
}

} // namespace

bool PowerAdapter::start()
{
    if (timer_ != nullptr) return true;

    // Referencing the singleton keeps the power provider linked into this host target.
    (void)esp_brookesia::hal::PowerLinuxDevice::get_instance();
    battery_ = esp_brookesia::hal::acquire_interface<BatteryIface>(
        esp_brookesia::hal::PowerLinuxDevice::BATTERY_IFACE_NAME
    );
    if (!battery_) {
        std::fprintf(stderr, "host_sim: Brookesia Linux battery interface is unavailable\n");
        return false;
    }
    if (!locate_widgets()) {
        std::fprintf(stderr, "host_sim: Quick Settings battery widgets are unavailable\n");
        battery_.reset();
        return false;
    }

    poll();
    timer_ = lv_timer_create(timer_callback, POLL_PERIOD_MS, this);
    return timer_ != nullptr;
}

void PowerAdapter::stop()
{
    if (timer_ != nullptr) {
        lv_timer_delete(timer_);
        timer_ = nullptr;
    }
    percentage_label_ = nullptr;
    charge_icon_ = nullptr;
    battery_.reset();
}

void PowerAdapter::timer_callback(lv_timer_t *timer)
{
    auto *adapter = static_cast<PowerAdapter *>(lv_timer_get_user_data(timer));
    if (adapter != nullptr) adapter->poll();
}

void PowerAdapter::poll()
{
    BatteryIface::State state;
    if (!battery_ || !battery_->get_state(state)) {
        lv_label_set_text(percentage_label_, "--%");
        lv_obj_add_flag(charge_icon_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (!state.is_present || !state.percentage.has_value()) {
        lv_label_set_text(percentage_label_, "--%");
    } else {
        lv_label_set_text_fmt(percentage_label_, "%u%%", state.percentage.value());
    }

    if (shows_charge_icon(state)) lv_obj_remove_flag(charge_icon_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(charge_icon_, LV_OBJ_FLAG_HIDDEN);
}

bool PowerAdapter::locate_widgets()
{
    percentage_label_ = find_label(lv_layer_top(), "100%");
    if (percentage_label_ == nullptr) return false;

    auto *parent = lv_obj_get_parent(percentage_label_);
    if (parent == nullptr || lv_obj_get_child_count(parent) < 3) return false;

    // The unchanged Quick Settings component orders Wi-Fi, battery, then percent.
    charge_icon_ = lv_obj_get_child(parent, 1);
    return charge_icon_ != nullptr && lv_obj_check_type(charge_icon_, &lv_image_class);
}

} // namespace host_sim
