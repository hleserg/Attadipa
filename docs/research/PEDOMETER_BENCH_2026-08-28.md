# QMI8658 pedometer, bench 2026-08-28

**The watch was moved vigorously for many seconds and the step register never
moved.** That is a MEASURED negative, and it is not the twenty-step walk
[#116](https://github.com/hleserg/Attadipa/issues/116) asks for — which could
not be logged at all, for a reason worth recording.

| Label | Value |
| --- | --- |
| Status | MEASURED (shake) · NOT EXECUTED — HARDWARE REQUIRED (walk) |
| Date | 2026-08-28 |
| Part | QMI8658, I2C `0x6B`, `REVISION_ID = 0x7C` (Rev A — chapter 11 pedometer) |
| Board | Waveshare ESP32-S3 AMOLED 2.06 |
| Operator | the owner, watch in hand, attached over USB |
| Source key | **S15** ([HARDWARE_MATRIX](HARDWARE_MATRIX.md)) |

## What was measured

One run, 159 one-second windows, the watch shaken by hand while attached.

| Quantity | Value |
| --- | --- |
| configured `ped_fix_peak2peak` | 80 in u6.10 ≈ **78 mg** |
| probe's maximum peak-to-peak, per axis over a one-second window | **2242 mg** |
| seconds of the 158 exceeding 78 mg by that measure | **18** |
| step count during those seconds | **0**, `+0` on every one |
| `STATUS1` | `0x00` throughout |

The probe exists so that a zero count can be told apart from a threshold never
reached, and on that question it answers clearly: the watch was moved hard, for
many seconds, and nothing counted.

**But the two numbers are not the same quantity, and an earlier draft of this
report divided them.** 78 mg is the configured `ped_fix_peak2peak`; 2242 mg is
the probe's own per-axis maximum over a one-second window. **UNKNOWN:** chapter
11 does not state the window length or the axis combination the engine's own
peak-to-peak uses. Until that is sourced, "29× the threshold" is not a claim this
run supports — only "far more motion than 78 mg, sustained, and no count".

## Both known-good configurations were tried

1. **The datasheet's own.** Chapter 11, accelerometer alone, §11.1's worked
   example scaled from its stated 50 Hz to the 62.5 Hz the part actually offers.
2. **SensorLib's shipping profile.** `examples/sensor/qmi8658_pedometer` runs the
   engine in 6DOF with both sensors enabled and says the datasheet profile is too
   conservative for handheld bring-up. Its parameters were used verbatim —
   `configPedometer(50, 80, 60, 400, 8, 1, 0, 1)`, including `entry_count = 1`
   and `sig_count = 1` so the register moves on the *first* step rather than
   holding nine back. That library ships on this board.

**SensorLib is not among this repository's pinned upstreams**, so its profile is
an unpinned read and is cited here as a lead rather than as a source. Reproducing
this needs the revision recorded.

`CTRL7 = 0x03` — both sensors enabled — is also the state the probe **found** on
the board at start-up. An earlier draft read that as the vendor firmware running
6DOF. **It is not evidence of that.** T-166 replaced this unit's factory image on
2026-08-25 ([BENCH_DEVICES](BENCH_DEVICES.md)), three days before this session, so
the residue is Attadipa's own firmware. What the vendor configured is **UNKNOWN**
and would now have to come from the S12 flash image.

The CTRL9 handshake was confirmed on every run: both `0x0D` calls acknowledged
with CmdDone set and cleared, so the parameters reached the engine. The probe
refuses to print a step count after a failed configuration, because that number
would mean nothing.

## What this does NOT establish, and why the walk is still open

**A shake is not a walk, and the engine is entitled to reject one.** The
pedometer's whole job is to separate steps from other motion: besides amplitude
it applies a cadence window (`time_low = 8`, `time_up = 400` samples — roughly
71 ms to 3.6 s between steps at 112.1 Hz) and a peak-pattern test this probe
cannot see. Hand-shaking clears the amplitude bar and plausibly falls inside the
cadence window, but "amplitude exceeded and nothing counted" is strong evidence
that the engine is not counting on this part — **not proof**. The twenty-step
walk remains the experiment that settles it.

**Three earlier runs prove nothing at all.** `pedo-run`, `pedo-run2` and
`pedo-run3` total 1380 one-second samples with a step count of zero, and every
one of them is a board lying still on a desk. Zero is the *correct* answer for a
stationary board. #116 already says this about the 2026-08-23 session, and it is
worth repeating because the sample count is large enough to look like evidence.

## The blocker: a walk cannot be logged over USB serial

The walk was attempted. It ends in
`serial.serialutil.SerialException: device reports readiness to read but returned
no data (device disconnected …)` — because walking with the watch means
unplugging it, and the probe reports over the USB serial console. The act of
performing the experiment destroys the channel that records it. No walk data
exists.

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
from its battery while unplugged, and whether the IMU keeps its configuration
and count across that. If it does not, this route does not work either and the
experiment needs on-device storage instead.

## A caveat about the raw logs

The archived logs misdescribe their own configuration. Two `printf` labels had
gone stale against the constants they printed:

- the header prints `CTRL2 = 0x16 (+/-8 g, 62.5 Hz accel-only)`. `0x16` is
  **±4 g at 112.1 Hz in 6DOF**. The register value was right and the label was
  not — the resting `az ≈ -8257` against `ACCEL_LSB_PER_G = 8192` confirms ±4 g.
- the p2p header prints `ped_fix_peak2peak = 200 mg; ped_fix_peak = 100 mg`,
  which are the **datasheet's** numbers. The probe was running SensorLib's
  loosened **80/60** (≈78/59 mg), and that is the bar the `OVER` marks were
  computed against.

Both are fixed in the probe, and the threshold line now prints the constants
rather than quoting them. **No register or parameter value changed**, so the run
above stands exactly as recorded; where the log headers and this report
disagree, this report is correct.
