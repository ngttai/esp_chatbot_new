#pragma once

#include <iosfwd>

namespace host_sim {

struct HostCapabilities {
    const char *display;
    const char *storage;
    const char *media;
    const char *wifi;
    const char *power;
};

const HostCapabilities &configured_capabilities();
void print_configured_capabilities(std::ostream &stream);

} // namespace host_sim
