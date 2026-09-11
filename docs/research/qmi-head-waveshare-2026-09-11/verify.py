#!/usr/bin/env python3
"""Re-check this archive against its own bytes.

Everything asserted in README.md that a machine can check is checked here, so a
later edit to the prose that the streams do not support fails loudly. Run it
from anywhere: `python3 docs/research/qmi-head-waveshare-2026-09-11/verify.py`.
"""
import hashlib
import json
import pathlib
import re
import statistics

run = pathlib.Path(__file__).resolve().parent

manifest = json.loads((run / 'SHA256.json').read_text())
for name, want in manifest.items():
    raw = (run / name).read_bytes()
    assert len(raw) == want['bytes'], (name, len(raw), want['bytes'])
    assert hashlib.sha256(raw).hexdigest() == want['sha256'], name

runs = {}
for name in ('console-head-A.txt', 'console-head-B.txt'):
    runs[name] = (run / name).read_text(errors='strict')


def one(text, pattern, path):
    found = re.findall(pattern, text)
    assert len(found) == 1, (path, pattern, len(found))
    return found[0]


for name, text in runs.items():
    # Entry: the FIFO was empty, so the drain had nothing to do and never ran.
    # This is the claim the README rests on and the reason the drain path is
    # still NOT EXECUTED on hardware.
    assert 'QMI before CTRL1=20' in text, name
    assert one(text, r'QMI start=(\d+) ', name) == '0', name
    assert 'QMI drained' not in text, name
    assert 'QMISTALE' not in text, name

    # Acquisition: both streams, full duration, no error of any kind.
    qmi = one(text, r'QMI summary samples=(\d+) ', name)
    assert 3700 < int(qmi) < 3900, (name, qmi)
    assert re.search(r'QMI summary .* result=0 stop=4 ', text), name
    ak = re.search(
        r'AK09911 summary duration_us=(\d+) samples=(\d+) not_ready=\d+ '
        r'overflow=(\d+) invalid=(\d+) dor=(\d+) result=(\d+)', text)
    assert ak, name
    assert 40_000_000 <= int(ak.group(1)) < 40_100_000, (name, ak.group(1))
    assert int(ak.group(2)) == 398, (name, ak.group(2))
    assert ak.group(3) == ak.group(4) == ak.group(5) == '0', name
    assert ak.group(6) == '0', name

    # The stop defect, unchanged and still UNKNOWN.
    assert 'QMI stop_check step=count_after_reset remaining_valid=1 ' \
           'remaining_words=3 remaining_status=50' in text, name

    # Stationary magnetometer, raw counts. The README quotes these.
    rows = re.findall(
        r'AKRAW,\d+,(-?\d+),(-?\d+),(-?\d+),([0-9a-f]{2}),([0-9a-f]{2})', text)
    assert len(rows) == 398, (name, len(rows))
    assert {r[3] for r in rows} == {'01'}, name
    assert {r[4] for r in rows} == {'00'}, name
    axes = [[int(r[i]) for r in rows] for i in range(3)]
    means = [statistics.mean(a) for a in axes]
    assert 44.0 < means[0] < 46.0, (name, means)
    assert 65.0 < means[1] < 68.0, (name, means)
    assert -268.0 < means[2] < -265.0, (name, means)
    for a in axes:
        assert statistics.pstdev(a) < 1.5, (name, statistics.pstdev(a))
    mag = [(x * x + y * y + z * z) ** 0.5 for x, y, z in zip(*axes)]
    assert 277.0 < statistics.mean(mag) < 280.0, (name, statistics.mean(mag))

# The production image came back and stayed up.
boot = (run / 'NORMAL_BOOT.txt').read_text(errors='strict')
assert 'ELF file SHA256:  2915714b7' in boot
assert 'alive 11s' in boot
assert 'Guru Meditation' not in boot and 'panic' not in boot

print('qmi-head-waveshare-2026-09-11: hashes, both runs, two streams and the '
      'restored production boot check out.')
