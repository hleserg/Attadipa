# S17 USB-power capture reanalysis — 2026-09-09

This report is software analysis of the existing private S17 capture, executed
on 2026-09-09. It is not a new physical measurement. Hardware re-capture is
**NOT EXECUTED — HARDWARE REQUIRED**. The canonical interpretation remains in
[Verified Facts](VERIFIED_FACTS.md#the-t-watchs-usb-input-carries-779-mw-and-an-unknown-share-of-that-is-charge-current).

## Inputs and limits

The scripts check both full SHA-256 hashes before analysis. The CSV and pinned
logger remain in the owner's private `~/attadipa-bench/` directory; neither is
included here. Only standard-library scripts and aggregate results are published.
The logger's upstream commit and original invocation's CRC flag are UNKNOWN.
Its `--crc` default is False; the CSV has no original report/checksum bytes.

Grouping starts at slot 0 after asserting continuous 0,1,2,3 ordering. There are
242847 rows, 60711 complete four-slot groups and one final three-slot group.
All 293 exclusions occupy different groups. No current-only exclusion shares a
group with a zero/high-binary exclusion. The legacy output key `structural`
below names only that narrow decoded pattern group, not a proven fault class.

This rejects the prediction of excluded samples clustering by whole report.
It does not rule out one-field transport errors, meter/decoder behavior or a
short physical disturbance. An independent voltage trace and original reports
are absent; the source of anomalies and the physical peak remain UNKNOWN.

## Executed aggregate results

The unchanged heuristic retains `4.0 < V < 5.5` and `0 <= I < 1.0`.
Counts are 242554 retained and 293 excluded: 118 zero-voltage, 35 high-binary,
72 current-only, 60 at 0.00187 V and eight at 0.07262–0.08837 V.
The last two groups carry 0.15065–0.19241 A and 0.14982–0.18332 A respectively.
All 68 low-voltage/current pairs warrant investigation; 153 matching the
zero/high-binary rule does not prove that only those 153 are anomalous.

For retained voltage counts, `(raw + 13) % 25` takes both 0 and 13. These two
residue classes do not establish a single 0.25 mV grid or an ADC resolution.

| Decoded set | Count | Mean mW | Median mW | p99 mW | Maximum mW |
| --- | ---: | ---: | ---: | ---: | ---: |
| Retained | 242554 | 778.9421 | 754.6752 | 949.0924 | 986.9061 |
| Retained + current-only | 242626 | 780.5616 | 754.6755 | 949.3524 | 6337.0790 |
| All rows | 242847 | 780.6784 | 754.6698 | 949.3853 | 12448.5385 |

Power is per-sample V × I. p99 interpolates at zero-based rank 0.99 × (n − 1).
The all-row mean differs from the retained mean by 1.7362748638 mW,
0.2229016547% of the retained mean. Extra decimals expose arithmetic, not meter
accuracy. No decoded maximum is a verified electrical peak.

## Reproduce grouping and mean sensitivity

Save this first block as `/tmp/attadipa-s17-recheck.py` and execute it with
`python3 /tmp/attadipa-s17-recheck.py`. All assertions passed with exit status 0.
The two later blocks reuse this file so that they repeat its input hash checks.

```python
from collections import Counter
from decimal import Decimal
from hashlib import sha256
import json
from pathlib import Path
from statistics import mean


bench = Path.home() / "attadipa-bench"
expected = {
    "twatch_taper_20260908.csv": "0062e49452b5e647c0b236a9260d74362a6c817309da91b45e9124373b9b4dac",
    "fnirsi_logger.py": "388061aeb580cde0b7306626d87f833dfdbbf1fdf6c17663b49fde557ace250b",
}
for name, digest in expected.items():
    assert sha256((bench / name).read_bytes()).hexdigest() == digest, name

reports = []
classes = Counter()
all_power, kept_power = [], []
previous_slot = 3
rows = 0
with (bench / "twatch_taper_20260908.csv").open() as capture:
    for line in capture:
        fields = line.split()
        if not fields or fields[0] == "timestamp":
            continue
        assert len(fields) == 9
        slot = int(fields[1])
        assert slot == (previous_slot + 1) % 4, (rows, previous_slot, slot)
        assert slot == rows % 4
        previous_slot = slot
        if slot == 0:
            reports.append([])
        voltage, current = map(Decimal, fields[2:4])
        retained = 4 < voltage < Decimal("5.5") and 0 <= current < 1
        power = voltage * current * 1000
        all_power.append(power)
        if retained:
            kept_power.append(power)
            kind = "retained"
        else:
            corrected = int(voltage * 100000) + 13
            binary_high = corrected % 100 == 0 and (corrected // 100) % 2048 == 0
            if voltage == 0:
                kind = "zero_voltage"
            elif binary_high:
                kind = "binary_high"
            elif 4 < voltage < Decimal("5.5"):
                kind = "current_only"
            elif voltage == Decimal("0.00187"):
                kind = "low_2mV"
            else:
                kind = "other_low"
            classes[kind] += 1
        reports[-1].append(kind)
        rows += 1

excluded_per_report = Counter(sum(k != "retained" for k in report) for report in reports)
overlap = sum(
    report.count("current_only")
    for report in reports
    if any(k in ("zero_voltage", "binary_high") for k in report)
)
assert rows == 242847 and len(kept_power) == 242554
assert Counter(map(len, reports)) == {4: 60711, 3: 1}
assert classes == {"zero_voltage": 118, "binary_high": 35, "current_only": 72, "low_2mV": 60, "other_low": 8}
assert excluded_per_report == {0: 60419, 1: 293}
assert overlap == 0
all_mean, kept_mean = mean(all_power), mean(kept_power)
print(json.dumps({
    "verified_sha256": expected,
    "rows": rows,
    "reports_reconstructed_from_contiguous_slots": len(reports),
    "report_size_histogram": dict(Counter(map(len, reports))),
    "excluded_classes": dict(classes),
    "excluded_samples_per_report_histogram": dict(excluded_per_report),
    "current_only_samples_sharing_report_with_structural_exclusion": overlap,
    "all_mean_mW": str(all_mean),
    "retained_mean_mW": str(kept_mean),
    "mean_change_mW": str(all_mean - kept_mean),
    "mean_change_percent_of_retained": str((all_mean - kept_mean) / kept_mean * 100),
}, indent=2))
```

## Reproduce the three decoded distributions

Execute this block as a Python script after saving the first block. It completed
with exit status 0 and printed the table's values before rounding.

```python
from contextlib import redirect_stdout
from io import StringIO
from decimal import Decimal
from statistics import mean, median
import json, runpy
with redirect_stdout(StringIO()):
    data = runpy.run_path("/tmp/attadipa-s17-recheck.py")
classes = [kind for report in data["reports"] for kind in report]
all_power = data["all_power"]
assert len(classes) == len(all_power)
retained = data["kept_power"]
with_current = [power for power, kind in zip(all_power, classes) if kind in ("retained", "current_only")]
assert len(with_current) == len(retained) + 72

def describe(values):
    ordered = sorted(values)
    rank = Decimal(len(ordered) - 1) * Decimal("0.99")
    low = int(rank)
    p99 = ordered[low] + (ordered[low + 1] - ordered[low]) * (rank - low)
    return {"count": len(values), "mean_mW": str(mean(values)), "median_mW": str(median(values)), "p99_linear_mW": str(p99), "max_decoded_mW": str(max(values))}
print(json.dumps({name: describe(values) for name, values in (("all", all_power), ("retained_plus_current_only", with_current), ("retained", retained))}, indent=2))
```

## Reproduce low-voltage currents and residue classes

This block also completed with exit status 0; its assertions check the exact
counts, current ranges and two residue classes stated above.

```python
from contextlib import redirect_stdout
from io import StringIO
from decimal import Decimal
import json, runpy
with redirect_stdout(StringIO()):
    data = runpy.run_path("/tmp/attadipa-s17-recheck.py")
low_2mV, other_low, residues = [], [], set()
with (data["bench"] / "twatch_taper_20260908.csv").open() as capture:
    for line in capture:
        fields = line.split()
        if not fields or fields[0] == "timestamp":
            continue
        voltage, current = map(Decimal, fields[2:4])
        if 4 < voltage < Decimal("5.5") and 0 <= current < 1:
            residues.add((int(voltage * 100000) + 13) % 25)
        elif voltage == Decimal("0.00187"):
            low_2mV.append(current)
        elif Decimal("0.07262") <= voltage <= Decimal("0.08837"):
            other_low.append(current)
assert len(low_2mV) == 60 and len(other_low) == 8
assert (min(low_2mV), max(low_2mV)) == (Decimal("0.15065"), Decimal("0.19241"))
assert (min(other_low), max(other_low)) == (Decimal("0.14982"), Decimal("0.18332"))
assert residues == {0, 13}
print(json.dumps({
    "low_2mV": {"count": len(low_2mV), "min_current_A": str(min(low_2mV)), "max_current_A": str(max(low_2mV))},
    "other_low": {"count": len(other_low), "min_current_A": str(min(other_low)), "max_current_A": str(max(other_low))},
    "retained_raw_plus_13_mod_25": sorted(residues),
}, indent=2))
```
