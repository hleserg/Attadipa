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

---

## 0. The headline, before the detail

**The two boards are not in the same situation, and one of them may not be in
the situation its own datasheet used to describe.**

| | T-Watch S3 Plus — BMA423 | Waveshare AMOLED 2.06 — QMI8658 |
|---|---|---|
| A step counter in the part? | **Yes** — but the datasheet does not document it | **It depends which part**, and we do not know which |
| Counter width | **32-bit**, registers `0x1E`–`0x21` | **24-bit**, registers `0x5A`–`0x5C` — *on the variant that has it* |
| Where the behaviour is written down | a **separate Bosch application note**, not the datasheet | chapter 11 of the QMI8658**C** datasheet, and of **older revisions** of the QMI8658A one |
| The trap | the feature lives in a 6 144-byte blob the host uploads at every boot, and a soft reset loses it | **the QMI8658A datasheet revision D has deleted the pedometer entirely** — chapter, registers, feature list |

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

### 1.2 And the datasheet does not say how it behaves. `UNKNOWN`, and this matters.

Each of the four step-counter registers carries exactly one line of
description in the datasheet:

> `DESCRIPTION: Application note – Wearable feature set`

That is the whole entry. **The 101-page datasheet documents the registers and
defers their behaviour to a separate document** — Bosch's *Wearable Feature
Set* application note, `BST-MAS-AN032`. Every question below that the driver
cannot answer is `UNKNOWN` because that document is where the answer lives.

`bosch-sensortec.com` returned **HTTP 403** to two attempts to retrieve it on
2026-08-22, so it has not been read. Getting it is a small, concrete task and
it is filed as T-060a.

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
- A soft reset — `0xB6` to `BMA4_CMD_ADDR` (`0x7E`) — returns the part to its
  power-on state. **Whether the uploaded feature blob survives it is the whole
  question**, and the datasheet does not answer it in the pages that mention
  either. Bosch's driver re-uploads after reset in its own initialisation path,
  which is behaviour rather than a statement. `UNKNOWN` — the application note.

*Source: Bosch BMA423 driver v1.1.4, `bma4.c` / `bma4_defs.h`.*

### 1.4 The watermark. `SUPPORTED`.

- `BMA423_STEP_CNTR_WM_MSK = 0x03FF` — a **10-bit** watermark, so 0 … 1023
  steps between interrupts.
- **Value 0 is not "every step"**; it selects the *step detector* interrupt
  instead, which is a different feature with its own enable bit
  (`BMA423_STEP_DETECTOR_EN_MSK = 0x08` versus
  `BMA423_STEP_CNTR_EN_MSK = 0x10`).
- LilyGo's own board support sets the watermark to **1**.
  *Source: `LilyGoWatchS3.cpp`, `sensor.setStepCounterWatermark(1)`.* That is an
  interrupt per step, which is a power decision made for us by a vendor and one
  we should not inherit without arithmetic.

*Source: Bosch BMA423 driver v1.1.4, `bma423.h`.*

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

**What is `UNKNOWN` and needs the application note:** whether the step counter
works at all in low-power mode, what ODR it requires, and what the feature adds
on top of plain acquisition. The datasheet's only statement is the marketing
line *"Low current consumption of data acquisition and all integrated
features"*, which is not a number.

### 1.7 Does it count while the host sleeps? `UNKNOWN`, and it is the question OD-6 turns on.

Everything above is consistent with a feature engine running on the sensor's
own ASIC and accumulating into registers the host reads later — that is what a
32-bit counter and a 1023-step watermark are *for*, and a counter that needed
the host awake would need neither. But *consistent with* is not *stated*, and
this document does not get to promote an inference to a fact. The application
note, again.

### 1.8 Overflow. `UNKNOWN`.

32 bits is 4.29 billion steps — roughly two million kilometres, so wrap is not
a practical concern for a wearer. What happens *at* the boundary is still
undocumented, and the honest answer for T-061 is that the firmware must treat
a decrease in the counter as **"the counter was reset or wrapped"** and never as
negative steps, regardless of which it was.

---

## 2. QMI8658 — Waveshare ESP32-S3-Touch-AMOLED-2.06

### 2.1 The variant question comes first, and it is not answered.

[HARDWARE_MATRIX](HARDWARE_MATRIX.md) records the part as **"QMI8658 /
QMI8658C"** — the board's own documentation does not pin the variant, and the
Waveshare BSP does not drive the IMU at all
([VERIFIED_FACTS](VERIFIED_FACTS.md)), so there is no vendor code to read the
answer out of. **Which part is on the board is `UNKNOWN` and is now a
first-order question**, because the two variants differ on precisely the feature
OD-6 makes mandatory.

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

### 2.3 QMI8658A — the pedometer was documented, and then it was not. `UNKNOWN`, urgently.

| Document | Date | Pedometer |
|---|---|---|
| QMI8658A Datasheet, `13-52-25`, **Rev A** | 20 June 2022 | **Present.** Feature list p. 1; chapter 11 "Pedometer" pp. 64–66; `STEP_CNT_LOW/MIDL/HIGH` at `0x5A`–`0x5C`; `CTRL8.Pedo_EN`; both CTRL9 commands |
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

**The comparison that matters:** the BMA423 draws 13 µA at 50 Hz in low-power
mode; the QMI8658 draws 42 µA at 21 Hz. The Waveshare board pays roughly three
times as much for the same duty, before anything is known about whether its part
has the feature at all. A 6-axis IMU is not a cheaper accelerometer.

### 2.5 If the part turns out to have no pedometer

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
| Every behavioural detail of the BMA423 step counter — power mode, required ODR, reset survival, sleep behaviour, overflow | Bosch **Wearable Feature Set** application note `BST-MAS-AN032`. The datasheet points at it by name for all four step registers. Returned HTTP 403 on 2026-08-22 — **T-060a** |
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
   happen before any step is counted. If a soft reset drops it — `UNKNOWN` — then
   every reset is a 150 ms hole in the day's total, and OD-6's *"no
   interpolation"* rule means the hole is reported and not filled.
6. **The Waveshare board costs about three times the current for the same
   duty**, and that is before its variant question is settled.
