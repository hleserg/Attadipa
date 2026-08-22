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
| M1 | **AKM `AK09911` Short Datasheet**, `ShortDatasheet-E-00`, 2014/1, md5 `1d7e1960c86b2a1fb38ecc862196c4a7`. It is the right document for the ordered part — `AK09911C` is the only variant named, in the package line and again in the recommended-connection schematic. It is a *short* datasheet: it runs to §9 but contains **no register address map** (see §2.4) |
| M2 | **QST `QMC5883L` Datasheet Rev. B**, document `13-52-04`, `QST-PD-B002-22`, fetched from `qstcorp.com`, md5 `d13221b15c034c3f9b24befa48c8f4ab`. Rev B, not the Rev 1.0 that most of the internet mirrors |
| M3 | **QST `QMI8658C` Rev 0.6**, md5 `3d2bd7b24172e5d3448f2c9ecf2ef752` — the IMU already on the board, consulted for §5. Marked `ADVANCE INFORMATION — CONFIDENTIAL AND PROPRIETARY` on every page; it is a pre-release document |
| M5 | **QST `QMI8658A` Datasheet Rev A**, doc `13-52-25`, md5 `5a0fef65a358430d6499944a75d22e19`. Admissible here as evidence about **M3's own document lineage** and nothing else: its revision-history rows 0.4, 0.5 and 0.6 are *verbatim identical* to M3's, so it is the same document renamed at 0.7. Used only for what the vendor did to the documentation — **never** for an electrical characteristic, per §5.3 |
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

**3.3 V is inside the range**, and this needed correcting because I had it wrong
first: I called the part a low-voltage sensor in [#83](https://github.com/hleserg/Attadipa/issues/83)
before reading M1, on a half-remembered "AK0991x is 1.8 V". **M1 covers the
`AK09911C` — it is the only variant named, appearing in the package line (§1) and
in the recommended external connection (§7) — and specifies `VDD` 2.4–3.6 V. So
3.3 V is in range for the ordered part.** What the suffix `C` denotes is *not
stated by M1* and does not need to be; §8.1 Marking gives the die mark as
"Product name: 9911" with no suffix at all. Where the 1.8 V recollection comes
from is `UNKNOWN`, and inventing a part to explain a mistake is worse than the
mistake.

`VID` may be run below `VDD` if the host wants, but on this board there is no
reason to: everything else on the main I2C bus is at 3.3 V.

**The same power-state prohibition that §3.5 describes for the QMC5883L applies
here too**, and it is recorded in both places so that neither part looks like the
safe one. M1 §6.1: *"the transition from state 2 to state 3 and the transition
from state 3 to state 2 are prohibited"*, where state 2 is `VDD` off with `VID`
at 1.65–3.6 V and state 3 is `VDD` on with `VID` off. This is not a difference
between the two candidates.

### 2.3 Sensing

- **14-bit**, **0.6 µT/LSB** typical, range **±4900 µT** (M1 §2)
- Modes: power-down · single measurement · continuous 1/2/3/4 · self-test ·
  fuse-ROM access, selected by `CNTL2` `MODE[4:0]` (M1 §6.3)
- Continuous rates: `00010` → 10 Hz, `00100` → 20 Hz, `00110` → 50 Hz,
  `01000` → 100 Hz. Single measurement is `00001` and returns to power-down by
  itself, which is the mode a watch actually wants
- Magnetic sensor overflow monitor, and a `DRDY` status for data-ready
- **Time for one measurement `TSM` = 7.2 ms typ, 8.5 ms max** (M1 §5.3.3, analog
  circuit characteristics). This is the number §4.1 needs and an earlier draft of
  this document wrongly said M1 did not give

### 2.4 I2C

Standard and Fast mode; **High-speed mode reaches 2.5 MHz only with bus
capacitance ≤ 100 pF, and falls to 1.7 MHz at the 400 pF maximum** (M1 §5.3.4).
The headline "up to 2.5 MHz" in M1 §1 and the "maximum capacitive load 400 pF"
in M1 note 3 are both true and **cannot be had together** — a driver author who
reads them as one sentence will set 2.5 MHz on a shared watch bus that is
nowhere near 100 pF. Open-drain, external pull-ups required.

Address is strapped by the `CAD` pin, which the module breaks out:

| `CAD` | 7-bit address |
|---|---|
| `VSS` | `0b0001100` = **`0x0C`** |
| `VDD` | `0b0001101` = **`0x0D`** |

**`CAD` must be tied low.** See §4.3 — the other candidate part is at `0x0D` and
has no strap of its own, so `0x0C` is the only address that keeps both options
open.

**What M1 does not contain:** the register address map. M1 is not truncated — it
runs through §7 recommended connection, §8 package and §9 field-to-output-code —
it simply never prints register addresses. `WIA1`/`WIA2` company and device ID
values, the `ST1`/`ST2` status bits and the `HXL…HZH` data register addresses are
**`UNKNOWN` from a primary source**. Only the names `CNTL2`, `MODE[4:0]` and the
`SRST` bit appear, and never with an address. The full datasheet is available from AKM on request; failing
that, the Linux IIO driver `drivers/iio/magnetometer/ak8975.c` carries an
`AK09911` entry and is a defensible secondary source. **Do not** copy register
numbers out of an Arduino library without checking them against one of those two.

### 2.5 Package

`AK09911C` is an **8-pin WL-CSP (BGA), 1.2 × 1.2 × 0.5 mm** typical (M1 §1).

That is a wafer-level chip-scale ball grid array a bit over a millimetre on a
side, with eight balls. It is not hand-solderable in any ordinary sense. This matters for §6: if
the intention is to move the bare die off its breakout board and place it inside
the watch, this part is the harder of the two by a wide margin.

## 3. QMC5883L — the blue GY-271

### 3.1 What it is, and what it is *not*

**It is not an HMC5883L.** The GY-271 is the board sold for a decade as a
Honeywell HMC5883L breakout and populated with the QST part for most of that
time. Here the listing title says `QMC5883L` and the die marking in the photograph
reads `DA 5883`. M2 §3.2.3 documents a tracking code whose only fixed element is
a leading `D`, so the datasheet does not itself certify `5883` as part of the
mark — the honest statement is that **`DA…` is consistent with QST's scheme and
inconsistent with a Honeywell HMC5883L, which marks its parts `L883`**. Together
with a listing title that says `QMC5883L`, that is good enough to proceed on and
short of proof. It will be settled in one line the moment the part is on a bus:
`0x0D` answers, or it does not. But the consequence stands:
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
| Continuous, ODR 10 Hz | | 75 / **100** | | µA | M2 Table 2 |
| Continuous, ODR 50 Hz | | 150 / **250** | | µA | M2 Table 2 |
| Continuous, ODR 100 Hz | | 250 / **450** | | µA | M2 Table 2 |
| Continuous, ODR 200 Hz | | 450 / **850** | | µA | M2 Table 2 |
| Peak during measurement | | 2.6 | | mA | M2 Table 2 |
| Operating temperature | −40 | | 85 | °C | M2 Table 2 |
| POR completion `PORT` | | | 350 | µs | M2 Table 7 |

**Read the two current columns the right way round.** M2's condition column says
`Low/High Power Mode (OSR=64 or 512)`, so the *first* number is the low-power
setting and the second is high power — and **the reset default is `OSR=00`,
which is 512**, the highest oversample and the higher current (M2 §9.2.4 and
Table 16). Out of the box this part draws the **bolded** figure. Getting the
lower one means writing `OSR=11` (64), which widens the filter bandwidth and
raises in-band noise. It is available, it is not free, and it is not the default.

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
- Sensitivity tempco **±0.05 %/°C** over −40…85 °C — this is the magnetic
  sensitivity's drift
- **100 LSB/°C is a different thing**: M2 Table 2 names that row *"Temperature
  Sensor Sensitivity"*, and §9.2.3 confirms it is the scale factor of the `TOUT`
  registers at `0x07`–`0x08`. An earlier draft dropped the word "Sensor" and
  turned a temperature-readout gain into a magnetic drift figure. §9.2.3 also
  warns the temperature gain is factory-calibrated but the **offset is not**, so
  `TOUT` gives relative temperature, not absolute

### 3.4 I2C and registers

**Address `0x0D`** (`0b0001101`) — **no address-select pin**, confirmed by the
pin table, so nothing on the module can move it. M2 §5.4 calls it the *default*
and adds *"If other I2C address options are required, please contact factory"*,
which is not a route open to anyone buying a module from AliExpress. For our
purposes it is immovable; "fixed" was the wrong word for the right conclusion.

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

**And there is a typo in M2 that is dangerous specifically for §4.3's plan.**
M2's I2C read sequence figure prints the repeated-start address byte as
`0 0 0 1 1 0 0 1` = `0x19`. That is address **`0x0C`** with the read bit set. The
correct byte for `0x0D` is `0b00011011` = `0x1B`, which is what the write
sequences in the same section use (`0 0 0 1 1 0 1 0` = `0x1A`). A driver
transcribed literally from that figure reads from `0x0C` — **which, under the
recommendation in §4.3, is where the AK09911C lives.** Two magnetometers on one
bus and a datasheet figure that silently points at the wrong one is a debugging
session nobody should have to have twice.

Three further internal inconsistencies in M2, recorded rather than resolved, in
the same spirit as the `VDDIO` one in §3.2: Table 12 marks `0x0C` "Reserved"
while §9.2.2 opens by saying there are status registers at `0x06` **and `0x0C`**;
and §3.2.3's marking scheme fixes only a leading `D`, so the datasheet does not
by itself define `5883` as part of the die mark — see §3.1.

### 3.5 Package and the power-gating trap

**LGA, 3 × 3 × 0.9 mm** (M2 §3.2.2). Sixteen lands, **five** of them `NC`
(3, 5, 6, 7, 14) and two `GND`.

**What has to travel with the die if it is moved off the breakout**, which §6
contemplates and which an incomplete list would sabotage:

- **`C1` reservoir capacitor — nominally 4.7 µF ceramic, ESR under 200 mΩ**
  (M2 §4.4.3), with the datasheet's own warning that many 0402 parts will not
  meet the ESR and one should be prepared to up-size
- the **set/reset capacitor across `SETP`/`SETC`** — the AMR technology needs it
  and the Hall-effect AKM part has no equivalent
- **pin 4 `S1` tied to `VDDIO`** (M2 Table 5). Not a signal, not optional, and
  easy to lose because it is not a supply, a ground or a bus line

And the assembly constraint that undercuts the cheerful reading of "3 mm is
solderable": M2 §4.4.2 says in as many words **"Hand soldering is not
recommended"**, recommends a 4 mil stencil with 100 % paste coverage (§4.4.1),
and classifies the part **MSL 3**, requiring a bake before reflow unless it has
been kept below 10 % RH. Buying it already reflowed onto a GY-271 sidesteps all
of that. Taking it off again does not.

**The power-state trap** (M2 §5.2 and Table 6), stated precisely, because an
earlier draft overstated it. What the datasheet prohibits is the **direct
transition between state 2 and state 3** — state 2 being `VDD` off with `VDDIO`
up, state 3 being `VDD` up with `VDDIO` off. Going from *running* (state 4) to
state 2, i.e. **cutting `VDD` while the I2C rail stays up, is permitted**. It is
merely a bad idea: Table 6 describes state 2 as drawing *unpredictable leakage
current on `VDD` due to a floating node*, so the gating saves less than it
promises and by an amount the datasheet declines to bound.

The rule to carry forward is therefore narrower than "both rails together":
**never hop straight between the two single-rail states.** If both rails come
from one 3.3 V net, as they will on the breakout, none of this can arise.

**And this is not a difference between the candidates.** M1 §6.1 imposes the
identical prohibition on the AK09911C — see §2.2. Neither part is the safe one
here.

## 4. The comparison that actually decides it

### 4.1 Power, which is the one that matters

| At the same output rate | AK09911C | QMC5883L, default `OSR` / low-power `OSR` | Ratio |
|---|---|---|---|
| Idle / standby | 3 µA | 3 µA | — |
| Continuous 10 Hz | **≈ 220 µA** `ESTIMATED`, see below | 100 / 75 µA | ≈ 2–3× |
| Continuous 100 Hz | **2.4 mA** average | 450 / 250 µA | ≈ 5× at the default, ≈ 10× at `OSR=11` |

*(This table was itself wrong in the first draft, and the prose below it was
corrected without it. Both cells now agree with the paragraphs that follow.
A reader who reads a table and stops should not come away with the errors the
prose exists to kill.)*

**The QMC5883L is cheaper to run, and by how much depends on settings that must
be named.** An earlier draft of this section made two errors in the owner's
disfavour and both are corrected here.

**First, the QMC figure.** 250 µA at 100 Hz is the *low-power* `OSR` column and
the reset default is the other one, so the honest out-of-the-box comparison at
100 Hz is **2.4 mA against 450 µA — about 5×**, not 10×. Configuring `OSR=11`
gets 250 µA and about 10×, at the cost of filter bandwidth.

**Second, the AKM figure at a useful rate is computable after all.** M1 §5.3.3
gives `TSM` = 7.2 ms typ / 8.5 ms max, which the earlier draft wrongly said was
absent. That closes the model:

| | typ | worst case |
|---|---|---|
| Drive current `IDD2` | 3 mA | 6 mA |
| Measurement time `TSM` | 7.2 ms | 8.5 ms |
| At 10 measurements/s, plus `IDD1` idle | **≈ 0.22 mA** | ≈ 0.51 mA |

`3 mA × 7.2 ms × 10 s⁻¹ + 3 µA ≈ 219 µA`. **Labelled `ESTIMATED`, not
`MEASURED`.** The model is worth trusting this far because it reproduces AKM's
own published headline at the one rate they specify: `3 mA × 7.2 ms × 100 s⁻¹ =
2.16 mA` against a datasheet figure of 2.4 mA typ — the right answer with a
sensible margin for the digital block and the I2C traffic the model omits.

So at a watch-plausible 10 Hz the two parts are **≈ 220 µA against 75–100 µA**,
a factor of two or three — not an order of magnitude. The gap is a duty-cycle
artefact and it closes as the rate drops, which is exactly the direction a watch
wants to go.

Against a 400 mAh cell, 2.4 mA continuous is roughly 0.6 % of the pack per hour
for a compass alone — but nothing in this product needs a 100 Hz compass. At
10 Hz the same cell sees ≈ 0.05 % per hour from the AKM part and ≈ 0.02 % from
the QST one, and **at that point the magnetometer is not what is draining the
watch** ([BATTERY_UPGRADE](BATTERY_UPGRADE.md)). Choosing on current alone was
the wrong frame, and §8's recommendation is revised accordingly.

### 4.2 Everything else

The QMC5883L's range and its resolution are **two settings, not one part**. Its
`RNG` bit picks ±2 G or ±8 G, and quoting the fine quantisation of one next to
the wide range of the other describes a device that does not exist. The table
below keeps them apart.

| | AK09911C | QMC5883L at ±2 G | QMC5883L at ±8 G |
|---|---|---|---|
| Technology | Hall + concentrator | AMR with set/reset | same |
| Bits | 14 | 16 | 16 |
| Range | **±4900 µT** | ±200 µT | ±800 µT |
| Quantisation | 0.6 µT/LSB | 0.0083 µT/LSB | 0.033 µT/LSB |
| **Honest noise floor** | not specified | **0.2 µT** (2 mG) | not specified |
| Orthogonality spec | absent | 90 ± 1° | 90 ± 1° |
| Package | 1.2 × 1.2 × 0.5 mm WL-CSP | 3 × 3 × 0.9 mm LGA | same |
| Register map from a primary source | **no** (§2.4) | **yes** | **yes** |
| Self-test with internal source | **yes** | no | no |

Note what the ±2 G column costs: **±200 µT is only three to eight times Earth's
field**, which in a watch is not much headroom at all. The setting that gives
the QST part its resolution advantage is also the one most likely to saturate
next to a motor magnet, and the setting that survives the motor (±8 G) gives up
most of it.

Earth's field is 25–65 µT, so all three columns have range to spare *in free
air*. In a watch they are not in free air — §6. The AKM part's ±4900 µT is six
times the QST part's widest setting and twenty-four times its precise one, and
that is the axis on which it wins outright: it is far harder to saturate next to
a vibration motor, and it does not have to trade resolution away to get there.

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

## 5. The IMU probably cannot do this for us — one good reason, not two

> **This section was rewritten on 2026-08-22 after an adversarial re-read.** The
> first version gave two "independent, either alone fatal" reasons. The second
> one was an inference, it was wrong, and withdrawing it changes what the owner
> should do. The corrected version is below; the withdrawal is §5.3.

The QMI8658C has an I2C *master* interface for exactly this purpose — Mode 2,
"Mag Mode", in which the IMU reads the magnetometer itself and **time-aligns the
magnetometer samples with the accelerometer and gyroscope samples** (M3 §11.1,
paraphrasing *"To simplify data acquisition between the magnetometer and the
IMU, the QMI8658C can time align the magnetometer samples with the gyroscope and
accelerometer samples"*). For a fusion problem that is a genuine prize; sample
alignment is one of the things that makes nine-axis fusion hard on a host.

### 5.1 The reason that stands: the supported-device list

M3 §11.1, quoted in full this time — the earlier draft dropped the first word:

> *"**Currently** the QMI8658C can support the following magnetometers: AK09915C,
> AK09918CZ, and QMC6308."*

`CTRL4` `mDEV<3:0>` designates the device. **Neither ordered part is on that
list** — not the AK09911C, not the QMC5883L. Same two vendors, adjacent part
numbers, wrong parts.

Two honest qualifications on that sentence:

- **`mDEV` is four bits and M3 publishes no encoding for any of them.** The
  string `mDEV` occurs **once** in the whole document — the `CTRL4[6:3]` field
  definition with reset value `4'b0` — and the three part names occur once, in
  §11.1. Nothing maps a part to a code. **So Mag Mode cannot be driven from M3
  whichever magnetometer is fitted**, including the three it names. Table 18, the
  9DOF current figures, is `tbd` in every cell, which is what a feature looks
  like when it is announced and not finished.
- **"Currently" is the datasheet's word, and the answer to what happened next is
  not `UNKNOWN` — it is the opposite of what one would hope.** M5 is the same
  document renamed, and its revision history continues past where M3 stops:

  > Rev **0.8**, 10 Sep 2021 — *"…**deleted descriptions of magnetometer**, …
  > deleted the specifications, registers, and application diagrams that relative
  > to **I2CM interface**…"*
  >
  > Rev **0.95**, 6 May 2022 — *"…**remove CTRL4 & CTRL6**…"*

  The list did not grow. **The vendor withdrew the feature from the
  documentation**, register and all. Stated carefully, because §5.3 forbids
  carrying part characteristics across part numbers: this is evidence about
  **what QST documented**, not a claim that the C silicon lacks the block. The
  block may well be there. There is no published way to reach it and there is
  not going to be one.
- **M3 is marked `ADVANCE INFORMATION — CONFIDENTIAL AND PROPRIETARY`** and is
  Rev 0.6. It is the version Waveshare's own wiki links, which is why this
  repository uses it, but it is a pre-release document and the `CONFLICTING`
  I2C-address row in [HARDWARE_MATRIX](HARDWARE_MATRIX.md) already shows it
  disagreeing with later revisions.

### 5.2 A third path, named so it can be closed

M3 Table 30 documents `CTRL_CMD_I2CM_WRITE` (`0x06`, via `WCtrl9`), which
programs a device on the I2C master bus by writing `CAL1_[H,L]`, `CAL2_[H,L]`
and `CAL3_L`, with `I2CM_STATUS` at `0x2C` reporting completion. So a
**host-driven arbitrary I2C-master transaction does exist** and is not textually
gated on `mDEV`. It does not rescue us: M3 never publishes the I2CM sub-register
map that `CAL3_L` indexes, so the command cannot be issued from this document.
Recorded because "the datasheet lists three parts" and "the hardware can only
talk to three parts" are different claims, and only the first is supported.

### 5.3 The reason that was withdrawn

The first version of this section argued that Mode 2 needs pins `SDx` (2) and
`SCx` (3) as `MSDA`/`MSCL`, that M3 Table 2 requires those pins be **"Connect to
VDDIO or GND"** in Mode 1, and that a board using Mode 1 must therefore have
tied them off — making Mag Mode physically unreachable.

**That inference does not hold.** Mode 2 is entered *in firmware*: `CTRL7` bit 2
is `mEN`, reset default `0`, and `CTRL4` selects the device and rate (M3 Table
24). There is no pin that selects the mode. Table 2's "Connect to VDDIO or GND"
is design guidance for a board that will only ever use Mode 1 — it tells you what
to do with two pins you are not using — not a constraint that forecloses Mode 2
for a board that wires them.

So the honest status is: **`UNKNOWN` whether the Waveshare board leaves `SDx`
and `SCx` usable.** [HARDWARE_MATRIX](HARDWARE_MATRIX.md)'s IMU row records
`SDO/SA0` to GND and `CS` to `VCC3V3` from the schematic (S6) and is silent on
these two. A targeted re-read of the schematic would settle it, and unlike last
time it is worth doing, because §5.4 now depends on the answer.

*(A related claim that is true but not from M3: the QMI8658**A** Rev A datasheet
states there are internal 200 kΩ pull-ups on `SCL`, `SDA`, `CS`, `SDx`, `SCx`
and `RESV`. M3 Rev 0.6's note 1 attaches its 200 kΩ pull-up only to pin 1. Do not
carry a pin characteristic across part numbers on this project.)*

### 5.4 What this changes for the owner

**Put the magnetometer on the host I2C bus, and let the host read it and do the
fusion.** That has been the recommendation throughout and it is now the only one
this document supports.

This section has been wrong twice in opposite directions and the history is kept
because the second error was the more dangerous one:

1. **First draft:** "the door was never open, you could not have ordered better"
   — resting partly on a pin argument that does not hold (§5.3).
2. **Second draft:** having withdrawn that, it concluded the door *might* be
   open, and advised **considering an AK09918CZ or a QMC6308 in the next order**
   if the schematic showed `SDx`/`SCx` free. That advice was given to the owner
   in [#83](https://github.com/hleserg/Attadipa/issues/83) and it is **withdrawn
   here**. It would have had them spend money on a capability that no datasheet
   describes how to use.
3. **What holds:** no `mDEV` encoding is published for *any* part, and the vendor
   deleted the magnetometer and I2CM documentation outright at Rev 0.8 (§5.1).
   Buying a listed part would not open Mag Mode, because there is no documented
   way to select it. **Do not order a magnetometer on account of the IMU.**

The `SDx`/`SCx` schematic question stays `UNKNOWN` and is now merely
interesting rather than decisive: even with those pins free and a listed part
fitted, there is nothing to write to `CTRL4`.

The conclusion the owner should take away is the plain one: **the two parts in
the post are the right two parts to have**, they go on the host bus, and nothing
about the IMU argues for a third.

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
  ranging from ≈ 75 µA to 2.4 mA depending on the part, the output rate and the
  `OSR` setting (§4.1).
- `HARDWARE_MATRIX`'s "no magnetometer" rows stay true **for the board as
  shipped** and need a retrofit column, not a correction. A stock board still has
  no magnetometer, and the firmware must run on one.

## 8. Recommendation, and what would overturn it

> **Revised 2026-08-22 after the adversarial re-read.** The first version
> recommended the QMC5883L on two grounds that did not survive: "an order of
> magnitude of current" (it is 5× at the default `OSR`, and ~2–3× at a rate a
> watch would actually use — §4.1) and "a package a human can actually solder"
> (M2 §4.4.2: *"Hand soldering is not recommended"* — §3.5). Both corrections
> push the same way, so the recommendation is now weaker and more conditional.

**Fit the GY-271 (QMC5883L) first — as a module, not as a transplanted die.**
What survives of the case for it: a register map from a primary source, a
specified noise floor and a specified orthogonality where the AKM part specifies
neither, genuinely lower current, and — decisively for a first attempt — **it is
already reflowed onto a board with its reservoir and set/reset capacitors
fitted**. Nothing has to be got right about MSL 3 baking, 4 mil stencils or a
4.7 µF low-ESR part, because somebody already did it.

**Keep the CJMCU-9911 (AK09911C) and strap its `CAD` low** so both can be on the
bus at once. It wins on range without a resolution trade (§4.2), it has a
self-test with an internal magnetic source — worth real money on a hand-built
assembly — and if the QST part sits near overflow wherever it physically fits,
the AKM part is not a fallback but the answer.

**Do not plan on transplanting either die.** §3.5 lists what has to travel with
the QST part and §2.5 gives the AKM part's dimensions: 1.2 × 1.2 mm, eight balls,
wafer-level CSP. If neither module fits under the cover as a module, that is a
finding to report, not a soldering challenge to accept.

**Do not order a third part on account of the IMU.** An earlier revision of this
section suggested an AK09918CZ or a QMC6308 to unlock the IMU's Mag Mode. That
suggestion is **withdrawn** — §5.1 and §5.4. No datasheet publishes how to select
any magnetometer, and the vendor removed the feature's documentation entirely.
The two parts already ordered are the right two to have.

**What would overturn all of this: a measurement.** The field at the candidate
mounting position with the motor driven. It needs both parts and a board, it
takes minutes, and it decides the question — which is why nothing above §6
should be treated as settled until it exists.

---

*Facts here are datasheet-derived and marked with their source. Nothing has been
verified on hardware. See [VERIFIED_FACTS](VERIFIED_FACTS.md) for the standard
this has not yet met.*
