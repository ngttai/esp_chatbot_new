/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

#include <atomic>
#include <cstdint>

namespace system_stats_ctx {

// Name of the active UI screen; points to a static string, nullptr until first published.
inline std::atomic<const char *> screen_name{nullptr};

// Transition counters. Each increments on every change, so a Home -> Launcher -> Home round trip inside one
// measurement window still shows as a non-zero delta. Wrap-around is harmless (only the difference is used).
inline std::atomic<uint32_t> screen_rev{0};
inline std::atomic<uint32_t> power_rev{0};

} // namespace system_stats_ctx
