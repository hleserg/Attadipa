#!/usr/bin/env python3
"""Verify archived bytes and recompute raw observations; does not access hardware."""
from pathlib import Path
import hashlib
import json
import math
import re
import statistics

here = Path(__file__).resolve().parent
manifest = json.loads((here / "SHA256.json").read_text())
for name, expected in manifest.items():
    actual = hashlib.sha256((here / name).read_bytes()).hexdigest()
    assert actual == expected, f"Hash mismatch: {name}"

text = (here / "console.txt").read_text()
expected = json.loads((here / "ANALYSIS.json").read_text())
rows = re.findall(r"AKRAW,(\d+),(-?\d+),(-?\d+),(-?\d+),([0-9a-fA-F]{2}),([0-9a-fA-F]{2})", text)
assert len(rows) == text.count("AKRAW,") == expected["parsed_samples"]
assert expected["identity_line"] in text
samples = [tuple(int(v) for v in row[:4]) for row in rows]
times = [row[0] for row in samples]
intervals = [b - a for a, b in zip(times, times[1:])]
assert all(dt > 0 for dt in intervals) == expected["timestamps_strictly_increasing"]
actual_intervals = {"min": min(intervals), "max": max(intervals), "mean": statistics.mean(intervals)}
for key, value in actual_intervals.items():
    assert math.isclose(value, expected["interval_us"][key], rel_tol=1e-12), key
for index, recorded in enumerate(expected["raw_axis_stats"], start=1):
    axis = [row[index] for row in samples]
    actual = {"min": min(axis), "max": max(axis), "mean": statistics.mean(axis),
              "population_stdev": statistics.pstdev(axis)}
    for key, value in actual.items():
        assert math.isclose(value, recorded[key], rel_tol=1e-12), (recorded["axis"], key)
status = sorted({(int(row[4], 16), int(row[5], 16)) for row in rows})
assert status == [tuple(pair) for pair in expected["status_pairs"]]
summary_lines = [line for line in text.splitlines() if "AK09911 summary " in line]
assert len(summary_lines) == 1
summary = dict(re.findall(r"(\w+)=(\w+)", summary_lines[0]))
for key, value in expected["summary"].items():
    assert summary[key] == str(value), key
assert int(summary["samples"]) == len(rows)
boot = (here / "NORMAL_BOOT.txt").read_text()
assert "USB_UART_CHIP_RESET" in boot and "SPI_FAST_FLASH_BOOT" in boot
assert "UI ready:" in boot
print(f"Offline evidence verified: {len(rows)} raw samples; timestamps, XYZ statistics, status and cleanup match.")
print("No new hardware execution; the recorded run's scope and limits remain unchanged.")
