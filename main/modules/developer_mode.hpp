/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

class DeveloperMode {
public:
    static bool is_requested();
    static void request_and_restart();
    static bool start();

private:
    static void exit_clicked(void *event);
    static void clear_and_restart();
};
