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
