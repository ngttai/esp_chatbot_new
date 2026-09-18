#ifndef FLIP_CLOCK_DISPLAY_PROFILE_H
#define FLIP_CLOCK_DISPLAY_PROFILE_H

/**
 * @file display_profile.h
 *
 * Trimmed-down stand-in for HTC_Flip_Clock_with_weather's own
 * display_profile.h (see that project's README): that file lets a single
 * codebase build for either a RECT or ROUND display via a CMake option.
 * sdl_ui_simulator only ever targets one display -- 360x360 round,
 * SIZE in speaker_ui.c -- so this just hardcodes the ROUND branch instead
 * of porting the whole dual-profile CMake machinery.
 */
#define DISPLAY_W       360
#define DISPLAY_H       360
#define DISPLAY_ROUND   1

#endif /* FLIP_CLOCK_DISPLAY_PROFILE_H */
