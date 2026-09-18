#ifndef WEATHER_SOURCE_H
#define WEATHER_SOURCE_H

#include "weather_icon.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file weather_source.h
 *
 * sdl_ui_simulator's stand-in for HTC_Flip_Clock_with_weather's
 * weather_client.h/.c: same weather_data_t shape and weather_client_get()
 * call weather_panel.c expects, but backed ONLY by mock_weather.c's canned
 * scenarios -- no libcurl/cJSON HTTP fetch, no background pthread, no real
 * network access. See weather_source.c.
 */

#define WEATHER_FORECAST_DAYS 4

typedef struct {
    char dayname[4];   /* "Mon", "Tue", ... */
    weather_icon_type_t icon;
    int hi;
    int lo;
} weather_forecast_day_t;

typedef struct {
    char city[48];
    char condition[32];
    weather_icon_type_t icon;
    int current_temp;
    int hi;
    int lo;
    char sunrise[6];   /* "HH:MM" */
    char sunset[6];    /* "HH:MM" */
    int wind_kmh;
    char wind_dir[4];  /* "N", "SW", ... */
    weather_forecast_day_t forecast[WEATHER_FORECAST_DAYS];
} weather_data_t;

/**
 * Always returns a fully-populated struct straight from mock_weather.c's
 * current scenario -- no thread, no mutex, no network, safe to call
 * directly from the LVGL/UI thread/timer at any time.
 */
void weather_client_get(weather_data_t * out);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*WEATHER_SOURCE_H*/
