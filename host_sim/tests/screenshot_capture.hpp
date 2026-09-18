#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

typedef struct _lv_display_t lv_display_t;

namespace host_sim::tests {

int capture_screenshot(
    std::string_view path,
    std::string_view screen_name,
    lv_display_t *display,
    const std::vector<uint8_t> &rgb565_buffer,
    uint32_t width,
    uint32_t height
);

} // namespace host_sim::tests
