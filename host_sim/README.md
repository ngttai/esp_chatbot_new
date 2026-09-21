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

`networkmanager` can change the computer's real network state. It is enabled
only when that exact value is selected; Wi-Fi `auto` intentionally remains on
the safe stub. Keep `stub` for normal development and CI. Camera, video, and BLE
are outside this simulator's scope and remain disabled.

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

## Simulated battery and power

The host-only power adapter reads Brookesia Linux `PowerBattery` state and
updates the existing Quick Settings battery widgets without changing their
source:

- The default `stub` backend deterministically reports 67%, external power, and
  charging.
- `HOST_SIM_POWER_BACKEND=upower` opts into the host power backend. When its
  dependency or a physical battery is unavailable, Brookesia reports the
  fallback and uses the explicit deterministic mock.
- A missing percentage is displayed as `--%`; the charge icon is shown only for
  external power or a charging state.
- The adapter is read-only and never calls charger-control APIs.

## Deterministic Wi-Fi mock

The default `stub` backend now drives the unchanged WLAN UI through Brookesia
Linux Wi-Fi interfaces without reading or changing the host network:

- WLAN and the Quick Settings Wi-Fi button start/stop the mock backend together.
- Scan results match the original UI: locked `ESP-Lab`, locked `NTT_Office`, and
  open `Guest`.
- `ESP-Lab` accepts the mock password `esp123456`; any other password produces
  `Authentication failed`.
- `NTT_Office` accepts `ntt123456`, times out on the first attempt, then succeeds
  on retry.
- Opening/leaving the original SoftAP QR screen starts/stops the mock
  `ESP-Speaker-Setup` AP.
- `auto` or `networkmanager` still falls back to this deterministic policy when
  the real NetworkManager backend is unavailable.

The adapter and credentials are simulator-only. No scan, connection, or SoftAP
operation reaches the computer's network while the default `stub` backend is in
use.

## Real Wi-Fi through NetworkManager (opt-in)

Install NetworkManager and its development metadata, then use a separate build
directory so the normal deterministic build remains untouched:

```sh
sudo apt-get install network-manager libnm-dev
cmake -S host_sim -B build-host-networkmanager -G Ninja \
  -DHOST_SIM_WIFI_BACKEND=networkmanager
cmake --build build-host-networkmanager
./build-host-networkmanager/esp_chatbot_host_sim --print-capabilities
SDL_VIDEODRIVER=dummy \
  ./build-host-networkmanager/esp_chatbot_host_sim --self-test-wifi-real-readonly
./build-host-networkmanager/esp_chatbot_host_sim
```

Before running the SDL application, verify that capability output contains both
`wifi: networkmanager` and `wifi resolved: networkmanager`. A resolved value of
`stub` means a required dependency (`libnm` development metadata or `nmcli`) is
missing, so no real network operation will be attempted.

With the real backend resolved:

- Startup scans and reports status but never initiates a connection.
- The first three NetworkManager scan results replace the text in the existing
  three WLAN rows; no layout or imported UI source is changed.
- Selecting a locked AP uses the original password screen. Selecting an open AP
  connects without requesting a password.
- Passwords are passed directly to the backend and are not written to source,
  simulator storage, artifacts, or application logs.
- Explicit WLAN off/disconnect and SoftAP actions can change the computer's real
  network state. Merely closing the simulator does not disconnect Wi-Fi.
- The deterministic Wi-Fi mock test is not registered when the real backend is
  resolved, so CI must continue to use the default stub build.

The read-only self-test performs scan/status only. It is intentionally not
registered with CTest and does not connect, disconnect, or start SoftAP.

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
the next time the simulator starts. Because the embedded Display service default
turns the backlight off, the host adapter explicitly keeps the SDL display on
after reset so the restored UI remains visible and interactive.

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

The power test verifies the deterministic battery state and the unchanged Quick
Settings widgets:

```sh
./build-host/esp_chatbot_host_sim --self-test-power-simulation
```

The Wi-Fi mock test covers UI on/off binding, locked/open scans, wrong and
correct passwords, connect/disconnect, timeout/retry, and UI-driven SoftAP:

```sh
./build-host/esp_chatbot_host_sim --self-test-wifi-mock
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
