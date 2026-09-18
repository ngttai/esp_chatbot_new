# Exact SDL UI source import manifest

## Baseline

- Source repository: `/home/nttai/ntt_ws/esp_project/sdl_ui_simulator`
- Source commit: `5a4bb94089ce47a66e4ecd01546adc724d1589a7`
- Baseline report: `docs/ui_porting_baseline/README.md`
- Import date: `2026-09-18`
- Destination root: `main/modules/display/speaker_ui/`

## Integrity policy

Every imported UI source, header, generated component, font, icon, and animation declaration is copied byte-for-byte. No include, formatting, symbol, layout, style, behavior, or API change was made during Phase 1.

`SOURCE_PATHS.txt` contains the 74 imported paths relative to both the source repository and this module. `SOURCE_FILES.sha256` records their source-baseline SHA-256 values.

Verify the imported tree from this directory with:

```sh
sha256sum -c SOURCE_FILES.sha256
```

## Path mapping

The mapping is intentionally one-to-one:

```text
sdl_ui_simulator/src/...                    → speaker_ui/src/...
sdl_ui_simulator/vendor/esp_speaker_ui/... → speaker_ui/vendor/esp_speaker_ui/...
```

This preserves the source directory structure and minimizes future integration-only changes.

## Imported scope

- `src/speaker_ui.c` and `src/speaker_ui.h`.
- Desktop compatibility headers used by the reference UI.
- Complete `src/flip_clock/` implementation.
- Launcher icons compiled by the reference target.
- Settings icons and fonts compiled by the reference target.
- Generated Quick Settings component and assets.
- Generated AI Profile screen and assets.
- Animation declaration headers transitively included by the speaker asset header.

## Intentionally not imported into the shared UI module

| Source path | Reason |
|---|---|
| `src/main.c` | SDL executable and self-test runner harness. Its assertions and CLI contract were ported in Phase 3 to the Brookesia host runner without placing SDL ownership in the shared UI module. |
| `src/boot_splash.c` | Boot overlay is outside the agreed shell scope and is not used by screenshots or self-tests. |
| `src/boot_splash.h` | Header belonging to the excluded boot overlay. |

These exclusions are recorded, not substitutions. Their hashes remain available in the Phase 0 baseline.

## Phase 1 result

- Imported files: 74
- Source/hash mismatches: 0
- UI source modifications: 0
- Existing SquareLine Settings removed: no
- Build integration performed: no
- Host behavior changed: no

## Phase 2 integration result

- The module is compiled by `host_sim/CMakeLists.txt` without editing imported files.
- The host entrypoint calls `speaker_ui_create()` and passes the Brookesia LVGL
  pointer input to `speaker_ui_set_input()`.
- SDL ownership remains in Brookesia HAL Linux; LVGL's built-in SDL driver remains
  disabled.
- `sha256sum -c SOURCE_FILES.sha256`: 74/74 passed after integration.
- Launcher, Quick Settings, Settings, AI Profile, and Clock were opened through
  their original gestures/click paths in the host simulator.

## Phase 3 self-test result

- All 11 original `--self-test-*` CLI options are available in the Brookesia host
  executable.
- Test coordinates, gesture thresholds, operation order, assertions, pass/fail
  messages, and assertion exit codes match the source runner.
- Input travels through `Display::inject_touch()` and the normal Brookesia
  Display/HAL Linux/LVGL input path.
- CTest result: 11/11 passed.
- Imported UI source changes: none.

## Phase 4 visual parity result

- All 17 reference screens were captured from the Brookesia RGB565 buffer output
  at 360×360 using the same 30 × 10 ms UI pump interval as the source runner.
- All 16 non-time screens have zero differing pixels at a 5% per-channel fuzz
  threshold.
- All four regions outside Clock's live/animated digit rectangle have zero
  differing pixels at the same threshold.
- Whole-frame differences are limited to RGB565 conversion/renderer rounding and
  the live Clock value/flip-animation phase; no geometry or state mismatch was
  found.
- Imported file integrity remains 74/74 after Phase 4.
- Detailed evidence: `docs/ui_porting_phase4.md`.

## Post-Phase 4 host brightness simulation

- A host-only adapter observes the existing Quick Settings brightness level and
  Settings > Display slider without editing this imported module.
- Values are sent to Brookesia Display service's `SetBacklightBrightness` API.
- The Linux HAL renders the result through its SDL backlight overlay.
- Quick Settings uses the original firmware mapping: 40%, 70%, and 100%.
- The Display slider passes its existing 0–100 value directly.
- Auto adjust is intentionally not simulated because no ambient-light input is
  present.
- Imported UI source changes: none.
