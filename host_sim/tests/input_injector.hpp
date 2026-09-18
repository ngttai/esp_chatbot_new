#pragma once

#include <cstdint>
#include <string>

#include "lvgl.h"

namespace host_sim::tests {

class InputInjector {
public:
    InputInjector(std::string output_name, lv_indev_t *input);
    ~InputInjector();

    InputInjector(const InputInjector &) = delete;
    InputInjector &operator=(const InputInjector &) = delete;

    bool press(int32_t x, int32_t y);
    bool move(int32_t x, int32_t y);
    bool release();
    bool click(int32_t x, int32_t y);
    void pump(int count) const;

    const std::string &last_error() const;

private:
    bool inject(int32_t x, int32_t y);
    bool synchronize(bool pressed, int32_t x, int32_t y);

    std::string output_name_;
    lv_indev_t *input_ = nullptr;
    std::string last_error_;
};

} // namespace host_sim::tests
