# Component overrides

The project keeps selected ESP-Brookesia components under `components/` and
selects them with `override_path` in `main/idf_component.yml`. This makes the
hardware-specific fixes reproducible and keeps them independent of the
generated `managed_components/` directory.

## Current overrides

| Component | Registry baseline | Modified source | Purpose |
| --- | --- | --- | --- |
| `brookesia_service_wifi` | 0.8.2 | `src/service_wifi.cpp` | Scan visible APs before selecting a saved network, retry the disconnected AP before scanning, stop recovery scans after connection, and allow a visible AP to recover from an earlier failed attempt. |
| `brookesia_service_display` | 0.8.2 | `include/brookesia/service_display/macro_configs.h` | Default backlight auto-load to disabled when the false Kconfig bool is absent from `sdkconfig.h`. The application restores the persisted state after the display pipeline is ready. |
| `brookesia_agent_xiaozhi` | 0.8.2 | `src/agent_xiaozhi.cpp` | Cancel a pending delayed audio-channel open when XiaoZhi enters sleep, preventing an unexpected return to Listening. |
| `brookesia_gui_lvgl` | 0.8.5 | `include/brookesia/gui_lvgl/display_source.hpp`, `src/port/esp/display_source.cpp` | Allow the LVGL display source framebuffer to be allocated in PSRAM. |
| `esp_audio_simple_player` | 0.9.6~1 | `src/audio_simple_player_pool.c`, `src/esp_audio_simple_player.c` | Create the resampler with `ESP_AE_RATE_CVT_PERF_TYPE_MEMORY` instead of the GMF default `SPEED`. The default profile allocates ~31 KB (25,600 + 2 x 2,708 B) of internal-only RAM (`audio_effects_calloc_inner`) while a track is open, held through Pause; MEMORY removes those blocks at the cost of more CPU. Sample rate, channels, bits, buffers and the pipeline lifecycle are unchanged. An optional weak PCM observer after the converters supplies the Music spectrum through a bounded try-lock/copy. |

`components/bq27220` and `components/gen_bmgr_codes` are project components;
they are not Registry component overrides.

## esp_audio_simple_player resampler profile

`esp_audio_simple_player` 0.9.6~1 registers its `aud_rate_cvt` element with
`DEFAULT_ESP_GMF_RATE_CVT_CONFIG()`, whose `perf_type` is `SPEED`. The version
has no Kconfig option, element setter or application-reachable handle for
`perf_type` (the av_processor player handle is opaque), so the project patch is
a single field in `src/audio_simple_player_pool.c`. The player is shared by
Music and the XiaoZhi prompts, so both use the MEMORY profile. Boot log line to
confirm the build: `ASP_POOL: Dest rate:16000 perf_type:0`.

Measured on the board (Wi-Fi off, one 10-minute track): internal free while
playing 100,051 B (SPEED) vs 131,711 B (MEMORY); DMA largest block 31,744 B vs
47,104 B. CPU is higher with MEMORY (core 0 about 47% busy while playing); a
strict A/B figure was not taken. Music and the goodbye prompt were checked by ear on the board. Re-evaluate when upgrading `esp_audio_simple_player`,
`gmf_audio` or `esp_audio_effects`: newer versions name the profiles
differently (SPEED/QUALITY in the online docs).

## v1.1.1 review

Reviewed on 2026-10-04 against the exact Registry packages resolved by
`dependencies.lock`. All four overrides remain necessary:

- Wi-Fi 0.8.2 does not contain the scan-first recovery and bounded reconnect
  changes in the local `service_wifi.cpp`.
- Display 0.8.2 still defaults backlight auto-load to enabled when the false
  Kconfig bool is absent.
- XiaoZhi agent 0.8.2 does not cancel its delayed audio-channel open on sleep.
- GUI LVGL 0.8.5 does not forward the `use_psram` display-source setting.

`brookesia_service_helper` remains pinned to 0.8.4. Registry version 0.8.5
includes `brookesia/hal_interface/interfaces/expansion/module_manager.hpp`,
but the resolved `brookesia_hal_interface` 0.8.2 package does not provide that
header. Upgrading the helper therefore requires a coordinated HAL dependency
upgrade and is outside the scope of the v1.1.1 patch.

## Identify active overrides

List the local paths selected by the project manifest:

```bash
rg -n "override_path" main/idf_component.yml
```

Confirm the resolved version and local source in the dependency lock file:

```bash
rg -n "brookesia_(service_wifi|service_display|agent_xiaozhi|gui_lvgl)|esp_audio_simple_player" \
  dependencies.lock -A 12
```

Entries with `source.type: local` and a path under `components/` are active
overrides. The component's own `idf_component.yml` records its upstream
baseline version.

## Compare an override with the Registry package

Compare against the exact version recorded in `dependencies.lock`, not the
ESP-Brookesia `master` branch. Master can move independently of the packaged
version used to build the firmware.

ESP Component Manager reports its local package-cache path with:

```bash
compote cache path
```

For example, compare the Wi-Fi override with Registry version 0.8.2:

```bash
CACHE="$(compote cache path)"
REMOTE="$(find "$CACHE" -type d \
  -name 'espressif__brookesia_service_wifi_0.8.2_*' | head -n 1)"

diff -ru \
  --exclude=.component_hash \
  --exclude=CHECKSUMS.json \
  --exclude=README_CN.md \
  --exclude=test_apps \
  "$REMOTE" \
  components/brookesia_service_wifi
```

To inspect the actual patch for one modified file:

```bash
diff -u \
  "$REMOTE/src/service_wifi.cpp" \
  components/brookesia_service_wifi/src/service_wifi.cpp
```

Replace the component name and version in `REMOTE` for the other overrides.
If the matching package is no longer in the cache, obtain that exact version
from ESP Component Registry before comparing it; do not substitute a newer
package or the current upstream branch.

Files such as `CHECKSUMS.json`, `.component_hash`, Registry README files, and
component test applications may exist only in the downloaded package. Exclude
these packaging-only differences when reviewing the firmware patch.
