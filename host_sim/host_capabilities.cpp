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
        .wifi = HOST_SIM_WIFI_BACKEND,
        .power = HOST_SIM_POWER_BACKEND,
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
           << "  wifi:    " << capabilities.wifi << '\n'
           << "  power:   " << capabilities.power << '\n';
    if (capabilities.media == std::string_view("auto") ||
            capabilities.wifi == std::string_view("auto") ||
            capabilities.power == std::string_view("auto")) {
        stream << "  note: auto resolves to a real backend when its dependencies are available; "
                  "otherwise Brookesia logs a stub fallback.\n";
    }
}

} // namespace host_sim
