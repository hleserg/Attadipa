"""Reproduce structural and descriptive checks of a paired physical transcript."""
import collections
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys

run = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent
for name, expected in json.loads((run / 'SHA256.json').read_text()).items():
    data = (run / name).read_bytes()
    assert len(data) == expected['bytes']
    assert hashlib.sha256(data).hexdigest() == expected['sha256'], name
raw = (run / 'console.txt').read_bytes()
lines = raw.decode('utf-8', errors='strict').splitlines()
ak, batches, samples = [], [], collections.defaultdict(list)
for line in lines:
    m = re.search(r'i2c-probe: (AKRAW|QMIBATCH|QMIRAW),(.+)$', line)
    if not m:
        continue
    kind, payload = m.groups()
    cells = payload.split(',')
    if kind == 'AKRAW':
        assert len(cells) == 6
        ak.append([int(x, 16 if i >= 4 else 10) for i, x in enumerate(cells)])
    elif kind == 'QMIBATCH':
        assert len(cells) == 6
        batches.append([int(x, 16 if i == 5 else 10) for i, x in enumerate(cells)])
    else:
        assert len(cells) == 5
        n, idx, *xyz = map(int, cells)
        assert idx == len(samples[n]), (n, idx)
        samples[n].append(xyz)

assert ak and batches, 'Both streams required'
assert [b[0] for b in batches] == list(range(batches[0][0], batches[0][0] + len(batches)))
assert set(samples) == {b[0] for b in batches}
for n, requested, frozen, released, count, status in batches:
    assert requested <= frozen <= released
    assert len(samples[n]) == count
    assert status & 0x20 == 0, 'FIFO overflow bit set'
assert all(a[0] < b[0] for a, b in zip(ak, ak[1:]))
assert all(a[3] < b[1] for a, b in zip(batches, batches[1:]))
assert all(row[4] & 1 and not row[4] & 2 and not row[5] & 8 for row in ak)

def stats(values):
    return {'min': min(values), 'max': max(values), 'mean': statistics.mean(values),
            'population_sd': statistics.pstdev(values)}

qxyz = [xyz for batch in batches for xyz in samples[batch[0]]]
summary = [line for line in lines if 'summary' in line or 'QMI before' in line or 'QMI after' in line or 'QMI start' in line or 'AK09911 ID' in line or 'QMI stop_check' in line or 'QMI stop_final' in line]
qsum = next(line for line in summary if 'QMI summary' in line)
assert re.search(rf'samples={len(qxyz)}\b.*batches={len(batches)}\b', qsum), qsum
cleanup_ok = 'stop=0' in qsum and 'close=ESP_OK' in qsum
boot = (run / 'NORMAL_BOOT.txt').read_bytes()
# No default. The three Waveshare archives in this directory restored three
# different production images -- d5e09572c, 2915714b7 and 4997786e8 -- so a
# literal baked in here checks a future archive against an unrelated run and
# reports the coincidence as a result. An archive that does not record what it
# expected cannot have that checked; say so rather than invent the expectation.
expected_elf = json.loads((run / 'LOAD_RESULT.json').read_text()).get(
    'expected_ordinary_elf_prefix')
result = {
    'classification': 'MEASURED physical transcript; pose, accuracy and calibration UNKNOWN',
    'console_sha256': hashlib.sha256(raw).hexdigest(),
    'structural_checks': 'PASS: stream syntax/counts/order/status only',
    'qmi_cleanup_check': 'PASS' if cleanup_ok else 'FAIL — actual stop/close result below',
    'ak_samples': len(ak),
    'ak_host_timestamp_span_us': ak[-1][0] - ak[0][0],
    'ak_host_interval_us': stats([b[0] - a[0] for a, b in zip(ak, ak[1:])]),
    'ak_raw_axes': {axis: stats([r[i+1] for r in ak]) for i, axis in enumerate('xyz')},
    'qmi_samples': len(qxyz),
    'qmi_batches': len(batches),
    'qmi_batch_size_histogram': dict(collections.Counter(b[4] for b in batches)),
    'qmi_raw_axes': {axis: stats([r[i] for r in qxyz]) for i, axis in enumerate('xyz')},
    'qmi_host_request_to_release_us': stats([b[3] - b[1] for b in batches]),
    'qmi_host_request_to_freeze_confirm_us': stats([b[2] - b[1] for b in batches]),
    'qmi_host_freeze_confirm_to_release_us': stats([b[3] - b[2] for b in batches]),
    'qmi_host_first_request_to_last_release_us': batches[-1][3] - batches[0][1],
    'qmi_host_timestamp_caveat': 'Host transaction boundaries, not conversion timestamps or exact device frozen durations; dropped conversion count UNKNOWN.',
    'source_summary_lines': summary,
    'normal_boot_spi': b'SPI_FAST_FLASH_BOOT' in boot,
    'normal_boot_expected_elf': (expected_elf.encode('ascii') in boot) if expected_elf
        else 'UNKNOWN — this archive records no expected_ordinary_elf_prefix; '
             'LOAD_RESULT.json asserts expected_production_elf_seen separately, '
             'and this verifier does not reproduce that assertion',
    'normal_boot_relevant_lines': [line for line in boot.decode('utf-8', errors='replace').splitlines() if any(k in line for k in ('SPI_FAST_FLASH_BOOT', 'ELF file SHA256', 'UI ready', 'ui ready', 'watch-control'))],
}
# Not in SHA256.json: this line rewrites the file, so pinning its hash in the
# manifest asserted above would make the documented reproduce command report the
# archive as corrupt after any change to what the summary filter collects.
(run / 'ANALYSIS.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
