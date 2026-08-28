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
| smallest of those 17 | **322 mg**, still 4× the bar |
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

Raw, from the run:

```
--- before ---   CTRL2=0x27 CTRL7=0x01 CTRL8=0x90

two CTRL9 0x0D calls (Table 38): both acknowledged -- CmdDone set and cleared each time
  sample_cnt=50  peak2peak=0x0050  peak=0x003C  time_up=400
  time_low=8  cnt_entry=1  precision=0  sig_count=1

--- armed  ---   CTRL2=0x16 CTRL7=0x03 CTRL8=0x90  step count = 0

t=   0s  ax=   350 ay=    24 az= -8257  p2p mg: x= 436 y= 723 z= 387  OVER   steps=0 (+0)  STATUS1=0x00
t=  36s  ax= -1337 ay= -2555 az=  -295  p2p mg: x= 989 y= 929 z=2027  OVER   steps=0 (+0)  STATUS1=0x00
t=  42s  ax= 12253 ay= -4278 az=   484  p2p mg: x=2242 y=1393 z= 963  OVER   steps=0 (+0)  STATUS1=0x00
t=  49s  ax= -1267 ay=   862 az= -8122  p2p mg: x= 254 y= 322 z= 151  OVER   steps=0 (+0)  STATUS1=0x00
t= 158s  ax= -1258 ay=   862 az= -8127  p2p mg: x=   0 y=   0 z=   1  under  steps=0 (+0)  STATUS1=0x00
```

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

The three desk runs at 12:58, 13:04 and 13:20 each found `CTRL2 = 0x24,
CTRL7 = 0x03, CTRL8 = 0x00` and each restored it, verified. An earlier draft read
that `CTRL7 = 0x03` as the vendor firmware running 6DOF. **This run is not
evidence of that**, on two counts. T-166 replaced this unit's factory image on
2026-08-25 ([BENCH_DEVICES](BENCH_DEVICES.md)), three days earlier, so nothing
found on the board now is attributable to vendor firmware. And the shake run did
not find `0x03` at all: it found `CTRL2 = 0x27, CTRL7 = 0x01, CTRL8 = 0x90` — the
*walk probe's own armed state*, still set 29 minutes after that attempt was
abandoned. Found state on this board is the last program's leavings, and it
varies by run.

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
to 3.6 s between steps at 112.1 Hz) and a peak-pattern test this probe cannot
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
   destroys the very number it was sent to fetch. That is §11.6 of
   `13-52-25 ∙ QMI8658A Datasheet ∙ Rev A` (© 2022 QST), the document
   `REVISION_ID = 0x7C` identifies — catalogued in
   [PEDOMETER_PARTS](PEDOMETER_PARTS.md), and **uncorroborated here by any second
   source**, so step 3 should be verified before it is relied on.

The one thing to establish first is **UNKNOWN**: whether the board stays powered
from its battery while unplugged, and whether the IMU keeps its configuration and
count across that. There is a **lead, not an answer**: the walk probe's armed
state survived from 14:39 to 15:08, across the disconnect and across the SoC
reset that the next RAM boot performs. That is consistent with the IMU never
losing power. It does not settle the question, because the `SerialException` says
*"device disconnected **or multiple access on port**"* — if the port was merely
contended, the unit never came off USB power and the survival proves nothing. The
read-after-walk experiment above is still what answers it.

## A caveat about the raw logs

The archived logs misdescribe their own configuration. Two `printf` labels had
gone stale against the constants they printed:

- the header prints `CTRL2 = 0x16 (+/-8 g, 62.5 Hz accel-only)`. `0x16` is
  **±4 g at 112.1 Hz in 6DOF**. The register value was right and the label was
  not — the resting `az ≈ -8257` against `ACCEL_LSB_PER_G = 8192` confirms ±4 g.
- the p2p header prints `ped_fix_peak2peak = 200 mg; ped_fix_peak = 100 mg`,
  which are the **datasheet's** numbers, while the engine had been configured
  with SensorLib's loosened 80/60 (≈78/59 mg). The header agreed with the `OVER`
  column's hard-coded bar and disagreed with the engine.

Both are fixed in the probe, and the threshold and the `OVER` bar now come from
the same constant. **No register or parameter value changed**, so the run above
stands exactly as recorded; where the log headers and this report disagree, this
report is correct.
