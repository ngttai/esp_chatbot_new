#include "host_capabilities.hpp"

#include "host_capabilities_config.hpp"

#include <ostream>
#include <string_view>

namespace host_sim {

const HostCapabilities &configured_capabilities()
{
    static constexpr HostCapabilities capabilities{
        .display = "sdl2",
        .storage = "linux-filesystem",
        .media = HOST_SIM_MEDIA_BACKEND,
        .media_resolved = HOST_SIM_MEDIA_BACKEND_RESOLVED,
        .wifi = HOST_SIM_WIFI_BACKEND,
        .wifi_resolved = HOST_SIM_WIFI_BACKEND_RESOLVED,
        .power = HOST_SIM_POWER_BACKEND,
        .time = HOST_SIM_TIME_BACKEND,
        .weather = HOST_SIM_WEATHER_BACKEND,
    };
    return capabilities;
}

void print_configured_capabilities(std::ostream &stream)
{
    const auto &capabilities = configured_capabilities();
    stream << "ESP VoCat host simulator backend selection:\n"
           << "  display: " << capabilities.display << '\n'
           << "  storage: " << capabilities.storage << '\n'
           << "  media:   " << capabilities.media << '\n'
           << "  media resolved: " << capabilities.media_resolved << '\n'
           << "  wifi:    " << capabilities.wifi << '\n'
           << "  wifi resolved: " << capabilities.wifi_resolved << '\n'
           << "  power:   " << capabilities.power << '\n';
    stream << "  time:    " << capabilities.time << '\n'
           << "  weather: " << capabilities.weather << '\n';
    if (capabilities.media == std::string_view("auto") ||
            capabilities.wifi == std::string_view("auto") ||
            capabilities.power == std::string_view("auto")) {
        stream << "  note: media/power auto may select a real backend; Wi-Fi auto stays on "
                  "the safe stub and requires explicit networkmanager opt-in.\n";
    }
}

} // namespace host_sim
