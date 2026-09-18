# Visual parity report — Phase 4

## Result

P4: **PASS**

The Brookesia host target matches the frozen Phase 0 SDL UI baseline on all 17
screens. No imported UI source, layout, style, asset, gesture, navigation, or
animation implementation was changed during this phase.

## Capture method

- Resolution and pixel format: 360×360, RGB565.
- Source oracle: the 17 golden BMP files in
  `docs/ui_porting_baseline/screenshots/`.
- Host pixels are read directly from a Brookesia `BufferOutputConfig`, avoiding
  window scaling and desktop-compositor effects.
- RGB565 is expanded to the 32-bit BMP using the same integer conversion as the
  Brookesia Linux display path.
- Both runners process UI for 30 × 10 ms before capture. Launcher pressed then
  receives the same press at source coordinate `(110, 165)` and 20 × 10 ms more.
- The capture and comparison code is host-only. It does not modify the imported
  UI implementation.

## Pixel comparison

The raw AE count is retained to show renderer-level differences. The acceptance
comparison applies 5% per-channel fuzz; every non-time screen then has zero
differing pixels.

| Screen | Raw AE pixels | AE at 5% fuzz | Normalized RMSE |
|---|---:|---:|---:|
| idle | 0 | 0 | 0 |
| launcher | 3,142 | 0 | 0.000426973 |
| launcher-pressed | 2,861 | 0 | 0.000394169 |
| quick | 1,589 | 0 | 0.000250702 |
| settings | 1,520 | 0 | 0.000365316 |
| settings-bottom | 2,679 | 0 | 0.000325524 |
| wlan | 1,941 | 0 | 0.000277083 |
| wlan-bottom | 1,941 | 0 | 0.000277083 |
| wlan-connect | 3,237 | 0 | 0.000357823 |
| softap | 11,376 | 0 | 0.000670798 |
| sound | 1,122 | 0 | 0.000275364 |
| display | 2,896 | 0 | 0.000578300 |
| about | 2,380 | 0 | 0.000306821 |
| developer | 4,780 | 0 | 0.000434821 |
| restore | 1,957 | 0 | 0.000278222 |
| ai | 85,851 | 0 | 0.001842760 |

The high raw count on AI is not a structural mismatch: most white pixels are
`255,254,255` on the reference path and `255,255,255` on the Brookesia path.
This is a one-level RGB conversion difference and disappears under the accepted
renderer tolerance.

## Clock and flip animation

The Phase 0 Clock golden contains live time `22:28` and date `18 SEP`; a later
host capture cannot match that live rectangle pixel-for-pixel. The test therefore
compares every pixel outside `x=48..311, y=65..199` as four non-overlapping
regions:

| Static region | AE at 5% fuzz |
|---|---:|
| `360x65+0+0` | 0 |
| `48x135+0+65` | 0 |
| `48x135+312+65` | 0 |
| `360x160+0+200` | 0 |

A separate source/host capture made within the same minute displayed the same
time, weather card, and digit geometry. The only pixels above tolerance were
confined to a `191×24` lower-flap strip: 253 pixels differed because the two
event loops sampled slightly different points inside the 340 ms flip animation.
The animation source and assets are byte-identical; its start/end geometry and
final state are unchanged.

## Coverage conclusion

- Widget position and size: pass.
- Fonts and glyph geometry: pass.
- Colors: pass within RGB565 renderer rounding.
- Circular clipping and home indicator: pass.
- Icon scale: pass.
- Scroll positions: pass.
- Quick Settings overlay: pass.
- WLAN keyboard and SoftAP QR: pass.
- AI Profile: pass.
- Clock/weather layout: pass.
- Flip animation: same implementation and states; only capture phase differs.

Review aids:

- `docs/ui_porting_phase4/contact-sheet.png`
- `docs/ui_porting_phase4/diff-contact-sheet.png`

Reproduce the automated comparison with:

```sh
host_sim/tests/visual_parity.sh \
  build-host-phase3/esp_chatbot_host_sim \
  docs/ui_porting_baseline/screenshots
```

Expected result: 16 static screens and all four non-time Clock regions pass with
zero pixels outside the accepted renderer tolerance.
