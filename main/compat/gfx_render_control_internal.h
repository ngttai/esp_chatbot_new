/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "gfx_render_control.h"

// Protected by render_mutex; the lifecycle event group lives for the engine lifetime.
#define GFX_RENDER_PAUSED BIT3
static bool gfx_render_is_paused(gfx_core_context_t *ctx)
{
    return (xEventGroupGetBits(ctx->sync.lifecycle_events) & GFX_RENDER_PAUSED) != 0;
}

esp_err_t gfx_try_set_render_paused(gfx_handle_t handle, bool paused)
{
    gfx_core_context_t *ctx = (gfx_core_context_t *)handle;
    if (!ctx || !ctx->sync.render_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTakeRecursive(ctx->sync.render_mutex, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    const bool was_paused = gfx_render_is_paused(ctx);
    if (paused) {
        xEventGroupSetBits(ctx->sync.lifecycle_events, GFX_RENDER_PAUSED);
    } else if (was_paused) {
        // Restart timer deadlines without a burst of expired animation callbacks.
        const uint32_t now = gfx_timer_tick_get();
        for (gfx_timer_t *timer = ctx->timer_mgr.timer_list; timer; timer = timer->next) {
            timer->last_run = now;
        }
        ctx->timer_mgr.last_tick = now;
        xEventGroupClearBits(ctx->sync.lifecycle_events, GFX_RENDER_PAUSED);
        if (ctx->disp) gfx_disp_refresh_all(ctx->disp);
        xEventGroupSetBits(ctx->sync.render_events, GFX_EVENT_INVALIDATE);
    }
    xSemaphoreGiveRecursive(ctx->sync.render_mutex);
    return ESP_OK;
}
