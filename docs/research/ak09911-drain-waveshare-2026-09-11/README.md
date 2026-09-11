# The FIFO a previous run left, and the first stationary AK09911 numbers — 2026-09-11

Waveshare ESP32-S3-Touch-AMOLED-2.06, MAC `28:84:85:b2:18:a4`, USB-powered with
no battery, lying untouched on the bench for every run below. Every image was a
`PURE_RAM_APP` loaded through `tools/flash/ramhold.py`; no flash write command
was issued at any point, and the production image came back with a hard reset.

## Provenance — three images, not one

An earlier version of this line named one source commit and one binary for all
six captures. That was wrong, and the console files say so themselves: the
loader prints every segment it downloads, and the three images differ there.
The corrected record is per capture.

| capture | image | segments, bytes | binary | source |
|---|---|---|---|---|
| `console.txt` | `build-drain`, second build | 63896 / 464 / 125968 / 32 | 190464, SHA-256 `09654987a4fbc40535bf5776d57fd73df4930a2ff8fa2e3c21b1380cc6fe6d17` | `b4a8f45b2e12691e69609a5d39d82c128b1ae291` (#515) |
| `console-bypass-attempt.txt` | `build-drain`, first build | 63880 / 464 / 125908 / 32 | **not retained** — the directory was rebuilt over it 31 minutes later | **UNKNOWN** — an uncommitted working tree between `488ac295` and `b4a8f45b` |
| `console-ak-stationary-A.txt`, `-B`, `-C` | `build-akonly` | 62744 / 464 / 122512 / 32 | 185856, SHA-256 `4a32336778843cd3be6e57f3bc4a65db58222219099e8fba4902269bac8cd314` | the working tree later committed as `b4a8f45b`, `CONFIG_ATTADIPA_AK09911_PROBE` alone |
| `NORMAL_BOOT.txt` | the unit's own flash image | — | — | not built here |

`sdkconfig` SHA-256: `build-drain`
`78f28143efa1f788cba52e68cdb02eff0520f3a5c2f28ad7dc7a4e62cb8928a7`, `build-akonly`
`e90aecf70b420ac6f99a22039bc2010889abb4b387d707c59cd9f3226dd8cf8e`. The binaries
and configs live in the private build directories and are identified here by
hash rather than committed, the way the two sibling archives do it.

**The refused run cannot be reproduced from a commit, and this record says so
rather than naming the nearest one.** Its image is gone and its tree was never
committed: the fix and the AK09911 change landed together in `b4a8f45b`, and
that image had neither. `console-bypass-attempt.txt:69` — "AK09911 summary duration_us=39799 samples=0"
is a 39.8 **millisecond** window, which the committed head cannot produce —
`firmware/main/i2c_probe.cpp:146` — "  constexpr std::int64_t duration_us = 40000000;"
— so the refused run predates it. What the capture is evidence of is the FIFO,
and that evidence does not depend on which tree built it.

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
  This is the measurement, not an inference from the datasheet: the drain
  entering a FIFO mode first succeeded on the same board 11 minutes later, with
  the same three words present and the same count to clear. The two images are
  not otherwise identical and this is not a one-write A/B — they differ by 60
  bytes of instruction segment, and the second also carries the change that
  lets the AK09911 outlive a refusal, which is why only one of them has a
  40-second magnetometer window. What is held constant across the pair is the
  board, the three words and the request; what changed about the request is the
  mode it was issued from. What those words *are* stays UNKNOWN: `00 80` is a
  plausible sample and also what `0x17` returned in the refused attempt, where
  the request moved nothing. The count is the measured thing here, not the
  payload.
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

**NOT EXECUTED — HARDWARE REQUIRED: nothing here re-ran on the current head.**
Every capture in this archive belongs to the image named for it in the table
above. `b4a8f45b` is not the head of #515: `1df9fcfb` moved the CTRL8 handshake
write ahead of the drain's own CTRL9 command, and the drain now re-reads the
frozen count after `REQ_FIFO` rather than sizing the payload from the entry
snapshot (`17337383` touched only the host test). Both changes are covered by
the host transport model and by no bench run. The 40-second paired capture has
not been re-run on the head yet; the readings below are not restated as its
result.

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

**The same sensor on the same board read 68.6 counts two days earlier.** The
2026-09-09 paired capture in [the first archive](../qmi-paired-waveshare-2026-09-09/README.md)
reports mean raw axes 46.04, -12.91, -49.19 from the same part — `ID=48 05`,
`ASA=17 17 13`, `fuse_mode=00` — which is `|B|` = 68.6 counts, near 41 µT at the
same nominal scale and entirely plausible for the geomagnetic field alone.
Between then and now `|B|` went to 277.9 while X barely moved: 46.04 to 44.85,
against Y from -12.91 to +66.33 and Z from -49.19 to -266.16.

A rotation preserves the magnitude, so this is not the watch having been turned.
Some field roughly in the sensor's YZ plane is present now and was not present
then, and 278 counts is far from the part's saturation, so it is a real reading
rather than a clipped one. **What produces it is UNKNOWN** and cannot be settled
without moving things on the bench, which nothing here did. Candidates that were
not on the bench in the same arrangement on 2026-09-09 include the vibromotors,
which contain permanent magnets, and the other powered boards.

This retires the hard-iron reading of today's magnitude rather than supporting
it. A hard-iron offset is fixed to the body that carries the sensor and does not
change between sessions with the body untouched; this did. Any calibration
computed from today's field would bake in whatever changed. USB delivered the
board's current throughout both sessions, so that is not the difference either.

## What this does not establish

Pose, axis mapping and the direction of any axis relative to the watch body are
UNKNOWN, and so is the source of the field that changed between 2026-09-09 and
2026-09-11. No hard-iron or soft-iron calibration was computed, no reference
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
