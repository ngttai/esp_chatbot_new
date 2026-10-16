/* SPDX-License-Identifier: Apache-2.0 */
#include "private/display_impl.hpp"
#include "brookesia/lib_utils/function_guard.hpp"
#if defined(ESP_PLATFORM)
#include "esp_lcd_panel_ops.h"
#endif

namespace esp_brookesia::service {
std::expected<void, std::string> Display::send_panel_command(uint32_t output_id, PanelCommand command)
{
    std::shared_ptr<std::mutex> draw_mutex;
    std::shared_ptr<hal::display::PanelIface> panel;
    {
        std::lock_guard lock(mutex_);
        auto it = outputs_.find(output_id);
        if (it == outputs_.end() || it->second.info.slot != OutputSlot::HalPanel)
            return std::unexpected("Panel output is not available");
        draw_mutex = it->second.draw_mutex;
        panel = it->second.panel.get();
    }
    if (!draw_mutex || !panel) return std::unexpected("Panel draw resources are not available");
    std::unique_lock draw_lock(*draw_mutex, std::try_to_lock);
    if (!draw_lock.owns_lock()) return std::unexpected("Panel draw is busy");
    {
        std::lock_guard lock(mutex_);
        auto it = outputs_.find(output_id);
        if (it == outputs_.end() || it->second.draw_mutex != draw_mutex)
            return std::unexpected("Panel output changed");
        auto &output = it->second;
        if (output.frame_admission_enabled || output.pending_frame || output.inflight_frame_id ||
            output.outstanding_sync_frames || output.outstanding_async_frames ||
            output.panel_completion_uncertain || output.panel_power_busy)
            return std::unexpected("Panel output is not quiescent");
        output.panel_power_busy = true;
    }
    // Do not hold the service mutex during bus I/O. Admission cannot reopen until this clears.
    lib_utils::FunctionGuard command_guard([this, output_id] {
        std::lock_guard lock(mutex_);
        auto it = outputs_.find(output_id);
        if (it != outputs_.end()) it->second.panel_power_busy = false;
    });
#if defined(ESP_PLATFORM)
    hal::display::PanelIface::DriverSpecific specific;
    if (!panel->get_driver_specific(specific) || !specific.panel_handle)
        return std::unexpected("Panel command interface is not available");
    const auto handle = static_cast<esp_lcd_panel_handle_t>(specific.panel_handle);
    if (command == PanelCommand::SleepIn || command == PanelCommand::SleepOut) {
        std::lock_guard lock(mutex_);
        auto output = outputs_.find(output_id);
        if (output != outputs_.end()) output->second.panel_needs_wake = true;
    }
    esp_err_t result = ESP_ERR_INVALID_ARG;
    switch (command) {
    case PanelCommand::DisplayOff: result = esp_lcd_panel_disp_on_off(handle, false); break;
    case PanelCommand::SleepIn: result = esp_lcd_panel_disp_sleep(handle, true); break;
    case PanelCommand::SleepOut: result = esp_lcd_panel_disp_sleep(handle, false); break;
    case PanelCommand::DisplayOn: result = esp_lcd_panel_disp_on_off(handle, true); break;
    }
    if (command == PanelCommand::DisplayOn) {
        std::lock_guard lock(mutex_);
        auto output = outputs_.find(output_id);
        if (output != outputs_.end()) output->second.panel_needs_wake = result != ESP_OK;
    }
    if (result != ESP_OK) return std::unexpected(esp_err_to_name(result));
    return {};
#else
    (void)command;
    return std::unexpected("Native panel power commands are not supported on this host");
#endif
}
}
