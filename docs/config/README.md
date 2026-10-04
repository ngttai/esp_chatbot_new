# Weather configuration

Copy `weather-import.json` to `/config/weather-import.json` in the SD card root,
replace the API key, then boot the device. A successful import is persisted to
NVS and the file is renamed to `weather-imported.json`.

After Wi-Fi connects, the device fetches metric forecast data in the background.
The last successful result remains visible during temporary network failures.

City mode is also accepted. Replace `location` with:

```json
{
  "mode": "city",
  "city": "Ho Chi Minh City",
  "country": "VN",
  "display_name": "Ho Chi Minh City"
}
```

`coordinates` is preferred because it is unambiguous and does not require a
separate geocoding request. City mode is resolved once through the OpenWeather
Geocoding API; the resulting coordinates are then stored in NVS.
