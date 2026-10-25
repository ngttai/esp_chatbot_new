# Extend pinned dependencies in the build directory; managed sources stay immutable.
function(render_power_replace variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Render power extension anchor missing; review dependency compatibility")
    endif()
    string(REPLACE "${before}" "${after}" result "${${variable}}")
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

idf_component_get_property(gfx_dir espressif2022__esp_emote_gfx COMPONENT_DIR)
idf_component_get_property(gfx_lib espressif2022__esp_emote_gfx COMPONENT_LIB)
idf_component_get_property(emote_dir espressif__brookesia_expression_emote COMPONENT_DIR)
idf_component_get_property(emote_lib espressif__brookesia_expression_emote COMPONENT_LIB)
set(power_compat "${CMAKE_CURRENT_LIST_DIR}")
set(power_generated "${CMAKE_BINARY_DIR}/render_power")
file(SHA256 "${gfx_dir}/src/core/runtime/gfx_core.c" gfx_hash)
file(SHA256 "${emote_dir}/include/brookesia/expression_emote/emote.hpp" emote_hash)
if(NOT gfx_hash STREQUAL "adc963887b0cc11731e25ae31df5e17d111fb4ea1c899bd4602a0a1583f7813e" OR
   NOT emote_hash STREQUAL "2592bf7fbcb09fa53b02a1dd4cdf9b1daa5397d70749fe29400e9164ca435502")
    message(FATAL_ERROR "Render power extension requires review for the resolved GFX/Emote version")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${gfx_dir}/src/core/runtime/gfx_core.c"
    "${emote_dir}/include/brookesia/expression_emote/emote.hpp")

file(READ "${gfx_dir}/src/core/runtime/gfx_core.c" gfx_source)
render_power_replace(gfx_source "static const char *TAG = \"core\";"
    "static const char *TAG = \"core\";\n#include \"gfx_render_control_internal.h\"")
render_power_replace(gfx_source "        bool evt_refr;"
    "        if (gfx_render_is_paused(ctx)) {\n            xSemaphoreGiveRecursive(mutex);\n            next_sleep_ms = GFX_RENDER_TASK_IDLE_SLEEP_MS;\n            continue;\n        }\n\n        bool evt_refr;")
render_power_replace(gfx_source "    if (ctx->disp != NULL) {\n        gfx_render_handler(ctx);"
    "    if (!gfx_render_is_paused(ctx) && ctx->disp != NULL) {\n        gfx_render_handler(ctx);")
file(MAKE_DIRECTORY "${power_generated}/include/brookesia/expression_emote")
file(CONFIGURE OUTPUT "${power_generated}/gfx_core.c" CONTENT "${gfx_source}" @ONLY)
get_target_property(gfx_sources ${gfx_lib} SOURCES)
list(FILTER gfx_sources EXCLUDE REGEX "core/runtime/gfx_core\\.c$")
set_property(TARGET ${gfx_lib} PROPERTY SOURCES "${gfx_sources}")
target_sources(${gfx_lib} PRIVATE "${power_generated}/gfx_core.c")
target_include_directories(${gfx_lib} PRIVATE "${power_compat}")

file(READ "${emote_dir}/include/brookesia/expression_emote/emote.hpp" emote_header)
render_power_replace(emote_header "    bool native_notify_flush_finished();"
    "    bool native_notify_flush_finished();\n    /** Nonblocking render control. The service must remain bound. */\n    bool native_try_set_render_paused(bool paused);\n    /** Marks the renderer paused at once; an in-flight frame may still finish. */\n    bool native_request_render_pause();")
file(CONFIGURE OUTPUT "${power_generated}/include/brookesia/expression_emote/emote.hpp"
    CONTENT "${emote_header}" @ONLY)
target_include_directories(${emote_lib} BEFORE PUBLIC "${power_generated}/include")
target_include_directories(${emote_lib} PRIVATE "${power_compat}" "${emote_dir}/src")
target_sources(${emote_lib} PRIVATE "${power_compat}/emote_render_control.cpp")
