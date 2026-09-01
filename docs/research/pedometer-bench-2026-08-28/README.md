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
line. That is a missing record of the loader step, not a different route:
**none of the five prints `Partition Table:`, `esp_image: segment` or
`Loaded app from partition`**, the lines a flash boot on this board does print
([WAVESHARE_RUNNING_OUR_CODE.md:73-77](../WAVESHARE_RUNNING_OUR_CODE.md)), and
every capture's banner names `pedo`. The shared
`compile time` string is *not* evidence here and is worth recording as a trap:
`shake.log:6-10` and `walk.log:6-10` load demonstrably different images — 48112
against 47712 bytes, entry `40375a80` against `40375a74` — under the identical
`Aug 28 2026 12:53:17`. Nothing in the session writes flash; the unit still
carries the T-166 candidate there.

The QMI8658 datasheet these were read against is not here: it is QST's copyright
and its cover marks it *Security Level: 3*. The report names it and where it came
from.

## The probe

`probe/` is the source that produced `shake.log`, and only that run. **It is
neither the source of `walk.log` nor of the three desk runs.** The desk runs
came from an earlier build that never configured the engine, and the captures
say so: no `CTRL9 0x0D` block, no
`CTRL3`, no `p2p` column, `--- after ---` where this source prints
`--- armed ---`, and a header naming three registers where
`pedo.c:261` — "Writes CTRL2/CTRL3/CTRL7/CTRL8/CTRL9" writes five plus
`CAL1_L..CAL4_H`. `pedo.c:23-31` — "WHY THIS FILE CHANGED" names it as one of
**two** sufficient causes for those runs reading zero; the board also never
moved.

`walk.log` is a third build, and it also says so: it writes `CTRL2 = 0x27`
where this source defines `0x16` (`pedo.c:111` — "#define CTRL2_VALUE 0x16"),
carries **no `CTRL3` line at all** where this source writes `0x36`
(`pedo.c:112` — "#define CTRL3_VALUE 0x36"), configures a different
engine — `sample_cnt=62 peak2peak=0x00CC peak=0x0066 time_up=250 time_low=25
cnt_entry=10 sig_count=4` at `walk.log:52-53` against `50 / 0x0050 / 0x003C /
400 / 8 / 1 / 1` here (`pedo.c:140-147` — "#define PED_SAMPLE_CNT") — and
prints a `g` triad per line
where this source prints `p2p mg`. Nine constants, not one.

What `probe/` holds is `pedo.c`, its two
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
4. **behaviour** — `ACCEL_LSB_PER_G` is now derived from `CTRL2_VALUE`'s `aFS`
   field rather than written out beside it, and the parameter header prints it.
   No archived capture records the divisor its binary used to turn LSB into the
   `p2p` milligravity column, and none of them can be made to: the scale of
   every mg figure the report publishes is therefore UNKNOWN. Deriving it is
   what keeps the printed line a record instead of a second assertion that can
   go stale the way `shake.log:49` did;
5. **text** — five `printf` labels are corrected in this source. Four are the
   stale `shake.log` labels named above: `CTRL2 = 0x16` is ±4 g at 112.1 Hz in
   6DOF; `CTRL3 = 0x36` is ±128 dps, not the ±1000 dps printed here; the
   banner's register list omitted `CTRL3`; and the p2p header printed the
   datasheet's 200/100 mg while the engine held SensorLib's loosened 80/60. The
   fifth is not a configuration label and so is not among that five: the
   `REVISION` line printed `0x7C = QMI8658A 13-52-25 Rev A` as settled fact.
   #341 has since settled it the other way: two Rev A datasheets exist, and the
   `0x7C` is read from `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A`. The probe's
   label now names **both** papers and says the byte does not identify which
   (`probe/pedo.c:267-269` — "0x7C in BOTH Rev A datasheets").

**No register or parameter constant differs from the shake run.** That is
checkable without trusting this note: `shake.log:48-59` echoes every value the
probe wrote
— `CTRL2 = 0x16`, `CTRL3 = 0x36`, `CTRL8` `0x80` then `0x90`, `sample_cnt=50`,
`peak2peak=0x0050`, `peak=0x003C`, `time_up=400`, `time_low=8`, `cnt_entry=1`,
`precision=0`, `sig_count=1` — and each matches the `#define` in `probe/pedo.c`.
