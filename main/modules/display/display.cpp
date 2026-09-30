/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <cstdint>
#include <string>
#include <vector>
#include "boost/json/array.hpp"
#include "boost/json/object.hpp"
#include "boost/json/value.hpp"
#include "private/utils.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_helper.hpp"
#include "brookesia/expression_emote.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "screens/speaker_shell.hpp"
#include "display.hpp"

using namespace esp_brookesia;
using EmoteHelper = service::helper::ExpressionEmote;
using DisplayHelper = service::helper::Display;
using VideoHelper = service::helper::Video;
using LvglDisplaySource = gui::lvgl::DisplaySource;

namespace {

constexpr uint32_t BACKLIGHT_ON_DELAY_MS = 1000;
constexpr uint32_t LOAD_ASSETS_TIMEOUT_MS = 10000;
constexpr uint32_t DISPLAY_SERVICE_TIMEOUT_MS = 1000;

} // namespace

bool Display::start(const Config &config)
{
    BROOKESIA_LOG_TRACE_GUARD();

    BROOKESIA_CHECK_NULL_RETURN(config.task_scheduler, false, "Task scheduler is null");

    task_scheduler_ = config.task_scheduler;
    BROOKESIA_CHECK_FALSE_RETURN(start_display_service(), false, "Failed to start display service");

    // The imported Speaker UI owns the LVGL screen lifecycle and navigation.
    BROOKESIA_CHECK_FALSE_RETURN(
        start_lvgl_display_source(), false, "Failed to start LVGL"
    );
    BROOKESIA_CHECK_FALSE_RETURN(set_active_source_role(DrawSource::Lvgl), false, "Failed to activate LVGL source");
    if (!config.developer_mode) {
        emote_ready_ = start_expression_emote_assets();
        if (!emote_ready_) {
            BROOKESIA_LOGW("Native Emote is unavailable; keep the black LVGL idle screen");
        }
        BROOKESIA_CHECK_FALSE_RETURN(start_speaker_shell(), false, "Failed to start Speaker UI shell");
    }

    auto delayed_task = []() {
        auto result = DisplayHelper::call_function_async(
                          DisplayHelper::FunctionId::SetBacklightOnOff,
                          Display::get_instance().display_output_id_,
                          true
                      );
        BROOKESIA_CHECK_FALSE_EXIT(result, "Failed to set backlight on");
    };
    auto post_delayed_ret = task_scheduler_->post_delayed(delayed_task, BACKLIGHT_ON_DELAY_MS);
    BROOKESIA_CHECK_FALSE_RETURN(post_delayed_ret, false, "Failed to post delayed task");

    return true;
}

bool Display::start_display_service()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_FALSE_RETURN(DisplayHelper::is_available(), false, "Display service is not available");

    display_service_binding_ = service::ServiceManager::get_instance().bind(DisplayHelper::get_name().data());
    BROOKESIA_CHECK_FALSE_RETURN(display_service_binding_.is_valid(), false, "Failed to bind Display service");

    auto outputs_json = DisplayHelper::call_function_sync<boost::json::array>(
                            DisplayHelper::FunctionId::GetOutputs,
                            service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                        );
    BROOKESIA_CHECK_FALSE_RETURN(
        outputs_json.has_value(), false, "Failed to get Display outputs: %1%", outputs_json.error()
    );

    std::vector<DisplayHelper::OutputInfo> outputs;
    BROOKESIA_CHECK_FALSE_RETURN(
        BROOKESIA_DESCRIBE_FROM_JSON(boost::json::value(outputs_json.value()), outputs), false,
        "Failed to parse Display outputs"
    );
    BROOKESIA_CHECK_FALSE_RETURN(!outputs.empty(), false, "No Display output is available");

    const auto &main_output = outputs.front();
    BROOKESIA_CHECK_FALSE_RETURN(
        (main_output.width > 0) && (main_output.height > 0), false,
        "Invalid Display output size: %1%x%2%", main_output.width, main_output.height
    );

    display_output_name_ = main_output.name;
    display_output_id_ = main_output.id;
    display_width_ = main_output.width;
    display_height_ = main_output.height;
    BROOKESIA_LOGI("Using display output %1% (%2%x%3%)", display_output_name_, display_width_, display_height_);

    return true;
}

bool Display::start_lvgl_display_source()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    gui::lvgl::DisplaySourceConfig config{};
    config.output_name = "";
    config.task_core_id = CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_INIT_THREAD_CORE_ID;
    // Match the original ESP-Speaker hardware path: render complete frames into
    // two PSRAM buffers instead of exposing each small partial stripe on the LCD.
    config.buffer_height = static_cast<uint16_t>(display_height_);
    config.use_psram = true;
    config.require_double_buffer = true;

    auto &source = LvglDisplaySource::get_instance();
    BROOKESIA_CHECK_FALSE_RETURN(source.start(config), false, "Failed to start LVGL display source");

    return true;
}

bool Display::start_speaker_shell()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_EXCEPTION_RETURN(
        speaker_shell_ = std::make_unique<ScreenSpeakerShell>(), false,
        "Failed to create Speaker UI shell"
    );
    BROOKESIA_CHECK_FALSE_RETURN(
        speaker_shell_->start(display_output_id_, task_scheduler_), false, "Failed to initialize Speaker UI shell"
    );

    return true;
}

bool Display::set_active_source_role(DrawSource source)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    std::string source_role;
    switch (source) {
    case DrawSource::Lvgl:
        source_role = gui::lvgl::DISPLAY_SOURCE_ROLE;
        break;
    case DrawSource::Emote:
        source_role = expression::Emote::DISPLAY_SOURCE_ROLE;
        break;
    case DrawSource::Video:
        source_role = std::string(VideoHelper::DISPLAY_SOURCE_ROLE);
        break;
    default:
        return false;
    }

    BROOKESIA_CHECK_FALSE_RETURN(!source_role.empty(), false, "Display source role is not initialized");

    auto result = DisplayHelper::call_function_sync(
                      DisplayHelper::FunctionId::SetActiveSourceRole,
                      std::string(),
                      source_role,
                      service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                  );
    BROOKESIA_CHECK_FALSE_RETURN(
        result.has_value(), false, "Failed to set active display source role: %1%", result.error()
    );

    return true;
}

bool Display::show_video()
{
    return set_active_source_role(DrawSource::Video);
}

bool Display::show_ui()
{
    return set_active_source_role(DrawSource::Lvgl);
}

bool Display::show_emote()
{
    BROOKESIA_CHECK_FALSE_RETURN(emote_ready_, false, "Native Emote is not ready");
    return set_active_source_role(DrawSource::Emote);
}

bool Display::start_expression_emote_assets()
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    if (!EmoteHelper::is_available()) {
        BROOKESIA_LOGW("Emote is not available, skip initialization");
        return true;
    }

    BROOKESIA_LOGI("Initializing emote assets...");

    emote_service_binding_ = service::ServiceManager::get_instance().bind(EmoteHelper::get_name().data());
    BROOKESIA_CHECK_FALSE_RETURN(emote_service_binding_.is_valid(), false, "Failed to bind Emote service");

    {
        // Set emote config
        EmoteHelper::Config config{
            .task_priority = 6,
            .task_stack = 8 * 1024,
            .task_affinity = CONFIG_BROOKESIA_HAL_ADAPTOR_DISPLAY_LCD_PANEL_INIT_THREAD_CORE_ID,
            // Match ESP-Speaker: the animation task does not need scarce internal RAM.
            .task_stack_in_ext = true,
            .flag_buff_dma = true,
        };
        auto result = EmoteHelper::call_function_sync(
                          EmoteHelper::FunctionId::SetConfig, BROOKESIA_DESCRIBE_TO_JSON(config).as_object()
                      );
        BROOKESIA_CHECK_FALSE_RETURN(result.has_value(), false, "Failed to set emote config: %1%", result.error());
    }

    {
        EmoteHelper::AssetSource source{
            .source = ASSETS_PARTITION_NAME,
            .type = EmoteHelper::AssetSourceType::PartitionLabel,
            .flag_enable_mmap = false,
        };
        auto result = EmoteHelper::call_function_sync(
                          EmoteHelper::FunctionId::LoadAssetsSource, BROOKESIA_DESCRIBE_TO_JSON(source).as_object(),
                          service::helper::Timeout(LOAD_ASSETS_TIMEOUT_MS)
                      );
        BROOKESIA_CHECK_FALSE_RETURN(result.has_value(), false, "Failed to load emote assets: %1%", result.error());
    }

    {
        auto result = EmoteHelper::call_function_sync(
                          EmoteHelper::FunctionId::SetEmoji, "neutral"
                      );
        BROOKESIA_CHECK_FALSE_RETURN(
            result.has_value(), false, "Failed to set initial emote emoji: %1%", result.error()
        );
    }

    {
        auto result = EmoteHelper::call_function_sync(
                          EmoteHelper::FunctionId::SetEventMessage,
                          BROOKESIA_DESCRIBE_TO_STR(EmoteHelper::EventMessageType::Idle)
                      );
        BROOKESIA_CHECK_FALSE_RETURN(
            result.has_value(), false, "Failed to set emote event message: %1%", result.error()
        );
    }

    return true;
}

bool Display::send_display_task(esp_brookesia::lib_utils::TaskScheduler::OnceTask &&task)
{
    BROOKESIA_LOG_TRACE_GUARD_WITH_THIS();

    BROOKESIA_CHECK_NULL_RETURN(task_scheduler_, false, "Task scheduler is null");

    auto result = task_scheduler_->post(std::move(task), nullptr, Display::TASK_GROUP_NAME);
    BROOKESIA_CHECK_FALSE_RETURN(result, false, "Failed to post task function");

    return true;
}
