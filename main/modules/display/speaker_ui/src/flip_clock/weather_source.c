/**
 * @file weather_source.c
 *
 * Mock-only weather_client_get() backing weather_panel.c's compact panel.
 * Copies the current mock_weather.c scenario into a weather_data_t on
 * every call, and slowly rotates through mock_weather.c's scenarios on a
 * timer (mirroring the spirit of weather_client.c's own comment: cycling
 * scenarios exercises the whole panel reacting to changing data) -- at a
 * much slower cadence than weather_panel.c's own REFRESH_PERIOD_MS poll,
 * so the panel isn't seen to "jump" scenarios on every refresh.
 */

#include "weather_source.h"
#include "mock_weather.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

#define SCENARIO_ROTATE_MS (5 * 60 * 1000) /* 5 minutes */

static void rotate_timer_cb(lv_timer_t * t)
{
    LV_UNUSED(t);
    mock_weather_next();
}

static void ensure_rotation_started(void)
{
    static bool started = false;
    if(started) return;
    started = true;
    lv_timer_create(rotate_timer_cb, SCENARIO_ROTATE_MS, NULL);
}

void weather_client_get(weather_data_t * out)
{
    ensure_rotation_started();

    const mock_weather_t * m = mock_weather_get();

    memset(out, 0, sizeof(*out));
    snprintf(out->city, sizeof(out->city), "%s", m->city);
    snprintf(out->condition, sizeof(out->condition), "%s", m->condition);
    out->icon = m->icon;
    out->current_temp = m->current_temp;
    out->hi = m->hi;
    out->lo = m->lo;
    snprintf(out->sunrise, sizeof(out->sunrise), "%s", m->sunrise);
    snprintf(out->sunset, sizeof(out->sunset), "%s", m->sunset);
    out->wind_kmh = m->wind_kmh;
    snprintf(out->wind_dir, sizeof(out->wind_dir), "%s", m->wind_dir);

    for(int i = 0; i < WEATHER_FORECAST_DAYS; i++) {
        snprintf(out->forecast[i].dayname, sizeof(out->forecast[i].dayname),
                 "%s", m->forecast[i].dayname);
        out->forecast[i].icon = m->forecast[i].icon;
        out->forecast[i].hi = m->forecast[i].hi;
        out->forecast[i].lo = m->forecast[i].lo;
    }
}
