#pragma once

struct AgentPowerActivity {
    bool listening = false;
    bool thinking = false;
    bool speaking = false;
    bool starting = false;
    bool session_requested = false;
    bool slept = false;
    bool stopped = false;
    bool error = false;
};

inline bool agent_blocks_power_idle(const AgentPowerActivity &state)
{
    if (state.listening || state.thinking || state.speaking) {
        return true;
    }
    if (state.slept || state.stopped || state.error) {
        return false;
    }
    return state.starting || state.session_requested;
}
