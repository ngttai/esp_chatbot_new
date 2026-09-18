#ifndef MOCK_WEATHER_H
#define MOCK_WEATHER_H

#include "weather_icon.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOCK_FORECAST_DAYS 4

typedef struct {
    const char * dayname;
    weather_icon_type_t icon;
    int hi;
    int lo;
} mock_forecast_day_t;

typedef struct {
    const char * city;
    const char * condition;
    weather_icon_type_t icon;
    int current_temp;
    int hi;
    int lo;
    const char * sunrise;
    const char * sunset;
    int wind_kmh;
    const char * wind_dir;
    mock_forecast_day_t forecast[MOCK_FORECAST_DAYS];
} mock_weather_t;

/** Get the currently-active mock scenario. */
const mock_weather_t * mock_weather_get(void);

/** Advance to the next mock scenario (used to demo the UI reacting to new data). */
const mock_weather_t * mock_weather_next(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*MOCK_WEATHER_H*/
