#pragma once

#include <atomic>
#include <mutex>

class WifiControl {
public:
    static WifiControl &get_instance();
    void init();
    bool is_enabled() const { return enabled_.load(); }
    bool set_enabled(bool enabled);
    bool reset_preferences();

private:
    std::atomic_bool enabled_{true};
    std::mutex mutex_;
};
