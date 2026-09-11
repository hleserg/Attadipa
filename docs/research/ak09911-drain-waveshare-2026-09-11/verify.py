"""Reproduce the structural checks of the 2026-09-11 drain and stationary runs.

    python3 docs/research/ak09911-drain-waveshare-2026-09-11/verify.py

Structural assertions passing is not a hardware pass for anything this archive
does not claim: pose, axis mapping and calibration remain UNKNOWN.
"""
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys

run = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
for name, expected in json.loads((run / 'SHA256.json').read_text()).items():
    data = (run / name).read_bytes()
    assert len(data) == expected['bytes'], name
    assert hashlib.sha256(data).hexdigest() == expected['sha256'], name


def ak_rows(path):
    rows = []
    for line in path.read_text(errors='strict').splitlines():
        m = re.search(r'i2c-probe: AKRAW,(.+)$', line)
        if m:
            c = m.group(1).split(',')
            assert len(c) == 6
            rows.append([int(c[1]), int(c[2]), int(c[3]), int(c[4], 16), int(c[5], 16)])
    return rows


def summary(path, pattern):
    m = re.search(pattern, path.read_text(errors='strict'))
    assert m, (path.name, pattern)
    return m


paired = run / 'console.txt'
text = paired.read_text(errors='strict')

# The entry the bypass attempt could not reach. `stale_words=3` is the count
# `start()` saw on entry; this image sized the payload read from it, and whether
# the count the part froze agreed is exactly what the head now re-reads. What
# the three words themselves are is UNKNOWN: `00 80` is both a plausible sample
# and the value 0x17 returned in the refused attempt below, where the request
# had moved nothing at all.
assert 'QMI start=0 ' in text, 'paired entry did not succeed'
assert 'QMI drained stale_words=3 (entry succeeded)' in text
assert text.count('QMISTALE,') == 3
for w in range(3):
    assert f'QMISTALE,{w},00,80' in text, w

# The first attempt: same board, same three words, request issued from bypass.
bypass = (run / 'console-bypass-attempt.txt').read_text(errors='strict')
assert 'QMI start=8 ' in bypass, 'the bypass attempt is supposed to be refused'
assert 'QMI drained stale_words=3 (entry still refused)' in bypass
assert bypass.count('QMISTALE,') == 3
assert re.search(r'QMI summary samples=0 batches=0 result=8 ', bypass)
assert re.search(r'AK09911 summary duration_us=\d+ samples=0 ', bypass), (
    'the refusal is supposed to have silenced the magnetometer; that is the defect')

# Both streams ran for the whole paired window.
m = summary(paired, r'QMI summary samples=(\d+) batches=(\d+) result=(\d+) ')
qmi_samples, qmi_batches, qmi_result = (int(g) for g in m.groups())
assert qmi_result == 0 and qmi_samples == 3814 and qmi_batches == 3114
m = summary(paired, r'AK09911 summary duration_us=(\d+) samples=(\d+)'
                    r' not_ready=(\d+) overflow=(\d+) invalid=(\d+) dor=(\d+) result=(\d+) ')
duration, samples, _not_ready, overflow, invalid, dor, ak_result = (int(g) for g in m.groups())
assert ak_result == 0 and (overflow, invalid, dor) == (0, 0, 0)
assert samples == 398 and 39_000_000 < duration < 41_000_000

# Four cold loads of the same untouched board: three AK-only, one paired.
stationary = sorted(run.glob('console-ak-stationary-*.txt'))
assert len(stationary) == 3
means = []
for path in [paired] + stationary:
    rows = ak_rows(path)
    assert len(rows) == (398 if path == paired else 199), path.name
    assert all(r[3] & 1 and not r[3] & 2 and not r[4] & 8 for r in rows), path.name
    means.append([statistics.fmean(r[i] for r in rows) for i in range(3)])
    for axis in range(3):
        assert statistics.stdev(r[axis] for r in rows) < 1.5, (path.name, axis)
for axis in range(3):
    column = [m[axis] for m in means]
    assert max(column) - min(column) < 1.5, axis  # run-to-run agreement, raw counts

boot = (run / 'NORMAL_BOOT.txt').read_text(errors='strict')
assert 'production image: no USB watch-control endpoint' in boot
assert 'UI ready' in boot

print('ak09911-drain-waveshare-2026-09-11: hashes, both streams, four cold loads'
      ' and the restored production boot check out.')
