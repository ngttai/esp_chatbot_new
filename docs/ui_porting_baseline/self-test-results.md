# Phase 0 self-test results

Source commit: 5a4bb94089ce47a66e4ecd01546adc724d1589a7
Build: clean CMake/Ninja build against LVGL 9.5.0
Date: 2026-09-18
Result: 11/11 PASS

| Test | Result | Output |
|---|---|---|
| `--self-test-home` | PASS | Bottom-edge upward black-screen gesture passed |
| `--self-test-launcher` | PASS | Launcher icon swipe passed |
| `--self-test-idle` | PASS | Black-screen long press passed |
| `--self-test-settings-bar` | PASS | Settings bottom bar remained fixed while scrolling |
| `--self-test-settings-pages` | PASS | Settings child-page navigation passed |
| `--self-test-settings-no-scroll` | PASS | Settings list stayed put during a bottom-edge Home gesture |
| `--self-test-quick-gesture` | PASS | Quick Settings top-edge drag gesture passed |
| `--self-test-quick-close-gesture` | PASS | Quick Settings bottom-edge drag-to-close gesture passed |
| `--self-test-wlan-toggle` | PASS | WLAN on/off toggle sync passed |
| `--self-test-wlan-keyboard` | PASS | WLAN password keyboard passed |
| `--self-test-quick-buttons` | PASS | Quick Settings Volume/Brightness passed |
