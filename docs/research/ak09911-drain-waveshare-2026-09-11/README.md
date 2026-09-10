# The FIFO a previous run left, and the first stationary AK09911 numbers — 2026-09-11

Source: `b4a8f45b2e12691e69609a5d39d82c128b1ae291` (#515). Binary: 190464 bytes,
SHA-256 `09654987a4fbc40535bf5776d57fd73df4930a2ff8fa2e3c21b1380cc6fe6d17`.
Waveshare ESP32-S3-Touch-AMOLED-2.06, MAC `28:84:85:b2:18:a4`, USB-powered with
no battery, lying untouched on the bench for every run below. Every image was a
`PURE_RAM_APP` loaded through `tools/flash/ramhold.py`; no flash write command
was issued at any point, and the production image came back with a hard reset.

## MEASURED — the entry defect and its fix

The board began this session in the state the earlier paired capture left it in:
three words in the QMI8658 FIFO that `stop()` could not reset, `FIFO=00`
(bypass), count 3, status 50.

- **Refused, magnetometer silenced** (`console-bypass-attempt.txt`, drain built
  in and requested): `QMI start=8`, `QMI drained stale_words=3 (entry still
  refused)`, three words of `00 80` — 0x8000 each — and the count still 3
  afterwards. `QMI summary samples=0 batches=0 result=8`.
  `AK09911 summary duration_us=39799 samples=0`: the AK09911 identified
  correctly (`ID=48 05`, `start=0`) and then acquired **nothing**, because the
  paired loop broke out on the QMI result. A working sensor produced no data
  because a different sensor refused entry.
- **Cause of the failed drain**: the request was issued with FIFO_CTRL in
  bypass. `read()` only ever requests a batch from a FIFO mode; the drain did
  not, so the request moved nothing and the payload read returned 0x8000 words.
  This is the measurement, not an inference from the datasheet: the identical
  code with one added `FIFO_CTRL` write succeeded on the same board minutes
  later, with the same three words present.
- **Accepted** (`console.txt`, drain entering a FIFO mode first):
  `QMI start=0`, `QMI drained stale_words=3 (entry succeeded)`, the same three
  `00 80` words recorded, and the count clear. Then **398 AK09911 samples and
  3814 QMI samples in 3114 batches over 40.047958 s**, `result=0` on both,
  `overflow=0 invalid=0 dor=0`, `st1=01 st2=00` on every magnetometer frame.

`stop=4` and `remaining_words=3` at the end of the accepted run: the FIFO still
holds three words when acquisition stops. That is the separate stop-path finding
already recorded in [the stop diagnostic](../qmi-stop-waveshare-2026-09-09/README.md)
and it is not resolved here. What changed is that the state it leaves behind no
longer locks the next run out.

## MEASURED — four cold loads of an untouched board, raw counts

Three AK-only 20-second runs and the 40-second paired run above, each a separate
cold RAM load, the watch never moved between them.

| run | n | mean X | mean Y | mean Z | σ X | σ Y | σ Z | peak-to-peak |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| stationary A | 199 | 44.86 | 66.50 | -266.89 | 1.21 | 1.09 | 1.24 | 6, 6, 6 |
| stationary B | 199 | 44.86 | 66.51 | -266.99 | 1.11 | 1.16 | 1.31 | 6, 7, 7 |
| stationary C | 199 | 45.11 | 67.12 | -267.49 | 1.21 | 1.23 | 1.25 | 6, 7, 8 |
| paired | 398 | 44.85 | 66.33 | -266.16 | 1.24 | 1.27 | 1.28 | 7, 7, 6 |

Run-to-run mean spread is at most 1.33 counts on any axis, and `|B|` lands
between 277.95 and 279.45 counts across all four. The paired run sits inside the
AK-only spread, so a QMI8658 acquiring 95 samples per second on the same bus did
not visibly disturb the magnetometer over this window.

`fuse_mode=00`, so `ASA=17 17 13` is of UNKNOWN validity and these are raw
counts, not adjusted field units. Converting at the part's nominal 0.6 µT/LSB
puts `|B|` near 167 µT — **ESTIMATED**, roughly 3.3× the geomagnetic magnitude.
A field that large and that repeatable is consistent with the retrofit report's
§5 hard-iron hypothesis, but this archive does not establish it: an offset is
identified by rotating the sensor, and nothing here rotated. USB delivered the
board's current throughout, so whatever that contributes is inside these numbers.

## What this does not establish

Pose, axis mapping and the direction of any axis relative to the watch body are
UNKNOWN. No hard-iron or soft-iron calibration was computed, no reference
heading was compared, no tilt compensation exists, and mounting rigidity was not
measured — the owner confirmed the part is fastened, which is not the same fact.
This is not an accuracy or a heading result. Motor and vibration influence stay
after a working calibrated compass, not before it.

## Reproduce the evidence checks

Run `python3 docs/research/ak09911-drain-waveshare-2026-09-11/verify.py` from the
repository root. It checks the six original file hashes, both transcript
structures, the four cold-load agreement and the restored production boot.
Nothing it asserts converts a structural pass into a hardware pass for anything
listed under "What this does not establish".
