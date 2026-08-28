# QMI8658 pedometer, bench 2026-08-28

**The watch was moved far harder than the engine's configured threshold, for
sixteen seconds without a break, and the step register never moved.** That is a
MEASURED negative. It is not the twenty-step walk
[#116](https://github.com/hleserg/Attadipa/issues/116) asks for — which was
attempted, could not be logged, and destroyed its own result on the way out.

| Label | Value |
| --- | --- |
| Status | MEASURED (shake) · NOT EXECUTED — HARDWARE REQUIRED (walk) |
| Date | 2026-08-28 |
| Part | QMI8658, I2C `0x6B`, `REVISION_ID = 0x7C` (Rev A — chapter 11 pedometer) |
| Board | Waveshare ESP32-S3 AMOLED 2.06 |
| Operator | the owner, watch in hand, attached over USB |
| How the probe ran | `esptool` **RAM boot** via `tools/flash/ramhold.py` — every segment loaded to a RAM address, nothing written to flash |
| Source key | **S15** ([HARDWARE_MATRIX](HARDWARE_MATRIX.md)) |

## What was measured

One run: **159** one-second windows, `t=0` to `t=158`.

| Quantity | Value |
| --- | --- |
| configured `ped_fix_peak2peak` | 80 in u6.10 ≈ **78 mg** |
| maximum peak-to-peak, per axis over a one-second window | **2242 mg**, at `t=42s` |
| seconds of the 159 above 78 mg | **17** |
| smallest of those 17 | **322 mg** |
| when they fall | one at `t=0`, then a contiguous burst `t=34`–`t=49` |
| the other 142 seconds | 0–2 mg — the watch lying still |
| step count | **0**, `+0` on every window |
| `STATUS1` | `0x00` throughout |

**The shape of the run matters and an earlier draft of this report got it
wrong.** It said the watch was shaken for 158 s. It was not: the motion is one
16-second burst plus a single second at pick-up, inside a run that was otherwise
a board on a desk. What the run establishes is not 158 seconds of motion but
**sixteen consecutive seconds** of it, every one of them far above the bar, with
the count pinned at zero.

**The two numbers are still not the same quantity, and an earlier draft divided
them.** 78 mg is the configured `ped_fix_peak2peak`; 2242 mg is the probe's own
per-axis maximum over a one-second window. **UNKNOWN:** chapter 11 does not state
the window length or the axis combination the engine's own peak-to-peak uses, so
"29× the threshold" is not a claim this run supports — only "far more motion than
78 mg, sustained, and no count".

Raw, from the run — the whole capture is committed at
[`pedometer-bench-2026-08-28/shake.log`](pedometer-bench-2026-08-28/shake.log),
with the other four runs beside it, so every line number below resolves:

```
--- before ---   CTRL2=0x27 CTRL7=0x01 CTRL8=0x90

CTRL7 = 0x00 (aEN=gEN=0, required by 11.2 before configuring) -> ACK
CTRL8 = 0x80 (CTRL9 handshake on STATUSINT, Pedo_EN still 0)  -> ACK
CTRL2 = 0x16 (+/-8 g, 62.5 Hz accel-only)                     -> ACK
CTRL3 = 0x36 (+/-1000 dps, 112.1 Hz -- 6DOF needs the gyro up)  -> ACK

two CTRL9 0x0D calls (Table 38): both acknowledged -- CmdDone set and cleared each time
  sample_cnt=50  peak2peak=0x0050  peak=0x003C  time_up=400
  time_low=8  cnt_entry=1  precision=0  sig_count=1

CTRL8 = 0x90 (Pedo_EN; the 0->1 edge clears the count, 11.6) -> ACK
CTRL7 = 0x03 (aEN; the engine starts counting here, 11.3)     -> ACK
CTRL9 0x0F (reset step count): acknowledged
--- armed  ---   CTRL2=0x16 CTRL7=0x03 CTRL8=0x90  step count = 0

[shake.log:60-80 elided: the CTRL8 readback banner and the expectations
 written down before the run. Nothing is written to the IMU in them.]

t=   0s  ax=   350 ay=    24 az= -8257  p2p mg: x= 436 y= 723 z= 387  OVER   steps=0 (+0)  STATUS1=0x00
t=  36s  ax= -1337 ay= -2555 az=  -295  p2p mg: x= 989 y= 929 z=2027  OVER   steps=0 (+0)  STATUS1=0x00
t=  42s  ax= 12253 ay= -4278 az=   484  p2p mg: x=2242 y=1393 z= 963  OVER   steps=0 (+0)  STATUS1=0x00
t=  49s  ax= -1267 ay=   862 az= -8122  p2p mg: x= 254 y= 322 z= 151  OVER   steps=0 (+0)  STATUS1=0x00
t= 158s  ax= -1258 ay=   862 az= -8127  p2p mg: x=   0 y=   0 z=   1  under  steps=0 (+0)  STATUS1=0x00
```

**The `0→1` edge is in that excerpt, and it takes three lines to see.** `CTRL8`
reads `0x90` on the first line and `0x90` on the last, so bit 4 looks unchanged;
between them the probe drives it **low** — `CTRL8 = 0x80`, clearing `Pedo_EN` so
the parameters can be passed with the sensors off, which §11.2 asks for in a
closing note: *"Configuration should be done when accelerometer and gyroscope are
disabled(CTRL7.aEN = CTRL7.gEN =0)"* — and then back **high** at `CTRL8 = 0x90`.
That is the edge, and §11.6 describes this exact two-step as a reset: *"Host can
simply clear the CTRL8.bit4 and then set it to restart the Pedometer engine and
reset the Step Count registers."* The probe then issues `CTRL9 0x0F`
(`CTRL_CMD_RESET_PEDOMETER`) as well, which §11.6 lists as a second, independent
reset — so whatever the 14:39 walk had accumulated was discarded twice over.
`step count = 0` on the `--- armed ---` line is therefore **not** a post-walk
reading; it is the value immediately after two clears, and it is worth nothing.
An earlier revision of this report quoted the block with those lines elided,
which left its own headline hazard unverifiable from the evidence it printed.

**Two caveats about that `OVER` column, both found by re-reading the probe rather
than the log.** The column's bar was a hard-coded **200 mg** — the *datasheet's*
`ped_fix_peak2peak`, not the 80 (≈78 mg) the engine was configured with. And an
earlier draft of this report asserted the opposite, that the marks were computed
against 80/60. They were not. It changes nothing here: the same 17 seconds clear
both bars, because the smallest of them is 322 mg. The probe now derives the
column's bar and its printed threshold from one constant so they cannot drift
apart again.

## The two profiles are not equally exercised

1. **SensorLib's bring-up profile — this is the run above.** 6DOF with both
   sensors, `configPedometer(50, 80, 60, 400, 8, 1, 0, 1)` used verbatim,
   including `entry_count = 1` and `sig_count = 1` so the register moves on the
   *first* step rather than holding nine back. From
   `examples/sensor/qmi8658_pedometer/qmi8658_pedometer.ino` at
   `lewisxhe/SensorLib` **`2b9e591f245e447d3d00ec8798c3f49b897882d9`**
   (`v0.4.1-123-g2b9e591`, 2026-07-30), whose own comment says the datasheet
   profile *"is conservative and designed to reject non-step vibration"*.
   **SensorLib is not among this repository's pinned upstreams**, so it is cited
   as a lead at a recorded revision, not as a source.
2. **The datasheet's own — configured, acknowledged, and never exercised under
   recorded motion.** It was armed for the abandoned walk at 14:39
   (`sample_cnt=62 peak2peak=0x00CC peak=0x0066 time_up=250 time_low=25
   cnt_entry=10 sig_count=4`, accelerometer alone at `CTRL2=0x27`, `CTRL7=0x01`),
   and 36 seconds of samples were recorded at zero before the serial link died.
   That binary printed no amplitude column and nothing records whether the watch
   was moving, so those 36 seconds are **not evidence**. Saying "both known-good
   configurations were tried" would overstate it: both were *configured*, one was
   *measured*.

The CTRL9 handshake was confirmed on every run — both `0x0D` calls acknowledged
with CmdDone set and cleared — so the parameters reached the engine. The probe
refuses to print a step count after a failed configuration, because that number
would mean nothing.

## What the found register state does and does not say

The three desk runs at 12:58, 13:04 and 13:20 and the abandoned walk attempt at
14:39 all found `CTRL2 = 0x24, CTRL7 = 0x03, CTRL8 = 0x00`; the three desk runs
restored it, verified. An earlier draft read that `CTRL7 = 0x03` as the vendor
firmware running 6DOF. **This run is not evidence of that** — but the reason is
narrower than the previous revision of this report said, and the difference
matters, because a second draft then over-corrected in the opposite direction.

`0x24 / 0x03 / 0x00` is exactly what the 2026-08-23 session left behind: the
vendor firmware wrote `CTRL2 = 0x24` and `CTRL7 = 0x03`, and `CTRL8` was then
cleared to `0x00` by hand
([`WAVESHARE_RUNNING_OUR_CODE.md:615-619`](WAVESHARE_RUNNING_OUR_CODE.md)
"restored to the power-on default"). The same section records why that could
still be sitting there five days later: **loading a RAM image**
([`WAVESHARE_RUNNING_OUR_CODE.md:620-623`](WAVESHARE_RUNNING_OUR_CODE.md)
"does not reset the peripherals") — the SoC restarts, the parts on the I2C bus
keep what the last program left. Whether this IMU in fact kept power across the
five days, through T-166's reflash on 2026-08-25 and everything after it, is not
recorded anywhere; a power cycle would have cleared it. So the residue may be the
vendor's own write from 2026-08-23, or some later writer's, and **this session
cannot tell which**. That, not any claim about who wrote it, is what makes it
useless as corroboration.

The shake run shows the ambiguity resolving the other way: it found
`CTRL2 = 0x27, CTRL7 = 0x01, CTRL8 = 0x90` — the *walk probe's own armed state*,
still set 29 minutes after that attempt was abandoned. There the writer is known,
because this session logged it. Found state on this board is whatever the last
program left; which program that was varies by run, and the shake run is the only
one whose value traces to a writer this session can name.

**What the vendor firmware configured is separately known, and it is not
UNKNOWN.** On 2026-08-23, with the factory image still present, booting it was
observed to write `CTRL2 = 0x24` and `CTRL7 = 0x03` over what a probe had left,
and to leave `CTRL8` alone — so the vendor runs the IMU in 6DOF and does not use
the pedometer engine at all
([`WAVESHARE_RUNNING_OUR_CODE.md:608-610`](WAVESHARE_RUNNING_OUR_CODE.md)
"Booting the vendor firmware restored"). That is S13, and it stands. What the
2026-08-28 residue cannot do is corroborate it.

**The shake run's own restore is unverified.** Its capture ends at `t=158` with
no restore block, while the probe's loop runs to 1200 — so the capture was
stopped, not the probe, and the IMU was left armed with `Pedo_EN` set. The
QMI8658 holds none of this in non-volatile memory, so a power cycle clears it;
short of one, it persists, which is exactly how the walk attempt's state was
still there half an hour later.

## What this does NOT establish, and why the walk is still open

**A shake is not a walk, and the engine is entitled to reject one.** The
pedometer's job is to separate steps from other motion: besides amplitude it
applies a cadence window (`time_low = 8`, `time_up = 400` samples — roughly 71 ms
to 3.6 s between steps at 112.1 Hz — see below for where that rate comes from)
and a peak-pattern test this probe cannot
see. SensorLib's own example says so in as many words — *"step engine is for
periodic gait, not random shaking"*, and it advises simulating a 1–2 Hz walking
cadence for handheld debug. Sixteen unbroken seconds above the amplitude bar with
nothing counted is strong evidence that the engine is not counting on this part;
it is **not proof**. The twenty-step walk remains the experiment that settles it.

**Three earlier runs prove nothing at all.** `pedo-run`, `pedo-run2` and
`pedo-run3` total 1380 one-second samples with a step count of zero, and every
one of them is a board lying still on a desk. Zero is the *correct* answer for a
stationary board. #116 already says this about the 2026-08-23 session, and it is
worth repeating because the sample count is large enough to look like evidence.

## The blocker: a walk cannot be logged over USB serial

The walk was attempted at 14:39. It ends in
`serial.serialutil.SerialException: device reports readiness to read but returned
no data (device disconnected …)` — because walking with the watch means
unplugging it, and the probe reports over the USB serial console. The act of
performing the experiment destroys the channel that records it. No walk data
exists.

**And whatever the walk did produce was then thrown away.** The engine stayed
armed after the disconnect. If the owner walked at all in that window, the count
accumulated in `0x5A`–`0x5C` — and the next run at 15:08 armed the engine before
reading it, which per §11.6 clears the count on the `0→1` edge of `CTRL8` bit 4.
The probe now reads and prints the found count *before* it writes anything. This
is the §11.6 hazard below, demonstrated rather than predicted.

**Recommended next step.** The step counter is an accumulating register in the
IMU, not a stream. So the run does not need to be observed live:

1. configure the engine while attached, and read the count;
2. unplug and walk a counted twenty steps;
3. re-attach and read the count again, **without reconfiguring** — the `0→1`
   edge on `CTRL8` bit 4 clears the count, so a probe that reconfigures on boot
   destroys the very number it was sent to fetch. That is §11.6 of `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A`
   (© 2022 QST, 88 pp., 20 June 2022), read in full this session — it lists four
   resets, of which the probe fires two. See *"Which datasheet this report was
   read against"* below.

The one thing to establish first is **UNKNOWN**: whether the board stays powered
from its battery while unplugged, and whether the IMU keeps its configuration and
count across that. There is a **lead, not an answer**: the walk probe's armed
state survived from 14:39 to 15:08, across the disconnect and across the SoC
reset that the next RAM boot performs. That is consistent with the IMU never
losing power. It does not settle the question, because the `SerialException` says
*"device disconnected **or multiple access on port**"* — if the port was merely
contended, the unit never came off USB power and the survival proves nothing. The
read-after-walk experiment above is still what answers it.

## Which datasheet this report was read against

Every chapter 11 and Table 22 claim above was read from one document, opened in
full for this report:

| | |
| --- | --- |
| Cover page | `Document No. 13-52-27 · Title: QMI8658C Datasheet · Rev: A` |
| Date, size | 20 June 2022, 88 pages, © 2022 QST Corporation |
| Origin | QST's own site, `qstcorp.com/upload/pdf/202210/` |
| `REVISION_ID` | `0x7C` — the value this silicon reads |
| Pedometer | chapter 11, §11.1–11.6, `CTRL8.bit4` and `CTRL9 0x0F` both described |

It is **not committed**: it is QST's copyright and its own cover marks it
*Security Level: 3*.

**This repository names that document two ways, and this report does not settle
which is right.** Four sites, and they do not agree:

| Site | What it says |
| --- | --- |
| [`WAVESHARE_RUNNING_OUR_CODE.md:325-327`](WAVESHARE_RUNNING_OUR_CODE.md) "document number of the Rev A datasheet is" | the number is `13-52-25`, **not** `13-52-27` |
| [`OPEN_QUESTIONS.md:90`](OPEN_QUESTIONS.md) "the Rev A document number is" | the same correction, in H14's tail |
| [`VERIFIED_FACTS.md:573-575`](VERIFIED_FACTS.md) "documents it fully" | `13-52-27` is QMI8658**C** Rev A, and it exists |
| [`VERIFIED_FACTS.md:577-579`](VERIFIED_FACTS.md) "documents the identical feature" | `13-52-25` is QMI8658**A** Rev A, and it exists too |
| [`VERIFIED_FACTS.md:1479-1481`](VERIFIED_FACTS.md) "values for that byte" | `REVISION_ID = 0x7C` comes from `13-52-25` |

A document numbered `13-52-27`, titled *QMI8658C Datasheet*, marked `Rev: A`,
demonstrably does exist — it is the one quoted here, and it reports `0x7C` on its
own register-description page. What is in `13-52-25` is **UNKNOWN** to this
repository: no record shows anyone opening it. This report therefore cites only
the paper it read, and the tree-wide reconciliation — including which document
the `0x7C` attribution at `VERIFIED_FACTS.md:1481` actually came from — is
[#341](https://github.com/hleserg/Attadipa/issues/341), not this pull request.

## A caveat about the raw logs

The archived logs misdescribe their own configuration. **Three** `printf` labels
had gone stale against the constants they printed:

- the header prints `CTRL2 = 0x16 (+/-8 g, 62.5 Hz accel-only)`. `0x16` is
  **±4 g at 112.1 Hz in 6DOF** — wrong on both counts, though the register value
  itself was right. `aFS = 001` is ±4 g (`aFS = 010` would be ±8 g), and the
  resting `az ≈ -8257` against `ACCEL_LSB_PER_G = 8192` confirms it at 1.008 g.
  `aODR = 0110` has **two** rates in Table 22, in adjacent columns headed *"ODR
  Rate (Hz) (Accel only)"* and *"ODR Rate (Hz) (6DOF)"*: **125** and **112.1**.
  This run had the gyro up (`CTRL7 = 0x03`, `CTRL3 = 0x36`), so the 6DOF column
  applies and the rate is 112.1 Hz; the label's 62.5 Hz is the *accel-only* rate
  of the **next** setting down, `aODR = 0111`. That column pair is also why
  [VERIFIED_FACTS](VERIFIED_FACTS.md) records 125 Hz for the same nibble on
  2026-08-23 — that run was accelerometer-only — and this one records 112.1 Hz.
  Both are Table 22, one row, two columns; a firmware author sizing the pedometer
  ODR needs the mode before the number.
- the header prints `CTRL3 = 0x36 (+/-1000 dps, 112.1 Hz -- 6DOF needs the gyro
  up)`. The rate is right; **the full scale is not, and no such rung exists.**
  Table 22's `CTRL3` entry gives `gFS<2:0>` as ±16 / ±32 / ±64 / ±128 / ±256 /
  ±512 / ±1024 dps for `000`–`110`, with `111` marked N/A — so `0x36`'s
  `gFS = 011` is **±128 dps**, and 1000 dps is not on the ladder at all. It comes
  from a different IMU family. Nothing in this report depends on it: the gyro is
  enabled only to put the accelerometer into 6DOF mode, and no gyro sample is
  read, printed or used. `gODR = 0110` → 112.1 Hz is correct, from the same
  register's own ODR table.
- the p2p header prints `ped_fix_peak2peak = 200 mg; ped_fix_peak = 100 mg`,
  which are the **datasheet's** numbers, while the engine had been configured
  with SensorLib's loosened 80/60 (≈78/59 mg). The header agreed with the `OVER`
  column's hard-coded bar and disagreed with the engine.

Both are fixed in the probe, and the threshold and the `OVER` bar now come from
the same constant. **No register or parameter value changed**, so the run above
stands exactly as recorded; where the log headers and this report disagree, this
report is correct.
