/*
 * Desktop simulator stub.
 *
 * The real esp_mmap_assets.h (an ESP-IDF component for reading assets out of
 * a flash partition) is only transitively included by the generated
 * mmap_generate_anim_*.h headers for their MMAP_*_FILES / MMAP_*_CHECKSUM
 * macros and MMAP_*_LISTS enums. Nothing the simulator compiles calls any
 * mmap_assets_* function, so this header intentionally declares nothing.
 */
#pragma once
