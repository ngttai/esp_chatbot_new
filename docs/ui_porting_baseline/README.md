# SDL UI porting baseline — Phase 0

## Status

P0: **PASS**

The source simulator was rebuilt from a clean temporary build directory, all 11 self-tests passed, and 17 golden screenshots were captured.

## Source identity

- Repository: `/home/nttai/ntt_ws/esp_project/sdl_ui_simulator`
- Commit: `5a4bb94089ce47a66e4ecd01546adc724d1589a7`
- Commit date: `2026-09-17T15:47:35+00:00`
- Subject: `Add boot splash overlay, ported from HTC_Flip_Clock_with_weather`
- Tracked source state: unchanged.
- Existing untracked directory: `Claude outputs/`.
- The untracked directory is not part of this baseline or porting manifest.

## Build environment

- LVGL: 9.5.0
- SDL2: 2.30.0
- C compiler: GCC 13.3.0
- CMake: 3.28.3
- Ninja: 1.11.1
- Resolution: 360×360
- Clean build directory: `/tmp/sdl_ui_phase0_build`

The repository's existing `build/` cache referenced an obsolete mount path, so it was not reused. The clean baseline build used the already-present LVGL 9.5.0 checkout and did not modify the source repository.

## Source inventory

- 77 direct source/header dependencies discovered from the clean Ninja dependency graph.
- Total size: 4,840,262 bytes.
- `source-manifest.txt` lists the dependency paths.
- `source-files.sha256` records hashes for those dependencies plus `CMakeLists.txt`, `lv_conf.h`, and `scripts/vendor-manifest.txt`.

The dependency inventory includes `src/main.c` and boot splash files because they are part of the reference executable. Phase 1 may classify them as host harness/reference files rather than shared UI files, but their baseline hashes remain recorded.

## Self-tests

See `self-test-results.md`. Result: **11/11 PASS**.

Tests ran directly on the available X display `:0`. The local `xvfb-run` wrapper could not create a separate X server because of the ownership of `/tmp/.X11-unix`; this did not affect the simulator or test results.

## Golden screenshots

17 screenshots were captured as 360×360, 32-bit BMP files:

- idle
- launcher
- launcher-pressed
- quick
- settings
- settings-bottom
- wlan
- wlan-bottom
- wlan-connect
- softap
- sound
- display
- about
- developer
- restore
- ai
- timer

Their hashes are stored in `screenshots.sha256`. `contact-sheet.png` is a review aid and is not itself a golden reference.

The screenshots use the reference runner's standard capture delay (30 UI pumps at 10 ms). The Clock image was captured at 2026-09-18 22:28:39 +07:00 and contains live system time, so Phase 4 must compare its layout and non-time pixels rather than expecting a future whole-file hash match. WLAN screenshots likewise preserve the exact early state produced by the standard 300 ms capture delay, before the mocked multi-second reveal sequence settles.

## Acceptance

- [x] Source commit recorded.
- [x] Source working state recorded.
- [x] Exact dependency inventory recorded.
- [x] SHA-256 hashes recorded.
- [x] Clean baseline build succeeded.
- [x] 11/11 self-tests passed.
- [x] 17 golden screenshots captured.
- [x] Screenshot dimensions and formats verified.
- [x] Contact sheet visually inspected.

Phase 0 is complete. Phase 1 must use this baseline to verify copied source integrity.
