#include "screenshot_capture.hpp"

#include <array>
#include <cstdio>
#include <fstream>
#include <string>

#include "boost/chrono.hpp"
#include "boost/thread.hpp"
#include "brookesia/gui_lvgl.hpp"
#include "lvgl.h"

extern "C" {
#include "speaker_ui.h"
}

namespace host_sim::tests {
namespace {

class UiLock {
public:
    UiLock() { esp_brookesia::gui::lvgl::lock_thread(); }
    ~UiLock() { esp_brookesia::gui::lvgl::unlock_thread(); }
};

void pump_ui(int count, int delay_ms)
{
    for (int i = 0; i < count; ++i) {
        {
            UiLock lock;
            lv_timer_handler();
        }
        boost::this_thread::sleep_for(boost::chrono::milliseconds(delay_ms));
    }
}

bool contains_point(lv_obj_t *object, int32_t x, int32_t y)
{
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

lv_obj_t *find_clickable_image(lv_obj_t *object, int32_t x, int32_t y)
{
    if (object == nullptr || lv_obj_has_flag(object, LV_OBJ_FLAG_HIDDEN) ||
            !contains_point(object, x, y)) {
        return nullptr;
    }

    const uint32_t child_count = lv_obj_get_child_count(object);
    for (uint32_t reverse_index = 0; reverse_index < child_count; ++reverse_index) {
        const int32_t index = static_cast<int32_t>(child_count - reverse_index - 1);
        if (auto *found = find_clickable_image(lv_obj_get_child(object, index), x, y)) {
            return found;
        }
    }

    if (lv_obj_has_flag(object, LV_OBJ_FLAG_CLICKABLE) && lv_obj_check_type(object, &lv_image_class)) {
        return object;
    }
    return nullptr;
}

void write_u16(std::ofstream &stream, uint16_t value)
{
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8) & 0xFFU),
    };
    stream.write(bytes.data(), bytes.size());
}

void write_u32(std::ofstream &stream, uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8) & 0xFFU),
        static_cast<char>((value >> 16) & 0xFFU),
        static_cast<char>((value >> 24) & 0xFFU),
    };
    stream.write(bytes.data(), bytes.size());
}

bool save_bmp(std::string_view path, const std::vector<uint8_t> &buffer,
              uint32_t width, uint32_t height)
{
    const size_t required_size = static_cast<size_t>(width) * height * 2U;
    if (buffer.size() < required_size) return false;

    std::ofstream stream(std::string(path), std::ios::binary);
    if (!stream) return false;

    constexpr uint32_t header_size = 14U + 40U;
    const uint32_t pixel_bytes = width * height * 4U;
    stream.put('B');
    stream.put('M');
    write_u32(stream, header_size + pixel_bytes);
    write_u16(stream, 0);
    write_u16(stream, 0);
    write_u32(stream, header_size);

    write_u32(stream, 40);
    write_u32(stream, width);
    write_u32(stream, height);
    write_u16(stream, 1);
    write_u16(stream, 32);
    write_u32(stream, 0);
    write_u32(stream, pixel_bytes);
    write_u32(stream, 0);
    write_u32(stream, 0);
    write_u32(stream, 0);
    write_u32(stream, 0);

    for (uint32_t output_y = 0; output_y < height; ++output_y) {
        const uint32_t source_y = height - output_y - 1U;
        for (uint32_t x = 0; x < width; ++x) {
            const size_t offset = (static_cast<size_t>(source_y) * width + x) * 2U;
            const uint16_t pixel = static_cast<uint16_t>(buffer[offset]) |
                                   (static_cast<uint16_t>(buffer[offset + 1]) << 8U);
            const uint8_t red = static_cast<uint8_t>(((pixel >> 11U) & 0x1FU) * 255U / 31U);
            const uint8_t green = static_cast<uint8_t>(((pixel >> 5U) & 0x3FU) * 255U / 63U);
            const uint8_t blue = static_cast<uint8_t>((pixel & 0x1FU) * 255U / 31U);
            stream.put(static_cast<char>(blue));
            stream.put(static_cast<char>(green));
            stream.put(static_cast<char>(red));
            stream.put(static_cast<char>(0xFF));
        }
    }
    return stream.good();
}

} // namespace

int capture_screenshot(
    std::string_view path,
    std::string_view screen_name,
    lv_display_t *display,
    const std::vector<uint8_t> &rgb565_buffer,
    uint32_t width,
    uint32_t height
)
{
    const bool launcher_pressed = screen_name == "launcher-pressed";
    const std::string requested_screen = launcher_pressed ? "launcher" : std::string(screen_name);
    {
        UiLock lock;
        if (!speaker_ui_show(requested_screen.c_str())) {
            std::fprintf(stderr, "Unknown screenshot screen: %s\n", requested_screen.c_str());
            return 2;
        }
    }

    // Match the reference runner exactly: 30 pumps with a 10 ms delay.
    pump_ui(30, 10);
    if (launcher_pressed) {
        {
            UiLock lock;
            lv_obj_update_layout(lv_screen_active());
            auto *target = find_clickable_image(lv_screen_active(), 110, 165);
            if (target == nullptr) {
                std::fprintf(stderr, "Could not locate launcher icon at (110, 165)\n");
                return 3;
            }
            lv_obj_send_event(target, LV_EVENT_PRESSED, nullptr);
        }
        pump_ui(20, 10);
    }

    {
        UiLock lock;
        lv_refr_now(display);
    }

    if (!save_bmp(path, rgb565_buffer, width, height)) {
        std::fprintf(stderr, "Could not save screenshot: %.*s\n",
                     static_cast<int>(path.size()), path.data());
        return 1;
    }
    return 0;
}

} // namespace host_sim::tests
