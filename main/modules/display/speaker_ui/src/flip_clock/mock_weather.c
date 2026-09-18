/**
 * @file mock_weather.c
 *
 * Canned rotating weather scenarios, used by weather_client.c as a
 * fallback when no OpenWeatherMap API key is configured or a real fetch
 * fails. Cycling through a few scenarios exercises the whole UI (icons,
 * colors, forecast row, sun/wind row) reacting to changing data.
 */

#include "mock_weather.h"
#include <stddef.h>

static const mock_weather_t scenarios[] = {
    {
        .city = "Ho Chi Minh City", .condition = "Sunny", .icon = WEATHER_ICON_SUNNY,
        .current_temp = 34, .hi = 35, .lo = 26,
        .sunrise = "05:42", .sunset = "18:12", .wind_kmh = 12, .wind_dir = "SW",
        .forecast = {
            { "Fri", WEATHER_ICON_SUNNY,          35, 26 },
            { "Sat", WEATHER_ICON_PARTLY_CLOUDY,  33, 25 },
            { "Sun", WEATHER_ICON_RAIN,           30, 24 },
            { "Mon", WEATHER_ICON_STORM,          29, 23 },
        },
    },
    {
        .city = "Ho Chi Minh City", .condition = "Thunderstorm", .icon = WEATHER_ICON_STORM,
        .current_temp = 27, .hi = 30, .lo = 23,
        .sunrise = "05:43", .sunset = "18:10", .wind_kmh = 28, .wind_dir = "S",
        .forecast = {
            { "Sat", WEATHER_ICON_STORM,          29, 23 },
            { "Sun", WEATHER_ICON_RAIN,           28, 23 },
            { "Mon", WEATHER_ICON_CLOUDY,         30, 24 },
            { "Tue", WEATHER_ICON_PARTLY_CLOUDY,  32, 25 },
        },
    },
    {
        .city = "Ho Chi Minh City", .condition = "Partly Cloudy", .icon = WEATHER_ICON_PARTLY_CLOUDY,
        .current_temp = 30, .hi = 32, .lo = 25,
        .sunrise = "05:44", .sunset = "18:09", .wind_kmh = 9, .wind_dir = "SE",
        .forecast = {
            { "Sun", WEATHER_ICON_PARTLY_CLOUDY,  32, 25 },
            { "Mon", WEATHER_ICON_CLOUDY,         31, 25 },
            { "Tue", WEATHER_ICON_SUNNY,          34, 26 },
            { "Wed", WEATHER_ICON_SUNNY,          35, 27 },
        },
    },
    {
        .city = "Ho Chi Minh City", .condition = "Clear Night", .icon = WEATHER_ICON_CLEAR_NIGHT,
        .current_temp = 24, .hi = 33, .lo = 23,
        .sunrise = "05:44", .sunset = "18:09", .wind_kmh = 6, .wind_dir = "N",
        .forecast = {
            { "Mon", WEATHER_ICON_CLOUDY,         31, 25 },
            { "Tue", WEATHER_ICON_SUNNY,          34, 26 },
            { "Wed", WEATHER_ICON_SUNNY,          35, 27 },
            { "Thu", WEATHER_ICON_FOG,            30, 24 },
        },
    },
    {
        .city = "Ho Chi Minh City", .condition = "Partly Cloudy Night", .icon = WEATHER_ICON_PARTLY_CLOUDY_NIGHT,
        .current_temp = 26, .hi = 32, .lo = 25,
        .sunrise = "05:44", .sunset = "18:09", .wind_kmh = 8, .wind_dir = "SE",
        .forecast = {
            { "Tue", WEATHER_ICON_SUNNY,          34, 26 },
            { "Wed", WEATHER_ICON_SUNNY,          35, 27 },
            { "Thu", WEATHER_ICON_FOG,            30, 24 },
            { "Fri", WEATHER_ICON_PARTLY_CLOUDY,  33, 25 },
        },
    },
};

#define SCENARIO_COUNT (int)(sizeof(scenarios) / sizeof(scenarios[0]))

static int current_idx = 0;

const mock_weather_t * mock_weather_get(void)
{
    return &scenarios[current_idx];
}

const mock_weather_t * mock_weather_next(void)
{
    current_idx = (current_idx + 1) % SCENARIO_COUNT;
    return &scenarios[current_idx];
}
