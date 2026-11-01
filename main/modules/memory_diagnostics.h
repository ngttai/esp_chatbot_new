/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_EXAMPLE_MEMORY_DIAGNOSTICS

/*
 * Logs the app_main_pre_diag baseline, creates the diagnostics queue and
 * worker, then logs the diag_ready baseline. Call once, first in app_main.
 */
void memory_diag_start(void);

/* Queues a heap-only checkpoint. Never blocks; safe in latency-sensitive callbacks. */
void memory_diag_checkpoint(const char *label);

/* Queues a checkpoint with heap, task and CPU statistics. Never blocks. */
void memory_diag_checkpoint_full(const char *label);

/* Queues a full checkpoint that is taken delay_ms after the call. Never blocks. */
void memory_diag_checkpoint_after(const char *label, uint32_t delay_ms);

#else

static inline void memory_diag_start(void) {}
static inline void memory_diag_checkpoint(const char *label)
{
    (void)label;
}
static inline void memory_diag_checkpoint_full(const char *label)
{
    (void)label;
}
static inline void memory_diag_checkpoint_after(const char *label, uint32_t delay_ms)
{
    (void)label;
    (void)delay_ms;
}

#endif

#ifdef __cplusplus
}
#endif
