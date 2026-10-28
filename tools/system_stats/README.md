# System stats log parser

Parse terminal logs from firmware built with `CONFIG_EXAMPLE_SYSTEM_STATS=y`.
The Python 3 tool uses only the standard library. It does not connect to the board,
change firmware or start a serial monitor.

## Parse a capture

Keep each capture and its generated results in an ignored local directory:

```sh
mkdir -p .local-artifacts/system-stats/my-run
cp tools/system_stats/templates/manifest.json .local-artifacts/system-stats/my-run/manifest.json
```

Save the terminal output as `raw.log` in that directory. ANSI-colored ESP-IDF
monitor output and unrelated module lines are accepted. Retain the original log.
Fill in the manifest's run ID, scenario and initialization history as appropriate.
Then parse using the **ELF that produced the flashed binary**:

```sh
python3 tools/system_stats/scripts/sysstats_parse.py \
  .local-artifacts/system-stats/my-run \
  --elf /path/to/matching/esp_chatbot.elf
```

`--elf` records its SHA-256 in the manifest. It is optional only when the manifest
already contains the full `elf_sha256`; a conflicting supplied ELF is rejected.
Each block must contain the same full hash. A missing/truncated/different block
hash makes that block invalid; a startup hash mismatch invalidates the capture's
blocks. Do not substitute the Git commit hash for the ELF hash.

By default, only unchanged `Active` power windows qualify. For a separate dimmed
scenario, use `--require-power Dimmed`; do not combine it with Active results.
The template contains example board/scenario settings: verify them for the run,
and leave unknown measurements null rather than inferring them from the log.

Outputs (regenerated on each run):

- `samples.jsonl`: all parsed blocks, raw counters, recomputed CPU and rejection reasons.
- `summary.csv`: per-window CPU and memory, `valid=yes/no`; unknown values stay empty.
- `manifest.json`: updated evidence references and firmware identity.
- `SHA256SUMS`: checksums of the manifest, raw capture and generated evidence files.

The parser preserves incomplete blocks for inspection and rejects missing required
sections, SRAM/PSRAM rows, task-row count mismatches, failed samples, nonmatching
power states and windows with observed screen/power/Wi-Fi/agent changes. CPU values
mean busy time per core (`100 − IDLE%`); DMA memory overlaps SRAM and must not be
added to it. `min_boot` is a historical low, not current free memory.

## Interpretation limits

`valid=yes` qualifies the printed measurement fields; it does **not** prove that
frames reached the LCD, that the UI responds or that no other module logged an
error. Inspect the raw log separately for runtime errors.

Screen revisions count changes observed by a 20 ms poll. Wi-Fi and agent context
only compare window endpoints, so intervening round trips may be missed. The log
also cannot infer whether Wi-Fi was enabled at boot or later: record that history
in the manifest and compare like scenarios. Separate captures at reboot boundaries;
sequence numbers and uptime restart. The parser does not infer workload stability,
subtract diagnostic overhead or establish statistically significant differences.

## Tests

```sh
python3 -m unittest discover -s tools/system_stats/scripts -p 'test_*.py'
```

The fixtures are synthetic. Actual board logs, release baselines and generated
reports belong in `.local-artifacts/`, not in this directory. CI runs these tests
alongside the simulator checks. Serial capture automation is outside this tool.
