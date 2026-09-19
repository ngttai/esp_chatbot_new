#pragma once

#include <expected>
#include <string>
#include <string_view>

#include "lvgl.h"

namespace host_sim {

struct SimulatorUiState {
    bool wlan_enabled = true;
    int ai_profile = 0;
};

class PersistenceAdapter {
public:
    bool start();
    void stop();

    static const char *storage_namespace();
    static std::string sandbox_file_path(std::string_view mount, std::string_view relative_path);
    static std::expected<SimulatorUiState, std::string> load_ui_state();
    static std::expected<void, std::string> save_ui_state(const SimulatorUiState &state);
    static std::expected<void, std::string> reset_simulator_data();

private:
    static void timer_callback(lv_timer_t *timer);
    static void ai_select_callback(lv_event_t *event);
    static void restore_callback(lv_event_t *event);

    void poll();
    bool apply_wlan_state(bool enabled);
    void apply_ai_profile(int profile);
    bool attach_restore_handler();
    bool save_current_state();
    void apply_reset_state();

    lv_timer_t *timer_ = nullptr;
    lv_obj_t *restore_button_ = nullptr;
    SimulatorUiState state_{};
};

} // namespace host_sim
