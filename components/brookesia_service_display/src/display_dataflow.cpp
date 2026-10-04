/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "brookesia/service_display/macro_configs.h"
#if !BROOKESIA_SERVICE_DISPLAY_ENABLE_DEBUG_LOG
#   define BROOKESIA_LOG_DISABLE_DEBUG_TRACE 1
#endif
#include "private/utils.hpp"
#include "private/dataflow_provider.hpp"
#include "brookesia/hal_interface/interface.hpp"
#include "brookesia/hal_interface/interfaces/display/panel.hpp"
#include "brookesia/service_display/service_display.hpp"
#include "brookesia/service_manager/dataflow/provider.hpp"
#include "brookesia/service_manager/dataflow/registry.hpp"
#include "brookesia/service_manager/dataflow/visual/operation.hpp"
#include "brookesia/service_manager/service/manager.hpp"

namespace esp_brookesia::service {

namespace {

constexpr std::string_view DATAFLOW_PROVIDER_ID = "Display";
constexpr std::string_view DATAFLOW_OUTPUT_ROLE = "display";

dataflow::VisualPixelFormat to_dataflow_pixel_format(Display::PixelFormat pixel_format)
{
    switch (pixel_format) {
    case Display::PixelFormat::RGB565:
        return dataflow::VisualPixelFormat::RGB565;
    case Display::PixelFormat::RGB888:
        return dataflow::VisualPixelFormat::RGB888;
    default:
        return dataflow::VisualPixelFormat::Unknown;
    }
}

Display::PixelFormat to_display_pixel_format(dataflow::VisualPixelFormat pixel_format)
{
    switch (pixel_format) {
    case dataflow::VisualPixelFormat::RGB565:
        return Display::PixelFormat::RGB565;
    case dataflow::VisualPixelFormat::RGB888:
        return Display::PixelFormat::RGB888;
    default:
        return Display::PixelFormat::Max;
    }
}

dataflow::VisualPresentResult to_dataflow_present_result(Display::PresentResult result)
{
    switch (result) {
    case Display::PresentResult::Presented:
        return dataflow::VisualPresentResult::Presented;
    case Display::PresentResult::DroppedNotActive:
        return dataflow::VisualPresentResult::DroppedNotActive;
    case Display::PresentResult::DroppedInvalidFrame:
        return dataflow::VisualPresentResult::DroppedInvalidFrame;
    case Display::PresentResult::DroppedQueueFull:
        return dataflow::VisualPresentResult::DroppedQueueFull;
    default:
        return dataflow::VisualPresentResult::Error;
    }
}

dataflow::VisualPresentSubmitState to_dataflow_submit_state(Display::PresentSubmitState state)
{
    switch (state) {
    case Display::PresentSubmitState::Queued:
        return dataflow::VisualPresentSubmitState::Queued;
    case Display::PresentSubmitState::DroppedNotActive:
        return dataflow::VisualPresentSubmitState::DroppedNotActive;
    case Display::PresentSubmitState::DroppedInvalidFrame:
        return dataflow::VisualPresentSubmitState::DroppedInvalidFrame;
    default:
        return dataflow::VisualPresentSubmitState::Error;
    }
}

dataflow::VisualPresentResult to_dataflow_present_result(Display::PresentSubmitState state)
{
    switch (state) {
    case Display::PresentSubmitState::DroppedNotActive:
        return dataflow::VisualPresentResult::DroppedNotActive;
    case Display::PresentSubmitState::DroppedInvalidFrame:
        return dataflow::VisualPresentResult::DroppedInvalidFrame;
    case Display::PresentSubmitState::Queued:
    case Display::PresentSubmitState::Error:
    default:
        return dataflow::VisualPresentResult::Error;
    }
}

dataflow::SourceState to_dataflow_source_state(Display::SourceState state)
{
    switch (state) {
    case Display::SourceState::Registered:
        return dataflow::SourceState::Registered;
    case Display::SourceState::Requested:
        return dataflow::SourceState::Requested;
    case Display::SourceState::Granted:
        return dataflow::SourceState::Granted;
    case Display::SourceState::Dummy:
        return dataflow::SourceState::Released;
    case Display::SourceState::Revoked:
    default:
        return dataflow::SourceState::Revoked;
    }
}

dataflow::VisualByteOrder get_visual_byte_order(const Display::OutputInfo &output)
{
    if ((output.slot != Display::OutputSlot::HalPanel) || output.panel_instance.empty()) {
        return dataflow::VisualByteOrder::Native;
    }

    auto panel_handles = hal::acquire_interfaces<hal::display::PanelIface>();
    for (auto &panel_handle : panel_handles) {
        if (panel_handle.instance_name() != output.panel_instance) {
            continue;
        }
        hal::display::PanelIface::DriverSpecific specific;
        if (panel_handle->get_driver_specific(specific) &&
                (specific.bus_type == hal::display::PanelIface::BusType::Generic)) {
            return dataflow::VisualByteOrder::Swap16;
        }
        break;
    }
    return dataflow::VisualByteOrder::Native;
}

dataflow::VisualOutputInfo to_dataflow_output(const Display::OutputInfo &output)
{
    return {
        .output = {
            .provider_id = std::string(DATAFLOW_PROVIDER_ID),
            .name = output.name,
            .role = std::string(DATAFLOW_OUTPUT_ROLE),
            .id = output.id,
            .width = output.width,
            .height = output.height,
            .model = dataflow::Model::Visual,
        },
        .pixel_format = to_dataflow_pixel_format(output.pixel_format),
        .slot = output.slot == Display::OutputSlot::Buffer ?
        dataflow::VisualOutputSlot::Buffer : dataflow::VisualOutputSlot::Device,
        .byte_order = get_visual_byte_order(output),
        .stride_bytes = 0,
    };
}

dataflow::SourceInfo to_dataflow_source(const Display::SourceInfo &source)
{
    return {
        .name = source.name,
        .role = source.role,
        .preferred_outputs = source.preferred_outputs,
        .priority = source.priority,
    };
}

Display::FrameInfo to_display_frame(const dataflow::VisualFrameInfo &frame)
{
    return {
        .x = frame.x,
        .y = frame.y,
        .width = frame.width,
        .height = frame.height,
        .pixel_format = to_display_pixel_format(frame.pixel_format),
    };
}

class DisplayVisualOperation final: public dataflow::VisualOperation {
public:
    DisplayVisualOperation(Display &display, uint32_t source_id, std::string source_name)
        : display_(&display)
        , source_id_(source_id)
        , source_name_(std::move(source_name))
    {
        output_unregistered_connection_ = display_->connect_output_unregistered(
        [this](const std::string & output_name) {
            handle_output_unregistered(output_name);
        }
                                          );
    }

    ~DisplayVisualOperation() override
    {
        close();
    }

    std::vector<dataflow::VisualOutputInfo> get_outputs() const override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return {};
        }

        auto outputs = display->get_outputs();
        std::vector<dataflow::VisualOutputInfo> result;
        result.reserve(outputs.size());
        for (const auto &output : outputs) {
            result.push_back(to_dataflow_output(output));
        }
        return result;
    }

    std::vector<dataflow::SourceInfo> get_sources() const override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return {};
        }

        auto sources = display->get_sources();
        std::vector<dataflow::SourceInfo> result;
        result.reserve(sources.size());
        for (const auto &source : sources) {
            result.push_back(to_dataflow_source(source));
        }
        return result;
    }

    std::vector<std::string> get_source_roles() const override
    {
        auto *display = get_display();
        return display == nullptr ? std::vector<std::string> {} : display->get_source_roles();
    }

    std::expected<void, std::string> request_output(std::string_view output_name) override
    {
        auto route = get_route();
        if (!route.has_value()) {
            return std::unexpected("Display visual operation is unavailable");
        }

        auto result = route->display->request_output(route->source_id, output_name);
        if (result) {
            std::lock_guard lock(mutex_);
            requested_outputs_.insert(std::string(output_name));
        }
        return result;
    }

    std::expected<void, std::string> release_output(std::string_view output_name) override
    {
        auto route = get_route();
        if (!route.has_value()) {
            return std::unexpected("Display visual operation is unavailable");
        }

        auto result = route->display->release_output(route->source_id, output_name);
        if (result) {
            std::lock_guard lock(mutex_);
            requested_outputs_.erase(std::string(output_name));
        }
        return result;
    }

    std::expected<void, std::string> set_active_source(std::string_view output_name) override
    {
        auto route = get_route();
        if (!route.has_value()) {
            return std::unexpected("Display visual operation is unavailable");
        }
        return route->display->set_active_source(output_name, route->source_name);
    }

    std::expected<void, std::string> set_active_source_named(
        std::string_view output_name, std::string_view source_name
    ) override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return std::unexpected("Display visual operation is unavailable");
        }
        return display->set_active_source(output_name, source_name);
    }

    std::expected<void, std::string> set_active_source_role(
        std::string_view output_name, std::string_view role
    ) override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return std::unexpected("Display visual operation is unavailable");
        }
        return display->set_active_source_role(output_name, role);
    }

    std::expected<std::string, std::string> get_active_source(std::string_view output_name) const override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return std::unexpected("Display visual operation is unavailable");
        }
        return display->get_active_source(output_name);
    }

    std::expected<std::string, std::string> get_active_source_role(std::string_view output_name) const override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return std::unexpected("Display visual operation is unavailable");
        }
        return display->get_active_role(output_name);
    }

    dataflow::VisualPresentResult present_frame_sync(
        std::string_view output_name, const dataflow::VisualFrameInfo &frame, std::span<const uint8_t> data,
        uint32_t timeout_ms
    ) override
    {
        auto route = get_route();
        if (!route.has_value()) {
            return dataflow::VisualPresentResult::Error;
        }
        return to_dataflow_present_result(
                   route->display->present_frame_sync(
                       route->source_id, output_name, to_display_frame(frame), RawBuffer(data.data(), data.size()), timeout_ms
                   )
               );
    }

    dataflow::VisualPresentResult present_buffer_frame_sync(
        std::string_view output_name, const dataflow::VisualFrameInfo &frame, BufferWriter writer
    ) override
    {
        if (writer == nullptr) {
            return dataflow::VisualPresentResult::Error;
        }
        auto route = get_route();
        if (!route.has_value()) {
            return dataflow::VisualPresentResult::Error;
        }

        auto display_writer = [writer = std::move(writer)](Display::BufferOutputView & view) {
            auto *data = view.buffer.to_ptr<uint8_t>();
            if ((data == nullptr) || (view.buffer.data_size == 0)) {
                return false;
            }
            auto dataflow_view = dataflow::VisualBufferView{
                .output = to_dataflow_output(view.info),
                .data = std::span<uint8_t>(data, view.buffer.data_size),
                .stride_bytes = view.stride_bytes,
            };
            dataflow_view.output.stride_bytes = view.stride_bytes;
            return writer(dataflow_view);
        };
        return to_dataflow_present_result(
                   route->display->present_buffer_frame_sync(
                       route->source_id, output_name, to_display_frame(frame), std::move(display_writer)
                   )
               );
    }

    dataflow::VisualAsyncSubmitResult present_frame_async(
        std::string_view output_name, const dataflow::VisualFrameInfo &frame, std::span<const uint8_t> data,
        CompletionCallback on_complete, uint32_t timeout_ms
    ) override
    {
        if (on_complete == nullptr) {
            return {};
        }
        auto route = get_route();
        if (!route.has_value()) {
            on_complete(0, dataflow::VisualPresentResult::Error);
            return {
                .frame_id = 0,
                .state = dataflow::VisualPresentSubmitState::Error,
            };
        }

        auto callback_completed = std::make_shared<std::atomic<bool>>(false);
        auto complete_once = [on_complete, callback_completed](uint32_t frame_id, dataflow::VisualPresentResult result) {
            bool expected = false;
            if (callback_completed->compare_exchange_strong(expected, true)) {
                on_complete(frame_id, result);
            }
        };
        auto completion = [complete_once](uint32_t frame_id, Display::PresentResult result) {
            complete_once(frame_id, to_dataflow_present_result(result));
        };
        auto result = route->display->present_frame_async(
                          route->source_id, output_name, to_display_frame(frame), RawBuffer(data.data(), data.size()),
                          std::move(completion), timeout_ms
                      );
        if (result.state != Display::PresentSubmitState::Queued) {
            complete_once(result.frame_id, to_dataflow_present_result(result.state));
        }
        return {
            .frame_id = result.frame_id,
            .state = to_dataflow_submit_state(result.state),
        };
    }

    std::expected<dataflow::VisualBufferView, std::string> map_output_buffer(std::string_view output_name) const override
    {
        auto *display = get_display();
        if (display == nullptr) {
            return std::unexpected("Display visual operation is unavailable");
        }

        auto view = display->get_buffer_output(output_name);
        if (!view) {
            return std::unexpected(view.error());
        }
        auto *data = view->buffer.to_ptr<uint8_t>();
        if ((data == nullptr) || (view->buffer.data_size == 0)) {
            return std::unexpected("Display buffer output is not writable");
        }
        auto result = dataflow::VisualBufferView{
            .output = to_dataflow_output(view->info),
            .data = std::span<uint8_t>(data, view->buffer.data_size),
            .stride_bytes = view->stride_bytes,
        };
        result.output.stride_bytes = view->stride_bytes;
        return result;
    }

    lib_utils::connection connect_source_state_changed(SourceStateCallback callback) override
    {
        auto *display = get_display();
        if ((display == nullptr) || (callback == nullptr)) {
            return {};
        }

        const auto source_name = get_source_name();
        return display->connect_source_state_changed(
                   [source_name, callback = std::move(callback)](
                       const std::string & changed_source_name, const std::string & output_name,
                       Display::SourceState state
        ) {
            if (changed_source_name == source_name) {
                callback(changed_source_name, output_name, to_dataflow_source_state(state));
            }
        }
               );
    }

    lib_utils::connection connect_active_source_changed(ActiveSourceCallback callback) override
    {
        auto *display = get_display();
        if ((display == nullptr) || (callback == nullptr)) {
            return {};
        }
        return display->connect_active_source_changed(std::move(callback));
    }

protected:
    void on_close() override
    {
        Display *display = nullptr;
        uint32_t source_id = 0;
        std::set<std::string> requested_outputs;
        {
            std::lock_guard lock(mutex_);
            output_unregistered_connection_.disconnect();
            display = std::exchange(display_, nullptr);
            source_id = std::exchange(source_id_, 0);
            requested_outputs = std::move(requested_outputs_);
        }

        if ((display == nullptr) || (source_id == 0)) {
            return;
        }
        for (const auto &output_name : requested_outputs) {
            (void)display->release_output(source_id, output_name);
        }
        (void)display->unregister_source(source_id);
    }

private:
    struct Route {
        Display *display = nullptr;
        uint32_t source_id = 0;
        std::string source_name;
    };

    Display *get_display() const
    {
        if (!is_available()) {
            return nullptr;
        }
        std::lock_guard lock(mutex_);
        return display_;
    }

    std::optional<Route> get_route() const
    {
        if (!is_available()) {
            return std::nullopt;
        }
        std::lock_guard lock(mutex_);
        if ((display_ == nullptr) || (source_id_ == 0)) {
            return std::nullopt;
        }
        return Route{
            .display = display_,
            .source_id = source_id_,
            .source_name = source_name_,
        };
    }

    std::string get_source_name() const
    {
        std::lock_guard lock(mutex_);
        return source_name_;
    }

    void handle_output_unregistered(const std::string &output_name)
    {
        bool unavailable = false;
        {
            std::lock_guard lock(mutex_);
            unavailable = requested_outputs_.contains(output_name);
            requested_outputs_.erase(output_name);
        }
        if (unavailable) {
            set_state(dataflow::OperationState::Unavailable);
        }
    }

    mutable std::mutex mutex_;
    Display *display_ = nullptr;
    uint32_t source_id_ = 0;
    std::string source_name_;
    std::set<std::string> requested_outputs_;
    lib_utils::connection output_unregistered_connection_;
};

class DisplayDataFlowProvider final: public dataflow::DataFlowProvider {
public:
    explicit DisplayDataFlowProvider(Display &display)
        : display_(display)
    {}

    dataflow::ProviderInfo get_provider_info() const override
    {
        return {
            .id = std::string(DATAFLOW_PROVIDER_ID),
            .service_name = std::string(Display::Helper::get_name()),
            .description = "Display visual output and touch provider.",
            .models = {dataflow::Model::Visual},
            .priority = 0,
            .available = true,
        };
    }

    std::vector<dataflow::OutputInfo> list_outputs(dataflow::Model model) const override
    {
        if (model != dataflow::Model::Visual) {
            return {};
        }
        auto outputs = display_.get_outputs();
        std::vector<dataflow::OutputInfo> result;
        result.reserve(outputs.size());
        for (const auto &output : outputs) {
            result.push_back(to_dataflow_output(output).output);
        }
        return result;
    }

    std::expected<std::shared_ptr<dataflow::VisualOperation>, std::string> open_visual_operation(
        const dataflow::VisualOperationConfig &config
    ) override
    {
        if (config.source.name.empty()) {
            return std::unexpected("Display visual operation source name cannot be empty");
        }

        auto source = Display::SourceInfo{
            .name = config.source.name,
            .role = config.source.role,
            .preferred_outputs = config.source.preferred_outputs,
            .priority = config.source.priority,
        };
        if (!config.output_name.empty() && source.preferred_outputs.empty()) {
            source.preferred_outputs.push_back(config.output_name);
        }
        auto source_id = display_.register_source(std::move(source));
        if (!source_id) {
            return std::unexpected("Failed to register Display data-flow source: " + source_id.error());
        }

        auto operation = std::make_shared<DisplayVisualOperation>(display_, *source_id, config.source.name);
        if (config.request_output) {
            if (config.output_name.empty()) {
                operation->close();
                return std::unexpected("Display output name is required when requesting an output");
            }
            auto request_result = operation->request_output(config.output_name);
            if (!request_result) {
                operation->close();
                return std::unexpected("Failed to request Display output: " + request_result.error());
            }
        }
        if (config.activate_source) {
            if (config.output_name.empty()) {
                operation->close();
                return std::unexpected("Display output name is required when activating a source");
            }
            auto activate_result = operation->set_active_source(config.output_name);
            if (!activate_result) {
                operation->close();
                return std::unexpected("Failed to activate Display source: " + activate_result.error());
            }
        }
        return operation;
    }

private:
    Display &display_;
};

} // namespace

std::expected<dataflow::ProviderRegistration, std::string> register_display_dataflow_provider(Display &display)
{
    auto provider = std::make_shared<DisplayDataFlowProvider>(display);
    return ServiceManager::get_instance().get_dataflow_registry().register_provider(std::move(provider));
}

} // namespace esp_brookesia::service
