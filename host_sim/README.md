# ESP VoCat chatbot PC simulator

This target runs the exact imported `speaker_ui` shell from the SDL reference
project through the official ESP-Brookesia Linux HAL and an SDL2 window. It does
not change the ESP-IDF firmware build or modify the imported UI sources.

## Dependencies (Ubuntu/Debian)

Install the Brookesia Linux dependencies (once):

```sh
cd /path/to/esp-brookesia/hal/brookesia_hal_linux
./scripts/install_linux_deps.sh --minimal --display
```

Resolve this project's IDF components once so `managed_components/lvgl__lvgl` is
available:

```sh
. "$HOME/.espressif/v6.1/esp-idf/export.sh"
idf.py reconfigure
```

## Build and run

The default layout expects the Brookesia checkout next to this project:

```text
esp_project/
├── esp-brookesia/
└── esp_chatbot_v1/
```

Then run:

```sh
cmake -S host_sim -B build-host -G Ninja
cmake --build build-host
./build-host/esp_chatbot_host_sim
```

For sources stored elsewhere, pass absolute paths during configuration:

```sh
cmake -S host_sim -B build-host -G Ninja \
  -DBROOKESIA_SOURCE_DIR=/path/to/esp-brookesia \
  -DLVGL_SOURCE_DIR=/path/to/lvgl
```

## Host backend selection

The default build is deterministic: Display uses SDL2 while media, Wi-Fi, and
power use Linux HAL stubs. Show the configured selection without starting SDL:

```sh
./build-host/esp_chatbot_host_sim --print-capabilities
```

Select optional backends at CMake configure time:

```sh
cmake -S host_sim -B build-host-real -G Ninja \
  -DHOST_SIM_MEDIA_BACKEND=ffmpeg_portaudio \
  -DHOST_SIM_WIFI_BACKEND=networkmanager \
  -DHOST_SIM_POWER_BACKEND=upower
```

Accepted values are:

| Option | Values | Default |
|---|---|---|
| `HOST_SIM_MEDIA_BACKEND` | `stub`, `auto`, `ffmpeg_portaudio` | `stub` |
| `HOST_SIM_WIFI_BACKEND` | `stub`, `auto`, `networkmanager` | `stub` |
| `HOST_SIM_POWER_BACKEND` | `stub`, `auto`, `upower` | `stub` |

`networkmanager` can change the computer's real network state once Wi-Fi service
integration is enabled. Keep it at `stub` for normal development and CI. Camera,
video, and BLE are outside this simulator's scope and remain disabled.

The simulator currently covers the display/touch path and UI interactions. Audio
AFE/wake-word, flash partitions, provisioning, and concrete AI-agent components
remain firmware-only and are not started by this host target.

## UI controls

- Start at the black idle screen; press and hold to open Launcher.
- Drag down from the top edge to open Quick Settings.
- Drag up from the bottom Home indicator to return to Launcher; repeat from
  Launcher to return to idle.
- Swipe Launcher horizontally to reach the remaining app pages.

The host target includes the exact Launcher, Quick Settings, Settings, AI Profile,
and Clock UI from the source simulator.

## Simulated brightness

The Linux HAL simulates LCD backlight brightness by applying a black alpha
overlay to the SDL renderer. The host-only brightness adapter connects the
unchanged UI controls to Brookesia Display service:

- Quick Settings cycles through the firmware-compatible levels 40%, 70%, and
  100%.
- Settings > Display controls the simulated backlight continuously from 0% to
  100%.
- Display service persists the selected brightness between simulator runs.
- Auto adjust remains presentation-only because the PC simulator has no ambient
  light sensor source.

The adapter is outside `main/modules/display/speaker_ui`, so the imported UI
source and its behavior remain byte-identical to the reference simulator.

## Simulated volume

The host-only volume adapter connects the unchanged UI to Brookesia Audio
Playback service and the selected Linux HAL media backend:

- Quick Settings uses the firmware-compatible levels Mute, 30%, 60%, and 90%.
- Settings > Sound controls volume continuously from 0% to 100%; 0% also mutes.
- Opening either control synchronizes it with the current service state.
- Volume and mute are persisted by Brookesia Storage service.
- The default `stub` backend is deterministic and does not change global OS
  volume. With the opt-in PortAudio backend, volume applies only to audio played
  by the simulator.

This adapter also stays outside `main/modules/display/speaker_ui`; no imported
layout, asset, gesture, or navigation source is changed.

## Persistent simulator data and Factory Reset

The normal host build stores simulator state beside the executable under
`.brookesia/`. The persistence adapter keeps the unchanged UI synchronized with
Brookesia Storage:

- Display service owns brightness persistence.
- Audio Playback service owns volume and mute persistence.
- The host-only `HostSimulator` namespace stores WLAN on/off and the selected AI
  Profile.
- The Linux file-system sandbox contains the virtual SPIFFS, LittleFS, FATFS,
  and SD-card mounts.

Settings > Restore Factory now resets those service namespaces and clears only
the four virtual file-system mounts inside the simulator sandbox. It never
removes project files or host configuration. The full default UI state is loaded
the next time the simulator starts.

For CI, the four cross-process persistence tests override both KV and file-system
roots with dedicated directories under the build tree. They verify write/read,
the Restore settings button, sandbox cleanup, and defaults in a fresh process.

## Self-tests

The host executable preserves all 11 `--self-test-*` options from the source
simulator. Input is injected through Brookesia Display service rather than through
LVGL's built-in SDL driver:

```sh
./build-host/esp_chatbot_host_sim --self-test-home
./build-host/esp_chatbot_host_sim --self-test-quick-buttons
```

Run the complete suite headlessly with:

```sh
ctest --test-dir build-host --output-on-failure
```

An additional host-only test verifies that both brightness controls reach the
Brookesia backlight interface:

```sh
./build-host/esp_chatbot_host_sim --self-test-brightness-simulation
```

The volume test verifies both Quick Settings and Settings > Sound against the
Audio Playback service:

```sh
./build-host/esp_chatbot_host_sim --self-test-volume-simulation
```

CTest also runs the ordered P4.3 process chain automatically:

```text
persistence-write → persistence-read → factory-reset → persistence-defaults
```

## Visual parity

Capture one screen through the Brookesia RGB565 buffer output without involving
the desktop compositor:

```sh
SDL_VIDEODRIVER=dummy ./build-host/esp_chatbot_host_sim \
  --screenshot /tmp/launcher.bmp launcher
```

The accepted screen names are `idle`, `launcher`, `launcher-pressed`, `quick`,
`settings`, `settings-bottom`, `wlan`, `wlan-bottom`, `wlan-connect`, `softap`,
`sound`, `display`, `about`, `developer`, `restore`, `ai`, and `timer`.

Run the complete Phase 4 comparison against the frozen Phase 0 golden set:

```sh
host_sim/tests/visual_parity.sh \
  build-host/esp_chatbot_host_sim \
  docs/ui_porting_baseline/screenshots
```

The script accepts only per-channel RGB565/renderer variance up to 5%. Because
Clock displays live system time, its four static regions are compared separately
from the animated digit rectangle.
