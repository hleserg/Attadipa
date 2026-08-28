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
three `printf` labels in these captures had gone stale against the constants they
printed, and where a header and the report disagree, the report is correct. No
register or parameter value is affected.

Each capture opens with an `esptool` **RAM boot** — every segment loads to a RAM
address and there is no `write_flash` in the session. The unit still carries the
T-166 candidate in flash.

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
Three things changed afterwards, all of them in text rather than behaviour:

1. the `OVER` column's bar and the printed threshold now come from one macro, so
   they cannot disagree (in the captures the column compares against 200 mg while
   the engine was configured with 80);
2. the `--- before ---` block now reads and prints the accumulated step count
   **before** anything is written, which is what makes a read-after-walk possible
   at all;
3. two stale `printf` labels are corrected — `CTRL2 = 0x16` is ±4 g at 112.1 Hz
   in 6DOF, and `CTRL3 = 0x36` is ±128 dps, not the ±1000 dps printed here.

**No register or parameter constant differs from the run.** That is checkable
without trusting this note: `shake.log:53-59` echoes every value the probe wrote
— `CTRL2 = 0x16`, `CTRL3 = 0x36`, `CTRL8` `0x80` then `0x90`, `sample_cnt=50`,
`peak2peak=0x0050`, `peak=0x003C`, `time_up=400`, `time_low=8`, `cnt_entry=1`,
`precision=0`, `sig_count=1` — and each matches the `#define` in `probe/pedo.c`.
