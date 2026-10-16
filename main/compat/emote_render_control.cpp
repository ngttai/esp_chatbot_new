/* SPDX-License-Identifier: Apache-2.0 */
#include "brookesia/expression_emote/emote.hpp"
#include "expression_emote.h"
#include "../priv_include/emote_defs.h"
#include "gfx_render_control.h"

namespace esp_brookesia::expression {
bool Emote::native_try_set_render_paused(bool paused)
{
    auto handle = static_cast<emote_handle_t>(native_handle_);
    return handle && gfx_try_set_render_paused(handle->gfx_handle, paused) == ESP_OK;
}
}
