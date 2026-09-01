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
| Part | QMI8658, I2C `0x6B`, `REVISION_ID = 0x7C` (`13-52-27` Rev A — chapter 11 pedometer) |
| Board | Waveshare ESP32-S3 AMOLED 2.06 |
| Operator | the owner, watch in hand, attached over USB |
| How the probe ran | `esptool` **RAM boot** via `tools/flash/ramhold.py` — every segment loaded to a RAM address, nothing written to flash. Two of the five captures record the loader step itself; the other three open at the image's own banner, and **none of the five prints `esp_image: segment` or `Loaded app from partition`** — what a flash boot on this board does print ([WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) `:73-77`) |
| Source key | **S15** ([HARDWARE_MATRIX](HARDWARE_MATRIX.md)) |

## What was measured

One run: **159** one-second windows, `t=0` to `t=158`. Each window is 20 samples
50 ms apart — a 20 Hz view of the part's 112.1 Hz output, which **under-reads**:
a subsample's extremes lie inside the full stream's, so every figure below is a
lower bound, which makes the negative result stronger, not weaker.

| Quantity | Value |
| --- | --- |
| configured `ped_fix_peak2peak` | 80 in u6.10 ≈ **78 mg** |
| maximum peak-to-peak, per axis over a one-second window | **2242 mg**, at `t=42s` |
| seconds of the 159 above 78 mg | **16**, all of them contiguous: `t=34`–`t=49` |
| smallest of those 16 | **322 mg** |
| one further window above the bar | `t=0` — a start-up artefact, not motion |
| the other 142 seconds | 0–1 mg — the watch lying still |
| step count | **0**, `+0` on every window |
| `STATUS1` | `0x00` throughout |

**The milligravity scale is UNKNOWN.** Every `p2p` figure above is
`(hi - lo) * 1000 / ACCEL_LSB_PER_G`
(`pedometer-bench-2026-08-28/probe/pedo.c:402` — "(hi[a] - lo[a]) * 1000"), and
no capture
records which divisor its binary used. `shake.log:49`, the run's own header,
prints `+/-8 g` — but that label is itself one of the stale four, listed below in
this same report ([`PEDOMETER_BENCH_2026-08-28.md:403-405`](PEDOMETER_BENCH_2026-08-28.md) "62.5 Hz accel-only"):
the register it names means ±4 g. It is therefore the *reason to doubt* the
divisor, not a value for it. The label drifted from its constant when the full
scale changed, and nothing in the capture shows the divisor did not drift with
it. The source says 8192, and that source is the probe *as corrected after the
session*, not the binary that ran. The capture cannot settle
it: the earlier builds printed a g triad on every line and this one prints none
(`grep -c ' g)' shake.log` → 0, against 240 in `pedo-run.log`). At 4096 every
figure above is half what is printed — 2242 → 1121, 322 → 161. **No conclusion
in this report moves**: the smallest window still clears the 78 mg bar by more
than 2×, and nothing here rests on a numeric multiple. The probe now **derives**
`ACCEL_LSB_PER_G` from `CTRL2_VALUE`'s `aFS` field and prints it, so the next
capture records a divisor that cannot disagree with the register it was
configured with — which a second hand-kept constant could not have promised.

**The shape of the run matters and two earlier drafts of this report got it
wrong.** The first said the watch was shaken for 158 s. It was not: the motion is
one 16-second burst inside a run that was otherwise a board on a desk. The second
counted 17 windows over the bar and called the seventeenth *"a single second at
pick-up"*. **Nothing was picked up.** The `t=0` window is a start-up artefact of
the probe, and every capture in this session carries it:

| capture | `t=0` | `t=1` |
| --- | --- | --- |
| `shake.log:81-82` | p2p 436 / 723 / 387 mg | 1 / 0 / 0 mg |
| `pedo-run.log:47-48` | `ay = -1.15 g` | `0.04 g` |
| `pedo-run2.log:47-48` | `ay = -0.46 g` | `0.04 g` |
| `pedo-run3.log:47-48` | `ax = 1.34 g` | `0.13 g` |
| `walk.log:75-76` | `az = -1.34 g` | `-0.99 g` |

Three of those five are the report's own *"desk, board stationary"* runs, and
`walk.log` holds a flat triad until the unplug, so the first window is wrong on
boards that demonstrably never moved. In `shake.log`
the giveaway is that the **attitude** does not move with it: `t=0` is
`(350, 24, -8257)` and `t=1` is `(353, 27, -8254)` — and `t=33`, half a minute
later, is `(354, 26, -8251)`. A watch is not lifted and replaced inside one
second to within 3 LSB on three axes. When it genuinely was handled, from `t=49`
on, the board settles somewhere else entirely.

So the run establishes **sixteen consecutive seconds** of motion, `t=34`–`t=49`,
every one far above the bar with the count pinned at zero. The headline is
unchanged; the count is 16, not 17.

**The two numbers are still not the same quantity, and an earlier draft divided
them.** 78 mg is the configured `ped_fix_peak2peak`; 2242 mg is the probe's own
per-axis maximum over a one-second window. **UNKNOWN:** chapter 11 does not state
the window length or the axis combination the engine's own peak-to-peak uses, so
"29× the threshold" is not a claim this run supports — only "far more motion than
78 mg, sustained, and no count".

Raw, from the run — the whole capture is committed at
[`pedometer-bench-2026-08-28/shake.log`](pedometer-bench-2026-08-28/shake.log),
with the other four runs and the probe's source beside it
([`probe/pedo.c`](pedometer-bench-2026-08-28/probe/pedo.c)), so every line
number below resolves and every constant can be checked against the register
values the log itself echoes:

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
against 80/60. They were not. It changes nothing here: the same 16 seconds clear
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
   That binary printed no amplitude column, but the capture does record whether
   the watch was moving, and it says it was **not**: `walk.log:77-108` holds the
   triad at `(0.13, 0.04, -1.01 g)` for thirty-two seconds, `az` reading -1.02
   on four of them (`:78`, `:81`, `:88`, `:103`) and nothing else moving at all
   — `:76` is still the start-up sample at `(0.09, 0.01, -0.99 g)` — the same
   attitude as the desk runs — and the first real movement is `t=35`, which is
   the unplug, one line before the link dies. So the walk had not begun while
   the link was alive; the capture ends there and cannot speak for what
   followed. Those 36
   seconds are a stationary board reading zero correctly, which is **not
   evidence** about the engine either way. Saying "both known-good configurations
   were tried" would still overstate it: both were *configured*, one was
   *measured*.

The CTRL9 handshake was confirmed on every run — both `0x0D` calls acknowledged
with CmdDone set and cleared — so the engine **processed both Configure
Pedometer commands**. It does not follow that the parameters were in place when
it did, and this report no longer says it does: `configure_pedometer()`
(`pedometer-bench-2026-08-28/probe/pedo.c:194-224` —
"static bool configure_pedometer(void)") discards every `wr()`
return for the eighteen
`CAL1_L..CAL4_H` bytes — the only checked calls are the two `ctrl9()`s — and
nothing reads those registers back. `shake.log:53-54` is not a readback either;
`pedometer-bench-2026-08-28/probe/pedo.c:317-322` — "sample_cnt=%d" prints
the `#define`s. **A return check on the `CAL`
writes and a readback before the second `0x0D` are required before the
read-after-walk run #116 needs, and are not in this pull request.** The probe
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
([`WAVESHARE_RUNNING_OUR_CODE.md:641`](WAVESHARE_RUNNING_OUR_CODE.md)
"restored to the power-on default"). The same section records why that could
still be sitting there five days later: **loading a RAM image**
([`WAVESHARE_RUNNING_OUR_CODE.md:646`](WAVESHARE_RUNNING_OUR_CODE.md)
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
([`WAVESHARE_RUNNING_OUR_CODE.md:633`](WAVESHARE_RUNNING_OUR_CODE.md)
"Booting the vendor firmware restored"). That is S13, and it stands. What the
2026-08-28 residue cannot do is corroborate it.

**The shake run's own restore is unverified, and the state it left is
`UNKNOWN`.** Its capture ends at `t=158` with no restore block **and no END
banner**, where all three desk runs print `--- restored ---` and the banner
together (`pedo-run.log:286-290`, `pedo-run3.log:946-950`). It was cut cleanly:
`shake.log:239` is a complete `t=` line with **no traceback**, where
`walk.log:111-121` prints a `SerialException` when the device dropped off the
bus. A RAM app does not stop when its logger does, so two readings survive and
this session cannot choose between them — the host tool was stopped and the
image ran on to its own loop bound, printing `--- restored ---` into a console
nobody read and leaving the `CTRL2 = 0x27, CTRL7 = 0x01, CTRL8 = 0x90` it found
at `shake.log:45`; or the part lost power, and the QMI8658 holds none of this in
non-volatile memory, so a power cycle clears it. The loop bound cannot break the
tie: the archive shows it moved inside the session — 240, then 900 — so the
corrected source cannot say where this run would have stopped. **Neither
reading leaves the engine armed at *this run's* `CTRL2 = 0x16, CTRL7 = 0x03,
CTRL8 = 0x90` of `shake.log:59` — but the first one leaves it armed at the
walk's.** What that branch restores is the `CTRL2 = 0x27, CTRL7 = 0x01,
CTRL8 = 0x90` of `shake.log:45`, and `CTRL8` bit 4 is `Pedo_EN` (§11.6) with
`CTRL7` bit 0 `aEN`: the same triple this report calls *"the walk probe's own
armed state"* above, and the same one it says the walk *"stayed armed"* in. So
the two readings differ in what the next `--- before ---` prints, not only in
provenance — **under the first the part has been counting since 15:08 and prints
a nonzero found count with no walk behind it; under the second it prints `0`
with the engine off.** This tree records no read of the part after 15:08. The
next operator to attach this unit takes the `--- before ---` line it prints as
the record, not this report.

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
which is right.** The sites below do not agree, and they are **not the whole
list** — [`PEDOMETER_PARTS.md:448`](PEDOMETER_PARTS.md),
[`WAVESHARE_RUNNING_OUR_CODE.md:299`](WAVESHARE_RUNNING_OUR_CODE.md),
[`MAGNETOMETER_RETROFIT.md:138`](MAGNETOMETER_RETROFIT.md),
[`HARDWARE_MATRIX.md:356`](HARDWARE_MATRIX.md) and
[`VERIFIED_FACTS.md:1561-1578`](VERIFIED_FACTS.md) name one number or the other
as well. Enumerating and reconciling them is #341's job, not this report's:

| Site | What it said on 2026-08-28 |
| --- | --- |
| [`WAVESHARE_RUNNING_OUR_CODE.md:329-331`](WAVESHARE_RUNNING_OUR_CODE.md) "document number of the Rev A datasheet is" | the number is `13-52-25`, **not** `13-52-27` |
| [`OPEN_QUESTIONS.md:90`](OPEN_QUESTIONS.md) "the Rev A document number is" | the same correction, in H14's tail |
| [`VERIFIED_FACTS.md:573-575`](VERIFIED_FACTS.md) "documents it fully" | `13-52-27` is QMI8658**C** Rev A, and it exists |
| [`VERIFIED_FACTS.md:577-579`](VERIFIED_FACTS.md) "documents the identical feature" | `13-52-25` is QMI8658**A** Rev A, and it exists too |
| [`VERIFIED_FACTS.md:1625`](VERIFIED_FACTS.md) "values for that byte" | `REVISION_ID = 0x7C` comes from `13-52-25` |
| [`pedometer-bench-2026-08-28/probe/pedo.c:8-13`](pedometer-bench-2026-08-28/probe/pedo.c) "actually read" | the probe now cites `13-52-27`, the paper this report read, and defers the number to #341 |
| the five archived captures — `shake.log:43`, `walk.log:43`, `pedo-run{,2,3}.log:32` | each prints `0x7C = QMI8658A 13-52-25 Rev A` as settled fact. **Immutable**: they are the run. The probe's label is corrected for the next capture |

A document numbered `13-52-27`, titled *QMI8658C Datasheet*, marked `Rev: A`,
demonstrably does exist — it is the one quoted here, and it reports `0x7C` on its
own register-description page. A paper numbered `13-52-25` has been read in
this tree too: [`PEDOMETER_PARTS.md:448`](PEDOMETER_PARTS.md) records its
chapter 11 *"Pedometer"* at pp. 64–66 with `STEP_CNT_LOW/MIDL/HIGH` at
`0x5A`–`0x5C`, `CTRL8.Pedo_EN` and both CTRL9 commands, and
[`MAGNETOMETER_RETROFIT.md:138`](MAGNETOMETER_RETROFIT.md) "Admissible here as evidence" gives its md5. **What
is `UNKNOWN` is which number names the Rev A part**, not what either paper
holds — the two records put the same chapter 11 in both, so no register below
turns on the number. This report therefore cites only the paper it read, and
the tree-wide reconciliation — including which document
the `0x7C` attribution at [`VERIFIED_FACTS.md:1561-1565`](VERIFIED_FACTS.md)
*"the datasheet with a pedometer in it"* actually came from — is
[#341](https://github.com/hleserg/Attadipa/issues/341), not this pull request.

**Resolved by #341, and the table above is the state it found.** Both Rev A
documents exist; the false correction — *"the Rev A document number is
13-52-25, not 13-52-27"* — is withdrawn at its two sites; and the `0x7C` is
attributed to `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A`, the paper this report
read, in `VERIFIED_FACTS`, `HARDWARE_MATRIX` and `WAVESHARE_RUNNING_OUR_CODE`
alike. Then `13-52-25` was fetched from the vendor's own published copy and
read directly, which **settles what this report left `UNKNOWN` and moots the
question it was asking**. Its md5 matches the one
[`MAGNETOMETER_RETROFIT.md:138`](MAGNETOMETER_RETROFIT.md) "Admissible here as evidence" already
recorded, and it gives **`REVISION_ID = 0x7C`** — the same byte as `13-52-27`,
in the same register-description section, with the same `0x68` in the same
register-map summary. `WHO_AM_I` and the product id are identical too. So the
`0x7C` in the five archived captures is attributable to either paper, and the
label they print is wrong only about which document, not about the byte.

Two consequences for this report. The attribution question above — *"which
number names the Rev A part"* — has no answer to find, because no register
distinguishes them; the schematic's `QMI8658C` is what picks `13-52-27`, and
[`VERIFIED_FACTS.md:583`](VERIFIED_FACTS.md) "no register tells them apart"
now carries that. And the passage where this report treated
`PEDOMETER_PARTS.md:448` as proof that `13-52-25` had been read for chapter 11
was right about the gap and wrong about the remedy: pp. 64–66 are indeed also
`13-52-27`'s, so the row proved nothing at the time — but the paper has since
been opened, and chapter 11 is there, on those pages, exactly as the row says.

## A caveat about the raw logs

The archived logs misdescribe their own configuration. **Five** `printf` labels
had gone stale — four in `shake.log`, three of them against the constants they
printed and one against the register list itself, and one in `walk.log` that
asserts hardware behaviour this report will not:

- the header prints `CTRL2 = 0x16 (+/-8 g, 62.5 Hz accel-only)`. `0x16` is
  **±4 g at 112.1 Hz in 6DOF** — wrong on both counts, though the register value
  itself was right. `aFS = 001` is ±4 g (`aFS = 010` would be ±8 g), and the
  resting triad at `shake.log:81` settles it independently of any label:
  `|(350, 24, -8257)| = 8264` LSB at 1 g is a sensitivity of **8192 LSB/g**, and
  would be an impossible **2.02 g** at 4096. That is a fact about the *silicon*,
  not about the divisor a `printf` used — those are the two different questions
  this report keeps apart.
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
- `walk.log:71-73` prints, as settled fact, *"a RAM image is not reset by losing
  the host, **the IMU keeps counting on battery**, and this loop picks up again
  when the cable comes back. **Restore runs at the end either way.**"* Both bold
  clauses are wrong to state that way. The first is **UNKNOWN** — this report
  says so itself, above: whether the unit stays powered off USB is the open
  precondition of the whole read-after-walk experiment. The second is falsified
  38 lines further down the same file: `walk.log:111-121` is the
  `SerialException`, no restore block was printed, and the engine was left armed
  — which is how the next run came to clear the walk's count. That header was
  written before the run as an expectation and never corrected afterwards.
- the banner prints *"Writes CTRL2/CTRL7/CTRL8/CTRL9 and CAL1..CAL4 on the IMU
  at 0x6B. Nothing else, on any device."* (`shake.log:39-40`) — and eleven lines
  later, at `shake.log:50`, the same run writes `CTRL3 = 0x36`. The omission is
  the run's, not the log's: the probe's own header carried it too, which is how
  it reached the capture. It is corrected in the probe, and
  [`HARDWARE_MATRIX.md:514`](HARDWARE_MATRIX.md) *"plus `CTRL3` in the shake run
  only"* is what states the run's real write set.
- the p2p header prints `ped_fix_peak2peak = 200 mg; ped_fix_peak = 100 mg`,
  which are the **datasheet's** numbers, while the engine had been configured
  with SensorLib's loosened 80/60 (≈78/59 mg). The header agreed with the `OVER`
  column's hard-coded bar and disagreed with the engine.

The four `shake.log` labels are fixed in the probe, and the threshold and the
`OVER` bar now come from the same constant; `walk.log`'s pre-run expectation is
corrected here rather than in the probe, because the probe no longer prints it. **No register or parameter value changed**, so the run above
stands exactly as recorded; where the log headers and this report disagree, this
report is correct.
