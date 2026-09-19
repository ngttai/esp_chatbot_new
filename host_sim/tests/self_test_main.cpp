#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <utility>

#include "brookesia/gui_lvgl.hpp"
#include "brookesia/lib_utils.hpp"
#include "brookesia/service_helper.hpp"
#include "input_injector.hpp"
#include "persistence_adapter.hpp"

extern "C" {
#include "speaker_ui.h"
#include "ui.h"
}

namespace host_sim::tests {
namespace {

class UiLock {
public:
    UiLock() { esp_brookesia::gui::lvgl::lock_thread(); }
    ~UiLock() { esp_brookesia::gui::lvgl::unlock_thread(); }

    UiLock(const UiLock &) = delete;
    UiLock &operator=(const UiLock &) = delete;
};

template <typename Function>
decltype(auto) with_ui_lock(Function &&function)
{
    UiLock lock;
    return std::forward<Function>(function)();
}

bool show(const char *name)
{
    return with_ui_lock([&]() { return speaker_ui_show(name); });
}

bool active(const char *name)
{
    return with_ui_lock([&]() { return speaker_ui_is_screen_active(name); });
}

bool input_ok(bool result, const InputInjector &input)
{
    if (result) {
        return true;
    }
    std::fprintf(stderr, "Brookesia touch injection failed: %s\n", input.last_error().c_str());
    return false;
}

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

auto get_backlight_brightness(uint32_t output_id)
{
    using DisplayHelper = esp_brookesia::service::helper::Display;
    return DisplayHelper::call_function_sync<double>(
        DisplayHelper::FunctionId::GetBacklightBrightness,
        static_cast<double>(output_id),
        esp_brookesia::service::helper::Timeout(1000)
    );
}

auto get_backlight_on_off(uint32_t output_id)
{
    using DisplayHelper = esp_brookesia::service::helper::Display;
    return DisplayHelper::call_function_sync<bool>(
        DisplayHelper::FunctionId::GetBacklightOnOff,
        static_cast<double>(output_id),
        esp_brookesia::service::helper::Timeout(1000)
    );
}

int test_settings_pages(InputInjector &input)
{
    struct Case {
        const char *start;
        int y;
        const char *expected;
    };
    static constexpr std::array<Case, 6> cases{{
        {"settings", 126, "wlan"},
        {"settings", 226, "sound"},
        {"settings", 274, "display"},
        {"settings-bottom", 160, "about"},
        {"settings-bottom", 210, "developer"},
        {"settings-bottom", 258, "restore"},
    }};

    for (const auto &test_case : cases) {
        show(test_case.start);
        input.pump(30);
        if (!input_ok(input.click(180, test_case.y), input)) return 1;
        if (!active(test_case.expected)) {
            std::fprintf(stderr, "Settings page navigation failed: %s\n", test_case.expected);
            return 9;
        }
        if (!input_ok(input.click(180, 38), input)) return 1;
        if (!active("settings")) {
            std::fprintf(stderr, "Settings child back navigation failed: %s\n", test_case.expected);
            return 10;
        }
    }

    static constexpr std::array<const char *, 2> nested_cases{{"wlan-connect", "softap"}};
    for (const char *nested_case : nested_cases) {
        show(nested_case);
        input.pump(30);
        if (!input_ok(input.click(180, 38), input)) return 1;
        if (!active("wlan")) {
            std::fprintf(stderr, "Nested WLAN back navigation failed: %s\n", nested_case);
            return 11;
        }
    }
    std::puts("Settings child-page navigation passed");
    return 0;
}

int test_settings_bar()
{
    if (!with_ui_lock([]() { return speaker_ui_settings_bar_stays_fixed(); })) {
        std::fprintf(stderr, "Settings bottom bar moved while scrolling\n");
        return 6;
    }
    std::puts("Settings bottom bar remained fixed while scrolling");
    return 0;
}

int test_idle(InputInjector &input)
{
    input.pump(20);
    if (!input_ok(input.press(180, 180), input)) return 1;
    input.pump(120);
    if (!input_ok(input.release(), input)) return 1;
    input.pump(30);

    if (!with_ui_lock([]() { return speaker_ui_is_launcher_active(); })) {
        std::fprintf(stderr, "Black-screen long press failed\n");
        return 5;
    }
    std::puts("Black-screen long press passed");
    return 0;
}

int test_launcher(InputInjector &input)
{
    show("launcher");
    input.pump(20);
    if (!input_ok(input.press(100, 165), input)) return 1;
    input.pump(5);
    if (!input_ok(input.move(25, 165), input)) return 1;
    input.pump(10);
    if (!input_ok(input.release(), input)) return 1;
    input.pump(10);

    if (with_ui_lock([]() { return speaker_ui_launcher_page(); }) != 1) {
        std::fprintf(stderr, "Launcher icon swipe failed\n");
        return 4;
    }
    std::puts("Launcher icon swipe passed");
    return 0;
}

int test_home(InputInjector &input)
{
    show("launcher");
    input.pump(20);
    if (!input_ok(input.press(180, 350), input)) return 1;
    input.pump(5);
    if (!input_ok(input.move(180, 340), input)) return 1;
    input.pump(12);

    int32_t bar_center = 0;
    int32_t bar_width = 0;
    bool geometry_ok = with_ui_lock([&]() {
        return speaker_ui_get_active_home_bar_geometry(&bar_center, &bar_width);
    });
    if (!geometry_ok || bar_center != 180 || bar_width < 53 || bar_width > 55) {
        std::fprintf(stderr,
                     "Bottom bar did not shrink symmetrically at half travel: center=%d width=%d\n",
                     static_cast<int>(bar_center), static_cast<int>(bar_width));
        return 7;
    }

    if (!input_ok(input.move(180, 330), input)) return 1;
    input.pump(12);
    geometry_ok = with_ui_lock([&]() {
        return speaker_ui_get_active_home_bar_geometry(&bar_center, &bar_width);
    });
    if (!geometry_ok || bar_center != 180 || bar_width > 6) {
        std::fprintf(stderr,
                     "Bottom bar did not converge on its center: center=%d width=%d\n",
                     static_cast<int>(bar_center), static_cast<int>(bar_width));
        return 8;
    }

    if (!input_ok(input.release(), input)) return 1;
    input.pump(30);
    if (!with_ui_lock([]() { return speaker_ui_is_idle_active(); })) {
        std::fprintf(stderr, "Bottom-edge upward black-screen gesture failed\n");
        return 3;
    }
    std::puts("Bottom-edge upward black-screen gesture passed");
    return 0;
}

int test_settings_no_scroll(InputInjector &input)
{
    show("settings");
    input.pump(20);
    int32_t scroll_before = with_ui_lock([]() { return speaker_ui_get_settings_scroll_y(); });

    if (!input_ok(input.press(180, 355), input)) return 1;
    input.pump(5);
    for (int y = 350; y >= 260; y -= 10) {
        if (!input_ok(input.move(180, y), input)) return 1;
        input.pump(5);
    }
    int32_t scroll_during = with_ui_lock([]() { return speaker_ui_get_settings_scroll_y(); });
    if (!input_ok(input.release(), input)) return 1;
    input.pump(20);

    if (scroll_during != scroll_before) {
        std::fprintf(stderr,
                     "Settings list scrolled during a bottom-edge Home gesture: before=%d during=%d\n",
                     static_cast<int>(scroll_before), static_cast<int>(scroll_during));
        return 10;
    }
    std::puts("Settings list stayed put during a bottom-edge Home gesture");
    return 0;
}

int drag(InputInjector &input, int start_x, int start_y, int end_x, int end_y,
         int step, int pump_per_step, int settle)
{
    if (!input_ok(input.press(start_x, start_y), input)) return 1;
    input.pump(5);
    if (step > 0) {
        for (int y = start_y; y <= end_y; y += step) {
            if (!input_ok(input.move(end_x, y), input)) return 1;
            input.pump(pump_per_step);
        }
    } else {
        for (int y = start_y; y >= end_y; y += step) {
            if (!input_ok(input.move(end_x, y), input)) return 1;
            input.pump(pump_per_step);
        }
    }
    if (!input_ok(input.release(), input)) return 1;
    input.pump(settle);
    return 0;
}

int test_quick_gesture(InputInjector &input)
{
    show("launcher");
    input.pump(20);
    if (int result = drag(input, 180, 5, 180, 260, 15, 5, 30); result != 0) return result;
    if (!active("quick")) {
        std::fprintf(stderr, "Top-edge drag past the threshold did not open Quick Settings\n");
        return 11;
    }

    show("launcher");
    input.pump(20);
    if (!input_ok(input.press(180, 5), input)) return 1;
    input.pump(5);
    if (!input_ok(input.move(180, 40), input)) return 1;
    input.pump(5);
    if (!input_ok(input.release(), input)) return 1;
    input.pump(30);
    if (active("quick") || !with_ui_lock([]() { return speaker_ui_is_launcher_active(); })) {
        std::fprintf(stderr,
                     "A short top-edge drag under the threshold should cancel, not open Quick Settings\n");
        return 12;
    }

    show("settings");
    input.pump(20);
    if (int result = drag(input, 180, 5, 180, 260, 15, 5, 30); result != 0) return result;
    if (!active("quick")) {
        std::fprintf(stderr, "Top-edge drag from a non-launcher screen did not open Quick Settings\n");
        return 13;
    }

    std::puts("Quick Settings top-edge drag gesture passed");
    return 0;
}

int test_quick_close_gesture(InputInjector &input)
{
    show("quick");
    input.pump(20);
    if (!active("quick")) {
        std::fprintf(stderr, "Quick Settings did not open for the close-gesture test setup\n");
        return 14;
    }

    if (int result = drag(input, 180, 355, 180, 320, -10, 5, 30); result != 0) return result;
    if (!active("quick")) {
        std::fprintf(stderr,
                     "A short bottom-edge drag on Quick Settings should spring back open, not close\n");
        return 15;
    }

    if (int result = drag(input, 180, 355, 180, 200, -15, 5, 30); result != 0) return result;
    if (active("quick")) {
        std::fprintf(stderr,
                     "A bottom-edge drag past the close threshold should close Quick Settings\n");
        return 16;
    }

    std::puts("Quick Settings bottom-edge drag-to-close gesture passed");
    return 0;
}

int test_wlan_toggle(InputInjector &input)
{
    show("wlan");
    input.pump(1100);

    bool initial_ok = with_ui_lock([]() {
        return speaker_ui_is_wlan_on() &&
               speaker_ui_is_wlan_section_visible("connected") &&
               speaker_ui_is_wlan_section_visible("available") &&
               speaker_ui_is_wlan_section_visible("softap");
    });
    if (!initial_ok) {
        std::fprintf(stderr, "WLAN should start on with all sections visible once settled\n");
        return 17;
    }

    int32_t x = 0;
    int32_t y = 0;
    if (!with_ui_lock([&]() { return speaker_ui_get_wlan_switch_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the WLAN switch\n");
        return 18;
    }
    if (!input_ok(input.click(x, y), input)) return 1;

    bool disabled_ok = with_ui_lock([]() {
        return !speaker_ui_is_wlan_on() &&
               !speaker_ui_is_wlan_section_visible("connected") &&
               !speaker_ui_is_wlan_section_visible("available") &&
               !speaker_ui_is_wlan_section_visible("softap");
    });
    if (!disabled_ok) {
        std::fprintf(stderr, "Turning the WLAN switch off should hide all three WLAN sections\n");
        return 19;
    }

    show("quick");
    input.pump(20);
    if (with_ui_lock([]() { return speaker_ui_is_quick_wifi_on(); })) {
        std::fprintf(stderr, "Quick Settings Wi-Fi button should turn off in sync with the WLAN switch\n");
        return 20;
    }
    if (!with_ui_lock([&]() { return speaker_ui_get_quick_wifi_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Wi-Fi button\n");
        return 21;
    }
    if (!input_ok(input.click(x, y), input)) return 1;
    bool enabled = with_ui_lock([]() {
        return speaker_ui_is_wlan_on() && speaker_ui_is_quick_wifi_on();
    });
    if (!enabled) {
        std::fprintf(stderr, "Clicking the Quick Settings Wi-Fi button should turn WLAN back on\n");
        return 22;
    }

    show("wlan");
    input.pump(1100);
    bool restored = with_ui_lock([]() {
        return speaker_ui_is_wlan_section_visible("connected") &&
               speaker_ui_is_wlan_section_visible("available") &&
               speaker_ui_is_wlan_section_visible("softap");
    });
    if (!restored) {
        std::fprintf(stderr,
                     "Turning WLAN back on from Quick Settings should restore all three WLAN sections once settled\n");
        return 23;
    }

    std::puts("WLAN on/off toggle sync passed");
    return 0;
}

int test_wlan_keyboard(InputInjector &input)
{
    show("wlan");
    input.pump(20);
    if (with_ui_lock([]() { return speaker_ui_is_wlan_keyboard_visible(); })) {
        std::fprintf(stderr, "The WLAN keyboard should stay hidden outside the password screen\n");
        return 24;
    }

    show("wlan-connect");
    input.pump(20);
    bool keyboard_open = with_ui_lock([]() {
        return speaker_ui_is_screen_active("wlan-connect") &&
               speaker_ui_is_wlan_keyboard_visible() && speaker_ui_wlan_keyboard_bound();
    });
    if (!keyboard_open) {
        std::fprintf(stderr,
                     "Opening the WLAN password screen should show a keyboard bound to the password field\n");
        return 25;
    }

    with_ui_lock([]() {
        speaker_ui_set_wlan_password("short");
        speaker_ui_confirm_wlan_password();
    });
    input.pump(20);
    bool short_rejected = with_ui_lock([]() {
        return speaker_ui_is_screen_active("wlan-connect") && speaker_ui_is_wlan_keyboard_visible();
    });
    if (!short_rejected) {
        std::fprintf(stderr, "A password under 8 characters should not confirm\n");
        return 26;
    }

    with_ui_lock([]() {
        speaker_ui_set_wlan_password("longenoughpassword");
        speaker_ui_confirm_wlan_password();
    });
    input.pump(20);
    bool long_accepted = with_ui_lock([]() {
        return speaker_ui_is_screen_active("wlan") && !speaker_ui_is_wlan_keyboard_visible();
    });
    if (!long_accepted) {
        std::fprintf(stderr, "A password of 8+ characters should confirm and hide the keyboard\n");
        return 27;
    }

    std::puts("WLAN password keyboard passed");
    return 0;
}

int test_quick_buttons(InputInjector &input)
{
    show("quick");
    input.pump(20);
    bool initial_levels = with_ui_lock([]() {
        return speaker_ui_get_quick_volume_level() == -1 &&
               speaker_ui_get_quick_brightness_level() == 0;
    });
    if (!initial_levels) {
        std::fprintf(stderr, "Volume/Brightness should start at MUTE/LEVEL_1\n");
        return 28;
    }

    int32_t x = 0;
    int32_t y = 0;
    if (!with_ui_lock([&]() { return speaker_ui_get_quick_volume_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Volume button\n");
        return 29;
    }
    static constexpr std::array<int, 4> expect_volume{{0, 1, 2, -1}};
    for (size_t i = 0; i < expect_volume.size(); ++i) {
        if (!input_ok(input.click(x, y), input)) return 1;
        int actual = with_ui_lock([]() { return speaker_ui_get_quick_volume_level(); });
        if (actual != expect_volume[i]) {
            std::fprintf(stderr, "Volume level after click %zu expected %d, got %d\n",
                         i, expect_volume[i], actual);
            return 30;
        }
    }

    if (!with_ui_lock([&]() { return speaker_ui_get_quick_brightness_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Brightness button\n");
        return 31;
    }
    static constexpr std::array<int, 3> expect_brightness{{1, 2, 0}};
    for (size_t i = 0; i < expect_brightness.size(); ++i) {
        if (!input_ok(input.click(x, y), input)) return 1;
        int actual = with_ui_lock([]() { return speaker_ui_get_quick_brightness_level(); });
        if (actual != expect_brightness[i]) {
            std::fprintf(stderr, "Brightness level after click %zu expected %d, got %d\n",
                         i, expect_brightness[i], actual);
            return 32;
        }
    }

    if (!with_ui_lock([&]() { return speaker_ui_get_quick_volume_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Volume button\n");
        return 33;
    }
    if (!input_ok(input.press(x, y), input)) return 1;
    input.pump(120);
    if (!input_ok(input.release(), input)) return 1;
    input.pump(20);
    if (!active("sound") || active("quick")) {
        std::fprintf(stderr,
                     "Long-pressing Volume should close Quick Settings and open Settings > Sound\n");
        return 34;
    }

    show("quick");
    input.pump(20);
    if (!with_ui_lock([&]() { return speaker_ui_get_quick_brightness_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Brightness button\n");
        return 35;
    }
    if (!input_ok(input.press(x, y), input)) return 1;
    input.pump(120);
    if (!input_ok(input.release(), input)) return 1;
    input.pump(20);
    if (!active("display") || active("quick")) {
        std::fprintf(stderr,
                     "Long-pressing Brightness should close Quick Settings and open Settings > Display\n");
        return 36;
    }

    std::puts("Quick Settings Volume/Brightness passed");
    return 0;
}

int test_brightness_simulation(InputInjector &input, uint32_t output_id)
{
    using DisplayHelper = esp_brookesia::service::helper::Display;
    auto initial_result = get_backlight_brightness(output_id);
    if (!initial_result) {
        std::fprintf(stderr, "Could not read initial simulated brightness: %s\n",
                     initial_result.error().c_str());
        return 37;
    }
    const double initial_brightness = initial_result.value();
    esp_brookesia::lib_utils::FunctionGuard restore_brightness([=]() {
        (void)DisplayHelper::call_function_sync(
            DisplayHelper::FunctionId::SetBacklightBrightness,
            static_cast<double>(output_id), initial_brightness,
            esp_brookesia::service::helper::Timeout(1000)
        );
    });

    show("quick");
    input.pump(20);
    int32_t x = 0;
    int32_t y = 0;
    if (!with_ui_lock([&]() { return speaker_ui_get_quick_brightness_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Brightness button\n");
        return 38;
    }
    if (!input_ok(input.click(x, y), input)) return 1;
    input.pump(20);

    auto quick_result = get_backlight_brightness(output_id);
    if (!quick_result || static_cast<int>(quick_result.value()) != 70) {
        std::fprintf(stderr, "Quick Settings brightness expected 70%%, got %.0f%%\n",
                     quick_result ? quick_result.value() : -1.0);
        return 39;
    }

    show("display");
    input.pump(20);
    lv_area_t slider_area{};
    bool found_slider = with_ui_lock([&]() {
        lv_obj_update_layout(lv_screen_active());
        auto *slider = find_first_slider(lv_screen_active());
        if (slider == nullptr) return false;
        lv_obj_get_coords(slider, &slider_area);
        return true;
    });
    if (!found_slider) {
        std::fprintf(stderr, "Could not locate the Settings > Display brightness slider\n");
        return 40;
    }

    const int32_t slider_y = (slider_area.y1 + slider_area.y2) / 2;
    const int32_t target_x = slider_area.x1 + (slider_area.x2 - slider_area.x1) / 4;
    if (!input_ok(input.click(target_x, slider_y), input)) return 1;
    input.pump(20);

    const int slider_value = with_ui_lock([]() {
        auto *slider = find_first_slider(lv_screen_active());
        return slider == nullptr ? -1 : static_cast<int>(lv_slider_get_value(slider));
    });
    auto slider_result = get_backlight_brightness(output_id);
    if (slider_value < 0 || !slider_result ||
            static_cast<int>(slider_result.value()) != slider_value) {
        std::fprintf(stderr, "Display slider expected simulated brightness %d%%, got %.0f%%\n",
                     slider_value, slider_result ? slider_result.value() : -1.0);
        return 41;
    }

    std::printf("Brightness simulation passed: Quick Settings=70%%, Display slider=%d%%\n",
                slider_value);
    return 0;
}

int test_volume_simulation(InputInjector &input)
{
    using AudioPlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
    constexpr auto timeout = esp_brookesia::service::helper::Timeout(1000);

    auto initial_volume_result = AudioPlaybackHelper::call_function_sync<double>(
        AudioPlaybackHelper::FunctionId::GetVolume, timeout
    );
    auto initial_mute_result = AudioPlaybackHelper::call_function_sync<bool>(
        AudioPlaybackHelper::FunctionId::GetMute, timeout
    );
    if (!initial_volume_result || !initial_mute_result) {
        std::fprintf(stderr, "Could not read initial simulated audio state\n");
        return 42;
    }
    const double initial_volume = initial_volume_result.value();
    const bool initial_mute = initial_mute_result.value();
    esp_brookesia::lib_utils::FunctionGuard restore_audio([=]() {
        (void)AudioPlaybackHelper::call_function_sync(
            AudioPlaybackHelper::FunctionId::SetVolume, initial_volume, timeout
        );
        (void)AudioPlaybackHelper::call_function_sync(
            AudioPlaybackHelper::FunctionId::SetMute, initial_mute, timeout
        );
    });

    show("quick");
    input.pump(40);
    const int initial_level = with_ui_lock([]() {
        return speaker_ui_get_quick_volume_level();
    });
    int32_t x = 0;
    int32_t y = 0;
    if (!with_ui_lock([&]() { return speaker_ui_get_quick_volume_button_center(&x, &y); })) {
        std::fprintf(stderr, "Could not locate the Quick Settings Volume button\n");
        return 43;
    }
    if (!input_ok(input.click(x, y), input)) return 1;
    input.pump(30);

    const int expected_level = initial_level == 2 ? -1 : initial_level + 1;
    const int actual_level = with_ui_lock([]() {
        return speaker_ui_get_quick_volume_level();
    });
    auto quick_volume_result = AudioPlaybackHelper::call_function_sync<double>(
        AudioPlaybackHelper::FunctionId::GetVolume, timeout
    );
    auto quick_mute_result = AudioPlaybackHelper::call_function_sync<bool>(
        AudioPlaybackHelper::FunctionId::GetMute, timeout
    );
    static constexpr std::array<int, 3> quick_volume{{30, 60, 90}};
    const bool quick_state_ok = actual_level == expected_level && quick_volume_result &&
        quick_mute_result && (expected_level == -1
            ? quick_mute_result.value()
            : !quick_mute_result.value() &&
              static_cast<int>(quick_volume_result.value()) == quick_volume[expected_level]);
    if (!quick_state_ok) {
        std::fprintf(stderr,
                     "Quick volume simulation mismatch: level=%d volume=%.0f mute=%d\n",
                     actual_level, quick_volume_result ? quick_volume_result.value() : -1.0,
                     quick_mute_result ? static_cast<int>(quick_mute_result.value()) : -1);
        return 44;
    }

    show("sound");
    input.pump(40);
    lv_area_t slider_area{};
    bool found_slider = with_ui_lock([&]() {
        lv_obj_update_layout(lv_screen_active());
        auto *slider = find_first_slider(lv_screen_active());
        if (slider == nullptr) return false;
        lv_obj_get_coords(slider, &slider_area);
        return true;
    });
    if (!found_slider) {
        std::fprintf(stderr, "Could not locate the Settings > Sound volume slider\n");
        return 45;
    }

    const int32_t slider_y = (slider_area.y1 + slider_area.y2) / 2;
    const int32_t target_x = slider_area.x1 + (slider_area.x2 - slider_area.x1) / 4;
    if (!input_ok(input.click(target_x, slider_y), input)) return 1;
    input.pump(30);

    const int slider_value = with_ui_lock([]() {
        auto *slider = find_first_slider(lv_screen_active());
        return slider == nullptr ? -1 : static_cast<int>(lv_slider_get_value(slider));
    });
    auto slider_volume_result = AudioPlaybackHelper::call_function_sync<double>(
        AudioPlaybackHelper::FunctionId::GetVolume, timeout
    );
    auto slider_mute_result = AudioPlaybackHelper::call_function_sync<bool>(
        AudioPlaybackHelper::FunctionId::GetMute, timeout
    );
    if (slider_value < 0 || !slider_volume_result || !slider_mute_result ||
            static_cast<int>(slider_volume_result.value()) != slider_value ||
            slider_mute_result.value() != (slider_value == 0)) {
        std::fprintf(stderr, "Sound slider expected volume %d%%, got %.0f%% mute=%d\n",
                     slider_value, slider_volume_result ? slider_volume_result.value() : -1.0,
                     slider_mute_result ? static_cast<int>(slider_mute_result.value()) : -1);
        return 46;
    }

    std::printf("Volume simulation passed: Quick Settings level=%d, Sound slider=%d%%\n",
                expected_level, slider_value);
    return 0;
}

int test_persistence_write(InputInjector &input, uint32_t output_id)
{
    using AudioPlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
    using DisplayHelper = esp_brookesia::service::helper::Display;
    using StorageHelper = esp_brookesia::service::helper::Storage;
    constexpr auto timeout = esp_brookesia::service::helper::Timeout(2000);

    auto brightness_result = DisplayHelper::call_function_sync(
        DisplayHelper::FunctionId::SetBacklightBrightness,
        static_cast<double>(output_id), 37.0, timeout
    );
    auto volume_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::SetVolume, 42.0, timeout
    );
    auto mute_result = AudioPlaybackHelper::call_function_sync(
        AudioPlaybackHelper::FunctionId::SetMute, true, timeout
    );
    auto ui_result = host_sim::PersistenceAdapter::save_ui_state({
        .wlan_enabled = false,
        .ai_profile = 1,
    });
    const auto probe_path = host_sim::PersistenceAdapter::sandbox_file_path(
        "/littlefs", "p4_3_probe.txt"
    );
    auto file_result = StorageHelper::fs_write_text(probe_path, "simulator sandbox", 2000);
    input.pump(100);

    if (!brightness_result || !volume_result || !mute_result || !ui_result || !file_result) {
        std::fprintf(stderr,
                     "Could not write persistence fixture: brightness=%s volume=%s mute=%s ui=%s file=%s\n",
                     brightness_result ? "ok" : brightness_result.error().c_str(),
                     volume_result ? "ok" : volume_result.error().c_str(),
                     mute_result ? "ok" : mute_result.error().c_str(),
                     ui_result ? "ok" : ui_result.error().c_str(),
                     file_result ? "ok" : file_result.error().c_str());
        return 47;
    }
    std::puts("Persistence fixture written");
    return 0;
}

int test_persistence_read(uint32_t output_id)
{
    using AudioPlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
    using StorageHelper = esp_brookesia::service::helper::Storage;
    constexpr auto timeout = esp_brookesia::service::helper::Timeout(2000);

    auto brightness_result = get_backlight_brightness(output_id);
    auto volume_result = AudioPlaybackHelper::call_function_sync<double>(
        AudioPlaybackHelper::FunctionId::GetVolume, timeout
    );
    auto mute_result = AudioPlaybackHelper::call_function_sync<bool>(
        AudioPlaybackHelper::FunctionId::GetMute, timeout
    );
    auto ui_result = host_sim::PersistenceAdapter::load_ui_state();
    const auto probe_path = host_sim::PersistenceAdapter::sandbox_file_path(
        "/littlefs", "p4_3_probe.txt"
    );
    auto file_result = StorageHelper::fs_read_text(probe_path, 2000);

    if (!brightness_result || static_cast<int>(brightness_result.value()) != 37 ||
            !volume_result || static_cast<int>(volume_result.value()) != 42 ||
            !mute_result || !mute_result.value() || !ui_result ||
            ui_result->wlan_enabled || ui_result->ai_profile != 1 ||
            !file_result || file_result.value() != "simulator sandbox") {
        std::fprintf(stderr,
                     "Persistence read mismatch: brightness=%.0f volume=%.0f mute=%d wlan=%d profile=%d file=%s\n",
                     brightness_result ? brightness_result.value() : -1.0,
                     volume_result ? volume_result.value() : -1.0,
                     mute_result ? static_cast<int>(mute_result.value()) : -1,
                     ui_result ? static_cast<int>(ui_result->wlan_enabled) : -1,
                     ui_result ? ui_result->ai_profile : -1,
                     file_result ? file_result->c_str() : file_result.error().c_str());
        return 48;
    }
    std::puts("Cross-process persistence passed");
    return 0;
}

bool simulator_defaults_are_loaded(uint32_t output_id)
{
    using AudioPlaybackHelper = esp_brookesia::service::helper::AudioPlayback;
    using StorageHelper = esp_brookesia::service::helper::Storage;
    constexpr auto timeout = esp_brookesia::service::helper::Timeout(2000);

    auto brightness_result = get_backlight_brightness(output_id);
    auto backlight_on_result = get_backlight_on_off(output_id);
    auto volume_result = AudioPlaybackHelper::call_function_sync<double>(
        AudioPlaybackHelper::FunctionId::GetVolume, timeout
    );
    auto mute_result = AudioPlaybackHelper::call_function_sync<bool>(
        AudioPlaybackHelper::FunctionId::GetMute, timeout
    );
    auto ui_result = host_sim::PersistenceAdapter::load_ui_state();
    auto host_entries = StorageHelper::kv_list(
        host_sim::PersistenceAdapter::storage_namespace(), 2000
    );
    const auto probe_path = host_sim::PersistenceAdapter::sandbox_file_path(
        "/littlefs", "p4_3_probe.txt"
    );
    auto file_result = StorageHelper::fs_stat(probe_path, 2000);

    return brightness_result && static_cast<int>(brightness_result.value()) == 90 &&
           backlight_on_result && backlight_on_result.value() &&
           volume_result && static_cast<int>(volume_result.value()) == 75 &&
           mute_result && !mute_result.value() && ui_result &&
           ui_result->wlan_enabled && ui_result->ai_profile == 0 &&
           host_entries && host_entries->empty() && file_result && !file_result->exists;
}

int test_factory_reset(InputInjector &input, uint32_t output_id)
{
    const bool restored_ui_state = with_ui_lock([]() {
        return !speaker_ui_is_wlan_on() && ui_ScreenAIProfileTabviewTabView != nullptr &&
               lv_tabview_get_tab_active(ui_ScreenAIProfileTabviewTabView) == 1;
    });
    if (!restored_ui_state) {
        std::fprintf(stderr, "Persisted WLAN/AI Profile state was not applied to the UI\n");
        return 49;
    }

    show("restore");
    input.pump(20);
    if (!input_ok(input.click(180, 216), input)) return 1;
    input.pump(100);

    const bool reset_ui_state = with_ui_lock([]() {
        return speaker_ui_is_wlan_on() && ui_ScreenAIProfileTabviewTabView != nullptr &&
               lv_tabview_get_tab_active(ui_ScreenAIProfileTabviewTabView) == 0;
    });
    if (!reset_ui_state || !simulator_defaults_are_loaded(output_id)) {
        std::fprintf(stderr, "Factory Reset did not restore the isolated simulator defaults\n");
        return 50;
    }

    show("idle");
    input.pump(20);
    if (!input_ok(input.press(180, 180), input)) return 1;
    input.pump(120);
    if (!input_ok(input.release(), input)) return 1;
    input.pump(30);
    if (!with_ui_lock([]() { return speaker_ui_is_launcher_active(); })) {
        std::fprintf(stderr, "Launcher long-press stopped working after Factory Reset\n");
        return 51;
    }
    std::puts("Factory Reset sandbox passed");
    return 0;
}

int test_persistence_defaults(uint32_t output_id)
{
    if (!simulator_defaults_are_loaded(output_id)) {
        std::fprintf(stderr, "Factory Reset defaults did not survive the next process\n");
        return 52;
    }
    std::puts("Post-reset cross-process defaults passed");
    return 0;
}

} // namespace

bool is_self_test_option(std::string_view option)
{
    static constexpr std::array<std::string_view, 17> options{{
        "--self-test-home",
        "--self-test-launcher",
        "--self-test-idle",
        "--self-test-settings-bar",
        "--self-test-settings-no-scroll",
        "--self-test-settings-pages",
        "--self-test-quick-gesture",
        "--self-test-quick-close-gesture",
        "--self-test-wlan-toggle",
        "--self-test-wlan-keyboard",
        "--self-test-quick-buttons",
        "--self-test-brightness-simulation",
        "--self-test-volume-simulation",
        "--self-test-persistence-write",
        "--self-test-persistence-read",
        "--self-test-factory-reset",
        "--self-test-persistence-defaults",
    }};
    for (auto known : options) {
        if (option == known) return true;
    }
    return false;
}

int run_self_test(std::string_view option, const std::string &output_name,
                  lv_indev_t *input_device, uint32_t backlight_output_id)
{
    InputInjector input(output_name, input_device);
    if (option == "--self-test-settings-pages") return test_settings_pages(input);
    if (option == "--self-test-settings-bar") return test_settings_bar();
    if (option == "--self-test-idle") return test_idle(input);
    if (option == "--self-test-launcher") return test_launcher(input);
    if (option == "--self-test-home") return test_home(input);
    if (option == "--self-test-settings-no-scroll") return test_settings_no_scroll(input);
    if (option == "--self-test-quick-gesture") return test_quick_gesture(input);
    if (option == "--self-test-quick-close-gesture") return test_quick_close_gesture(input);
    if (option == "--self-test-wlan-toggle") return test_wlan_toggle(input);
    if (option == "--self-test-wlan-keyboard") return test_wlan_keyboard(input);
    if (option == "--self-test-quick-buttons") return test_quick_buttons(input);
    if (option == "--self-test-brightness-simulation") {
        return test_brightness_simulation(input, backlight_output_id);
    }
    if (option == "--self-test-volume-simulation") return test_volume_simulation(input);
    if (option == "--self-test-persistence-write") {
        return test_persistence_write(input, backlight_output_id);
    }
    if (option == "--self-test-persistence-read") {
        return test_persistence_read(backlight_output_id);
    }
    if (option == "--self-test-factory-reset") {
        return test_factory_reset(input, backlight_output_id);
    }
    if (option == "--self-test-persistence-defaults") {
        return test_persistence_defaults(backlight_output_id);
    }
    return 2;
}

} // namespace host_sim::tests
