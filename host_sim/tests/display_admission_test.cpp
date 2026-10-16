#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "brookesia/service_display/service_display.hpp"
#include "brookesia/hal_linux.hpp"

using namespace esp_brookesia;
using D = service::Display;
using namespace std::chrono_literals;
#define CHECK(test) do { if (!(test)) { std::fprintf(stderr, "Display admission failed at %d: %s\n", __LINE__, #test); std::abort(); } } while (false)
template<class F> bool await(F predicate)
{
    const auto until = std::chrono::steady_clock::now() + 2s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

int main()
{
    CHECK(hal::DisplayLinuxDevice::get_instance().configure({
        .width_px = 16, .height_px = 16, .window_title = "Display admission", .render_driver = "software",
    }));
    auto &manager = service::ServiceManager::get_instance();
    CHECK(manager.init() && manager.start());
    auto binding = manager.bind("Display");
    CHECK(binding.is_valid());
    auto &display = D::get_instance();
    uint16_t pixels[4]{};
    uint16_t input[4]{1, 2, 3, 4};
    auto output = display.register_output({
        .name = "admission-test", .width = 2, .height = 2,
        .buffer = service::RawBuffer(pixels, sizeof(pixels)), .stride_bytes = 4,
    });
    CHECK(output);
    auto source = display.register_source({.name = "admission-source", .role = "test"});
    CHECK(source);
    CHECK(display.request_output(*source, "admission-test"));
    CHECK(display.set_active_source("admission-test", "admission-source"));
    const D::FrameInfo frame{.width = 2, .height = 2};
    const service::RawBuffer data(input, sizeof(input));
    CHECK(!*display.is_frame_output_drained(*output));
    CHECK(display.present_frame_sync(*source, "admission-test", frame, data) == D::PresentResult::Presented);

    std::atomic_bool writing{false}, release{false};
    std::thread writer([&] {
        CHECK(display.present_buffer_frame_sync(*source, "admission-test", frame, [&](auto &) {
            writing = true;
            while (!release) std::this_thread::sleep_for(1ms);
            return true;
        }) == D::PresentResult::Presented);
    });
    CHECK(await([&] { return writing.load(); }));
    std::atomic_uint completions{0};
    auto queued = display.present_frame_async(*source, "admission-test", frame, data, [&](auto, auto result) {
        CHECK(result == D::PresentResult::DroppedNotActive);
        // Callback can re-enter Display without deadlock.
        CHECK(display.get_outputs().size() >= 1);
        ++completions;
    });
    CHECK(queued.state == D::PresentSubmitState::Queued);
    CHECK(display.set_frame_admission(*output, false));
    CHECK(!*display.is_frame_output_drained(*output));
    CHECK(display.present_frame_sync(*source, "admission-test", frame, data) == D::PresentResult::DroppedNotActive);
    bool writer_called = false;
    CHECK(display.present_buffer_frame_sync(*source, "admission-test", frame, [&](auto &) {
        writer_called = true; return true;
    }) == D::PresentResult::DroppedNotActive);
    CHECK(!writer_called);
    auto rejected = display.present_frame_async(*source, "admission-test", frame, data, [&](auto, auto) {
        CHECK(false);
    });
    CHECK(rejected.frame_id == 0 && rejected.state == D::PresentSubmitState::DroppedNotActive);
    release = true;
    writer.join();
    CHECK(await([&] { return completions == 1 && *display.is_frame_output_drained(*output); }));
    CHECK(display.set_frame_admission(*output, false));
    CHECK(*display.is_frame_output_drained(*output));
    CHECK(display.set_frame_admission(*output, true));
    CHECK(display.present_frame_sync(*source, "admission-test", frame, data) == D::PresentResult::Presented);
    std::atomic_bool callback_entered{false}, callback_release{false};
    auto completing = display.present_frame_async(*source, "admission-test", frame, data, [&](auto, auto result) {
        CHECK(result == D::PresentResult::Presented);
        callback_entered = true;
        while (!callback_release) std::this_thread::sleep_for(1ms);
    });
    CHECK(completing.state == D::PresentSubmitState::Queued);
    CHECK(await([&] { return callback_entered.load(); }));
    CHECK(display.set_frame_admission(*output, false));
    CHECK(!*display.is_frame_output_drained(*output));
    callback_release = true;
    CHECK(await([&] { return *display.is_frame_output_drained(*output); }));
    CHECK(display.set_frame_admission(*output, true));
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(display.set_frame_admission(*output, false));
        CHECK(*display.is_frame_output_drained(*output));
        CHECK(display.set_frame_admission(*output, true));
    }
    CHECK(!display.set_frame_admission(0xffffffff, false));
    CHECK(!display.is_frame_output_drained(0xffffffff));
    CHECK(display.unregister_source(*source));
    CHECK(display.unregister_output("admission-test"));
    binding = {};
    manager.deinit();
    std::puts("Display admission PASS");
}
