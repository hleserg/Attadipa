# Adding a magnetometer to a board that shipped without one

> **Status:** datasheet research, 2026-08-22. Nothing here has touched hardware.
> The parts are ordered and have not arrived. Every electrical number below is
> quoted from a manufacturer datasheet identified by revision and md5; every
> number that would need a board to obtain is marked `UNKNOWN` and
> `NOT MEASURED`.
>
> **Owner decision this document rests on** ([#83](https://github.com/hleserg/Attadipa/issues/83),
> 2026-08-22): *two* modules were ordered, not one — a **CJMCU-9911** carrying an
> **AK09911C**, and a **GY-271** carrying a **QMC5883L**. The choice between them
> is still open and this document exists to inform it.

## 0. Why this document exists at all

`HARDWARE_MATRIX` has said "neither board has a magnetometer" since the survey,
and five epics were written around that being permanent. It is not permanent.
The owner is soldering one on, which turns a settled architectural constraint
back into an open question in at least four places — the heading ADR, the
capability registry, the compass epic and the power budget.

Two things follow, and they are not the same thing:

1. **The board gains a part.** That is a `boards/` question: a rail, a bus, an
   address, an axis frame.
2. **The system gains a capability it previously could not have.** That is a
   registry question, and it is the one that matters, because heading has *two*
   possible providers — a soldered magnetometer and an attached Attadipa node
   ([ADR-0009](../adr/0009-heading.md) §3) — and nothing above the capability
   registry may learn which one answered.

This document does the first. The second is a separate task.

## 1. Sources

| Key | Source |
|---|---|
| M1 | **AKM `AK09911` Short Datasheet**, `ShortDatasheet-E-00`, 2014/1, md5 `1d7e1960c86b2a1fb38ecc862196c4a7`. Names the `AK09911C` explicitly in the package line, so it is the right document for the C part — but see §2.4, it is a *short* datasheet and it stops before the register map |
| M2 | **QST `QMC5883L` Datasheet Rev. B**, document `13-52-04`, `QST-PD-B002-22`, fetched from `qstcorp.com`, md5 `d13221b15c034c3f9b24befa48c8f4ab`. Rev B, not the Rev 1.0 that most of the internet mirrors |
| M3 | **QST `QMI8658C` Rev 0.6**, md5 `3d2bd7b24172e5d3448f2c9ecf2ef752` — the IMU already on the board, consulted for §5 |
| M4 | Owner's photographs of both AliExpress listings, 2026-08-22 — silkscreen, pin labels and the die marking only. A photograph of a module is evidence about *labels*, not about *nets* |

## 2. AK09911C — the purple CJMCU-9911

### 2.1 What it is

A Hall-effect three-axis compass IC. Silicon monolithic with a magnetic
concentrator, on-chip oscillator, POR, and a self-test that uses an internal
magnetic source — so the part can prove it is alive without an external magnet,
which is worth having in a device that will be assembled by hand. (M1 §1, §2)

### 2.2 Electrical

| Parameter | Min | Typ | Max | Unit | Source |
|---|---|---|---|---|---|
| `VDD` (analog supply) | 2.4 | 3.0 | 3.6 | V | M1, DC characteristics |
| `VID` (interface supply) | 1.65 | — | `VDD` | V | M1, same |
| Absolute max on either | −0.3 | | +4.3 | V | M1, abs-max table |
| `IDD1` power-down | | 3 | 6 | µA | M1 |
| `IDD2` sensor driven | | **3** | **6** | **mA** | M1 |
| `IDD3` self-test | | 5 | 8 | mA | M1 |
| Average at 100 Hz | | **2.4** | | **mA** | M1 §1, §2 |
| Operating temperature | −30 | | +85 | °C | M1 §1 |

**3.3 V is inside the range.** The part is not a 1.8 V-only sensor — that is the
plain `AK09911` in some other packaging, and it is not what this is. `VID` may be
run lower than `VDD` if the host wants, but on this board there is no reason to:
everything else on the main I2C bus is at 3.3 V.

### 2.3 Sensing

- **14-bit**, **0.6 µT/LSB** typical, range **±4900 µT** (M1 §2)
- Modes: power-down · single measurement · continuous 1/2/3/4 · self-test ·
  fuse-ROM access, selected by `CNTL2` `MODE[4:0]` (M1 §6.3)
- Continuous rates: `00010` → 10 Hz, `00100` → 20 Hz, `00110` → 50 Hz,
  `01000` → 100 Hz. Single measurement is `00001` and returns to power-down by
  itself, which is the mode a watch actually wants
- Magnetic sensor overflow monitor, and a `DRDY` status for data-ready

### 2.4 I2C

**Standard, fast and high-speed to 2.5 MHz** (M1 §1), open-drain, external
pull-ups required, max 400 pF bus capacitance per line (M1 note 3).

Address is strapped by the `CAD` pin, which the module breaks out:

| `CAD` | 7-bit address |
|---|---|
| `VSS` | `0b0001100` = **`0x0C`** |
| `VDD` | `0b0001101` = **`0x0D`** |

**`CAD` must be tied low.** See §4.3 — the other candidate part is at `0x0D` and
has no strap of its own, so `0x0C` is the only address that keeps both options
open.

**What M1 does not contain:** the register address map. The short datasheet runs
to §6.3 and stops. `WIA1`/`WIA2` company and device ID values, the `ST1`/`ST2`
status bits and the `HXL…HZH` data register addresses are **`UNKNOWN` from a
primary source**. The full datasheet is available from AKM on request; failing
that, the Linux IIO driver `drivers/iio/magnetometer/ak8975.c` carries an
`AK09911` entry and is a defensible secondary source. **Do not** copy register
numbers out of an Arduino library without checking them against one of those two.

### 2.5 Package

`AK09911C` is an **8-pin WL-CSP (BGA), 1.2 × 1.2 × 0.5 mm** typical (M1 §1).

That is a wafer-level chip-scale ball grid array a bit over a millimetre on a
side. It is not hand-solderable in any ordinary sense. This matters for §6: if
the intention is to move the bare die off its breakout board and place it inside
the watch, this part is the harder of the two by a wide margin.

## 3. QMC5883L — the blue GY-271

### 3.1 What it is, and what it is *not*

**It is not an HMC5883L.** The GY-271 is the board sold for a decade as a
Honeywell HMC5883L breakout and populated with the QST part for most of that
time. Here the listing title says `QMC5883L` and the die marking in the
photograph reads `DA 5883` — QST's marking — and the two agree, so **this
particular purchase carries no bait-and-switch**. But the consequence stands:
the register map, the full-scale coding and the I2C address are all different
from Honeywell's, and every HMC5883L driver on the internet is the wrong driver.
Anisotropic magneto-resistive, not Hall — which is why it has a set/reset strap
and the AKM part does not.

### 3.2 Electrical

| Parameter | Min | Typ | Max | Unit | Source |
|---|---|---|---|---|---|
| `VDD` | 2.16 | | 3.6 | V | M2 Table 2 |
| `VDDIO` | 1.65 | | 3.6 | V | M2 Table 2 |
| Absolute max, either | −0.3 | | 5.4 | V | M2 Table 3 |
| Standby current | | **3** | | **µA** | M2 Table 2 |
| Continuous, ODR 10 Hz | | **75 / 100** | | µA | M2 Table 2, low / high `OSR` |
| Continuous, ODR 50 Hz | | 150 / 250 | | µA | M2 Table 2 |
| Continuous, ODR 100 Hz | | **250 / 450** | | µA | M2 Table 2 |
| Continuous, ODR 200 Hz | | 450 / 850 | | µA | M2 Table 2 |
| Peak during measurement | | 2.6 | | mA | M2 Table 2 |
| Operating temperature | −40 | | 85 | °C | M2 Table 2 |
| POR completion `PORT` | | | 350 | µs | M2 Table 7 |

**One internal inconsistency, recorded rather than resolved:** Table 2 gives
`VDDIO` min as **1.65 V**, and the pin table (M2 Table 5, pin 13) says
**"IO Power Supply (1.71V to VDD)"**. The same document, two numbers, 60 mV
apart. Irrelevant at 3.3 V — noted so that nobody rediscovers it and assumes one
of the two readings was a transcription error of ours.

### 3.3 Sensing

- **16-bit**. Full scale **±8 Gauss**; dynamic range programmable **±2 G or ±8 G**
- Sensitivity **12000 LSB/G** at ±2 G, **3000 LSB/G** at ±8 G (M2 Table 2)
- **Field resolution 2 mGauss** — standard deviation over 100 samples at ±2 G.
  This, not the LSB size, is the honest noise figure
- ODR programmable 10 / 50 / 100 / 200 Hz
- X-Y-Z orthogonality **90 ± 1 degree** — a specified error, which the AKM short
  datasheet does not give at all
- Sensitivity tempco ±0.05 %/°C; sensor sensitivity 100 LSB/°C over −40…85 °C

### 3.4 I2C and registers

**Address `0x0D`** (`0b0001101`), fixed — **no address-select pin** (M2 §5.4).

| Reg | Contents |
|---|---|
| `0x00`–`0x05` | `XOUT`/`YOUT`/`ZOUT`, LSB then MSB |
| `0x06` | status: `DOR`, `OVL`, `DRDY` |
| `0x07`–`0x08` | `TOUT[15:0]`, temperature |
| `0x09` | control 1: `OSR[1:0]` `RNG[1:0]` `ODR[1:0]` `MODE[1:0]` |
| `0x0A` | control 2: `SOFT_RST`, `ROL_PNT`, `INT_ENB` |
| `0x0B` | set/reset period `FBR[7:0]` — **datasheet says write `0x01`** (M2 §9.2.5) |
| `0x0C` | reserved, read only |
| `0x0D` | **Chip ID — returns `0xFF`** |

Note the coincidence before it costs somebody an hour: the device answers at
address `0x0D` *and* its identity register is at offset `0x0D`, and the value
that register returns is `0xFF` — which is also what an absent device looks like
on a floating bus. **`0xFF` is a valid ID here and simultaneously the classic
signature of nothing being there.** Probe by checking the address ACKs, not by
reading `0x0D` and comparing.

### 3.5 Package and the power-gating trap

**LGA, 3 × 3 × 0.9 mm** (M2 §3.2.2). Sixteen lands, four of them `NC`, two `GND`.
Needs a reservoir capacitor on `C1` and a set/reset capacitor across
`SETP`/`SETC` — those are on the breakout board and would have to be carried
across if the die is moved.

**The trap** (M2 Table 6, power states): with `VDD` at 0 V and `VDDIO` powered,
the device is off but draws *unpredictable leakage on `VDD` due to a floating
node*, and the datasheet says **transitions between that state and the
`VDD`-on/`VDDIO`-off state are prohibited**. In plain terms: **you may not
power-gate `VDD` alone while the I2C rail stays up.** In a watch that is exactly
the thing one is tempted to do — kill the sensor, keep the bus. Both rails go
together or neither does, and if both rails come from the same 3.3 V net (which
they will, on the breakout), the question never arises. It arises the moment
someone gets clever about the power budget.

## 4. The comparison that actually decides it

### 4.1 Power, which is the one that matters

| At the same output rate | AK09911C | QMC5883L | Ratio |
|---|---|---|---|
| Idle / standby | 3 µA | 3 µA | — |
| Continuous 10 Hz | *not specified separately* | **75 µA** | — |
| Continuous 100 Hz | **2.4 mA** average | **250 µA** (low `OSR`) | **≈ 10× worse** |

**The QMC5883L is about an order of magnitude cheaper to run**, and that is not
a close call. AKM specify only the 100 Hz average, so the 10 Hz comparison
cannot be made from primary sources — but the mechanism is not in doubt: the
AKM part draws 3 mA *while the sensor is driven* and the drive is what costs,
so duty-cycling helps it and single-measurement mode is how one would use it.
At a watch-plausible 10 Hz the gap should narrow. **How far it narrows is
`UNKNOWN` and cannot be computed from M1 without the per-measurement drive
duration, which M1 does not give.**

Against a 400 mAh cell, 2.4 mA continuous is roughly 0.6 % of the pack per hour
for a compass alone. That is a real number in a device whose whole power story
is still open ([BATTERY_UPGRADE](BATTERY_UPGRADE.md)).

### 4.2 Everything else

| | AK09911C | QMC5883L |
|---|---|---|
| Technology | Hall + concentrator | AMR with set/reset |
| Bits | 14 | 16 |
| Quantisation | 0.6 µT/LSB | 0.0083 µT/LSB at ±2 G |
| **Honest noise floor** | not specified | **0.2 µT** (2 mG) |
| Range | **±4900 µT** | ±800 µT (±8 G) |
| Orthogonality spec | absent | 90 ± 1° |
| Package | 1.2 × 1.2 × 0.5 mm WL-CSP | 3 × 3 × 0.9 mm LGA |
| Register map from a primary source | **no** (§2.4) | **yes** |
| Self-test with internal source | **yes** | no |

Earth's field is 25–65 µT, so both have range to spare *in free air*. In a watch
they are not in free air — §6. The AKM part's ±4900 µT is six times the QST
part's ceiling, and that is the one axis on which it wins outright: it is far
harder to saturate next to a vibration motor.

### 4.3 The bus, and why `CAD` goes to ground

The main I2C bus on the Waveshare board is already occupied
([HARDWARE_MATRIX](HARDWARE_MATRIX.md)):

| Address | Device |
|---|---|
| `0x34` | AXP2101 PMU — datasheet-fixed |
| `0x38` | FT3168 touch — driver source only, `LIKELY` |
| `0x6A` or `0x6B` | QMI8658 IMU — `CONFLICTING` across datasheet revisions |

**`0x0C` and `0x0D` are both free.** No conflict with anything already on the
board, for either candidate.

The conflict is between the two *candidates*: QMC5883L is `0x0D` and cannot move;
AK09911C can be `0x0C` or `0x0D`. **Strap `CAD` to `VSS`.** Then both parts can
sit on the same bus at the same time — which is not a hypothetical, because
having both present is the only way to compare them against each other in the
same magnetic environment, on the same wrist, in one sitting.

## 5. The IMU cannot do this for us

The QMI8658C has an I2C *master* interface for exactly this purpose — Mode 2,
"Mag Mode", in which the IMU reads the magnetometer itself and **time-aligns the
magnetometer samples with the accelerometer and gyroscope samples** (M3 §11.1).
For a fusion problem that is a genuine prize; sample alignment is one of the
things that makes nine-axis fusion hard on a host.

It is not available to us, for two independent reasons, either of which alone is
fatal:

**First, the part list is closed.** M3 §11.1: *"the QMI8658C can support the
following magnetometers: **AK09915C, AK09918CZ, and QMC6308**."* `CTRL4`
`mDEV<3:0>` designates the device from that list. **Neither ordered part is on
it** — not the AK09911C, not the QMC5883L. Same two vendors, adjacent part
numbers, wrong parts.

**Second, and this is the one that closes the door properly:** Mode 2 needs
pins `SDx` (2) and `SCx` (3) as `MSDA`/`MSCL`. In Mode 1 — the default, and what
this board uses — those same pins must be **"Connect to VDDIO or GND"** (M3
Table 2). They are tie-off pins on an LGA14 measuring 3 × 3 mm. If the board
ties them, and Mode 1 requires that it does, then Mag Mode is unreachable
without lifting two lands on a leadless package inside a watch.

> **`UNKNOWN`:** whether the Waveshare schematic routes `SDx`/`SCx` anywhere or
> ties them at the pad. [HARDWARE_MATRIX](HARDWARE_MATRIX.md)'s IMU row records
> `SDO/SA0` to GND and `CS` to `VCC3V3` from the schematic (S6) and is silent on
> these two. A targeted re-read would settle it. It changes nothing about which
> part to buy — see the first reason — but it should be recorded rather than
> assumed.

**Therefore: the magnetometer goes on the host I2C bus, the host reads it, and
the host does the fusion and the time alignment.** The owner did not order the
wrong parts; the option that would have preferred different ones was never
open. Worth writing down precisely so that nobody later reads "the IMU supports
an external magnetometer" and re-opens it.

## 6. Placement, which is the part that gets skipped

A magnetometer measures the field it is in. Inside a watch that field is mostly
not the Earth's.

Known magnetic and current sources on this board:

- **the vibration motor** — an AAC Technologies module at pads `P1`/`P2`
  ([HARDWARE_MATRIX](HARDWARE_MATRIX.md)). A linear resonant actuator or an ERM
  both contain a permanent magnet, and it moves
- **the speaker** — also a permanent magnet, also unavoidable
- **the battery and its return path** — a 3.3 V rail sourcing hundreds of
  milliamps through a loop generates a field proportional to the current and the
  loop area. Unlike the two above it changes with what the firmware is doing,
  which makes it the worst kind: a disturbance correlated with the application
- **the charge path** — largest currents in the whole device, and present exactly
  when the watch is stationary on a charger, which is when one would want to
  calibrate

None of this is measurable from a datasheet. What *can* be said now:

1. **Distance is the only free variable.** Dipole fields fall as 1/r³. Doubling
   the separation from the motor magnet cuts its contribution by eight.
2. **Saturation is a cliff, not a gradient.** Above full scale the reading does
   not degrade, it stops meaning anything — hence the overflow bits in both parts
   (`OVL` on the QMC, the overflow monitor on the AKM). Any driver must surface
   overflow as an *error state the application can see*, not silently pass the
   clipped number up. Per the Definition of Done, "errors handled in human
   language" — "compass unavailable near the motor" is a legitimate thing for a
   watch to say.
3. **The motor is knowable and therefore correctable.** Firmware knows when it
   drove the haptic. Heading samples taken during a buzz can be discarded rather
   than believed. That is a design constraint on whatever owns heading, and it
   belongs in the ADR, not in an application.
4. **The axis frame must be recorded, not guessed.** Both modules silkscreen an
   X/Y/Z arrow set; the board silkscreens its own beside the QMI8658
   (`HARDWARE_MATRIX` IMU row). Once the module is glued in at whatever angle it
   fits, the rotation between module frame and board frame is a fact about *this
   assembly* and has to be measured and written down, not inferred from a
   photograph.

> **`UNKNOWN` / `NOT MEASURED`, and every one of these needs the parts in hand:**
> the physical dimensions of both breakout boards; whether either fits under the
> cover as a module or must have its die transplanted; the field at candidate
> positions with the motor idle and driven; whether either part saturates there;
> the actual current at a watch-plausible duty cycle. The calipers already on
> order for [#64](https://github.com/hleserg/Attadipa/issues/64) answer the first
> two.

## 7. What this changes upstream

- [ADR-0009](../adr/0009-heading.md) assumes yaw is unobservable without an
  external node. It stops being true if this lands, and the ADR needs a
  superseding note — **not** an edit; the reasoning as written was correct for
  the hardware as it was.
- The heading capability gains a **second possible provider**. The registry
  already has to handle "an Attadipa node supplies it"; now it has to handle "the
  board supplies it", "a node supplies it", "both" and "neither", and nothing
  above the registry may learn which. Both-at-once is not a silly case: the node
  is not on the wrist and node orientation is not watch orientation.
- The power budget gains a consumer whose cost depends on a part not yet chosen,
  ranging over an order of magnitude (§4.1).
- `HARDWARE_MATRIX`'s "no magnetometer" rows stay true **for the board as
  shipped** and need a retrofit column, not a correction. A stock board still has
  no magnetometer, and the firmware must run on one.

## 8. Recommendation, and what would overturn it

**Fit the QMC5883L first**, on the strength of §4.1 — an order of magnitude of
current, a specified noise floor, a specified orthogonality, a register map from
a primary source, and a package a human can actually solder.

**Keep the AK09911C**, and strap its `CAD` low so both can be on the bus at once.
It wins on range (§4.2), and range is the axis most likely to matter next to a
motor magnet. If the QMC5883L turns out to saturate or to sit permanently near
overflow wherever it physically fits, the AKM part is not a fallback but the
correct answer, and the extra current buys a compass that works.

**What would overturn this:** a measurement. Specifically, the field at the
candidate mounting position with the motor driven. That is one measurement, it
needs both parts and a board, and it decides the question — which is why nothing
above §6 should be treated as settled until it exists.

---

*Facts here are datasheet-derived and marked with their source. Nothing has been
verified on hardware. See [VERIFIED_FACTS](VERIFIED_FACTS.md) for the standard
this has not yet met.*
