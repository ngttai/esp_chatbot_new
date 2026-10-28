/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "core/gfx_core.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Try to pause/resume the render engine without waiting for an in-flight render. */
esp_err_t gfx_try_set_render_paused(gfx_handle_t handle, bool paused);
/** Mark the render engine paused at once; an in-flight frame may still finish. */
esp_err_t gfx_request_render_pause(gfx_handle_t handle);
#ifdef __cplusplus
}
#endif
