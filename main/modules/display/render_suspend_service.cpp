/* SPDX-License-Identifier: CC0-1.0 */
#include <array>
#include <atomic>
#include <mutex>
#include "esp_timer.h"
#include "brookesia/expression_emote.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/service_display.hpp"
#include "brookesia/lib_utils.hpp"
#include "modules/power_manager.hpp"
#include "render_suspend.hpp"
#include "render_suspend_service.hpp"

using namespace esp_brookesia;
namespace {
class RenderParticipants final : public RenderSuspend::Backend, private PanelPower::Backend {
public:
    bool start(std::shared_ptr<lib_utils::TaskScheduler> scheduler, uint32_t output_id,
               const std::string &output_name, uint32_t width, uint32_t height, bool has_emote)
    {
        if (scheduler_) return false;
        output_id_ = output_id; output_name_ = output_name;
        width_ = width; height_ = height; has_emote_ = has_emote;
        for (const auto &source : display_.get_sources()) {
            if (source.name == expression::Emote::DISPLAY_SOURCE_NAME) emote_source_ = source.id;
        }
        completed_ = display_.connect_frame_completed([this](uint32_t source, const std::string &output,
                                                            const service::Display::FrameInfo &frame) {
            if (output != output_name_) return;
            std::lock_guard lock(fence_mutex_);
            fence_.complete(source, frame.x, frame.y, frame.width, frame.height);
        });
        const char *events[] = {"PrepareSuspend", "CommitSuspend", "CancelSuspend", "Resume"};
        for (size_t i = 0; i < commands_.size(); ++i) {
            commands_[i] = pwr_.subscribe_event(events[i], [this](const auto &, const auto &) {
                reconcile_requested_ = true;
            });
            if (!commands_[i].connected()) { disconnect(); return false; }
        }
        scheduler_ = std::move(scheduler);
        if (!scheduler_->configure_group("RenderSuspend", {.enable_serial_execution = true})) {
            disconnect(); scheduler_.reset(); return false;
        }
        if (!scheduler_->post_periodic([this]() {
                // Snapshot is authoritative: delayed broadcasts cannot execute an obsolete command.
                const auto snapshot = pwr_.suspend_status();
                if (reconcile_requested_.exchange(false) || snapshot.phase != SuspendCoordinator::Phase::Idle)
                    protocol_.update(snapshot, *this);
                return true;
            }, 20, &task_, "RenderSuspend")) {
            disconnect(); scheduler_.reset(); return false;
        }
        for (uint8_t id = RenderSuspend::LVGL_ID; id <= RenderSuspend::DISPLAY_ID; ++id) {
            if (!pwr_.register_participant(id)) {
                for (uint8_t previous = RenderSuspend::LVGL_ID; previous < id; ++previous)
                    pwr_.unregister_participant(previous);
                scheduler_->cancel(task_); task_ = 0;
                disconnect(); scheduler_.reset(); return false;
            }
        }
        BROOKESIA_LOGI("Pwr render participants ready: LVGL/GFX/Display; shared backend worker, no new stack");
        return true;
    }

    bool admission(bool enabled) override
    {
        return display_.set_frame_admission(output_id_, enabled).has_value();
    }
    bool pause_lvgl(bool paused) override
    {
        return gui::lvgl::DisplaySource::get_instance().try_set_render_paused(paused);
    }
    bool pause_gfx(bool paused) override
    {
        return !has_emote_ || expression::Emote::get_instance().native_try_set_render_paused(paused);
    }
    bool drained() override
    {
        const auto result = display_.is_frame_output_drained(output_id_);
        return result && *result;
    }
    uint32_t active_source() override
    {
        const auto result = display_.get_active_source(output_name_);
        if (!result) return 0;
        if (*result == gui::lvgl::DISPLAY_SOURCE_NAME)
            return gui::lvgl::DisplaySource::get_instance().source_id();
        if (*result == expression::Emote::DISPLAY_SOURCE_NAME && has_emote_) return emote_source_;
        return 0;
    }
    void arm_redraw(uint32_t source) override
    {
        std::lock_guard lock(fence_mutex_);
        fence_.arm(source, width_, height_);
    }
    bool redraw_complete() override
    {
        std::lock_guard lock(fence_mutex_);
        return fence_.done();
    }
    void report(uint32_t session, uint8_t id, SuspendCoordinator::Report status) override
    {
        (void)pwr_.report_suspend_status(session, id, status);
    }
    PanelPower::Result suspend_panel() override
    {
        return panel_power_.suspend(static_cast<uint32_t>(esp_timer_get_time() / 1000), *this);
    }
    PanelPower::Result resume_panel() override
    {
        return panel_power_.resume(static_cast<uint32_t>(esp_timer_get_time() / 1000), *this);
    }
private:
    bool blackout() override { return display_.set_backlight_pwm_temporary(output_id_, 0).has_value(); }
    bool command(PanelPower::Command command) override
    {
        using D = service::Display::PanelCommand;
        D native;
        const char *name;
        switch (command) {
        case PanelPower::Command::DisplayOff: native = D::DisplayOff; name = "DISPOFF"; break;
        case PanelPower::Command::SleepIn: native = D::SleepIn; name = "SLPIN"; break;
        case PanelPower::Command::SleepOut: native = D::SleepOut; name = "SLPOUT"; break;
        case PanelPower::Command::DisplayOn: native = D::DisplayOn; name = "DISPON"; break;
        default: return false;
        }
        const auto result = display_.send_panel_command(output_id_, native);
        if (!result) { BROOKESIA_LOGE("Panel %1% failed: %2%", name, result.error()); return false; }
        BROOKESIA_LOGD("Panel %1% sent", name);
        return true;
    }
    void disconnect()
    {
        for (auto &command : commands_) command.disconnect();
        completed_.disconnect();
    }
    PowerManager &pwr_ = PowerManager::get_instance();
    service::Display &display_ = service::Display::get_instance();
    std::shared_ptr<lib_utils::TaskScheduler> scheduler_;
    lib_utils::TaskSchedulerTaskId task_ = 0;
    std::array<service::EventSignalConnection, 4> commands_;
    lib_utils::connection completed_;
    std::atomic_bool reconcile_requested_{false};
    RenderSuspend protocol_;
    PanelPower panel_power_;
    RedrawFence fence_;
    std::mutex fence_mutex_;
    std::string output_name_;
    uint32_t output_id_ = 0, width_ = 0, height_ = 0, emote_source_ = 0;
    bool has_emote_ = false;
};
}

bool start_render_suspend_participants(std::shared_ptr<lib_utils::TaskScheduler> scheduler,
                                      uint32_t output_id, const std::string &output_name,
                                      uint32_t width, uint32_t height, bool has_emote)
{
    static RenderParticipants participants;
    return participants.start(std::move(scheduler), output_id, output_name, width, height, has_emote);
}
