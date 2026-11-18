#!/usr/bin/env python3
"""Parse a SYSTEM STATS log into samples.jsonl, summary.csv and SHA256SUMS.

CPU values are recomputed from the raw runtime deltas printed in the log, not from the
rounded percentages. N/A stays empty in the outputs; it is never written as 0.
"""
import argparse
import csv
import hashlib
import json
import pathlib
import re
import sys

LINE = re.compile(r"^(?:\x1b\[[0-9;]*m)?[IWE] \((\d+)\) SysStats: (.*?)(?:\x1b\[0m)?\s*$")
KV = re.compile(r"(\w+)=(\S+)")
CSV_COLUMNS = [
    "run_id", "source_sha", "scenario", "window_start_ms", "window_end_ms", "cpu0_busy_pct", "cpu1_busy_pct",
    "internal_free_bytes", "internal_largest_bytes", "internal_min_since_boot_bytes", "dma_largest_bytes",
    "psram_free_bytes", "psram_largest_bytes", "power_state", "valid", "notes",
]


def num(text):
    return int(text) if re.fullmatch(r"\d+", text or "") else None


def pct(text):
    return float(text.rstrip("%")) if text and text.rstrip("%") not in ("N/A", "") else None


def parse_task(rest):
    tokens = rest.rsplit(None, 9)
    if len(tokens) != 10:
        return None
    name, hdl, core, pri, cpu, drun, stack, hwm, state, note = tokens
    return {
        "name": name.strip(), "handle": hdl, "core": core, "priority": num(pri), "cpu_pct_printed": pct(cpu),
        "run_delta_us": num(drun), "stack": None if stack == "N/A" else stack, "hwm_bytes": num(hwm),
        "state": None if state == "N/A" else state, "note": note,
    }


def parse_log(text):
    blocks, cur, pending, enabled = [], None, [], None
    for raw in text.splitlines():
        m = LINE.match(raw)
        if not m:
            continue
        log_ms, msg = int(m.group(1)), m.group(2)
        if msg.startswith("SYSTEM STATS enabled"):
            enabled = dict(KV.findall(msg))
        elif msg.startswith("SYSTEM STATS WARN"):
            (cur["warnings"] if cur else pending).append(msg)
        elif msg.startswith("SYSTEM STATS begin"):
            if cur is not None:
                blocks.append(cur)
            kv = dict(KV.findall(msg))
            cur = {"seq": num(kv.get("seq")), "log_ms": log_ms, "up_ms": num(kv.get("up_ms")),
                   "period_ms": num(kv.get("period_ms")), "warnings": pending, "tasks": [], "complete": False}
            pending = []
        elif cur is None:
            continue
        elif msg.startswith("SYSTEM STATS end"):
            cur["complete"] = num(dict(KV.findall(msg)).get("seq")) == cur["seq"]
            blocks.append(cur)
            cur = None
        elif msg.startswith("build "):
            cur["build"] = dict(KV.findall(msg))
        elif msg.startswith("ctx "):
            cur["ctx"] = dict(KV.findall(msg))
        elif msg.startswith("cpu "):
            cur["cpu"] = dict(KV.findall(msg))
        elif msg.startswith("mem "):
            parts = msg.split()
            if parts[1] in ("SRAM", "PSRAM") and len(parts) == 7:
                cur.setdefault("mem", {})[parts[1]] = dict(zip(
                    ("total", "free", "min_boot", "seen_min", "largest"), (num(x) for x in parts[2:])))
        elif msg.startswith("dma "):
            kv = dict(KV.findall(msg))
            cur["dma"] = {"largest": num(kv.get("largest_B")), "free": num(kv.get("free_B"))}
        elif msg.startswith("tasks "):
            cur["task_summary"] = dict(KV.findall(msg))
        elif msg.startswith("task "):
            t = parse_task(msg[5:])
            if t:
                cur["tasks"].append(t)
        elif msg.startswith("self "):
            cur["self"] = dict(KV.findall(msg))
    if cur is not None:
        blocks.append(cur)
    return blocks, enabled


def evaluate(block, require_power):
    """Recompute CPU from raw counters and decide whether the window is usable."""
    reasons = []
    cpu = block.get("cpu", {})
    window = num(cpu.get("window_us"))
    ts = block.get("task_summary", {})
    if num(ts.get("shown")) != len(block.get("tasks", [])):
        reasons.append("task_rows_missing")
    for required in ("build", "ctx", "cpu", "mem", "dma", "task_summary", "self"):
        if required not in block:
            reasons.append(f"missing_{required}")
    for region in ("SRAM", "PSRAM"):
        if region not in block.get("mem", {}):
            reasons.append(f"missing_mem_{region}")
    if ts.get("sample") != "OK":
        reasons.append(f"sample={ts.get('sample', 'N/A')}")
    if not window:
        reasons.append("no_window")
    rec = {}
    for core in (0, 1):
        idle = num(cpu.get(f"idle{core}_us"))
        if window and idle is not None and idle <= window * 1.01:
            rec[f"core{core}_busy_pct"] = round(max(0.0, 100.0 * (1 - idle / window)), 2)
            printed = pct(cpu.get(f"core{core}_busy"))
            if printed is not None and abs(printed - rec[f"core{core}_busy_pct"]) > 0.02:
                reasons.append(f"core{core}_printed_vs_raw_mismatch")
        else:
            rec[f"core{core}_busy_pct"] = None
    block["recomputed"] = rec
    for t in block["tasks"]:
        d = t["run_delta_us"]
        t["cpu_pct_one_core"] = round(100.0 * d / window, 2) if (window and d is not None) else None
    ctx = block.get("ctx", {})
    # Screen/power use exact transition counters (a round trip leaves equal end values but a non-zero counter).
    # Wi-Fi/agent are compared at the two window ends only; a round trip inside one window is not detectable.
    if "mixed" not in ctx:
        reasons.append("no_mixed_marker")  # log from a build without revision counters: cannot be qualified
    else:
        if ctx["mixed"] != "no" and not ctx["mixed"].startswith("yes:"):
            reasons.append("mixed_marker_unreadable")
    chg_screen = num(ctx.get("chg_screen"))
    chg_power = num(ctx.get("chg_power"))
    if "power_start" in ctx or (chg_power or 0) > 0:
        reasons.append("power_changed_in_window")
    if "screen_start" in ctx or (chg_screen or 0) > 0:
        reasons.append("screen_changed_in_window")
    if "wifi_start" in ctx:
        reasons.append("wifi_changed_in_window")
    if "agent_start" in ctx:
        reasons.append("agent_changed_in_window")
    mixed = ctx.get("mixed", "")
    if mixed.startswith("yes:") and not any(r.endswith("_changed_in_window") for r in reasons):
        reasons.append("mixed_window:" + mixed[4:])
    if require_power and ctx.get("power") != require_power:
        reasons.append(f"power={ctx.get('power')}")
    if not block["complete"]:
        reasons.append("incomplete_block")
    block["valid"] = not reasons
    block["invalid_reasons"] = reasons
    return block


def sha256(path):
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def write_outputs(run_dir, blocks, enabled, manifest):
    # Qualify every block before emitting JSONL/CSV, including captures spanning reboots.
    expected = manifest.get("elf_sha256")
    identity_reasons = {"elf_identity_missing_or_incomplete", "elf_identity_mismatch",
                        "startup_elf_identity_mismatch"}
    for block in blocks:
        reasons = [r for r in block["invalid_reasons"] if r not in identity_reasons]
        if expected:
            actual = block.get("build", {}).get("elf", "")
            if not re.fullmatch(r"[0-9a-fA-F]{64}", actual):
                reasons.append("elf_identity_missing_or_incomplete")
            elif actual.lower() != expected.lower():
                reasons.append("elf_identity_mismatch")
            startup = enabled.get("elf_sha256") if enabled else None
            if startup and startup.lower() != expected.lower():
                reasons.append("startup_elf_identity_mismatch")
        block["invalid_reasons"] = reasons
        block["valid"] = not reasons
    with open(run_dir / "samples.jsonl", "w") as f:
        for b in blocks:
            f.write(json.dumps(b, sort_keys=True) + "\n")
    with open(run_dir / "summary.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(CSV_COLUMNS)
        for b in blocks:
            mem, rec = b.get("mem", {}), b["recomputed"]
            window_us = num(b.get("cpu", {}).get("window_us"))
            end_ms = b.get("up_ms")
            start_ms = (end_ms - window_us // 1000) if (end_ms is not None and window_us) else None
            sram, psram = mem.get("SRAM", {}), mem.get("PSRAM", {})
            row = [
                manifest.get("run_id"), manifest.get("source_sha"), manifest.get("scenario", {}).get("id"),
                start_ms, end_ms, rec["core0_busy_pct"], rec["core1_busy_pct"], sram.get("free"),
                sram.get("largest"), sram.get("min_boot"), b.get("dma", {}).get("largest"), psram.get("free"),
                psram.get("largest"), b.get("ctx", {}).get("power"), "yes" if b["valid"] else "no",
                ";".join(b["invalid_reasons"] + ["window bounds derived from uptime at emit",
                                                  "wifi/agent compared at window ends only"]),
            ]
            w.writerow(["" if v is None or v == "N/A" else v for v in row])
    manifest["evidence"].update({"samples": "samples.jsonl", "summary": "summary.csv", "sha256sums": "SHA256SUMS"})
    notes = manifest.setdefault("notes", [])
    if enabled:
        manifest["probe"]["storage_psram_bytes"] = num(enabled.get("store_bytes")) if enabled.get("store") == "PSRAM" else None
    log_elf = enabled.get("elf_sha256") if enabled else None
    if not log_elf and blocks:
        log_elf = blocks[0].get("build", {}).get("elf")
    if manifest.get("elf_sha256") and log_elf:
        if len(log_elf) != 64:
            notes.append("Log only contains an ELF prefix: full firmware identity is not verified")
        elif manifest["elf_sha256"] != log_elf:
            notes.append("ELF SHA-256 in the log differs from the recorded ELF: do not use this capture")
    if blocks:
        build = blocks[0].get("build", {})
        if manifest["probe"].get("storage_psram_bytes") is None and build.get("store") == "PSRAM":
            manifest["probe"]["storage_psram_bytes"] = num(build.get("store_bytes"))
        manifest["idf_version"] = build.get("idf") if build.get("idf") != "N/A" else manifest.get("idf_version")
        commit = build.get("commit")
        sha = manifest.get("source_sha")
        if commit not in (None, "N/A") and sha and not sha.startswith(commit):
            notes.append(f"log commit {commit} does not match manifest source_sha {sha[:12]}")
    manifest["status"] = "parsed"
    (run_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    names = ["manifest.json", "raw.log", "samples.jsonl", "summary.csv", "sdkconfig", "dependencies.lock"]
    sums = [f"{sha256(run_dir / n)}  {n}" for n in names if (run_dir / n).exists()]
    (run_dir / "SHA256SUMS").write_text("\n".join(sums) + "\n")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("run_dir", type=pathlib.Path)
    p.add_argument("--require-power", default="Active")
    p.add_argument("--elf", type=pathlib.Path, help="ELF of the flashed firmware; required if manifest has no ELF SHA-256")
    a = p.parse_args()
    manifest = json.loads((a.run_dir / "manifest.json").read_text())
    expected = manifest.get("elf_sha256")
    if a.elf:
        actual = sha256(a.elf)
        if expected and expected.lower() != actual:
            p.error("--elf differs from manifest elf_sha256; use the ELF matching this capture")
        manifest["elf_sha256"] = actual
        expected = actual
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-fA-F]{64}", expected):
        p.error("provide --elf or a full elf_sha256 in manifest.json; firmware identity must be checked")
    blocks, enabled = parse_log((a.run_dir / "raw.log").read_text(errors="replace"))
    blocks = [evaluate(b, a.require_power) for b in blocks]
    write_outputs(a.run_dir, blocks, enabled, manifest)
    ok = sum(1 for b in blocks if b["valid"])
    print(f"{len(blocks)} blocks, {ok} valid, {len(blocks) - ok} flagged; outputs in {a.run_dir}")


if __name__ == "__main__":
    sys.exit(main())
