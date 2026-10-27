import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import sysstats_parse as sp  # noqa: E402

FIXTURE = pathlib.Path(__file__).parents[1] / "fixtures" / "synthetic_blocks.log"


class ParseTest(unittest.TestCase):
    def setUp(self):
        self.blocks, self.enabled = sp.parse_log(FIXTURE.read_text())
        self.blocks = [sp.evaluate(b, "Active") for b in self.blocks]

    def test_enabled_line_and_block_count(self):
        self.assertEqual(self.enabled["store"], "PSRAM")
        self.assertEqual(len(self.blocks), 7)
        self.assertTrue(all(b["complete"] for b in self.blocks))

    def test_raw_counters_drive_cpu(self):
        b = self.blocks[0]
        self.assertEqual(b["cpu"]["idle0_us"], "6848000")
        self.assertEqual(b["recomputed"]["core0_busy_pct"], 31.52)
        self.assertEqual(b["recomputed"]["core1_busy_pct"], 68.10)
        gfx = next(t for t in b["tasks"] if t["name"] == "gfx_render")
        self.assertEqual(gfx["run_delta_us"], 1234000)
        self.assertEqual(gfx["cpu_pct_one_core"], 12.34)
        self.assertTrue(b["valid"])

    def test_task_names_with_spaces(self):
        names = [t["name"] for t in self.blocks[0]["tasks"]]
        self.assertIn("Tmr Svc", names)
        self.assertEqual(len(names), 4)

    def test_missing_task_row_is_rejected(self):
        b = self.blocks[0]
        b["tasks"].pop()
        self.assertIn("task_rows_missing", sp.evaluate(b, "Active")["invalid_reasons"])

    def test_missing_memory_header_data_is_rejected(self):
        b = self.blocks[0]
        del b["mem"]
        self.assertFalse(sp.evaluate(b, "Active")["valid"])

    def test_unfinished_block_is_preserved(self):
        text = FIXTURE.read_text().split("SYSTEM STATS end")[0]
        blocks, _ = sp.parse_log(text)
        self.assertEqual(len(blocks), 1)
        self.assertFalse(blocks[0]["complete"])

    def test_full_identity_can_be_checked_without_startup_line(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            run = pathlib.Path(d)
            manifest = json.loads((pathlib.Path(__file__).parents[1] / "templates" / "manifest.json").read_text())
            digest = "a" * 64
            manifest["elf_sha256"] = digest
            for block in self.blocks:
                block["build"]["elf"] = digest
            sp.write_outputs(run, self.blocks, None, manifest)
            self.assertFalse(any("ELF" in n for n in manifest["notes"]))
            self.blocks[0]["build"]["elf"] = "b" * 64
            sp.write_outputs(run, self.blocks, None, manifest)
            self.assertTrue(any("ELF SHA-256" in n for n in manifest["notes"]))

    def test_each_block_identity_controls_exported_validity(self):
        import csv
        import tempfile
        manifest = json.loads((pathlib.Path(__file__).parents[1] / "templates" / "manifest.json").read_text())
        manifest["elf_sha256"] = "a" * 64
        for block in self.blocks:
            block["build"]["elf"] = "a" * 64
        # Later block from another firmware, despite the startup and first block matching.
        self.blocks[6]["build"]["elf"] = "b" * 64
        with tempfile.TemporaryDirectory() as d:
            run = pathlib.Path(d)
            sp.write_outputs(run, self.blocks, {"elf_sha256": "a" * 64}, manifest)
            exported = [json.loads(line) for line in (run / "samples.jsonl").read_text().splitlines()]
            with (run / "summary.csv").open() as f:
                csv_rows = list(csv.DictReader(f))
            self.assertTrue(exported[0]["valid"])
            self.assertFalse(exported[6]["valid"])
            self.assertIn("elf_identity_mismatch", exported[6]["invalid_reasons"])
            self.assertEqual(csv_rows[6]["valid"], "no")
            del self.blocks[0]["build"]["elf"]
            sp.write_outputs(run, self.blocks, None, manifest)
            self.assertFalse(self.blocks[0]["valid"])
            self.assertIn("elf_identity_missing_or_incomplete", self.blocks[0]["invalid_reasons"])

    def test_individual_memory_regions_are_required(self):
        for region in ("SRAM", "PSRAM"):
            with self.subTest(region=region):
                blocks, _ = sp.parse_log(FIXTURE.read_text())
                del blocks[0]["mem"][region]
                b = sp.evaluate(blocks[0], "Active")
                self.assertFalse(b["valid"])
                self.assertIn(f"missing_mem_{region}", b["invalid_reasons"])

    def test_unknown_or_missing_power_is_rejected(self):
        for power in (None, "N/A", "Dimmed"):
            with self.subTest(power=power):
                blocks, _ = sp.parse_log(FIXTURE.read_text())
                blocks[0]["ctx"]["power"] = power
                self.assertFalse(sp.evaluate(blocks[0], "Active")["valid"])

    def test_cli_requires_identity_and_can_hash_elf(self):
        import hashlib
        import subprocess
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            run = pathlib.Path(d)
            manifest = json.loads((pathlib.Path(__file__).parents[1] / "templates" / "manifest.json").read_text())
            (run / "manifest.json").write_text(json.dumps(manifest))
            (run / "raw.log").write_text(FIXTURE.read_text())
            command = [sys.executable, str(pathlib.Path(sp.__file__)), str(run)]
            missing = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(missing.returncode, 0)
            self.assertIn("firmware identity must be checked", missing.stderr)
            self.assertFalse((run / "summary.csv").exists())
            elf = run / "fake.elf"
            elf.write_bytes(b"synthetic fixture only")
            digest = hashlib.sha256(elf.read_bytes()).hexdigest()
            text = FIXTURE.read_text().replace(self.enabled["elf_sha256"], digest)
            text = text.replace("elf=0123456789abcdef ", "elf=" + digest + " ")
            (run / "raw.log").write_text(text)
            accepted = subprocess.run(command + ["--elf", str(elf)], capture_output=True, text=True)
            self.assertEqual(accepted.returncode, 0, accepted.stderr)
            samples = [json.loads(line) for line in (run / "samples.jsonl").read_text().splitlines()]
            self.assertTrue(samples[0]["valid"])
            elf.write_bytes(b"different firmware")
            mismatch = subprocess.run(command + ["--elf", str(elf)], capture_output=True, text=True)
            self.assertNotEqual(mismatch.returncode, 0)
            self.assertIn("differs from manifest", mismatch.stderr)

    def test_memory_bytes_and_dma(self):
        b = self.blocks[0]
        self.assertEqual(b["mem"]["SRAM"]["min_boot"], 89456)
        self.assertEqual(b["mem"]["PSRAM"]["largest"], 8000000)
        self.assertEqual(b["dma"]["largest"], 65536)

    def test_warning_is_attached_to_its_block(self):
        self.assertEqual(self.blocks[0]["warnings"], [])
        self.assertTrue(any("core0 busy" in w for w in self.blocks[1]["warnings"]))

    def test_failed_window_is_not_zero(self):
        b = self.blocks[2]
        self.assertFalse(b["valid"])
        self.assertIsNone(b["recomputed"]["core0_busy_pct"])
        self.assertIn("sample=SAMPLE_ERROR", b["invalid_reasons"])
        self.assertEqual(b["tasks"], [])

    def test_power_change_is_flagged(self):
        b = self.blocks[3]
        self.assertFalse(b["valid"])
        self.assertIn("power_changed_in_window", b["invalid_reasons"])

    def test_screen_round_trip_is_excluded_even_when_ends_match(self):
        b = self.blocks[4]
        self.assertEqual(b["ctx"]["screen"], "Home")
        self.assertNotIn("screen_start", b["ctx"])
        self.assertFalse(b["valid"])
        self.assertIn("screen_changed_in_window", b["invalid_reasons"])

    def test_agent_change_between_window_ends_is_excluded(self):
        b = self.blocks[5]
        self.assertFalse(b["valid"])
        self.assertIn("agent_changed_in_window", b["invalid_reasons"])

    def test_steady_windows_stay_valid(self):
        self.assertTrue(self.blocks[0]["valid"])
        self.assertTrue(self.blocks[6]["valid"])
        self.assertEqual(self.blocks[6]["invalid_reasons"], [])

    def test_log_without_mixed_marker_is_not_qualified(self):
        text = "\n".join(l for l in FIXTURE.read_text().splitlines() if " ctx " not in l)
        blocks, _ = sp.parse_log(text)
        b = sp.evaluate(blocks[0], "Active")
        self.assertFalse(b["valid"])
        self.assertIn("no_mixed_marker", b["invalid_reasons"])

    def test_summary_leaves_unknown_cells_empty(self):
        import csv
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            run = pathlib.Path(d)
            manifest = json.loads((pathlib.Path(__file__).parents[1] / "templates" / "manifest.json").read_text())
            manifest["run_id"] = "t"
            (run / "manifest.json").write_text(json.dumps(manifest))
            (run / "raw.log").write_text(FIXTURE.read_text())
            sp.write_outputs(run, self.blocks, self.enabled, manifest)
            with open(run / "summary.csv") as f:
                rows = list(csv.DictReader(f))
            self.assertEqual(len(rows), 7)
            self.assertEqual(rows[2]["cpu0_busy_pct"], "")
            self.assertEqual(rows[2]["valid"], "no")
            self.assertEqual(rows[0]["cpu0_busy_pct"], "31.52")
            self.assertEqual(rows[0]["dma_largest_bytes"], "65536")
            self.assertTrue((run / "SHA256SUMS").exists())
            self.assertEqual(json.loads((run / "manifest.json").read_text())["probe"]["storage_psram_bytes"], 8808)


if __name__ == "__main__":
    unittest.main()
