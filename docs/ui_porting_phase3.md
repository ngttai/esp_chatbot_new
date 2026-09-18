# Phase 3 — Brookesia host self-test report

Date: 2026-09-18

## Scope

Port the source simulator's 11 self-test options to the Brookesia Linux host while
keeping the UI implementation and public `speaker_ui.h` API unchanged.

## Runner structure

```text
host_sim/tests/
├── input_injector.cpp
├── input_injector.hpp
└── self_test_main.cpp
```

`self_test_main.cpp` preserves the source test cases, coordinates, gesture travel,
wait counts, thresholds, assertions, messages, and assertion exit codes.

The only platform-specific substitution is the input adapter:

```text
Source: SDL_PushEvent
Host:   Display::inject_touch
```

Each injected state is synchronized with the Brookesia-created `lv_indev_t` before
the original wait period continues. This removes the race between the Display
service's 20 ms touch polling and LVGL's 5 ms timer without bypassing the production
input path.

## Preserved CLI options

1. `--self-test-home`
2. `--self-test-launcher`
3. `--self-test-idle`
4. `--self-test-settings-bar`
5. `--self-test-settings-no-scroll`
6. `--self-test-settings-pages`
7. `--self-test-quick-gesture`
8. `--self-test-quick-close-gesture`
9. `--self-test-wlan-toggle`
10. `--self-test-wlan-keyboard`
11. `--self-test-quick-buttons`

## Verification

Build and run the complete suite:

```sh
cmake -S host_sim -B build-host-phase3 -G Ninja
cmake --build build-host-phase3
ctest --test-dir build-host-phase3 --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 11
Total Test time (real) = 25.98 sec
```

All tests run in independent processes with `SDL_VIDEODRIVER=dummy`, matching the
source runner's one-option-per-process behavior while remaining suitable for CI.

## Source integrity

```sh
cd main/modules/display/speaker_ui
sha256sum -c SOURCE_FILES.sha256
```

Expected and verified result: 74/74 imported files pass.

## Checkpoint

P3 self-test runner: **PASS**.

Next checkpoint: Phase 4 visual parity against the Phase 0 golden screenshots.
