#ifndef WEATHER_SOURCE_H
#define WEATHER_SOURCE_H

#include "weather_icon.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file weather_source.h
 *
 * Shared weather snapshot ABI used by the unchanged Clock panel. Hardware is
 * backed by the background OpenWeather service; the simulator selects its
 * deterministic mock or opt-in HTTP adapter.
 */

#define WEATHER_FORECAST_DAYS 4

typedef struct {
    char dayname[4];   /* "Mon", "Tue", ... */
    weather_icon_type_t icon;
    int hi;
    int lo;
} weather_forecast_day_t;

typedef struct {
    bool available;
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
 * Returns the latest cached snapshot. The firmware refreshes it in a
 * background task; the simulator provides its selected mock or HTTP backend.
 */
void weather_client_get(weather_data_t * out);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*WEATHER_SOURCE_H*/
