/*
 * Linux/SDL2 entry point for the ESP VoCat chatbot UI.
 *
 * The display and touch path is the real ESP-Brookesia host path:
 * brookesia_hal_linux -> display service -> brookesia_gui_lvgl -> Speaker UI.
 * Hardware-only chatbot services are intentionally not started here.
 */
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "boost/chrono.hpp"
#include "boost/json.hpp"
#include "boost/thread.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/hal_linux.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_display/service_display.hpp"
#include "brookesia/service_helper.hpp"
#include "brookesia/service_manager.hpp"
#include "brightness_adapter.hpp"
#include "host_capabilities.hpp"
#include "keyboard_adapter.hpp"
#include "persistence_adapter.hpp"
#include "power_adapter.hpp"
#include "screenshot_capture.hpp"
#include "volume_adapter.hpp"
#include "wifi_adapter.hpp"
#include "weather_adapter.hpp"

extern "C" {
#include "speaker_ui.h"
}

using namespace esp_brookesia;
using DisplayHelper = service::helper::Display;
using AudioPlaybackHelper = service::helper::AudioPlayback;

namespace host_sim::tests {
bool is_self_test_option(std::string_view option);
int run_self_test(std::string_view option, const std::string &output_name,
                  lv_indev_t *input, uint32_t backlight_output_id,
                  host_sim::WifiAdapter *wifi_adapter,
                  host_sim::WeatherAdapter *weather_adapter);
} // namespace host_sim::tests

namespace {

constexpr uint16_t WINDOW_WIDTH = 360;
constexpr uint16_t WINDOW_HEIGHT = 360;
constexpr int MAIN_LOOP_DELAY_MS = 16;
constexpr uint32_t DISPLAY_SERVICE_TIMEOUT_MS = 1000;
constexpr std::string_view SCREENSHOT_OUTPUT_NAME = "SpeakerUiScreenshot";

int fail(std::string_view stage, std::string_view error)
{
    std::cerr << "host_sim: " << stage << " failed: " << error << '\n';
    return EXIT_FAILURE;
}

int run_main(int argc, char **argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--print-capabilities") {
        host_sim::print_configured_capabilities(std::cout);
        return EXIT_SUCCESS;
    }

    host_sim::print_configured_capabilities(std::cout);
    const bool screenshot_mode = argc >= 2 && std::string_view(argv[1]) == "--screenshot";
    if (screenshot_mode && argc < 3) {
        return fail("screenshot", "--screenshot requires an output path");
    }
    const std::string_view screenshot_path = screenshot_mode ? argv[2] : "";
    const std::string_view screenshot_screen = screenshot_mode && argc >= 4 ? argv[3] : "idle";
    const std::string_view option = argc >= 2 ? argv[1] : "";
    const bool self_test_mode = argc == 2 && host_sim::tests::is_self_test_option(argv[1]);
    if (host_sim::configured_capabilities().time == std::string_view("sntp") &&
        !screenshot_mode && !self_test_mode) {
        std::string sntp_error;
        if (!host_sim::sync_sntp_once(3000, sntp_error)) {
            std::cerr << "host_sim: SNTP sync unavailable, keeping system time: "
                      << sntp_error << '\n';
        }
    }
    const bool volume_runtime_enabled = !screenshot_mode &&
        (!self_test_mode || option == "--self-test-volume-simulation");
    const bool audio_test_mode = option == "--self-test-audio-stub" ||
        option == "--self-test-audio-real";
    const bool power_runtime_enabled = !screenshot_mode &&
        (!self_test_mode || option == "--self-test-power-simulation");
    const bool wifi_runtime_enabled = !screenshot_mode &&
        (!self_test_mode || option == "--self-test-wifi-mock" ||
         option == "--self-test-wifi-real-readonly");
    const bool persistence_test_mode = option == "--self-test-persistence-write" ||
        option == "--self-test-persistence-read" || option == "--self-test-factory-reset" ||
        option == "--self-test-persistence-defaults";
    const bool persistence_runtime_enabled = !screenshot_mode &&
        (!self_test_mode || option == "--self-test-factory-reset");
    const bool audio_service_enabled = volume_runtime_enabled || persistence_test_mode ||
        audio_test_mode;

    auto &display_device = hal::DisplayLinuxDevice::get_instance();
    if (!display_device.configure({
            .width_px = WINDOW_WIDTH,
            .height_px = WINDOW_HEIGHT,
            .window_title = "ESP VoCat v1.0 - Chatbot PC Simulator",
            .render_driver = "software",
        })) {
        return fail("display configure", "could not configure the HAL Linux display");
    }

    auto &service_manager = service::ServiceManager::get_instance();
    if (!service_manager.start()) {
        return fail("service manager start", "could not start ServiceManager");
    }
    lib_utils::FunctionGuard service_manager_cleanup([&service_manager]() {
        service_manager.deinit();
    });

    auto display_binding = service_manager.bind(DisplayHelper::get_name().data());
    if (!display_binding.is_valid()) {
        return fail("display bind", "Display service is unavailable");
    }

    service::ServiceBinding audio_playback_binding;
    if (audio_service_enabled) {
        audio_playback_binding = service_manager.bind(AudioPlaybackHelper::get_name().data());
        if (!audio_playback_binding.is_valid()) {
            return fail("audio playback bind", "Audio Playback service is unavailable");
        }
    }

    auto outputs_result = DisplayHelper::call_function_sync<boost::json::array>(
                              DisplayHelper::FunctionId::GetOutputs,
                              service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                          );
    if (!outputs_result) {
        return fail("display outputs", outputs_result.error());
    }

    std::vector<DisplayHelper::OutputInfo> outputs;
    if (!BROOKESIA_DESCRIBE_FROM_JSON(outputs_result.value(), outputs) || outputs.empty()) {
        return fail("display outputs", "no usable display output was registered");
    }
    const auto backlight_output = std::find_if(outputs.begin(), outputs.end(), [](const auto &output) {
        return output.backlight.has_value();
    });
    if (backlight_output != outputs.end()) {
        (void)DisplayHelper::call_function_sync(
            DisplayHelper::FunctionId::SetBacklightOnOff,
            static_cast<double>(backlight_output->id),
            true,
            service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
        );
    }

    std::vector<uint8_t> screenshot_buffer;
    if (screenshot_mode) {
        screenshot_buffer.assign(static_cast<size_t>(WINDOW_WIDTH) * WINDOW_HEIGHT * 2U, 0);
        auto output_result = service::Display::get_instance().register_output(
            service::Display::BufferOutputConfig{
                .name = std::string(SCREENSHOT_OUTPUT_NAME),
                .width = WINDOW_WIDTH,
                .height = WINDOW_HEIGHT,
                .pixel_format = service::Display::PixelFormat::RGB565,
                .buffer = service::RawBuffer(screenshot_buffer.data(), screenshot_buffer.size()),
                .stride_bytes = static_cast<size_t>(WINDOW_WIDTH) * 2U,
            }
        );
        if (!output_result) {
            return fail("screenshot output", output_result.error());
        }
    }

    auto &display_source = gui::lvgl::DisplaySource::get_instance();
    gui::lvgl::DisplaySourceConfig source_config;
    source_config.tick_period_ms = 5;
    if (screenshot_mode) {
        source_config.output_name = std::string(SCREENSHOT_OUTPUT_NAME);
    }
    if (!display_source.start(source_config)) {
        return fail("LVGL display source", "could not start brookesia_gui_lvgl");
    }
    lib_utils::FunctionGuard display_source_cleanup([&display_source]() {
        display_source.stop_timers();
        display_source.release_display_service();
        display_source.stop();
    });

    auto activate_result = DisplayHelper::call_function_sync(
                               DisplayHelper::FunctionId::SetActiveSourceRole,
                               display_source.output_name(),
                               source_config.source_role,
                               service::helper::Timeout(DISPLAY_SERVICE_TIMEOUT_MS)
                           );
    if (!activate_result) {
        return fail("display activate", activate_result.error());
    }

    auto *input = display_source.input();
    if (input == nullptr) {
        return fail("LVGL input", "brookesia_gui_lvgl did not create a pointer input");
    }

    host_sim::WeatherAdapter weather_adapter;
    const bool allow_real_weather = !screenshot_mode &&
        (!self_test_mode || option == "--self-test-weather-real");
    if (!weather_adapter.start(allow_real_weather)) {
        return fail("weather adapter", weather_adapter.last_error());
    }
    lib_utils::FunctionGuard weather_cleanup([&weather_adapter]() {
        weather_adapter.stop();
    });

    gui::lvgl::lock_thread();
    speaker_ui_create();
    speaker_ui_set_input(input);
    const bool keyboard_tweaks_applied = host_sim::apply_keyboard_layout_tweaks();
    gui::lvgl::unlock_thread();
    if (!keyboard_tweaks_applied) {
        return fail("keyboard adapter", "could not locate the WLAN keyboard");
    }

    host_sim::BrightnessAdapter brightness_adapter;
    if (!screenshot_mode && backlight_output != outputs.end()) {
        gui::lvgl::lock_thread();
        const bool brightness_started = brightness_adapter.start(backlight_output->id);
        gui::lvgl::unlock_thread();
        if (!brightness_started) {
            return fail("brightness adapter", "could not create the LVGL bridge timer");
        }
    }
    lib_utils::FunctionGuard brightness_cleanup([&brightness_adapter]() {
        gui::lvgl::lock_thread();
        brightness_adapter.stop();
        gui::lvgl::unlock_thread();
    });

    host_sim::VolumeAdapter volume_adapter;
    if (volume_runtime_enabled) {
        gui::lvgl::lock_thread();
        const bool volume_started = volume_adapter.start();
        gui::lvgl::unlock_thread();
        if (!volume_started) {
            return fail("volume adapter", "could not bridge the Audio Playback service to the UI");
        }
    }
    lib_utils::FunctionGuard volume_cleanup([&volume_adapter]() {
        gui::lvgl::lock_thread();
        volume_adapter.stop();
        gui::lvgl::unlock_thread();
    });

    host_sim::PowerAdapter power_adapter;
    if (power_runtime_enabled) {
        gui::lvgl::lock_thread();
        const bool power_started = power_adapter.start();
        gui::lvgl::unlock_thread();
        if (!power_started) {
            return fail("power adapter", "could not connect battery state to Quick Settings");
        }
    }
    lib_utils::FunctionGuard power_cleanup([&power_adapter]() {
        gui::lvgl::lock_thread();
        power_adapter.stop();
        gui::lvgl::unlock_thread();
    });

    host_sim::WifiAdapter wifi_adapter;
    if (wifi_runtime_enabled) {
        gui::lvgl::lock_thread();
        const bool wifi_started = wifi_adapter.start();
        gui::lvgl::unlock_thread();
        if (!wifi_started) {
            return fail("Wi-Fi adapter", "could not connect the Linux Wi-Fi backend to the UI");
        }
    }
    lib_utils::FunctionGuard wifi_cleanup([&wifi_adapter]() {
        gui::lvgl::lock_thread();
        wifi_adapter.stop();
        gui::lvgl::unlock_thread();
    });

    host_sim::PersistenceAdapter persistence_adapter;
    if (persistence_runtime_enabled) {
        gui::lvgl::lock_thread();
        const bool persistence_started = persistence_adapter.start(backlight_output->id);
        gui::lvgl::unlock_thread();
        if (!persistence_started) {
            return fail("persistence adapter", "could not connect persistent state to the UI");
        }
    }
    lib_utils::FunctionGuard persistence_cleanup([&persistence_adapter]() {
        gui::lvgl::lock_thread();
        persistence_adapter.stop();
        gui::lvgl::unlock_thread();
    });

    if (self_test_mode) {
        if (backlight_output == outputs.end()) {
            return fail("self-test", "no backlight-bound display output is available");
        }
        return host_sim::tests::run_self_test(
            argv[1], display_source.output_name(), input, backlight_output->id,
            &wifi_adapter, &weather_adapter
        );
    }
    if (screenshot_mode) {
        return host_sim::tests::capture_screenshot(
            screenshot_path,
            screenshot_screen,
            display_source.display(),
            screenshot_buffer,
            WINDOW_WIDTH,
            WINDOW_HEIGHT
        );
    }

    std::cout << "ESP VoCat simulator is running. Close the SDL window to stop.\n";
    while (!display_device.is_quit_requested()) {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(MAIN_LOOP_DELAY_MS));
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **argv) noexcept
{
    try {
        return run_main(argc, argv);
    } catch (const std::exception &e) {
        return fail("unhandled exception", e.what());
    } catch (...) {
        return fail("unhandled exception", "unknown error");
    }
}
