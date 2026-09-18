# Phase 2 — Brookesia host integration report

Date: 2026-09-18

## Scope

Phase 2 replaces only the UI entrypoint and build wiring of the Linux host target.
The 74 imported UI files remain byte-for-byte identical to the Phase 0 source
baseline. The Phase 3 self-test runner is not included in this phase.

## Integration

- `host_sim/main.cpp` creates the exact shell with `speaker_ui_create()`.
- The Brookesia LVGL pointer input is passed to `speaker_ui_set_input()`.
- `host_sim/CMakeLists.txt` compiles the imported UI, flip clock, generated screens,
  fonts, icons, and Quick Settings assets.
- `host_sim/lv_conf.h` enables the LVGL widgets and fonts required by the source UI.
- `LV_USE_SDL` remains disabled because Brookesia HAL Linux owns display and input.

## Verification

Host build:

```sh
cmake -S host_sim -B build-host-phase2 -G Ninja
cmake --build build-host-phase2
```

Result: successful executable link (`esp_chatbot_host_sim`).

Runtime smoke test:

- Brookesia service manager, storage, display, LVGL display source, and SDL2
  360x360 output initialized successfully.
- Closing the SDL window shut down all services cleanly with exit code 0.

Interactive checks performed through the original input paths:

1. Black idle screen loaded on startup.
2. Long press opened Launcher.
3. Top-edge downward drag opened Quick Settings.
4. Settings opened from its Launcher icon.
5. Bottom-edge Home gesture returned to Launcher.
6. AI Profile opened from its Launcher icon.
7. Horizontal Launcher swipes reached the Clock page.
8. Clock opened and rendered the flip clock, date, weather icon, temperature, and
   weather text.

Source integrity:

```sh
cd main/modules/display/speaker_ui
sha256sum -c SOURCE_FILES.sha256
```

Result: 74/74 files passed; no imported UI source was modified.

## Checkpoint

P2 host build and interactive shell verification: **PASS**.

Next checkpoint: Phase 3, port the 11-option self-test runner without changing the
UI implementation or its public API.
