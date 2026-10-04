/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <expected>
#include <string>

#include "brookesia/service_manager/dataflow/registration.hpp"

namespace esp_brookesia::service {

class Display;

std::expected<dataflow::ProviderRegistration, std::string> register_display_dataflow_provider(Display &display);

} // namespace esp_brookesia::service
