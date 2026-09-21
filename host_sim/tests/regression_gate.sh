#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "Usage: $0 <default-build-directory> [parity-output-directory]" >&2
    exit 2
fi

script_directory=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
project_root=$(realpath "$script_directory/../..")
build_directory=$(realpath "$1")
host_executable="$build_directory/esp_chatbot_host_sim"
golden_directory="$project_root/docs/ui_porting_baseline/screenshots"
ui_directory="$project_root/main/modules/display/speaker_ui"

if [[ ! -x "$host_executable" ]]; then
    echo "Host executable not found: $host_executable" >&2
    exit 2
fi

temporary_output=""
if [[ $# -eq 2 ]]; then
    parity_output=$(realpath -m "$2")
else
    temporary_output=$(mktemp -d "${TMPDIR:-/tmp}/esp-chatbot-regression.XXXXXX")
    parity_output="$temporary_output"
fi
cleanup()
{
    if [[ -n "$temporary_output" && -d "$temporary_output" ]]; then
        rm -rf -- "$temporary_output"
    fi
}
trap cleanup EXIT

echo "[1/6] Building the default simulator"
cmake --build "$build_directory"

echo "[2/6] Verifying deterministic default capabilities"
capabilities=$("$host_executable" --print-capabilities)
required_capabilities=(
    "  display: sdl2"
    "  storage: linux-filesystem"
    "  media:   stub"
    "  media resolved: stub"
    "  wifi:    stub"
    "  wifi resolved: stub"
    "  power:   stub"
    "  time:    system"
    "  weather: mock"
)
for expected in "${required_capabilities[@]}"; do
    if ! grep -Fqx "$expected" <<<"$capabilities"; then
        echo "Default capability mismatch; expected: $expected" >&2
        printf '%s\n' "$capabilities" >&2
        exit 1
    fi
done

echo "[3/6] Running all registered self-tests"
ctest --test-dir "$build_directory" --output-on-failure

echo "[4/6] Comparing all screens with the frozen baseline"
"$script_directory/visual_parity.sh" \
    "$host_executable" "$golden_directory" "$parity_output"

echo "[5/6] Verifying the 74 imported UI files"
if ! hash_report=$(cd "$ui_directory" && sha256sum -c SOURCE_FILES.sha256); then
    printf '%s\n' "$hash_report" >&2
    exit 1
fi
hash_count=$(grep -c ': OK$' <<<"$hash_report")
if [[ "$hash_count" -ne 74 ]]; then
    echo "Expected 74 UI hashes, found $hash_count" >&2
    exit 1
fi
if ! git -C "$project_root" diff --quiet -- main/modules/display/speaker_ui ||
   ! git -C "$project_root" diff --cached --quiet -- main/modules/display/speaker_ui; then
    echo "Imported UI directory contains tracked modifications" >&2
    exit 1
fi
untracked_ui=$(git -C "$project_root" ls-files --others --exclude-standard -- \
    main/modules/display/speaker_ui)
if [[ -n "$untracked_ui" ]]; then
    echo "Imported UI directory contains untracked files:" >&2
    printf '%s\n' "$untracked_ui" >&2
    exit 1
fi

echo "[6/6] Scanning tracked files for committed credentials or logs"
credential_candidates=$(git -C "$project_root" grep -nE \
    '(^|[^[:xdigit:]])[[:xdigit:]]{32}([^[:xdigit:]]|$)' -- . \
    ':!*.bmp' ':!*.png' ':!*.gif' 2>/dev/null || true)
credential_candidates=$(grep -vE 'sid=[[:xdigit:]]{32}' <<<"$credential_candidates" || true)
if [[ -n "$credential_candidates" ]]; then
    echo "Possible 32-character API credential found in tracked text:" >&2
    printf '%s\n' "$credential_candidates" >&2
    exit 1
fi
weather_assignments=$(git -C "$project_root" grep -nE \
    'OPENWEATHER_API_KEY[[:space:]]*=' -- . 2>/dev/null || true)
weather_assignments=$(grep -v "OPENWEATHER_API_KEY='your-key'" \
    <<<"$weather_assignments" || true)
if [[ -n "$weather_assignments" ]]; then
    echo "A non-placeholder OpenWeather key assignment is tracked:" >&2
    printf '%s\n' "$weather_assignments" >&2
    exit 1
fi
tracked_sensitive_files=$(git -C "$project_root" ls-files | \
    grep -E '(^|/)(\.env([^/]*)?|[^/]*\.log)$' || true)
if [[ -n "$tracked_sensitive_files" ]]; then
    echo "Tracked environment or log files require manual review:" >&2
    printf '%s\n' "$tracked_sensitive_files" >&2
    exit 1
fi

echo "P4.9 regression PASS: safe defaults, tests, visual parity, UI hashes, credentials."
