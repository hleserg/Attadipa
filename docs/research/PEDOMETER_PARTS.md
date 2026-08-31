# What each IMU actually does about steps

T-060, for [OD-6](OWNER_DECISIONS.md#od-6--the-watch-counts-steps-and-that-is-not-optional).
The pedometer is mandatory, so everything about how it gets built depends on
what the two parts can be asked to do. Read on 2026-08-22 from primary sources
in the order the GNSS tasks use — datasheet, then application note, then vendor
driver source, then vendor example — and every claim below says which tier it
came from.

**No hardware was involved.** `NOT EXECUTED — HARDWARE REQUIRED` for every
current figure, every timing, and every claim about what a real part does. A
datasheet is a promise, not a measurement.

**Updated 2026-08-22 (T-060a).** The BMA423 behavioural questions below were
`UNKNOWN` because revision 2.0 of the datasheet defers them to an application
note that Bosch's own site refuses to serve. They are answered now, and not
from the application note: **Bosch deleted the material from the datasheet
between revisions, and the earlier revision is still mirrored.** See
[§1.2](#12-the-behaviour-is-documented--in-a-revision-bosch-withdrew-supported) for the
provenance, and [VERIFIED_FACTS](VERIFIED_FACTS.md) for the fact itself. Every
BMA423 claim below now says which **revision** it came from, because for this
part that is not a formality.

---

## 0. The headline, before the detail

**The two boards are not in the same situation, and one of them may not be in
the situation its own datasheet used to describe.**

| | T-Watch S3 Plus — BMA423 | Waveshare AMOLED 2.06 — QMI8658 |
|---|---|---|
| A step counter in the part? | **Yes** — but the datasheet does not document it | **It depends which part**, and we do not know which |
| Counter width | **32-bit**, registers `0x1E`–`0x21` | **24-bit**, registers `0x5A`–`0x5C` — *on the variant that has it* |
| Where the behaviour is written down | **datasheet revision 1.1** — revision 2.0 deleted it and points at an application note Bosch will not serve | chapter 11 of the QMI8658**C** datasheet, and of **older revisions** of the QMI8658A one |
| The trap | the feature lives in a 6 144-byte blob the host uploads at every boot, and the current revision of the datasheet no longer describes what it does | **the QMI8658A datasheet revision D has deleted the pedometer entirely** — chapter, registers, feature list |

The second row is a hardware-variant problem of exactly the kind
[ADR-0003](../adr/0003-radio-not-lora.md) already exists for, arriving in a
different subsystem.

---

## 1. BMA423 — T-Watch S3 Plus

### 1.1 Does the part count steps itself? Yes.

- **The 32-bit counter is in the register map.** `STEP_COUNTER_0` … `_3` at
  `0x1E`, `0x1F`, `0x20`, `0x21`, each `RESET: 0x00`.
  *Source: BMA423 Data Sheet, revision 2.0, document `BST-BMA423-DS004-00`,
  August 2019, p. 53.* `SUPPORTED`.
- Bosch's own reference driver reads all four in one burst and assembles them
  little-endian into a `uint32_t`.
  *Source: `bma423_step_counter_output()`, Bosch BMA423 driver v1.1.4
  (`bma423.c`), reading `BMA4_STEP_CNT_OUT_0_ADDR = 0x1E`,
  `BMA423_STEP_CNTR_DATA_SIZE = 4`.* `SUPPORTED`.
- The first page of the datasheet lists *"Plug 'n' Play Step-Counter solution
  with watermark functionality"* under typical applications. `SUPPORTED`.
- **The counter is not the detector.** The two run in parallel and disagree:
  *"The step counter accumulates the steps detected by the step detector
  interrupt … There are situations when the step counting value is different
  than the sum of steps detected by the step detector."* The counter is
  *"optimized on high accuracy"*, the detector *"on low latency"*, and their
  interrupt outputs are **mutually exclusive** on the one status bit
  `INT_STATUS_0.step_counter_out`.
  *Source: revision 1.1, p. 33.* `SUPPORTED`.

### 1.2 The behaviour is documented — in a revision Bosch withdrew. `SUPPORTED`.

In **revision 2.0** (`BST-BMA423-DS004-00`, August 2019 — the revision
DigiKey serves and the one Bosch's product page links) each of the four
step-counter registers carries exactly one line of description:

> `DESCRIPTION: Application note – Wearable feature set`

That is the whole entry. The 101-page document defers the behaviour of its
headline feature to `BST-MAS-AN032`, and **`bosch-sensortec.com` answers HTTP
403** — twice on 2026-08-22, for both the datasheet and the note. Mouser, LCSC
and Octopart mirror only revision 2.0 or a product flyer.

**The material was not written for the application note. It was removed from
the datasheet.** Two earlier revisions carry a full *"Step Detector / Step
Counter"* chapter, a *"Minimum Bandwidth Settings"* section, the preset tables
and the per-field configuration list — pp. 32–37 — and the chapter is
byte-identical between them:

| Revision | Document number | Date | Step-counter chapter |
|---|---|---|---|
| 1.0 | `BST-BMA423-DS000-00` | Aug 2017 | **present**, pp. 33–37 |
| 1.1 | `BST-BMA423-DS000-01` | May 2019 | **present**, identical text |
| 2.0 | `BST-BMA423-DS004-00` | Aug 2019 | **removed**, replaced by a pointer |

Three months and a change of document-number series separate a datasheet that
documents the feature from one that does not.

**Provenance, because this is the whole basis of §§1.1, 1.4, 1.6, 1.7 and 1.9.**
Revision 1.1 was retrieved on 2026-08-22 from the mirror the Watchy project
publishes with its own hardware documentation:

- `https://watchy.sqfmi.com/assets/files/BST-BMA423-DS000-1509600-950150f51058597a6234dd3eaafbb1f0.pdf`
- SHA-256 `98b85747bd983435b2921266401cbeb095a57e2274b1f5c49f7f04145f22de04`, 2 363 646 bytes.

Revision 1.0 was retrieved from `opensourceinstruments.com/Electronics/Data/BMA423.pdf`
and used only to confirm the chapter is unchanged between the two.

**What this is, and is not.** It is a Bosch document, not a third party's
summary — the tier is *datasheet*, and it outranks the driver. It is **not the
current revision**, so where revision 2.0 states a number (the electrical
tables in §1.6) revision 2.0 wins, and where revision 2.0 says nothing at all
revision 1.1 is the only Bosch statement there is. Nothing below is taken from
1.1 where 2.0 contradicts it; there is no such case.

The application note may still add material — Bosch's own text calls the
preset table a starting point and sends sensitivity tuning to a field
application engineer. Getting it stays open, but it is no longer a blocker:
**T-060a is closed and the residue is filed as T-060b, `nice-to-have`.**

### 1.3 The feature is a 6 144-byte blob the host uploads. `SUPPORTED`.

This is the single most consequential fact for firmware structure.

- `BMA4_CONFIG_STREAM_SIZE = 6144`. The blob is a `const uint8_t` array in the
  driver, streamed to `BMA4_FEATURE_CONFIG_ADDR = 0x5E` in chunks.
- The upload sequence, from `bma4_write_config_file()`, is not optional and not
  reorderable: disable advanced power save → wait 1 ms → clear
  `BMA4_INIT_CTRL_ADDR` → stream 6 144 bytes → set init control to `0x01` →
  **wait 150 ms** → read `BMA4_INTERNAL_STAT` (`0x2A`) and require
  `BMA4_ASIC_INITIALIZED` (`0x01`) → re-enable advanced power save.
- **150 ms of the boot budget** belongs to this, every time. It is not a retry
  path or a slow case; it is the normal one, and the driver's own comment sends
  the reader to the datasheet for why.
- A soft reset — `0xB6` to `BMA4_CMD_ADDR` (`0x7E`) — **does not preserve it**:
  *"Initialization has to be performed as well after every POR or soft reset"*,
  and *"The softreset performs a fundamental reset to the device which is
  largely equivalent to a power cycle."* So the 150 ms is paid again on every
  reset, not only at boot. `SUPPORTED` — *revision 1.1, §4.2 and the `CMD`
  register description.* Bosch's driver re-uploading after reset is now a
  driver agreeing with the datasheet rather than the only evidence.
- **Reconfiguring one feature means rewriting all of them.** The feature block
  is not addressable field by field: the host burst-reads the whole
  `FEATURES_IN` area from `0x5E`, modifies its copy, and burst-writes it back.
  Bosch's `bma423_step_counter_set_watermark()` does exactly this — reads
  `BMA423_FEATURE_SIZE` bytes, sets the bits, writes them all back. Two
  subsystems changing two different features are therefore **not independent**,
  and the driver in `platform/` owns the block. `SUPPORTED`.

*Source: Bosch BMA423 driver v1.1.4, `bma4.c` / `bma4_defs.h`; BMA423 Data
Sheet revision 1.1, pp. 31, 35 and 82.*

### 1.4 The watermark, and the factor of twenty. `SUPPORTED` — **corrected**.

- `BMA423_STEP_CNTR_WM_MSK = 0x03FF` — a **10-bit** field, so 0 … 1023.
- **The field is not a step count.** It *"holds implicitly a 20x factor, so the
  range is 0 to 20460, with resolution of 20 steps"*. A written value of 10
  raises `INT_STATUS_0.step_counter_out` every **200** steps, and because
  *"the steps are buffered internally, the output may be triggered between
  200-210 steps"* — the interrupt is a floor, not an equality.
  *Source: revision 1.1, p. 36.*
- **Bosch's driver does not apply the factor.** `bma423_step_counter_set_watermark()`
  writes its `uint16_t` argument straight into the field with
  `BMA4_SET_BITS_POS_0`. Whatever the caller passes is the raw hardware value.
  *Source: `bma423.c` v1.1.4, l. 1049.*
- **Value 0 is not "every step"**: *"If 0, the Step Counter watermark is
  disabled and Step Detector enabled."* The detector is a separate feature with
  its own enable bit (`BMA423_STEP_DETECTOR_EN_MSK = 0x08` versus
  `BMA423_STEP_CNTR_EN_MSK = 0x10`) and it fires **once per step**.
- **This corrects what this document said before T-060a.** LilyGo's board
  support calls `setStepCounterWatermark(1)`
  (*source: `LilyGoWatchS3.cpp`*), which was read here as an interrupt per
  step. It is not: it is an interrupt every **20** steps, ×20 being applied by
  the sensor. The vendor default is an order of magnitude cheaper than this
  document claimed, and the arithmetic T-061 has to do is correspondingly
  different. It is still a vendor decision we should make on purpose — 20 steps
  is roughly every 15 seconds at a normal walking cadence, and the counter is
  readable at any time without an interrupt at all.

The four enable bits, all inside `FEATURES_IN.step_counter.settings_26`:

| Field | Effect |
|---|---|
| `en_counter` | the accumulating 32-bit counter |
| `en_detector` | one interrupt per detected step |
| `en_activity` | walking / running / still, in `ACTIVITY_TYPE.activity_type_out`; **requires `en_counter`** |
| `reset_counter` | zeroes the accumulator, then *"the value of this flag is automatically reset and counting is restarted"* |

*Source: revision 1.1, pp. 34–37; Bosch BMA423 driver v1.1.4, `bma423.h`.*

### 1.5 Interrupt lines, and the constraint that is already fixed. `SUPPORTED`.

`bma423_map_interrupt()` takes `BMA4_INTR1_MAP` (0) or `BMA4_INTR2_MAP` (1).
The step-counter interrupt is `BMA423_STEP_CNTR_INT = 0x02`.

On the T-Watch S3 Plus, **INT1 reaches GPIO 14 and INT2 is bonded out but not
routed** — R12 and R15 are not fitted
([HARDWARE_MATRIX](HARDWARE_MATRIX.md), `VERIFIED`). So there is exactly one
interrupt line, and LilyGo's board support already spends it on six features at
once: step counter, any-motion, no-motion, activity, tilt and wake-up, all
mapped to the same pin, with the handler reading `readIrqStatus()` to find out
which fired.
*Source: `LilyGoWatchS3.cpp`.*

**Consequence for T-061:** any design needing a private interrupt for steps is
already impossible on this board. The step-counter interrupt shares a line, and
whatever else is enabled shares it too.

### 1.6 Current. `SUPPORTED` for the part, `UNKNOWN` for the feature.

From the datasheet's electrical characteristics (typical, nominal VDD/VDDIO,
25 °C):

| | Typical |
|---|---|
| `IDD` — performance mode, ±4 g | **150 µA** |
| `IDDlp1` — low-power mode, 50 Hz ODR | **14 µA** |
| `IDDsum` — suspend | **3.5 µA** |
| Power-up time `ts_up` | 1 ms |

And the low-power table, which the datasheet itself marks *"based on limited
lab measurements. Only for reference"* — so these are `ESTIMATED`, in the
datasheet's own words, not `MEASURED`:

| ODR | no averaging | avg 2 | avg 4 |
|---|---|---|---|
| 12.5 Hz | 6 µA | 7 µA | 9 µA |
| 25 Hz | 8 µA | 11 µA | 14 µA |
| 50 Hz | 13 µA | 18 µA | 27 µA |
| 100 Hz | 22 µA | 32 µA | 51 µA |
| **200 Hz** | **42 µA** | 60 µA | 97 µA |

LilyGo's example comments its `configAccelerometer()` default as *"4G, 200HZ"*
— a comment in a vendor example, the weakest tier, and the exact library that
sets it (`SensorLib`'s `SensorBMA423`) is not in the pinned upstreams, so the
default is **not confirmed**. At 200 Hz the difference between low-power and
performance mode is 42 µA against 150 µA, on a 940 mAh cell, continuously.

**Answered by revision 1.1 — the step counter runs in low-power mode, and
50 Hz is the floor.** `SUPPORTED`.

> *"If Performance Mode is disabled (`ACC_CONF.acc_perf_mode` is `0b0`) (device
> in low power mode), then the minimum ODR setting must comply with the
> following restrictions: 1. The ODR must be set to minimum 50 Hz for the most
> features except Double Tap/Tap. 2. The ODR must be set to minimum 200 Hz for
> the use of Double Tap/ Tap feature."*

and, separately:

> *"The Features (algorithms) have as input data the acceleration samples,
> which are acquired at 50Hz."*

*Source: revision 1.1, p. 32.*

Three consequences, and they are the ones T-061 needs:

1. **The budget line for step counting is the 50 Hz low-power figure — 13 µA**
   (`ESTIMATED`, from the low-power table above; the electrical-characteristics
   table gives `IDDlp1` = **14 µA** at the same 50 Hz, so call it *13–14 µA
   `ESTIMATED`* and never a measurement). Not 42 µA, and not 150 µA.
   Performance mode buys the step counter nothing: above 50 Hz the feature
   engine ignores the extra samples.
2. **A violation is detectable, not silent.** `INTERNAL_STATUS.odr_50hz_error`
   reads *"The minimum bandwidth conditions are not respected for the features
   which require [50 Hz]"*, and `odr_high_error` is its tap-detection twin. The
   driver can assert on this at bring-up instead of shipping a pedometer that
   quietly counts nothing. *Source: revision 1.1, register `0x2A`.*
3. **Tap detection and cheap step counting are in tension.** Double-tap needs
   200 Hz in low-power mode — 42 µA against 13 µA — so a design that wants
   both either pays 3× or uses performance mode. That is a T-061 decision with
   a number attached, which is what it did not have before.

### 1.7 Does it count while the host sleeps? **Yes, from the sensor's side.** `SUPPORTED`.

The sensor does not need the host. In low-power mode it duty-cycles itself —
*"the accelerometer regularly changes between a suspend power mode phase where
no measurement is performed and a performance power mode phase, where data is
acquired … The period of the duty cycle … will be determined by the output data
rate"* — the feature engine consumes that stream at 50 Hz, and
*"in all global power configurations both register contents and FIFO contents
are retained."* Nothing in the counting path is a host transaction.
*Source: revision 1.1, pp. 20–21 and §4.3.*

So the question is no longer about the sensor. **It is now entirely about the
board**, and it has two remaining halves, both `UNKNOWN` and neither answerable
from a Bosch document:

- **Does the BMA423's rail stay up when the SoC sleeps?** A property of the
  AXP2101 configuration on the T-Watch S3 Plus and of whatever powers the
  QMI8658 on the Waveshare board. [HARDWARE_MATRIX](HARDWARE_MATRIX.md) is the
  place for the answer; it does not have it yet. If the rail drops, the counter
  is not merely paused — the 6 144-byte blob is gone and §1.3's 150 ms is owed
  again on wake.
- **What does the I²C bus do across a sleep?** Register contents survive the
  sensor's own power states, but in low-power mode *"register writes need an
  inter-write-delay of at least 1000 µs"* — a constraint on the driver's
  resume path, not on counting.

One practical rule survives regardless of how those come back, and T-061 should
be built on it: **the accumulator is the truth and the interrupt is an
optimisation.** A host that reads `0x1E`–`0x21` on wake gets the right number
whether or not it saw a single interrupt, so no design should depend on having
been awake.

### 1.8 Overflow. `UNKNOWN`.

32 bits is 4.29 billion steps — roughly two million kilometres, so wrap is not
a practical concern for a wearer. What happens *at* the boundary is still
undocumented, and the honest answer for T-061 is that the firmware must treat
a decrease in the counter as **"the counter was reset or wrapped"** and never as
negative steps, regardless of which it was.

Revision 1.1 does not describe the boundary either, so this stays `UNKNOWN`
after T-060a. It is the one BMA423 question the withdrawn chapter did not
answer, and the rule above does not need it answered.

### 1.9 Three things the withdrawn chapter says that nobody was asking. `SUPPORTED`.

Each of these would have been discovered on hardware, expensively.

**The algorithm has a wrist preset, and it is already the default.**
`FEATURES_IN.step_counter.settings_1.param_1` … `settings_25.param_25` — 25
16-bit parameters — select a *phone* or a *wrist* platform, and
*"by default, the wrist configuration is available for use. If the platform is
a wrist operated device, then there is no need to overwrite the step counter
parameter values."* Both our boards are wrist devices, so **T-061 writes no
parameters at all** — which is the opposite of what a 25-value table suggests
on first sight. The wrist column, recorded so a future disagreement has
something to check against:

| | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **wrist** | 301 | 31700 | 315 | 31451 | 4 | 31551 | 27853 | 1219 | 2437 | 1219 | −6420 | 17932 | 1 |
| phone | 306 | 30950 | 132 | 27804 | 7 | 30052 | 32426 | 1375 | 2750 | 1375 | −5994 | 16879 | 1 |

| | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24 | 25 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| **wrist** | 39 | 25 | 150 | 160 | 1 | 12 | 15600 | 256 | 1 | 3 | 1 | 14 |
| phone | 12 | 12 | 74 | 160 | 0 | 12 | 15600 | 256 | 0 | 0 | 0 | 0 |

Changing them is a three-step dance and not a poke: *"1. Disable step counter,
step detector, and activity detection. 2. Modify the 25 parameters of step
counter. 3. Enable step counter, step detector, and activity detection."*
Sensitivity beyond the presets is explicitly *"with the support of the
corresponding field application engineer"* — i.e. not something to guess at.
*Source: revision 1.1, pp. 34–36.*

**Axis remapping applies to the features and not to the data.** The sensor can
be told which physical axis is which, but *"the axis remapping does apply only
to the data fetched into the Features. The `DATA_0` to `DATA_13` registers and
FIFO are not affected and should be accordingly remapped on the driver level."*
So the step counter and the tilt detector can be given the wrist orientation
while raw acceleration still arrives in the part's own frame — and a driver
that remaps only once has got one of the two wrong. Neither board's IMU
orientation is recorded yet; that is a `HARDWARE_MATRIX` gap, filed with T-061.
*Source: revision 1.1, pp. 32–33.*

**There is a fatal-error interrupt, and the only recovery is reinitialisation.**
*"The Error Interrupt signals that the sensor stopped after a fatal error. The
Device reinitialization must be done."* — `INT_STATUS_0.error_int_out`. A
pedometer that silently stops is exactly the failure OD-6 cannot tolerate, and
this is the bit that says it happened. Handling it means paying §1.3's 150 ms
again, at an arbitrary moment, so the capability has to have a state for
*recovering* and the UI a way to say so in human language.
*Source: revision 1.1, p. 31.*

---

## 2. QMI8658 — Waveshare ESP32-S3-Touch-AMOLED-2.06

### 2.1 The variant question comes first, and it is half answered.

[HARDWARE_MATRIX](HARDWARE_MATRIX.md) records the part as **"QMI8658 /
QMI8658C"**, and the Waveshare BSP does not drive the IMU at all
([VERIFIED_FACTS](VERIFIED_FACTS.md)), so there is no vendor code to read the
answer out of. But this repository is not silent on it:
[TAGS_TRACKS_RECKONING §2.2](TAGS_TRACKS_RECKONING.md) reports that **the
schematic names `QMI8658C` twice**, and that the datasheet the vendor wiki links
is byte-identical to the C document. That is evidence, and this section was
written without it.

So the A-versus-C question is **not** wide open: the schematic says C. What it
does not do is *prove* C — a schematic symbol is a drawing, silkscreen on a
2.5 × 3.0 mm LGA is unreadable, and a part substitution never reaches either.
`WHO_AM_I` at `0x00` on a powered board settles it in one transaction.

**The half that is genuinely open is which C document is the real one**, and it
is the half that decides whether the feature exists at all — see the note at the
end of §2.2. Both halves matter because the two variants differ on precisely the
feature OD-6 makes mandatory.

### 2.2 QMI8658C — the pedometer is documented and complete. `SUPPORTED`.

*Source: QMI8658C Datasheet, document `13-52-27`, Rev A, 20 June 2022, QST
Corporation. Chapter 11, and the register map in chapter 5.*

- Feature list, p. 1: *"Integrated Pedometer, Tap, Any-Motion, No-Motion,
  Significant Motion"*.
- **24-bit step count**, `STEP_CNT_LOW` `0x5A`, `STEP_CNT_MIDL` `0x5B`,
  `STEP_CNT_HIGH` `0x5C`, all read-only, all reset `0x00`. 16.7 million steps.
- Enable is `CTRL8` bit 4, `Pedo_EN`.
- Two CTRL9 commands: `CTRL_CMD_CONFIGURE_PEDOMETER` (`0x0D`) and
  `CTRL_CMD_RESET_PEDOMETER` (`0x0F`, *"clear the step count"*).
- **It runs off the accelerometer ODR** (`CTRL2.aODR`) and **only in
  Non-SyncSample mode** — §11 and §6.2. That is a constraint on the whole IMU
  configuration, not a pedometer setting.
- Eight tunable parameters, written through two CTRL9 calls via the `CAL1`–`CAL4`
  registers: sample window, peak-to-peak threshold, peak threshold, timeout,
  quiet time, an entry count, a precision field, and how often the output
  registers are updated.

Three of those parameters are worth carrying into T-061 because they change what
a step count *means*:

- **`ped_time_cnt_entry`** — steps are discarded until this many consecutive
  ones have been seen, then all of them are counted retroactively. QST's own
  example uses 10. So the counter is not a running total of detected steps; it
  is a total of steps the engine decided were walking.
- **`ped_sig_count`** — the output registers are updated **every N steps**, not
  every step. QST's example uses 4, so the register goes 10, 14, 18. **A read is
  always up to N steps stale by design.**
- **`ped_fix_peak2peak` / `ped_fix_peak`** — the thresholds, and the datasheet
  says outright that they should be chosen for the placement and the movement:
  *"if places QMI8658C in a watch or bend, running normally shows more
  significant peaks than walking."* Wrist-worn thresholds are a tuning job on
  real people, not a constant to copy.

With QST's example parameters at 50 Hz, the engine detects steps between **0.4 s
and 4 s apart** — 0.25 to 2.5 steps per second. Slower or faster is not counted.

**RESOLVED — the conflict this section used to run was between two revisions,
not two readings of one part.** This section reads document `13-52-27`
**Rev A, 20 June 2022**, in which chapter 11 is present and `CTRL8` bit 4 is
`Pedo_EN`. [TAGS_TRACKS_RECKONING §2.2](TAGS_TRACKS_RECKONING.md) once said
that the only obtainable C datasheet was Rev 0.6 of January 2021, marked
ADVANCE INFORMATION, whose `CTRL8` reads *"Reserved: Not Used"* — and therefore
that no hardware pedometer was documented on the C. **That sentence is
withdrawn there**, under [#341](https://github.com/hleserg/Attadipa/issues/341):
`13-52-27` Rev A is obtainable, it is what this section reads, and it
supersedes an ADVANCE INFORMATION draft from eighteen months earlier.
**H14 is closed** ([`OPEN_QUESTIONS.md:90`](OPEN_QUESTIONS.md) "RESOLVED").
What is still worth doing is unchanged and still cheap: writing `Pedo_EN` and
reading `0x5A`–`0x5C` on a powered board tells you what the silicon does,
whatever the paper says — and on 2026-08-28 that was tried and
[the step register never moved](PEDOMETER_BENCH_2026-08-28.md).

### 2.3 QMI8658A — the pedometer was documented, and then it was not. `UNKNOWN`, urgently.

| Document | Date | Pedometer |
|---|---|---|
| QMI8658A Datasheet, `13-52-25`, **Rev A** | 20 June 2022 | **Present**, with a caveat this row must carry. Feature list p. 1; chapter 11 "Pedometer" pp. 64–66; `STEP_CNT_LOW/MIDL/HIGH` at `0x5A`–`0x5C`; `CTRL8.Pedo_EN`; both CTRL9 commands. **Every one of those is also true of `13-52-27`, page numbers included** — its chapter 11 opens on p. 64 and §11.6 is on p. 66 — so this row never distinguished which of the two Rev A documents was open. It no longer needs to: `13-52-25` has been read directly (md5 `5a0fef65a358430d6499944a75d22e19`, 2026-09-01) and every entry in this cell is confirmed in it, on those pages. [#341](https://github.com/hleserg/Attadipa/issues/341) |
| QMI8658A Datasheet, `QST-PD-B002-22`, **Rev D** | current at 2026-08-22 | **Absent.** The feature list reads *"Integrated Tap, Any-Motion, No-Motion, Significant-Motion detection"* — no Pedometer. There is no chapter 11 on it, and a search of the whole document finds **no `STEP_CNT` register and no `Pedo_EN` bit at all** |

The feature has not been marked deprecated or reserved. It has been **removed
from the document**, registers included.

That leaves three possibilities and the sources do not choose between them: the
silicon changed; the feature was withdrawn from support but the registers still
respond; or Rev D is an editorial reorganisation and the omission is
unintentional. **Reading a step count out of a QMI8658A and believing it would
be depending on a feature its current datasheet does not admit to having.**

This is the reason T-060 was worth doing before T-061 rather than alongside it.

### 2.4 Current. `SUPPORTED`.

*Source: QMI8658C datasheet §3.8, tables 15 and 31; VDD = VDDIO = 1.8 V, 25 °C,
typical.*

Accelerometer only, gyroscope disabled — and note 12: **low-power mode is only
available when the gyroscope is disabled**, which for a pedometer is the
configuration anyway.

| Mode | ODR | Typical `IDD` |
|---|---|---|
| Low power | 3 Hz | **30 µA** |
| Low power | 11 Hz | **35 µA** |
| Low power | 21 Hz | **42 µA** |
| Low power | 128 Hz | **55 µA** |
| High resolution | 31.25 Hz | 132 µA |
| High resolution | 125 Hz | 134 µA |
| High resolution | 1000 Hz | 182 µA |

And the idle states, from the operating-modes table: power-on default **15 µA**,
low power (250 kHz clock) **8 µA**, power-down **6 µA** — power-down preserving
all configuration and output registers.

**The comparison, and the mismatch in it.** The BMA423 draws 13 µA at 50 Hz in
low-power mode; the QMI8658 draws 42 µA at 21 Hz. Those are **not the same duty**,
and the table above is the reason the honest figure cannot simply be quoted: 50 Hz
is a documented mandatory floor for the BMA423's step counter (§1.6), while
nothing in the QMI8658 datasheet establishes a floor for its pedometer — and
QST's own worked example in §2.2 runs that pedometer at **50 Hz**, not 21 Hz.

So the like-for-like figure is not published. Interpolating between the 21 Hz
(42 µA) and 128 Hz (55 µA) rows puts a 50 Hz QMI8658 somewhere around 45–47 µA —
`ESTIMATED` by linear interpolation between two datasheet rows, which is not a
method the datasheet endorses, and stated here only to show which way the error
runs. It runs **against** the QMI8658: matching the ODRs makes the gap wider, not
narrower.

The claim that survives is therefore the weaker one and the safe one: **the
Waveshare board pays at least three times the current of the T-Watch for step
counting**, before anything is known about whether its part has the feature at
all. A 6-axis IMU is not a cheaper accelerometer.

An earlier version of this paragraph presented 13 µA against 42 µA as "the same
duty". It was not, and the independent review on #43 caught it. Nothing about the
conclusion changes; what changes is that the number is no longer offered as a
measured comparison when one of its two halves is at the wrong ODR.

### 2.5 If the part turns out to have no pedometer

*Both of the hardware questions this section rests on are now tracked rather
than living in this prose: the QMI8658 variant is **H14** and the IMU axis
orientation is **H15** in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md). A fact that
lives only in a narrative paragraph is one T-061 can lose, which is the same
argument as "a fact that lives only in a chat log does not exist".*

Then OD-6 is met in firmware from raw accelerometer samples, and the numbers
above become the budget for it. The datasheet gives what that costs:

- a **1 536-byte FIFO**, so the host need not wake per sample;
- low-power ODRs of 3, 11, 21 and 128 Hz;
- the duty-cycle percentages in table 22 — 8.5 % at 3 Hz, 31 % at 11 Hz, 58 % at
  21 Hz, 100 % at 128 Hz.

What that does **not** give is the FIFO watermark behaviour in low-power mode or
the wake rate a step algorithm would actually need, which is a design question
and belongs to T-061.

---

## 3. What is still `UNKNOWN`, and where the answer is

| Question | Source that would answer it |
|---|---|
| ~~Every behavioural detail of the BMA423 step counter~~ | **Answered 2026-08-22 by datasheet revision 1.1**, which still contains the chapter revision 2.0 deleted. T-060a closed — §1.2 |
| BMA423 counter behaviour *at the 32-bit boundary* | not in revision 1.1 either. Application note `BST-MAS-AN032`, still HTTP 403 — **T-060b**, and §1.8's rule does not wait for it |
| Whether the BMA423's rail survives SoC sleep on the T-Watch, and the QMI8658's on the Waveshare | the AXP2101 configuration and the board schematics. **A board question, not a sensor one** — §1.7 |
| Which physical axis is which on either IMU | the schematics and the mechanical drawing. Needed because feature-axis remapping and data-axis remapping are separate — §1.9 |
| What `BST-MAS-AN032` adds beyond revision 1.1 — tuning guidance, accuracy claims | the note itself. `nice-to-have`; nothing in T-061 blocks on it |
| Which QMI8658 variant is on the Waveshare board | the board itself, or Waveshare's schematic for the revision we have. **Neither has been read** — the BSP does not touch the IMU |
| Whether a QMI8658A responds at `0x5A`–`0x5C` despite Rev D | the part, on a bench. `NOT EXECUTED — HARDWARE REQUIRED` |
| What `SensorBMA423::configAccelerometer()` actually configures | `SensorLib`, which is not among the pinned upstreams. One clone away |
| What either pedometer's accuracy is on a wrist | nobody's datasheet. People, walking, counted by hand |

---

## 4. What this means for T-061

Consequences only; the design belongs in the task and the ADR.

1. **`MotionSensing` cannot be one implementation.** One board has a documented
   hardware counter behind an uploadable blob; the other may have a hardware
   counter, or may need the whole algorithm in firmware. That is precisely the
   shape [ADR-0007](../adr/0007-two-capability-layers.md) exists for, and the
   application must not learn which it got.
2. **A step count is not a measurement, on either part.** The QMI8658C
   retroactively counts steps it had discarded, and updates its registers every
   N steps. Whatever the BMA423 does, it is a Bosch algorithm with parameters.
   The number is an *estimate produced by somebody else's filter*, and the trust
   language [ADR-0011](../adr/0011-gnss-integrity.md) uses for a position
   applies to it unchanged.
3. **A counter that goes down means reset or wrap, never negative steps.** Both
   parts have a command to clear the count, so this will happen.
4. **The T-Watch has one interrupt line and it is already shared six ways.** A
   design that needs a private interrupt for steps does not fit the board.
5. **150 ms of the T-Watch's boot belongs to a 6 kB blob upload**, and it must
   happen before any step is counted. A soft reset **does** drop it (§1.3), so
   every reset is a 150 ms hole in the day's total, and OD-6's *"no
   interpolation"* rule means the hole is reported and not filled. So is every
   recovery from `error_int_out`.
6. **The Waveshare board costs about three times the current for the same
   duty**, and that is before its variant question is settled.
7. **The BMA423 line in the power budget is 13–14 µA `ESTIMATED`, at 50 Hz in
   low-power mode** (§1.6). Anything faster is spent on something other than
   steps, and if double-tap is wanted the line becomes 42 µA. T-061 should say
   which it chose and why, rather than inheriting a vendor example's 200 Hz.
8. **The driver owns the whole feature block, not a field in it.** Read–modify–
   write of `FEATURES_IN` is not composable, so step counting, tilt and
   any-motion cannot be configured by three independent callers (§1.3).
9. **No step-counter parameters get written on either board.** The wrist preset
   is the default (§1.9). Writing the table anyway is 25 chances to be wrong
   about something that was already right.
10. **`odr_50hz_error` is a bring-up assertion, not a diagnostic.** It is the
    difference between a pedometer that is misconfigured and one that is
    misconfigured *and silent*.
