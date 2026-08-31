# Raw bench output, 2026-08-28

The five console captures behind
[PEDOMETER_BENCH_2026-08-28](../PEDOMETER_BENCH_2026-08-28.md) and source key
**S15** in [HARDWARE_MATRIX](../HARDWARE_MATRIX.md). They are the evidence for a
`MEASURED` result, so they are committed unedited — every line number the report
cites resolves here.

| File | Run | What it is |
| --- | --- | --- |
| `pedo-run.log` | 12:58 | desk, board stationary, 240 windows |
| `pedo-run2.log` | 13:04 | desk, board stationary, 240 windows |
| `pedo-run3.log` | 13:20 | desk, board stationary, 900 windows |
| `walk.log` | 14:39 | walk attempt; ends in `SerialException` at the unplug |
| `shake.log` | 15:08 | **the result** — 159 windows, hand-moved, step count 0 |

Read them with the report's *"A caveat about the raw logs"* section beside you:
five `printf` labels in these captures had gone stale — four in `shake.log`,
one in `walk.log` — and where a header and the report disagree, the report is
correct. No register or parameter value
is affected.

Two of the five captures open with an `esptool` **RAM boot**: `shake.log:1-11`
and `walk.log:1-11` carry the loader transcript, every segment loading to a RAM
address, with no `write_flash` anywhere. The three `pedo-run` captures open at
the image's own boot log instead, with no `# port`, `RAM boot` or `Downloading`
line. That is a missing record of the loader step, not a different route: all
five carry `Project name: pedo` at the identical `compile time Aug 28 2026
12:53:17`, and `probe/sdkconfig.defaults:2-3` builds `PURE_RAM_APP`, which has
no flash image to boot from. Nothing in the session writes flash; the unit still
carries the T-166 candidate there.

The QMI8658 datasheet these were read against is not here: it is QST's copyright
and its cover marks it *Security Level: 3*. The report names it and where it came
from.

## The probe

`probe/` is the source that produced these captures — `pedo.c`, its two
`CMakeLists.txt` (the component's, and the top-level one as
`CMakeLists-top.txt`) and `sdkconfig.defaults`. ESP-IDF v5.5.5, target
`esp32s3`, built out of tree and RAM-booted with `tools/flash/ramhold.py`. It is
here as **evidence**, not as a component: nothing in `firmware/` or `platform/`
builds it or depends on it.

**It is the probe as corrected after the session, not byte-for-byte what ran.**
Five things changed afterwards, and four of them are behaviour rather than
text — this source does not reproduce the archived captures line for line:

1. **behaviour** — the `OVER` column's bar and the printed threshold now come
   from one macro, so they cannot disagree. In the captures the column compares
   against a hard-coded 200 mg while the engine was configured with 80, so the
   archived `OVER` column is not what this source would print;
2. **behaviour** — the `--- before ---` block now reads and prints the
   accumulated step count **before** anything is written, which is what makes a
   read-after-walk possible at all. No archived capture carries that line;
3. **behaviour** — `CTRL3` is now read before the writes and written back
   afterwards. The shake run did neither, which is why
   [`HARDWARE_MATRIX.md:514`](../HARDWARE_MATRIX.md) records `CTRL3 = 0x36` as
   *"residue this session knowingly left on the part"*. That remains the correct
   statement **about the run**; it is no longer what this source does;
4. **behaviour** — the parameter header now echoes `ACCEL_LSB_PER_G`. No
   archived capture records the divisor its binary used to turn LSB into the
   `p2p` milligravity column, and none of them can be made to: the scale of
   every mg figure the report publishes is therefore UNKNOWN. This line is so
   that the next capture settles it for itself;
5. **text** — five `printf` labels are corrected in this source. Four are the
   stale `shake.log` labels named above: `CTRL2 = 0x16` is ±4 g at 112.1 Hz in
   6DOF; `CTRL3 = 0x36` is ±128 dps, not the ±1000 dps printed here; the
   banner's register list omitted `CTRL3`; and the p2p header printed the
   datasheet's 200/100 mg while the engine held SensorLib's loosened 80/60. The
   fifth is not a configuration label and so is not among that five: the
   `REVISION` line printed `0x7C = QMI8658A 13-52-25 Rev A` as settled fact,
   where which document number names the Rev A part is disputed and deferred
   to #341.

**No register or parameter constant differs from the run.** That is checkable
without trusting this note: `shake.log:48-59` echoes every value the probe wrote
— `CTRL2 = 0x16`, `CTRL3 = 0x36`, `CTRL8` `0x80` then `0x90`, `sample_cnt=50`,
`peak2peak=0x0050`, `peak=0x003C`, `time_up=400`, `time_low=8`, `cnt_entry=1`,
`precision=0`, `sig_count=1` — and each matches the `#define` in `probe/pedo.c`.
