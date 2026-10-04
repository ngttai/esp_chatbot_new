/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "private/display_impl.hpp"

namespace esp_brookesia::service {

#if BROOKESIA_SERVICE_DISPLAY_ENABLE_AUTO_REGISTER
BROOKESIA_PLUGIN_REGISTER_SINGLETON_WITH_SYMBOL(
    ServiceBase, Display, Display::Helper::get_name().data(), Display::get_instance(),
    BROOKESIA_SERVICE_DISPLAY_PLUGIN_SYMBOL
);
#endif

} // namespace esp_brookesia::service
