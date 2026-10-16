#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "brookesia/gui_lvgl.hpp"
#include "brookesia/hal_linux.hpp"
#include "brookesia/service_display.hpp"
#include "lvgl/lvgl.h"
using namespace esp_brookesia;
using namespace std::chrono_literals;
#define CHECK(t) do { if (!(t)) { std::fprintf(stderr, "LVGL pause failed %d: %s\n", __LINE__, #t); std::abort(); } } while (false)
template<class F> bool await(F predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) { if (std::chrono::steady_clock::now() >= deadline) return false; std::this_thread::sleep_for(1ms); }
    return true;
}
int main()
{
    CHECK(hal::DisplayLinuxDevice::get_instance().configure({
        .width_px = 32, .height_px = 32, .window_title = "LVGL render pause", .render_driver = "software",
    }));
    auto &manager = service::ServiceManager::get_instance(); CHECK(manager.start());
    auto binding = manager.bind("Display"); CHECK(binding.is_valid());
    auto &source = gui::lvgl::DisplaySource::get_instance(); CHECK(source.start());
    auto &display = service::Display::get_instance();
    const auto outputs = display.get_outputs(); CHECK(!outputs.empty());
    const uint32_t id = outputs.front().id;
    CHECK(display.set_active_source(source.output_name(), gui::lvgl::DISPLAY_SOURCE_NAME));
    std::atomic_uint frames{0}, timers{0}, presses{0};
    auto connection = display.connect_frame_completed([&](auto, const auto &, const auto &) { ++frames; });
    gui::lvgl::lock_thread();
    lv_timer_t *control = lv_timer_create([](lv_timer_t *timer) {
        ++*static_cast<std::atomic_uint *>(lv_timer_get_user_data(timer));
    }, 5, &timers);
    CHECK(control);
    auto *screen = lv_display_get_screen_active(source.display());
    lv_obj_add_event_cb(screen, [](lv_event_t *event) {
        ++*static_cast<std::atomic_uint *>(lv_event_get_user_data(event));
    }, LV_EVENT_PRESSED, &presses);
    lv_obj_invalidate(screen);
    gui::lvgl::unlock_thread();
    CHECK(await([&] { return frames > 0; }));
    CHECK(display.set_frame_admission(id, false));
    CHECK(await([&] { return source.try_set_render_paused(true); }));
    CHECK(await([&] { auto drained = display.is_frame_output_drained(id); return drained && *drained; }));
    const auto before_frames = frames.load(), before_timers = timers.load();
    CHECK(display.inject_touch(source.output_name(), 16, 16));
    CHECK(await([&] { return presses > 0 && timers >= before_timers + 5; }));
    CHECK(display.inject_touch(source.output_name(), {}));
    // Reopen the sink while the source stays paused: render must still produce no frames.
    CHECK(display.set_frame_admission(id, true));
    std::this_thread::sleep_for(60ms);
    CHECK(frames == before_frames);
    CHECK(await([&] { return source.try_set_render_paused(false); }));
    CHECK(await([&] { return frames > before_frames; }));
    gui::lvgl::lock_thread(); lv_timer_delete(control); gui::lvgl::unlock_thread();
    connection.disconnect();
    source.stop(); binding = {}; manager.deinit();
    std::puts("LVGL render pause PASS: control timers and touch stay alive");
}
