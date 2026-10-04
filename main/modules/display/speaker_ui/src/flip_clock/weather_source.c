/**
 * @file weather_source.c
 *
 * Firmware bridge for the background OpenWeather service. The host simulator
 * excludes this file and supplies the same weather_client_get() ABI from its
 * weather adapter.
 */

#include "weather_source.h"
#include <string.h>
#include <stdio.h>

extern bool weather_service_get(weather_data_t * out);

void weather_client_get(weather_data_t * out)
{
    if(out == NULL) return;
    if(weather_service_get(out)) return;
    memset(out, 0, sizeof(*out));
    snprintf(out->city, sizeof(out->city), "%s", "Weather");
    snprintf(out->condition, sizeof(out->condition), "%s", "Not configured");
    out->icon = WEATHER_ICON_CLOUDY;
}
