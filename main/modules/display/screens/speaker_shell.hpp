/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#pragma once

class ScreenSpeakerShell {
public:
    bool start();

private:
    bool started_ = false;
};

