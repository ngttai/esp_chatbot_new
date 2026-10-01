#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 || $# -gt 3 ]]; then
    echo "Usage: $0 <host-executable> <golden-directory> [output-directory]" >&2
    exit 2
fi

host_executable=$(realpath "$1")
golden_directory=$(realpath "$2")
override_directory="$golden_directory/approved-overrides"
output_directory=${3:-"$(pwd)/visual-parity-output"}
mkdir -p "$output_directory/screenshots" "$output_directory/diffs" "$output_directory/masked"
capture_log="$output_directory/capture.log"
: > "$capture_log"

screens=(
    idle launcher launcher-pressed quick settings settings-bottom wlan wlan-bottom
    wlan-connect softap sound display about developer restore ai timer
)

static_screens=(
    idle launcher launcher-pressed settings settings-bottom wlan wlan-bottom
    wlan-connect softap sound display about developer ai
)

for screen in "${screens[@]}"; do
    SDL_VIDEODRIVER=dummy "$host_executable" \
        --screenshot "$output_directory/screenshots/$screen.bmp" "$screen" \
        >> "$capture_log" 2>&1
done

# Quick Settings contains live time, battery and memory values. Factory Reset
# intentionally uses device wording instead of the original simulator mock text.
# Mask only those content rectangles; all surrounding geometry, icons, controls,
# colors and navigation chrome remain pixel-compared against the frozen baseline.
content_masks=(
    "quick|rectangle 85,24 175,56 rectangle 205,24 275,56 rectangle 65,250 295,306"
    "restore|rectangle 45,125 320,160 rectangle 35,252 325,285"
)
failures=0
for entry in "${content_masks[@]}"; do
    screen=${entry%%|*}
    mask=${entry#*|}
    golden="$golden_directory/$screen.bmp"
    actual="$output_directory/screenshots/$screen.bmp"
    masked_golden="$output_directory/masked/$screen-golden.png"
    masked_actual="$output_directory/masked/$screen-actual.png"
    diff="$output_directory/diffs/$screen.png"
    convert "$golden" -fill black -draw "$mask" "$masked_golden"
    convert "$actual" -fill black -draw "$mask" "$masked_actual"
    raw_ae=$(compare -metric AE "$masked_golden" "$masked_actual" "$diff" 2>&1 || true)
    fuzz_ae=$(compare -metric AE -fuzz 5% "$masked_golden" "$masked_actual" null: 2>&1 || true)
    rmse=$(compare -metric RMSE "$masked_golden" "$masked_actual" null: 2>&1 || true)
    printf '%-20s %12s %12s %14s\n' "$screen (masked)" "$raw_ae" "$fuzz_ae" "$rmse"
    if [[ "$fuzz_ae" != "0" ]]; then
        failures=$((failures + 1))
    fi
done

printf '%-20s %12s %12s %14s\n' "screen" "raw AE" "AE @ fuzz5" "RMSE"
for screen in "${static_screens[@]}"; do
    golden="$golden_directory/$screen.bmp"
    if [[ -f "$override_directory/$screen.bmp" ]]; then
        golden="$override_directory/$screen.bmp"
    fi
    actual="$output_directory/screenshots/$screen.bmp"
    diff="$output_directory/diffs/$screen.png"
    raw_ae=$(compare -metric AE "$golden" "$actual" "$diff" 2>&1 || true)
    fuzz_ae=$(compare -metric AE -fuzz 5% "$golden" "$actual" null: 2>&1 || true)
    rmse=$(compare -metric RMSE "$golden" "$actual" null: 2>&1 || true)
    printf '%-20s %12s %12s %14s\n' "$screen" "$raw_ae" "$fuzz_ae" "$rmse"
    if [[ "$fuzz_ae" != "0" ]]; then
        failures=$((failures + 1))
    fi
done

# Clock digits and date contain live values, and the digits can be in a different
# frame of the 340 ms flip animation. Compare every pixel outside that rectangle.
timer_golden="$golden_directory/timer.bmp"
timer_actual="$output_directory/screenshots/timer.bmp"
timer_regions=(
    "360x65+0+0"
    "48x135+0+65"
    "48x135+312+65"
    # The animated clock glyphs anti-alias into row 200. Start the stable
    # lower region at row 201 so live digit frames are fully excluded.
    "360x159+0+201"
)
timer_failures=0
for region in "${timer_regions[@]}"; do
    metric=$(compare -metric AE -fuzz 5% \
        "$timer_golden[$region]" "$timer_actual[$region]" null: 2>&1 || true)
    printf '%-20s %12s\n' "timer $region" "$metric"
    if [[ "$metric" != "0" ]]; then
        timer_failures=$((timer_failures + 1))
    fi
done

if (( failures != 0 || timer_failures != 0 )); then
    echo "Visual parity FAILED: $failures static screen(s), $timer_failures clock region(s)." >&2
    exit 1
fi

echo "Visual parity PASS: 14 static screens, 2 content-masked screens, and all 4 non-time Clock regions."
