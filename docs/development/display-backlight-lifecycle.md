# Display backlight lifecycle

This note records the v1.1 investigation into the brief LCD noise and white
frame seen during boot on the VoCat v1.0 board.

## Persisted data

Brookesia Display Service stores two independent values in NVS for each
display output:

- `Brightness`: logical brightness in the range 0-100.
- `On`: the requested backlight on/off state.

It does not persist the raw LEDC PWM duty. On VoCat v1.0, logical brightness is
mapped to the board's hardware range when the backlight is on. The board does
not expose a separate backlight-enable GPIO through the HAL, so the off state
is applied as zero LEDC brightness.

## Auto-load behavior

`CONFIG_BROOKESIA_SERVICE_DISPLAY_BACKLIGHT_ENABLE_AUTO_LOAD_DATA` controls
whether Display Service restores both persisted values from NVS in its start
callback. Loading data immediately applies the restored state to the HAL; it
is not a cache-only operation.

In Brookesia Service Display 0.8.x, the public macro header defaults auto-load
to enabled when a disabled bool is absent from `sdkconfig.h`. The project keeps
a local component override whose fallback is disabled, allowing
`sdkconfig.defaults` to be the source of truth.

## Startup sequence used by this project

1. Display Service starts with automatic backlight restore disabled.
2. The LVGL display source, Emote assets, and Speaker UI shell initialize while
   the LCD backlight remains off.
3. After a one-second display-ready delay, the application calls `LoadData`.
   This restores and applies the saved brightness and on/off state.
4. The application requests `SetBacklightOnOff(true)` so a previously stored
   off state does not leave the device invisible after boot.

Restoring data before the delay was tested and removed because it exposed a
white frame. Letting the service auto-load during startup exposed uninitialized
panel contents as noise and then white. The delayed sequence avoids both on the
VoCat v1.0 hardware.
