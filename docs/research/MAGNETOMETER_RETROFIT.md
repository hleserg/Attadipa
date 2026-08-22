# Adding a magnetometer to a board that shipped without one

> **Status:** research, 2026-08-22. **Nothing here has touched hardware.** The
> parts are ordered and have not arrived. Every electrical number is quoted from
> a manufacturer datasheet or application note identified by revision; every
> number that would need a board to obtain is marked `UNKNOWN` or `ESTIMATED`.
> No test in this document has been run: the correct label for all of them is
> **`NOT EXECUTED — HARDWARE REQUIRED`**.
>
> **Owner decision this document rests on**
> ([#83](https://github.com/hleserg/Attadipa/issues/83), 2026-08-22): *two*
> modules were ordered, not one — a **CJMCU-9911** carrying an **AK09911C**, and
> a **GY-271** carrying a **QMC5883L**. The choice between them is open, and
> this document exists to inform it rather than to pre-empt it.
>
> **Revision note, 2026-08-22.** Four adversarial re-reads were run against this
> document's working strands before it was assembled. Most of what they found
> was real, and the corrections are applied in place rather than appended.
> Where a correction reverses something this file said in an earlier committed
> revision, the reversal is marked in the text — §6.1 (the motor) and §5.4 (the
> battery) — because a reader who remembers the old sentence needs to be told it
> is gone. Where this document declines a refutation, it says which one and why:
> §5.6 and §11 (Q1).

---

## 0. The decision that comes first, and it is not the part number

**The first thing that has to happen is a measurement, and it needs neither part
soldered nor any firmware.** Locate the permanent magnet inside the back cover —
its **position and its axis orientation** — and map the field at the positions a
sensor could physically occupy.

Everything else in this document is downstream of that measurement:

- **Which part.** The two ordered candidates differ mainly in full-scale range
  (±4900 µT against ±200/±800 µT). Range only matters once the static offset at
  the mounting position is known. Until then the comparison has no denominator.
- **Whether the retrofit works at all.** If the magnet sits near the centre of
  the back cover, the achievable separation roughly halves, and the field at the
  best available position may exceed what the narrow-range part can read.
- **Whether the speaker should come out.** Removing it eliminates the dominant
  hard-iron source *and* frees the cavity that
  [#64](https://github.com/hleserg/Attadipa/issues/64) is trying to size. The
  two questions have one answer available to them, and #64 has already declared
  itself downstream of #83. The cost is `Capability::AudioOut` — or
  `Capability::Haptics`, if **T-105** finds the part is an actuator rather than
  a speaker, in which case this unit would have neither a motor nor an actuator.
  That is an owner decision. It is recorded here as a live option, not a
  recommendation.

**What this document does not say.** An earlier framing of this task expected
the answer to be *"there is nowhere in this case far enough from the speaker
magnet, and that is the headline."* **That conclusion is not supported, in
either direction.** The extrapolation it rested on is invalid over the distances
that matter (§6.2), and the honest bracket for the field at 20 mm spans two
orders of magnitude — from about 1.5 µT to about 293 µT. The low end is
comfortable and the high end is not. The bracket straddles the verdict, so the
verdict is not available from documents. It is available from an afternoon with
a compass needle.

**The corollary is the useful part: the measurement is cheap and it is not
blocked.** It does not need the modules to have arrived, it does not need a
soldering iron, and it does not need the calipers on order for #64. A compass
needle or a second magnetometer on a wire, moved over the closed back cover,
locates the magnet in minutes and establishes its axis at no extra cost.

### 0.1 What to do before the parts arrive

| # | Action | Blocked on |
|---|---|---|
| 1 | Locate the speaker magnet's **position and axis** through the closed back cover | nothing |
| 2 | Read [PMC9919430](https://pmc.ncbi.nlm.nih.gov/articles/PMC9919430/) Figure 6 and extract the measured values at z = 15, 20 and 25 mm | nothing — see §6.2 |
| 3 | Evaluate **WMM/IGRF** at the owner's coordinates for F, H, inclination and declination | nothing — see §8.5 |
| 4 | Settle **T-105** — speaker or haptic actuator | nothing; it is a listening and continuity test |
| 5 | Trace whether the `+3V3` expansion pad is always-on or `ALDO1`-switched | schematic re-read |
| 6 | Decide **T-097** (motor) and the magnetometer position **together** | owner |

Items 1–4 change the conclusions of this document. Items 5–6 change what can be
built on top of it.

---

## 1. Sources

| Key | Source |
|---|---|
| M1 | **AKM `AK09911` Short Datasheet**, `ShortDatasheet-E-00`, 2014/1, md5 `1d7e1960c86b2a1fb38ecc862196c4a7`. The right document for the ordered part — `AK09911C` is the only variant named, in the package line and again in the recommended-connection schematic. It is a *short* datasheet: it runs to §9 and contains **no register address map** (§4.4) |
| M2 | **QST `QMC5883L` Datasheet Rev. B**, `13-52-04`, `QST-PD-B002-22`, md5 `d13221b15c034c3f9b24befa48c8f4ab`. Rev B, not the Rev 1.0 most of the internet mirrors |
| M3 | **QST `QMI8658C` Rev 0.6**, md5 `3d2bd7b24172e5d3448f2c9ecf2ef752` — the IMU already on the board, consulted for §5.6. Marked `ADVANCE INFORMATION — CONFIDENTIAL AND PROPRIETARY` on every page; a pre-release document |
| M5 | **QST `QMI8658A` Datasheet Rev A**, `13-52-25`, md5 `5a0fef65a358430d6499944a75d22e19`. Admissible here as evidence about **M3's own document lineage** and nothing else: its revision-history rows 0.4, 0.5 and 0.6 are verbatim identical to M3's. Used only for what the vendor did to the documentation — **never** for an electrical characteristic |
| M4 | Owner's photographs of both AliExpress listings, 2026-08-22 — silkscreen, pin labels and die marking only. A photograph of a module is evidence about *labels*, not about *nets* |
| A46 | **NXP/Freescale AN4246 Rev 3/4.0**, *Calibrating an eCompass in the Presence of Hard and Soft-Iron Interference*, T. Ozyagcilar. The ten-parameter model |
| A47 | **NXP/Freescale AN4247 Rev 3/4.0**, *Layout Recommendations for PCBs Using a Magnetometer Sensor*. The keep-out and current-trace arithmetic |
| A48 | **NXP/Freescale AN4248 Rev 3**, *Implementing a Tilt-Compensated eCompass* |
| A49 | **NXP/Freescale AN4249 Rev 1.0**, *Accuracy of Angle Estimation in eCompass and 3D Pointer Applications*. The error law |
| S1 | **Kim et al., PMC9919430**, *High-Fidelity 3D Stray Magnetic Field Mapping of Smartphones*. Measured speaker stray fields |
| N1 | **NOAA NCEI**, World Magnetic Model — accuracy, limitations and error model; blackout and caution zones |
| E1 | **Espressif**, ESP32-S3 ULP-RISC-V API reference — RTC I2C pin restrictions |
| F1 | **xioTechnologies/Fusion** (MIT), `FusionAhrs.c` and `README.md` at `main` |
| P1 | **ArduPilot**, `libraries/AP_Arming/AP_Arming.cpp` and `libraries/AP_Declination/AP_Declination.h` at `master` |

---

## 2. The two ordered parts

### 2.1 AK09911C — the purple CJMCU-9911

A Hall-effect three-axis compass IC, silicon monolithic with a magnetic
concentrator, on-chip oscillator, POR, and a self-test driven by an **internal
magnetic source** — so the part can prove it is alive without an external
magnet, which is worth having on an assembly built by hand. (M1 §1, §2)

| Parameter | Min | Typ | Max | Unit | Source |
|---|---|---|---|---|---|
| `VDD` (analog supply) | 2.4 | 3.0 | 3.6 | V | M1, DC characteristics |
| `VID` (interface supply) | 1.65 | — | `VDD` | V | M1 |
| Absolute max on either | −0.3 | | +4.3 | V | M1 |
| `IDD1` power-down | | 3 | 6 | µA | M1 |
| `IDD2` sensor driven | | **3** | **6** | **mA** | M1 |
| `IDD3` self-test | | 5 | 8 | mA | M1 |
| Average at 100 Hz | | **2.4** | | **mA** | M1 §1, §2 |
| `TSM` one measurement | | **7.2** | **8.5** | ms | M1 §5.3.3 |
| Operating temperature | −30 | | +85 | °C | M1 §1 |

**3.3 V is inside the range**, and this needed correcting once already: the part
was called a low-voltage sensor in #83 before M1 was read, on a half-remembered
*"AK0991x is 1.8 V"*. M1 covers the `AK09911C` and specifies `VDD` 2.4–3.6 V.
Where the 1.8 V recollection came from is `UNKNOWN`, and inventing a part to
explain a mistake is worse than the mistake.

**Do not confuse this part with the `AK09918C`**, which is a different die with
four pins, `Vdd` 1.65–1.95 V and an **absolute maximum of 2.5 V**. The
expansion row supplies 3V3 and would destroy it. The two part numbers differ by
one character and by whether the retrofit survives being powered on.

Sensing: **14-bit, 0.6 µT/LSB typ, range ±4900 µT** (M1 §2). Continuous rates
10/20/50/100 Hz plus a single-measurement mode that returns to power-down by
itself, which is the mode a watch actually wants. Magnetic-sensor overflow
monitor and a `DRDY` status bit. **Noise floor and orthogonality are not
specified by M1.**

**Set/reset: none.** Hall plus concentrator, so there is no AMR strap to
degauss and nothing to re-magnetise in the AMR sense. The concentrator has its
own hysteresis, which M1 does not quantify: `UNKNOWN`.

**Package: 8-pin WL-CSP (BGA), 1.2 × 1.2 × 0.5 mm** (M1 §1). Not hand-solderable
in any ordinary sense; the module is the only realistic route.

**I2C.** Standard and Fast mode. The headline *"up to 2.5 MHz"* (M1 §1) and the
*"maximum capacitive load 400 pF"* (M1 note 3) are both true and **cannot be had
together**: 2.5 MHz holds only at ≤ 100 pF and falls to 1.7 MHz at 400 pF
(M1 §5.3.4). A shared watch bus is nowhere near 100 pF.

**What M1 does not contain: the register address map.** M1 is not truncated — it
runs through §7 recommended connection, §8 package and §9 field-to-output-code —
it simply never prints register addresses. `WIA1`/`WIA2`, the `ST1`/`ST2` status
bits and the `HXL…HZH` data registers are **`UNKNOWN` from a primary source**.
Only the names `CNTL2`, `MODE[4:0]` and `SRST` appear, and never with an address.
The full datasheet is available from AKM on request; failing that the Linux IIO
driver `drivers/iio/magnetometer/ak8975.c` carries an `AK09911` entry and is a
defensible secondary source. **Do not copy register numbers out of an Arduino
library without checking them against one of those two.**

### 2.2 QMC5883L — the blue GY-271

An AMR three-axis magnetometer with an on-chip set/reset strap driver.

| Parameter | Value | Source |
|---|---|---|
| `VDD` | 2.16–3.6 V | M2 |
| `VDDIO` | 1.65–3.6 V per Table 2; *"1.71 V to VDD"* per the pin table — the same document, two numbers 60 mV apart, recorded rather than resolved. Irrelevant at 3.3 V | M2 |
| Absolute max | 5.4 V either rail | M2 |
| Current, `ODR` 10 Hz | 75 µA low-power `OSR` / **100 µA default `OSR`** | M2 |
| `ODR` 50 / 100 / 200 Hz | 150/250 · 250/450 · 450/850 µA | M2 |
| Peak during measurement | 2.6 mA | M2 |
| Standby | 3 µA | M2 |
| Field resolution | **2 mGauss = 0.2 µT** (σ over 100 samples at ±2 G) | M2 |
| Orthogonality | 90 ± 1° | M2 |

**The reset default is `OSR = 00 = 512`, the *higher* current of each pair.** The
low-power column costs filter bandwidth and in-band noise; quoting it as the
part's current without saying so overstates the case for it.

**The identity trap, and it is this project's actual case.** The boards sold
worldwide as "HMC5883L breakout" (GY-271 / GY-273) are almost always populated
with QST's QMC5883L instead. Honeywell's part answers at `0x1E` with a
completely different register map; QST's answers at `0x0D`. A driver written
from the Honeywell datasheet finds nothing and looks like a wiring fault. The
module ordered here is the QST part (M4, die marking).

**A second trap on the same part.** The device answers at `0x0D`, **its chip-ID
register is also at offset `0x0D`, and that register returns `0xFF`** — which is
also what a floating bus reads as. **Probe by checking that the address ACKs,
never by reading the ID register and comparing.**

**Set/reset is period-register controlled, not automatic out of reset.** `FBR` at
`0x0B`; M2 says write `0x01`. A driver that omits it gets an uncalibrated offset
that wanders.

**Package: LGA-16, 3.0 × 3.0 × 0.9 mm.** M2 §4.4.2: *"Hand soldering is not
recommended."* It needs external `SETP`/`SETC` capacitors, which is why the
module — already reflowed with them fitted — is the only sane route.

**Range and resolution are two settings, not one part.** Quoting the fine
quantisation of one next to the wide range of the other describes a device that
does not exist:

| | AK09911C | QMC5883L at ±2 G | QMC5883L at ±8 G |
|---|---|---|---|
| Technology | Hall + concentrator | AMR + set/reset | same |
| Bits | 14 | 16 | 16 |
| Range | **±4900 µT** | ±200 µT | ±800 µT |
| Quantisation | 0.6 µT/LSB | 0.0083 µT/LSB | 0.033 µT/LSB |
| Stated noise floor | **not specified** | **0.2 µT** | not specified |
| Orthogonality spec | **absent** | 90 ± 1° | 90 ± 1° |
| Register map, primary source | **no** | **yes** | **yes** |
| Self-test, internal source | **yes** | no | no |
| Automatic degauss | n/a (Hall) | **no — register-enabled** | same |

**±200 µT is only three to eight times Earth's field.** In free air every column
has range to spare. In a watch they are not in free air (§6), and the setting
that gives the QST part its resolution advantage is the one most likely to
saturate next to a permanent magnet.

### 2.3 Range is three different numbers, and they are not interchangeable

A part chosen on full scale alone can be inside its range and outside its
specification. Three limits exist and only the first is usually quoted:

| Limit | Meaning |
|---|---|
| **Full-scale range** | above this the output clips |
| **Continuous-field limit** | above this the part is out of specification even though it still reads |
| **Disturbing field** | above this the *characteristics change* and a set/reset is required to restore them — so exceeding it invalidates the **stored calibration**, not merely the sample |

Worked on the contingency parts of §3.2: `MMC5603NJ` is ±30 G full scale, but
its datasheet says the external field on each axis should not be *continuously*
above 16 G, and specifies a 32 G disturbing field. `MMC5983MA` is ±8 G with a
10 G disturbing field. `LIS3MDL`'s zero-gauss offset degrades above a 50 G
disturbance. Neither ordered part publishes a disturbing-field figure in the
documents held here: `UNKNOWN` for both.

**A large static offset also costs resolution, independently of saturation.** It
forces the coarse full-scale setting, and resolution and noise are specified per
setting: the QMC5883L is 12000 LSB/G at ±2 G against 3000 LSB/G at ±8 G — four
times coarser quantisation. A47 §3 requires interference be removed *"to
accuracy better than 0.5 µT or one part in two thousand for 1000 µT
interference"*, and the range the offset forces is what decides whether that is
reachable.

---

## 3. Address collision check, and the shortlist

### 3.1 The bus, checked against seven addresses rather than six

The task brief and `HARDWARE_MATRIX`:327 both record six devices on the main
I2C bus with *"nothing collides and `0x6A` is free"*. **That sentence is one
scan away from being true and is not safe to rely on yet**, because
`HARDWARE_MATRIX`:318 records the IMU address as `CONFLICTING`: the schematic
prints `0x6B`, while QMI8658C Rev 0.6 — the PDF Waveshare's own wiki links —
maps `SA0`-low to `0x6A`; revisions 0.8/0.9/A say `0x6B`. Exactly one of the two
is occupied and **which is `UNKNOWN` until a bus scan runs**. The two lines in
`HARDWARE_MATRIX` disagree with each other and that should be reconciled there.

Every candidate below is therefore checked against the **superset**
`{0x18, 0x34, 0x38, 0x40, 0x51, 0x6A, 0x6B}`:

| 7-bit | Occupant |
|---|---|
| `0x18` | ES8311 audio codec |
| `0x34` | AXP2101 PMU — datasheet-fixed |
| `0x38` | FT3168 touch — driver source only, `LIKELY` |
| `0x40` | ES7210 ADC |
| `0x51` | PCF85063ATL RTC |
| `0x6A` **or** `0x6B` | QMI8658 IMU — `CONFLICTING` |

| Candidate | Address(es) | Clear? |
|---|---|---|
| **AK09911C** | `0x0C` (`CAD`→`VSS`) or `0x0D` (`CAD`→`VDD`) | **yes, both** |
| **QMC5883L** | `0x0D`, no strap pin, immovable | **yes** |
| MMC5603NJ | `0x30` | yes |
| MMC5983MA | `0x30` | yes |
| LIS2MDL | `0x1E` | yes |
| LIS3MDL | `0x1C` / `0x1E` | yes |
| IST8310 | `0x0C`–`0x0F` | yes |
| BMM350 | `0x14` / `0x15` | yes |
| BMM150 | `0x10`–`0x13` | yes |
| AK09918C | `0x0C` | yes — **but 3.3 V destroys it**, §2.1 |
| HMC5883L | `0x1E` | yes — legacy, and see §2.2 |
| QMC6310 | `0x1C` (U) / `0x3C` (N) | yes; `0x3C` is adjacent to `0x38`, not equal |
| **MLX90393** | ordering-code dependent | **`ABA-x14` = `0x18`–`0x1B` COLLIDES with ES8311.** Order `ABA-011` (`0x0C`–`0x0F`) |

**One real collision in the whole survey, and it is invisible unless the
ordering table is read rather than the "default address" line.** On the QFN-16
the `A0`/`A1` pins set only the two LSBs and cannot move the part out of the
block; the UTDFN-8 has no address pins at all.

**Two 7-bit/8-bit traps that are not collisions.** The IST8310 datasheet prints
both forms side by side: 7-bit `0x0C`–`0x0F` have 8-bit write forms
`0x18`/`0x1A`/`0x1C`/`0x1E`, and that leading `0x18` is **not** the ES8311's
7-bit `0x18`. Same arithmetic makes HMC5883L's published `0x3C`/`0x3D` an 8-bit
pair for 7-bit `0x1E`. Compare 7-bit to 7-bit or the scan looks like a conflict
that is not there.

**The only collision that matters here is between the two ordered parts.** The
QMC5883L is immovable at `0x0D`; the AK09911C can be `0x0C` or `0x0D`.
**Strap `CAD` to `VSS`.** That is the only configuration in which both modules
sit on the bus at once — which is not a nicety, because it is the only way to
compare them in the same magnetic environment, on the same wrist, in one
sitting.

### 3.2 Shortlist

**Primary tier — the two parts already bought.** They are the right two to have
and they bracket the axis that the §0 measurement resolves: the QST part is the
low-noise, narrow-range option whose useful setting is ±200 µT; the AKM part is
the wide-range option at ±4900 µT with no published noise floor and no register
map in its short datasheet.

**Contingency tier, and it is triggered by a specific finding, not by
preference.** A third part is justified only if the survey shows the QMC5883L
saturating or overflowing at every position that physically fits **and** the
AK09911C's missing primary register map blocks a driver. In that case a survey
of eleven further parts converges on two:

- **`MMC5603NJ` (MEMSIC)** — `0x30`, 3V3 direct, ±30 G, 20-bit, and the reason
  it heads the contingency list: **automatic on-chip SET/RESET degauss on every
  measurement**. In a case containing a permanent magnet, a sensor that must be
  *told* to degauss is a sensor whose offset you will chase. Its data-ready
  interrupt is I3C-only; over I2C you poll.
- **`LIS2MDL` (ST)** — `0x1E`, 3V3 direct, ±49.152 G, 50 µA in low-power mode at
  20 Hz, an internal set pulse every 63 ODR ticks by default, offset
  cancellation, and a real `INT`/`DRDY` pin.

Ruled out and why, so the ground is not re-covered: `BMM350` — the
best-performing part in the survey — has `VDD` 1.72–1.98 V and the expansion row
has no 1.8 V rail; `AK09918C` likewise, and 3.3 V destroys it. `LIS3MDL` has no
set/reset at all and is marked **Obsolete**; `BMM150` is **Obsolete**;
`HMC5883L` is legacy and its breakouts do not carry it. `IST8310`'s set/reset
needs an external 4.7 µF that will not travel with a transplanted die.
`QMC6310` and `QMC5883L` both have set/reset as a register you must enable.
`MLX90393` is the only genuinely hand-solderable part here (QFN-16 with wettable
flanks, 50 mT saturation onset) but quantises more coarsely than any AMR part
and carries the ordering-code collision above.

**More candidates were eliminated by the rail than by address conflicts.**
Twelve of thirteen parts were clear at every one of their available addresses.

### 3.3 The discriminator

**How large the static field is at the position the sensor can physically
occupy, and how fast residual magnetisation returns there.** That is the §0
measurement, and it decides both tiers at once:

- **If the field is a slowly-varying static offset** — likely at separations
  above roughly 10–15 mm, if the magnet is near an edge — then the narrow-range,
  low-noise part is the better instrument, and within the contingency tier
  `LIS2MDL` wins on current and on having a real interrupt pin.
- **If the sensor must sit close enough that the offset approaches the ±200 µT
  class**, the wide-range part is not a fallback but the answer, and within the
  contingency tier `MMC5603NJ` wins because it degausses on every sample.

**Run this with the two parts already on order.** Strap `CAD` low, put both on
the bus, and read them at the same positions in the same sitting.

---

## 4. Power, against a ~300 mAh cell

### 4.1 The sensor's own contribution

The fitted `402728` cell is labelled 400 mAh and **holds ~250–310 mAh; working
figure 300 mAh, `ESTIMATED`** ([BATTERY_UPGRADE](BATTERY_UPGRADE.md) §3, three
independent methods).

> **Correction to an earlier revision of this file.** §4.1 previously computed
> against "a 400 mAh cell". That is the label, not the capacity. Every figure
> below uses 300 mAh.

The AKM part publishes only a 100 Hz average, so a duty-cycle model is needed.
M1 §5.3.3 gives `TSM` = 7.2 ms typ / 8.5 ms max, which closes it:

`3 mA × 7.2 ms × 10 s⁻¹ + 3 µA ≈ 219 µA`

The model is worth this much trust because it reproduces AKM's own published
headline at the one rate they specify: `3 mA × 7.2 ms × 100 s⁻¹ = 2.16 mA`
against a datasheet 2.4 mA typ — the right answer with a sensible margin for the
digital block and the I2C traffic the model omits. **`ESTIMATED`, not
`MEASURED`.**

| At the same output rate | AK09911C | QMC5883L (default / low-power `OSR`) | Ratio |
|---|---|---|---|
| Idle / standby | 3 µA | 3 µA | — |
| Continuous 10 Hz | ≈ **220 µA** `ESTIMATED` | 100 / 75 µA | ≈ 2–3× |
| Continuous 100 Hz | **2.4 mA** | 450 / 250 µA | ≈ 5× default, ≈ 10× at `OSR=11` |

At a watch-plausible 10 Hz the gap is **a factor of two or three, not an order of
magnitude**. It is a duty-cycle artefact and it closes as the rate drops, which
is the direction a watch wants to go.

| Configuration | Current | mAh/day | %/day of 300 mAh |
|---|---|---|---|
| QMC5883L alone, 10 Hz low-power | 75 µA | 1.80 | 0.60 % |
| QMC5883L alone, 10 Hz default | 100 µA | 2.40 | 0.80 % |
| AK09911C alone, 10 Hz | 220 µA | 5.28 | 1.76 % |
| Tilt-compensated, cheapest (mag 50 + accel LP 42) | 92 µA | 2.21 | 0.74 % |
| Tilt-compensated, mid (mag 200 + accel HR 132) | 332 µA | 7.97 | 2.66 % |
| Full AHRS (6DOF 750 + mag 200) | 950 µA | 22.80 | 7.60 % |

QMI8658C figures, all at `VDD = VDDIO = 1.8 V` (M3, and note the document is
pre-release): accelerometer-only 42 µA at 21 Hz low-power, 132 µA at 31.25 Hz
high-resolution; **gyroscope-only 651 µA at 31.25 Hz, rising only to 689 µA at
1000 Hz** — essentially flat with rate, so it cannot be bought cheaply by
sampling slowly; 6DOF 750 µA at 31.25 Hz.

### 4.2 That arithmetic does not answer "is an always-on compass affordable"

**It answers a subsystem question and the product question is a system
question.** Two terms dominate and neither is in the table.

**The SoC floor.** `ESP32-S3` light sleep is **240 µA typ, plus 140 µA with 8 MB
octal PSRAM at 3.3 V** (`PUBLISHED-SPEC`,
[TAGS_TRACKS_RECKONING](TAGS_TRACKS_RECKONING.md):232; octal is `VERIFIED` at
`HARDWARE_MATRIX`:303, and the die's own eFuses agree). That is **≈ 380 µA =
9.12 mAh/day = 3.04 %/day**, four times the entire "cheapest" sensor figure,
before a single wake-up occurs.

**The display.** A compass is looked at with the screen on. The 410×502 AMOLED's
supply currents are **`UNKNOWN` and unmeasured** — `HARDWARE_MATRIX`:315 records
the CO5300 panel and no current figure exists anywhere in this repository. The
missing term is almost certainly the dominant one.

**So the honest answer to "can this device run an always-on tilt-compensated
compass" is `UNKNOWN` — `NOT EXECUTED, HARDWARE REQUIRED`.** The sensor's own
contribution is 92–332 µA `ESTIMATED`; the system cost is not known. This is the
same conclusion the owner already reached about the pedometer
([OWNER_DECISIONS](OWNER_DECISIONS.md)): the firmware-side sampling path costs
*"far more than the first, and how much more is a measurement nobody has
taken."*

### 4.3 There is no offload path on this board, and that is three findings agreeing

The standard escape from a wake-per-sample budget is to let a coprocessor or a
FIFO absorb the periodic read. **Neither is available here, and the third route
is closed too.**

1. **The ULP-RISC-V cannot reach the bus.** Espressif states for ESP32-S3 RTC
   I2C that *"the SDA pin can only be set up as GPIO1 or GPIO3 and SCL pin can
   only be set up as GPIO0 or GPIO2"* (E1). The main bus and the expansion pads
   are **SDA = IO15, SCL = IO14**. The ULP cannot service a magnetometer
   attached at the pads.
2. **Neither candidate has a FIFO.** The QMC5883L has none; the AK09911C has
   none. Of the thirteen parts surveyed, every one whose FIFO status was read is
   recorded "no FIFO". So there is nothing to batch into.
3. **The IMU's auxiliary I2C master is documentation-dead** — §5.6, established
   in this file before the question was asked.

**Net: every sample is a main-CPU wake, at 10–20 Hz, forever.** That is
unmeasured and it is the term most likely to decide the answer. It also
undermines the tidy 10:1 compass-versus-AHRS ratio in §4.1, because the QMI8658
*does* have a FIFO with a watermark interrupt while the magnetometer has
nothing — so the AHRS path can let the SoC sleep between drains and the
tilt-compensated path cannot. **Once wake-ups are counted the ordering is not
established.** The recommendation in §7.4 is made in spite of that, not because
the arithmetic supports it.

**One term nobody has costed at all:** shared-bus occupancy. A permanent 10–20 Hz
poll is a permanent reservation on a bus shared with the PMU, RTC, touch
controller and two audio devices, and `ARCHITECTURE.md` §8 already names
shared-bus ownership as the coordinator's first real job on these boards.

---

## 5. Placement

### 5.1 The error law, and the right denominator

**Heading is computed from the horizontal projection only.** A47 §2: *"The
magnitude of the earth's magnetic field ... varies over the surface of the earth
from a minimum of 22 μT over South America to a maximum of 67 μT south of
Australia"*, but *"the heading of an eCompass is determined from the relative
strengths of the two horizontal geomagnetic field components and these vary from
zero at the magnetic poles to a maximum of 42 μT over East Asia."*

A49 §1.3 gives the law: *"The amplitude of this compass heading error is equal to
the ratio of the square root of the sum of the squares of the residual hard-iron
error to the horizontal geomagnetic field component."* Eq. 29–30:
`ΔΨ_max = √(ΔVx² + ΔVy²) / (B cos δ)` radians.

**The exact peak deviation is `asin(ΔV / B_h)`, not `atan`.** The ratio form is a
small-angle approximation of it. The three agree closely at small ratios and
diverge where it matters most, so this table uses A49's own formula and gives
the exact value beside it:

| Residual | `B_h` = 20 µT | `B_h` = 10 µT |
|---|---|---|
| 0.5 µT | 1.43° | 2.87° |
| 1 µT | 2.87° | 5.73° (exact 5.74°) |
| 2 µT | 5.73° | 11.46° (exact 11.54°) |
| 4 µT | 11.46° (exact 11.54°) | 22.92° (exact 23.58°) |
| 5 µT | 14.32° | **28.65° (exact 30.0°)** |
| 8 µT | 22.92° (exact 23.58°) | 45.84° (exact 53.1°) |

A47's own accuracy criterion: *"the lowest value of the horizontal field strength
likely to be experienced by a smartphone user is 10 μT in northern Canada and
Russia. A compass heading accuracy of 0.05 radians or 3 degrees therefore
requires that the error in estimating the geomagnetic field be no more than
0.5 μT."*

> **`B_h` = 10 µT is A47's stated *lowest likely* value, not a typical one.**
> Using it as the working denominator roughly doubles every error figure
> relative to a mid-latitude European Russian location, where `F ≈ 52–53 µT` and
> inclination `≈ 71°` give `H = F cos I ≈ 17 µT`. **That reconstruction is
> `APPROXIMATE` and unverified** — it is offered as a check to run, not a fact to
> record. This document uses a 10–20 µT band and every angle in it is
> `ESTIMATED` until a WMM/IGRF evaluation at the owner's coordinates exists
> (§8.5, Q3).

**The "10 % of Earth's field" criterion in the task brief is the wrong criterion
and it is answered anyway in §5.3.** 10 % of a 50 µT *total* field is 5 µT, and
5 µT against a 10 µT *horizontal* field is a 28–30° heading error. The right
criteria are three, one per class of source, and they are in §5.5.

### 5.2 The speaker magnet — the dominant source, and the bracket is two orders wide

No datasheet exists for `AAC210602A1`, so its magnetic moment is `UNKNOWN`. The
only measured proxies available are smartphone speakers (S1), and **the
extrapolation from them does not survive scrutiny.**

S1 publishes **two anchors per speaker**: a `Bmax` at z = 1 mm and the z at which
the field falls to ≤ 1 mT.

| Speaker | `Bmax` @1 mm | ≤ 1 mT at | Implied exponent |
|---|---|---|---|
| iPhone 13 Pro, top | 12.7 mT | 5 mm | **1.58** |
| iPhone 13 Pro, bottom | 12.3 mT | 9 mm | **1.14** |
| iPhone 12, top | 32.9 mT | 10 mm | **1.52** |
| iPhone 12, bottom | 35.5 mT | 12 mm | **1.44** |

**The two anchors for the same speaker are mutually inconsistent under the 1/r³
law** that S1's own text invokes: solving each pair gives a decay exponent
between 1.14 and 1.58, not 3. Projected to 20 mm the two anchors for one speaker
disagree by 9.8× to 59.3×. Neither is right, because at z = 1 mm from a magnet
several millimetres across you are inside the near field, where a point-dipole
law does not hold at all.

Reconciling each pair with a source-centre depth `d` — i.e. treating the true
dipole as sitting `d` below the surface — gives:

| Speaker | fitted `d` | field at 20 mm |
|---|---|---|
| 13 Pro top | 2.0 mm | 32 µT |
| 13 Pro bottom | 5.1 mm | 178 µT |
| 12 top | 3.1 mm | 182 µT |
| 12 bottom | 3.8 mm | 293 µT |

Anchoring naively on the `Bmax` values instead gives **1.5–4.4 µT** at 20 mm.

**So the honest bracket at 20 mm is roughly 1.5 µT to 293 µT — two orders of
magnitude.** Any statement of the form "the keep-out is 20–30 mm" is a number to
one significant figure taken from a bracket that spans a hundredfold. It is not
available from this source.

Two further corrections to how this was first read:

- **The proxy is not "probably conservative".** Anchoring a dipole at a
  surface-referenced distance `s` when the true source centre is at `s + d`
  underestimates the moment by `(s/(s+d))³` and therefore **underestimates** the
  far field. The method is systematically non-conservative. The separate
  argument that a watch micro-speaker has a smaller moment than an iPhone's is
  directionally sound and does not license the word.
- **A dipole is not isotropic.** At equal `r` the axial field is exactly **twice**
  the equatorial field. Every distance here carries an unstated factor-of-two
  spread depending on which way the magnet's axis points — which is why §0 asks
  for the **orientation** as well as the position. The same sweep establishes
  both at no extra cost.

**The measurement that replaces all of this is in the cited paper and was not
read.** S1 3D-mapped both speakers at **z = 1, 5, 10, 15, 20 and 25 mm** and
plots the distributions in Figure 6. The running text reports only the 1 mm
`Bmax` and the ≤ 1 mT crossing, so the values at 15–25 mm must be read off the
figure — but they exist, at exactly the distances that were extrapolated to.
Reading Figure 6 collapses a 200× bracket to a measurement. **Item 2 in §0.1.**

**The gradient is the reason the sensor must be bonded rigidly, and it is the one
conclusion here that survives the whole bracket.** A dipole's gradient is `3B/r`.
On the worst bracket that is 100 µT/mm at 15 mm, 32 µT/mm at 20 mm, 13 µT/mm at
25 mm; on the mildest it is sixteen times smaller. Even at the mild end, 0.1 mm
of sensor movement at 20 mm is measurable against a 10–20 µT horizontal field.
**A sensor that can move relative to the magnet has no stable calibration.**

### 5.3 Current-carrying conductors — the arithmetic, and where it stops applying

A47 §6 Eq. 4: `B(r₀) = µ₀I / 2πr₀ = 0.2×10⁻⁶ · I / r₀` tesla. A47's own worked
example — *"At 10⁻³ m separation, the magnetic field from a 1A current trace will
be 200 μT reducing to 20 μT at 10⁻² m separation"* — is reproduced exactly by
this arithmetic, so the scaling is checked against the source and not only
against itself.

At **200 mA**, the charge current the owner intends to set:

| r | single conductor | 25 mm finite segment | closed 20 × 15 mm loop | tight pair, d = 1 mm |
|---|---|---|---|---|
| 1 mm | 40.0 µT | — | — | — |
| 5 mm | 8.00 µT | 7.43 µT | — | 1.60 µT |
| 10 mm | 4.00 µT | 3.12 µT | — | 0.40 µT |
| 20 mm | 2.00 µT | 1.06 µT | 1.50 µT | 0.10 µT |
| 30 mm | 1.33 µT | 0.51 µT | 0.44 µT | 0.044 µT |
| 40 mm | 1.00 µT | 0.30 µT | 0.19 µT | — |
| 80 mm | 0.50 µT | 0.08 µT | 0.02 µT | — |

**The single-conductor column is a worst case that stops being physical past
about 20 mm.** A47 states its own assumption: *"The return current flow through
the ground plane is assumed to be spatially diffused and is ignored."* That is a
phone-with-a-large-ground-plane assumption. A watch battery on two flying leads
has a specific return conductor 1–3 mm away, and every current loop closes
somewhere inside a 34.6 × 25.8 mm board. Beyond roughly one loop dimension the
field falls as 1/r² or 1/r³, never as 1/r.

> **This reverses a conclusion an earlier strand of this work reached.** The
> sentence *"0.5 µT needs 80 mm, which is larger than the entire watch,
> therefore distance cannot solve this"* **is not established**: at 80 mm the
> real field is 0.02–0.08 µT, six to twenty-five times *below* the criterion.
> The isolated infinite conductor is the pathological case A47 adopted for a
> different board; the closed loop is the physical default here.

**The highest-leverage mitigation in this document costs nothing and needs no
parts.** The battery is on red/black flying leads and the speaker is on a
red/black wire pair ([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md)
§1.8) — both are free-routed wire and both are free to fix. **Twisting or laying
those pairs flat against each other turns 11.5° of error at 10 mm into 1.1°.**
It is worth more than any placement decision below it.

Two caveats against the comfortable reading. The 200 mA figure is the *charge*
current; on discharge the same lead carries the full system load including a
panel whose current is `UNKNOWN`, and the arithmetic scales linearly with it.
And A47 §6 warns the trace field is not the end of it: *"This field will in turn
induce a soft-iron field in local ferromagnetic components on the PCB which may
amplify the prediction of Equation 4 several times over."* Every number above is
a floor, not a bound.

### 5.4 Inductors, and a keep-out that was overstated by up to 130×

`L1`, `L2` and `IND2` sit on the back face ([BATTERY_UPGRADE](BATTERY_UPGRADE.md)
§1.3); their XY positions are `UNKNOWN`.

**A coil's external far field is a magnetic dipole falling as 1/r³, not a wire
field falling as 1/r.** A47's coherent-addition argument sets the *moment*; it
does not change the falloff law. For a 10-turn part at 200 mA:

| Coil diameter | 5 mm | 10 mm | 20 mm | 30 mm |
|---|---|---|---|---|
| 2 mm | 10.1 µT | 1.26 µT | 0.157 µT | 0.047 µT |
| 4 mm | 40.2 µT | 5.03 µT | 0.63 µT | 0.19 µT |

> **This corrects a wire-model estimate that gave 80 / 40 / 20 µT at 5 / 10 /
> 20 mm — overstated by 8× to 130×.** The inductors are a **local** problem
> inside roughly 5 mm, not a board-wide keep-out.

**Only the DC term is in band.** The switching ripple sits at the AXP2101's
switching frequency, far above the ceiling A47 §6 names: *"The highest sampling
rate of a consumer magnetometer is approximately 100 Hz so currents at
significantly higher frequencies will not be detectable by the magnetometer."*
What remains is the load-dependent DC component.

> **A misattribution to retire.** A47's 7 mm figure is about **shield cans
> only** — *"It is good practice to keep even low permeability shields a minimum
> of 7mm away from the magnetometer sensor"* (§9 item 5) — and says nothing
> about inductors. **There is no vendor-sourced inductor keep-out in A47.**
> Treat 7 mm as an absolute floor from any metal can, not as a general rule and
> not as sufficient.

### 5.5 Three classes of source, three different criteria

Using one threshold for all three produces either a false "impossible" or a
false "fine".

**(a) Switched / state-dependent fields — must be ≲ 0.5–2 µT, because nothing
removes them.** A47 §6 names this exact case as the worst one: *"The most
difficult situation for the calibration software is the placement too close to
the magnetometer of a power supply trace supplying a varying current depending
on the smartphone processor load, the state of the LCD backlight and whether the
RF power amplifier is active."* Distance is the wrong lever here (§5.3); tight
pairs and state-gating are the right ones.

**(b) Static magnet fields — must not saturate, and must be *stable*.** A rigidly
fixed magnet is a hard-iron offset and hard iron is what calibration is for. The
budget comes from the magnet's temperature coefficient, and it is looser than a
naive reading suggests. Working target: **50–100 µT**.

**(c) Soft-iron distortion — no distance threshold exists in a 42 mm body.** The
battery pouch, the AMOLED backing plate, the USB-C shell, the speaker's steel
yoke and any ferrous screws are distributed through the whole case. A47 §5:
soft iron is *"normally modelled as a six-component symmetric matrix"*, and A47
§8 names *"the smartphone battery and steel shields"* explicitly. Answered by a
full ellipsoid calibration or not at all.

> **The anti-pattern, pre-refuted because someone will propose it.** A47 §9 item
> 4: *"Don't attempt to shield the magnetometer from magnetic fields generated by
> components on the PCB. A steel shield can placed over the magnetometer would,
> for example, be disastrous since it would both create a strong interfering
> soft-iron field and also shield the magnetometer from the earth's geomagnetic
> field."* Mu-metal is not the answer.

**On the stability budget in (b), and its limits.** Supplier data for NdFeB gives
a reversible temperature coefficient of induction of about **−0.11 to
−0.12 %/°C**. Three qualifications, all of which push the same way:

- **Provenance.** One working strand cited the Eclipse Magnetics standard NdFeB
  datasheet by grade (−0.120 base, −0.115 M, −0.110 H, −0.105 SH); another
  recorded the fetch as having 404'd and the figure as second-hand. **The value
  is standard in the literature and the mechanism is certain; the citation is
  not settled.** Recorded as `ESTIMATED` and flagged for reconciliation (Q7).
- **The band is wrong for the use case.** The coefficient is quoted over
  20–100 °C. A wrist runs ≈ 33 °C and a winter street ≈ 0 °C. NdFeB's
  coefficient is not constant with temperature, so the extrapolation is
  unlicensed.
- **It is not the only thermal term, and the others are the same order.** Over
  20 °C: magnet drift on a 100 µT offset = 2.40 µT; the sensor's own offset
  tempco (LIS2MDL ±0.03 µT/°C, IST8310 0.024 µT/K) = ≈ 0.5–0.6 µT per axis;
  differential thermal expansion of a plastic case (≈ 70 ppm/°C over 20 mm =
  28 µm) sitting in a 32 µT/mm gradient = ≈ 0.9 µT. **Summed, ≈ 3.9 µT against
  the 2.4 µT that was budgeted — the stability budget is understated by roughly
  60 %**, and the 50–100 µT working target is correspondingly loose.

Two hedges against over-reading it: the angles assume the drift lies entirely in
the horizontal plane, and any real compass stack re-estimates hard iron
continuously between explicit calibrations, which absorbs slow drift. **It is a
budget, not a cliff.**

### 5.6 Wi-Fi and BLE transmission — the interferer nobody listed

A47 names *"whether the RF power amplifier is active"* in its own worst case, and
this board has Wi-Fi and BLE. The number is already in this repository:

> `| ESP32-S3 peak current, BLE TX @ +9 dBm | 193 mA **peak** | PUBLISHED-SPEC |`
> — [TAGS_TRACKS_RECKONING](TAGS_TRACKS_RECKONING.md):231

**That is the same magnitude as the charge current this whole analysis is built
on, and unlike charging it is switched.** The same file records the per-set
extended-advertising interval range as 20 ms to ~10485 s — a repetition envelope
from ≈ 50 Hz down to millihertz, squarely inside the ≤ 100 Hz band A47 says is
detectable. Wi-Fi TX current is higher still.

Two consequences:

- **The phrase "the Waveshare has no radio" is wrong as written.** It has no
  **LoRa** radio. The LoRa × Magnetometer row's conclusion is right; the reason
  usually given for it is not.
- **`INTERFERENCE_MATRIX.md` has no radio-current row for the radio this board
  actually has.** Wi-Fi/BLE TX × Magnetometer is a **fifth** candidate pair and
  it should be added (Q8). It is one of the few interferers the firmware can
  only partly schedule around, because BLE advertising is periodic and
  mesh-adjacent.

### 5.7 The IMU cannot take this load off the host

> **This section stands unchanged from the committed revision and it now answers
> a question a later reviewer raised as an open one.** A review of the power
> arithmetic listed *"the QMI8658's auxiliary I2C master is cited and never
> used"* as a gap, and proposed establishing whether the aux pins are usable as
> the first thing to do. **This document already closed that**, and the answer is
> not the hopeful one. It is recorded here rather than silently dropped, because
> it is what makes §4.3's "no offload path at all" a finding rather than a
> guess.

The QMI8658C has an I2C *master* interface for exactly this purpose — Mode 2,
"Mag Mode", in which the IMU reads the magnetometer itself and time-aligns those
samples with the accelerometer and gyroscope (M3 §11.1). For a fusion problem
that is a genuine prize.

**It is unreachable, for documentation reasons rather than silicon ones.**

M3 §11.1, quoted in full: *"**Currently** the QMI8658C can support the following
magnetometers: AK09915C, AK09918CZ, and QMC6308."* `CTRL4` `mDEV<3:0>`
designates the device. **Neither ordered part is on that list.** And more
decisively:

- **`mDEV` is four bits and M3 publishes no encoding for any of them.** The
  string `mDEV` occurs **once** in the whole document — the `CTRL4[6:3]` field
  definition with reset value `4'b0` — and the three part names occur once, in
  §11.1. Nothing maps a part to a code. **So Mag Mode cannot be driven from M3
  whichever magnetometer is fitted**, including the three it names. Table 18, the
  9DOF current figures, is `tbd` in every cell.
- **"Currently" is the datasheet's word, and what happened next is the opposite
  of what one would hope.** M5 is the same document renamed, and its revision
  history continues past where M3 stops: Rev **0.8**, 10 Sep 2021 — *"…deleted
  descriptions of magnetometer, … deleted the specifications, registers, and
  application diagrams that relative to I2CM interface…"*; Rev **0.95**, 6 May
  2022 — *"…remove CTRL4 & CTRL6…"*. **The vendor withdrew the feature from the
  documentation, register and all.** Stated carefully: this is evidence about
  what QST documented, not a claim that the C silicon lacks the block. The block
  may well be there. There is no published way to reach it and there is not
  going to be one.

A host-driven arbitrary I2C-master transaction does exist — `CTRL_CMD_I2CM_WRITE`
(`0x06` via `WCtrl9`), M3 Table 30 — and is not textually gated on `mDEV`. It
does not rescue us: M3 never publishes the I2CM sub-register map that `CAL3_L`
indexes. Recorded because "the datasheet lists three parts" and "the hardware
can only talk to three parts" are different claims, and only the first is
supported.

**Do not order a magnetometer on account of the IMU.** An earlier revision of
this file advised considering an `AK09918CZ` or a `QMC6308` in the next order if
the schematic showed `SDx`/`SCx` free. That advice was given to the owner in #83
and is **withdrawn**: buying a listed part would not open Mag Mode, because
there is no documented way to select it. Whether the board leaves `SDx`/`SCx`
usable stays `UNKNOWN` and is now merely interesting.

### 5.8 Geometry, and the unknown that decides it

External envelope, `VERIFIED` from the vendor mechanical drawing
([BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1.1): **50.80 mm** across including the
strap-lug bars, **42.00 mm** body height, **13.60 mm** total thickness with a
**12.90 mm** body step, rear plate **34.60 × 25.80 mm** with R4.5.

Opposite corners of that rear plate are **43.16 mm** apart.

> **That diagonal is not an achievable separation.** It is corner-point to
> corner-point with zero part size. The physical quantity is magnet-centre to
> sensor-die centre; the magnet has a radius of several millimetres, the sensor
> has a footprint, and both must fit inside a cavity whose dimensions this
> repository records as `UNKNOWN` — *"No section view, no cavity dimension, no
> STEP / STL / DXF / DWG anywhere in the wiki's Resources list"*
> ([BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1.3). **The usable top of the range is
> materially lower than 43 mm and cannot be stated without that drawing.**

Working figure: **≈ 20 to ≈ 40 mm, `ESTIMATED`**, bounded above by the envelope
and below by nothing yet established.

**The straddle is the finding.** If the speaker sits near one **edge** of the
back cover — which the grille slot in the case *wall* weakly suggests — the
opposite corner offers 30 mm or more. If it sits near the **centre**, the
maximum radius drops to roughly half the diagonal. Against a bracket that spans
1.5–293 µT at 20 mm, **neither position resolves to a verdict from documents.**

**The magnet's position is not recorded anywhere.** What the repository records
is the speaker's *solder pads* — `P5`/`P6`, bottom-right of the mainboard — and
that the module is mounted in the back cover. Pads are not the magnet, and the
difference is the answer.

### 5.9 Recommended placement

**Separate the electrical attachment from the sensor's physical position. They
want different places.**

**Electrical: the expansion pad row, and it helps.** Pads 10 (`3V3`), 9 or 2
(`GND`), 5 (`IO15`/SDA) and 6 (`IO14`/SCL) sit adjacent on one row. Three
constraints:

- **Do not spend `RXD`/`TXD` on the magnetometer.** That pair is the only
  uncommitted channel on the row and **T-096** reserves it for an attached
  Attadipa node. A magnetometer is an I2C slave and does not need it. Both fit.
- The T-096 objection to putting a device on the PMU's bus was about a
  *detachable* peripheral browning out and holding SDA low, taking the power
  manager, RTC and touch controller with it. A soldered part is not detachable,
  so that objection weakens without vanishing: a short in the retrofit still
  kills the bus. Keep the flex under ≈ 50 mm so added capacitance stays inside
  the 400 pF budget, and check rise times on a scope before believing it works.
- The pads are labelled as bare GPIO numbers. **They are not spare.** Nothing in
  the retrofit may drive them as GPIO.

**Physical: not at the pad row.** It runs along the bottom edge, and the bottom
edge is where both magnets connect — the speaker's `P5`/`P6` at bottom-right and
the `P1`/`P2` motor pads on the same back face. A47 §9 item 1 does say *"The best
locations are often to be found on an edge or at a corner of the PCB"* — but on
this board the convenient edge is the noisy one.

In order:

1. **Locate the speaker magnet's position and axis**, per §0.
2. **Put the sensor die at the in-plane point of maximum distance from it**, on
   the back face, kept **low against the PCB** rather than raised toward the
   cover — raising it moves it toward the speaker plane.
3. **Bond it rigidly.** Epoxy or UV adhesive, to the case or to the PCB, with
   non-ferrous hardware throughout — A47 §8: *"Materials that are safe for use in
   the proximity of the magnetometer include brass, aluminum, copper, gold,
   silver and titanium."* §5.2's gradient arithmetic is why this is not optional.
4. **Re-route the battery and speaker leads as tight pairs.** Free, and worth
   more than the placement.
5. **Then run the survey before committing the position** — A47 §10's own
   procedure: sensor on a temporary flex at 3–5 candidate spots × {back cover on,
   off} × {charging, not} × {audio playing, silent} × {day theme, night theme} ×
   {display on, off} × {BLE advertising, radio off}.

**Numbers to beat:** static offset ≲ 100 µT; state-dependent delta between any
two states ≲ 1–2 µT; no axis within 20 % of full scale in any orientation; and
the part's range chosen only **after** the offset is known.

**The strap is magnetically the best answer and architecturally the worst.** It
gets 20–40 mm of separation and buys it by leaving the watch body. A strap
flexes, its tension changes, and which buckle hole is used changes the geometry —
so the sensor-to-watch transform is unknown and time-varying. That is exactly
what [ADR-0009](../adr/0009-heading.md) §3 refuses for a node: *"`NodeBody`
heading is never presented as `WatchBody` heading. Not scaled, not offset, not
'close enough'."* A strap sensor is *worse* than `NodeBody`, because a rigidly
mounted node could at least be calibrated once.

**A short flex away from the body is legitimate, and the distinction is rigidity,
not location.** A sensor bonded to the *case* — under a lug, in a small pod on
the outside of the body — is still `WatchBody`, because the case and the display
are one rigid body, and it can win 5–10 mm. What is not allowed is a sensor that
can move relative to the display.

**Test the existing screws, spring bars and buckle with a paperclip.** A two-
minute test that decides whether a *moving* soft-iron source exists at all. If
the buckle is magnetic that is an argument for changing the strap, not for
moving the sensor.

### 5.10 The axis map is a fact about a unit, not about a board

The QMI8658's board-frame axes are silkscreened by the vendor — X toward the
battery edge, Y toward the USB-C edge, Z out of the back face — so the IMU's
axis map is a **board** fact. **A hand-soldered magnetometer's orientation is set
by whoever holds the iron**, so its axis map is a **unit** fact, different on
every retrofitted device, and it cannot be a compile-time constant. Re-soldering
the same part at a different angle produces the same part identity and a
different axis map.

Final §27 already requires the field: *"Store calibration with: sensor identity /
board-provider identity / axis mapping / calibration version / timestamp /
quality."* The specification accommodates this. The code's compile-time
`kWaveshareFeatures` mask does not. **`MAGNETOMETER_BACKLOG` G-03 becomes a
per-unit calibration step, not a constant** — and it gets harder than that file
assumes, because there will be two motion parts in the case with independent
orientations.

---

## 6. What changes in the interference matrix

[`INTERFERENCE_MATRIX.md`](../hardware/INTERFERENCE_MATRIX.md) already has four
magnetometer rows and they are the only rows in the file marked with a state
distinct from `THEORETICAL RISK`. Its own rationale block says of them: *"They
are kept in the table rather than deleted, because the day an external sensor is
added (OPEN_QUESTIONS A5) they become the first four tests to run."*

**That day has arrived, and two of the four become runnable. Not four.**

| Row | Was | Becomes | Why |
|---|---|---|---|
| Battery charging × Magnetometer | `NOT MEASURABLE` | **measurable** | the unit has a cell on `J1` and the charge current will be 150–200 mA |
| Audio amplifier × Magnetometer | `NOT MEASURABLE` | **measurable**, modulo T-105 | the `AAC210602A1` is a permanent magnet on either reading; the coil-current half depends on whether it is a speaker or an actuator |
| Haptic motor × Magnetometer | `NOT MEASURABLE` | **still not measurable, for a new reason** | not "no magnetometer" any more but **no motor**: `P1`/`P2` are bare, T-097. If T-105 says the `AAC210602A1` is an actuator, this pair becomes measurable through that part instead |
| LoRa TX × Magnetometer | `NOT MEASURABLE` | **still not measurable** | no LoRa radio on this board — and see §5.6, which is not the same as "no radio" |
| **Wi-Fi / BLE TX × Magnetometer** | *row does not exist* | **should be added, and is measurable** | 193 mA peak BLE TX, `PUBLISHED-SPEC` |

> **Correction to an earlier revision of this file.** §6 previously listed *"the
> vibration motor — an AAC Technologies module at pads `P1`/`P2`"* as a magnetic
> source. **That conflated two different parts and both halves are now wrong.**
> `P1`/`P2` are **bare** on this unit — the GPIO18 → R12 4.7k → Q1 MMBT3904 drive
> circuit from BLDO2 is present and no actuator is fitted (T-097). The
> `AAC210602A1` is a **separate** part, in the back cover, on solder pads, with a
> grille slot above it; whether it is a speaker or a haptic actuator is **T-105**
> and is open. The same conflation is in
> [`MAGNETOMETER_BACKLOG.md`](../hardware/MAGNETOMETER_BACKLOG.md) — *"Both
> boards have the buzz"* — and is listed for correction in §9.

**Nothing in the matrix records a magnetic-interference measurement.** Every
number is absent, not estimated. The Results section says *"Empty. No hardware
has been measured. No board is present."* — the second sentence is now false and
the first must stay true until a test runs.

**Two structural problems in that file, reported because they will bite whoever
edits it.** The candidate-pairs table is **split by the rationale prose**: rows
run 30–42, prose 44–58, then rows resume at 59–63 with no repeated header. And
the file uses `NOT MEASURABLE` where final §29 defines the evidence level as
`NOT MEASURABLE ON CURRENT HARDWARE`, while `COEXISTENCE_BACKLOG.md`:75 uses a
third spelling. The spec-mandated per-pair record also lists fields the table
does not have.

**`MAGNETOMETER_BACKLOG` epic gates change:** G-06 (hard iron) and G-07 (soft
iron) become startable — and hard-iron calibration here is not a formality,
because there is a known permanent magnet inside the case. G-09 (speaker) and
G-10 (charging) unblock; **G-08 (haptic) does not**. G-03 (axis mapping) becomes
fully answerable and harder (§5.10). `COEXISTENCE_BACKLOG` C-09 unblocks; C-08
does not.

---

## 7. Calibration, tilt compensation and trust

### 7.1 Hard iron and soft iron are two corrections, and an offset is not enough

A46 §1.2: measurements subject to both distortions *"lie on the surface of an
ellipsoid which can be accurately modelled by ten parameters ... Three model the
hard-iron offset, six model the soft-iron matrix and one models the geomagnetic
field strength."* The correction applied to every sample is `W⁻¹(Bp − V)`.

**Hard iron translates the sphere off the origin. Soft iron deforms it into an
ellipsoid.** An offset can only move a centre; it cannot restore an ellipsoid to
a sphere. Fit only offsets to ellipsoidal data and **the offsets themselves come
out biased**, so the hard-iron estimate is wrong too.

A46 allows a cheaper model — *"A four parameter calibration, comprising the three
hard-iron offsets plus the geomagnetic-field strength, may be sufficient for
circuit boards without strong soft-iron interference"* — but this assembly
contains a battery, a metal-can magnet module and case hardware, which is
exactly A47's list of soft-iron sources. **The 4-parameter fit is the fallback,
not the default.**

**A free diagnostic that separates the two failure modes.** A49 §6–§7: residual
**hard**-iron error produces a heading error with **one** cycle per 360°;
residual **soft**-iron error produces **two**. A slow turntable swing therefore
tells them apart by counting lobes. That is a host-testable acceptance test and
it is worth building.

**One conditioning risk that is not qualitative.** The AMOLED module's metal
backing plate is a thin sheet covering the whole footprint. A thin plate has a
near-zero demagnetising factor in-plane and near-unity out-of-plane, so a
magnetic-grade backing sheet produces a **strongly anisotropic, near-singular**
ellipsoid rather than a mild one. Whether the plate is austenitic (benign) or a
magnetic grade is `UNKNOWN`. It bears directly on whether the six-parameter fit —
the only remedy for soft iron in a 42 mm body — is well-conditioned at all.

### 7.2 What the user has to physically do

The figure-eight is folklore with a real basis: the fit needs vectors distributed
over a **sphere**, and any planar motion is degenerate — it constrains the
in-plane axes and leaves the third essentially unobserved. Minimum counts are
geometric, not statistical: 4 non-degenerate points for the 4-parameter fit, 10
for the 10-parameter fit, and in practice many more for conditioning.

A46 states no duration and no gesture. ArduPilot's onboard procedure does:
*"hold the vehicle in the air and rotate it so that each side (front, back, left,
right, top and bottom) points down towards the earth for a few seconds in turn"*,
elaborated as *"6 full turns plus possibly some additional time and turns to
confirm"*. **One to two minutes, two-handed, off the wrist.**

Design consequences:

- **The wizard shows coverage and quality, not a spinner.** 3D coverage is the
  thing that actually fails, so a progress bar that fills on a timer is a lie. It
  must be able to end in *"good enough, and here is what that means"* as well as
  in success, and it must never end on a timer regardless of what was collected.
- **A wrist-achievable subset is acceptable if the lower quality is reported
  honestly** — which is only possible if coverage is visible.
- Final §27 requires calibration be *"understandable"* and *"Do not expose raw
  XYZ to normal users."* A coverage-and-quality display is close to that line and
  the design has to argue its side of it.

**A46's headline claim is what makes an on-device wizard viable at all:** *"It is
possible for a smartphone eCompass to be calibrated by the owner in the street
with no a priori knowledge of location or the direction of magnetic north."*

### 7.3 Child Mode

**This is the primary user of the feature, not an edge case.** Final §49 lists
*"direction toward parent/known point where data permits"* among Child Mode's
goals, together with *"honest offline/no-position states"*, *"low reading
burden"* and *"Hide dangerous/complex settings behind parent/advanced
controls."*

Four consequences, none of them resolved:

1. **The retrofit changes when direction is drawable, and only on one unit.**
   Today a child's arrow can exist only while the child is walking. On a fitted
   unit it exists while standing still. **Both cases must be designed**, and the
   un-fitted case must not read to a six-year-old as breakage. ADR-0009 §4
   already has the language — *"Standing still is a first-class UI state ... It
   never shows a rotating arrow, and it never shows 0°"* — and Child Mode needs a
   non-textual rendering of it, because "low reading burden" and a paragraph of
   explanation are incompatible.
2. **The calibration wizard is not a Child Mode surface.** A six-year-old told to
   rotate the watch through six faces will produce a calibration worse than none.
   Calibration entry belongs behind the parent gate; Child Mode surfaces at most
   *"ask a grown-up to fix the compass."*
3. **Recalibration will be a routine demand, not a rare one.** §7.6 state 3 fires
   on a disturbing-field event, and a fridge magnet is an ordinary object in a
   child's day.
4. **Calibration takes the watch off the child's wrist for one to two minutes**,
   which suspends the safety function the watch exists for. Whether calibration
   can be interrupted and resumed is an open design question, and the Navigator
   and SOS paths need a designed state for that window.

**The fitment toggle is unambiguously a parent/advanced control** — a child
flipping *"this watch has a compass"* is a way to make the arrow lie, on a device
whose headline feature is finding a parent.

### 7.4 Tilt compensation, and what it costs

A48 computes roll and pitch from the accelerometer, de-rotates the magnetometer
vector, and takes heading as `atan2` of the horizontal components. **The QMI8658
supplies the accelerometer half, so this board plus a magnetometer is exactly
A48's parts list** — which makes ADR-0009's rejected alternative *"Use the
accelerometer for tilt-compensated heading. Not possible — it needs a
magnetometer to compensate, and neither board has one"* false for this unit.

**The accuracy cost is amplified by `tan(dip)`, and it is tilt *error*, not tilt,
that costs.** A49 Eq. 23: `ΔΨ_max = √((Δφ tan δ)² + (Δθ tan δ)²)`. Perfect tilt
knowledge costs nothing at any angle. At dip 70° — Honeywell AN-203: *"In North
America the field lines points downward toward north at an angle roughly 70
degrees into the earth's surface"* — `tan δ = 2.75`, so one degree of tilt error
becomes 2.7° of heading error. At 80°, 5.7×. **The dip at the owner's location is
`UNKNOWN`**, and northern latitudes mean high dip, so the amplification is on the
bad side here. A49 notes the approximation *"breaks down near the geomagnetic
poles where tan δ is unbounded."*

**Two separate failures near vertical, and both are ordinary on a wrist.** The
horizontal component available to resolve heading shrinks until yaw about gravity
is ill-conditioned and then undefined; and the gravity reference itself becomes
unreliable — A48: *"A tilt-compensated eCompass will give erroneous readings if
it is subjected to any linear acceleration."* A swinging arm violates that
continuously. **These are the normal condition, not edge cases**, exactly as
ADR-0009 already found for standing still, so they get designed first and get a
one-second remedy rather than an error dialog.

**Verdict on full AHRS: not required, and take Fusion's rejection architecture
rather than its filter.** The specification's product checklist asks for
*"Magnetometer-ready architecture"* (30), *"Magnetometer calibration plan"* (31)
and *"Explicit heading reference frames"* (32); §87 M5 asks for a *"honest
heading model; magnetometer-ready architecture"*. **A full AHRS is nowhere
required.** What it would buy is ride-through when the magnetometer is disturbed,
immunity to A48's linear-acceleration failure, and smoother output during arm
swing. What it costs is the gyroscope running continuously — 651 µA that does not
fall when you sample slowly.

The recommended shape: tilt-compensated heading as the only always-on path;
**Fusion's rejection logic reimplemented** — the `REUSE_LEDGER` already
classifies that as `INSPIRE ARCHITECTURE`, and its concrete numbers are usable
starting points (magnetic rejection 10°, acceleration rejection 10°, rejection
timeout 5 s); the gyroscope enabled only in states that need it — the calibration
wizard, and a bounded ride-through window opened when the magnetometer is being
rejected.

> **Held against §4.3.** That recommendation is made **in spite of** the power
> arithmetic, not because of it: the magnetometer has no FIFO and the IMU does,
> so once main-CPU wakes are counted the ordering between the two paths is not
> established. The verdict rests on the specification not requiring an AHRS, and
> on the gyro-off state being simpler to reason about — not on a measured
> saving.

**A verify-don't-trust finding that would silently defeat the plan.** Fusion
ships its rejection features **disabled**. `Fusion/FusionAhrs.c`:

```c
const FusionAhrsSettings fusionAhrsDefaultSettings = {
    .sampleRate = 100.0f, .convention = FusionConventionNwu, .gain = 0.5f,
    .gyroscopeRange = 0.0f, .accelerationRejection = 0.0f,
    .magneticRejection = 0.0f, .rejectionTimeout = 0.0f, };
```

The README's *"A value of 10 degrees is appropriate for most applications"* is a
recommendation, and the README itself says *"A value of zero will disable the
acceleration and magnetic rejection features."* Anyone who drops the library in
and calls `FusionAhrsInitialise` gets **no rejection at all** — the exact
behaviour the library is admired for is off until you turn it on. Licence remains
MIT.

**Two `REJECT` verdicts fire their own written revisit triggers**, not a proposal
of ours: `REUSE_LEDGER`:659-661 — *"Revisit when: an external magnetometer is
decided (OPEN_QUESTIONS A5)"* — and
[`research-integration.md`](../upstream/research-integration.md):405 — *"Verdict:
REJECT, and not deferred — the finding is inapplicable to this hardware rather
than premature."* The Waveshare with a magnetometer has accel + gyro + mag, which
is the full Madgwick/Mahony parts list. **The reason is now false for this unit;
the verdict should stand, amended, for the different reasons above.**

### 7.5 Trust tests, and the circularity that does not close cleanly

Four tests. Two are the classic ones and need an expected value; two need nothing
and therefore break the circularity.

**T1 — field magnitude.** After calibration, `|W⁻¹(Bp − V)|` must equal the
expected total intensity. ArduPilot's position-free gate (P1):

```c
#define AP_ARMING_COMPASS_MAGFIELD_EXPECTED 530   // milligauss = 53 µT
#define AP_ARMING_COMPASS_MAGFIELD_MIN      185   // 0.35 × 530
#define AP_ARMING_COMPASS_MAGFIELD_MAX      875   // 1.65 × 530
```

with the message *"Check mag field: %4.0f, max %d, min %d"*. A ±65 % gate against
a fixed global constant, deliberately loose. When position **is** known it
tightens to a component-wise comparison against the earth-field model at
`AP_ARMING_MAGFIELD_ERROR_THRESHOLD 100` milligauss (10 µT) on `max(|dx|,|dy|)`
and twice that on `|dz|`. **Two gates, wide when position-free and narrow when
the model is available** — that is the shape to copy, not one threshold that has
to be both.

**T2 — dip angle.** The angle between the calibrated field vector and measured
gravity is a rotation invariant. Warn above ≈ 5°, reject above 10–15° — justified
by Fusion's recommended 10° magnetic rejection with a 5 s recovery, not invented.
T2 is only meaningful when the accelerometer is trusted, so it sits behind an
acceleration check at the same order.

**T3 — the sensor's own verdict.** Overflow and saturation flags (both ordered
parts have one), and the disturbing-field condition of §2.3. Two consequences:
the flag is evidence, and **exceeding it invalidates the stored calibration, not
just the sample.** This is ADR-0011 §6's rule in a new subsystem: the part's own
verdict is the strongest single input and it is not the truth.

**T4 — self-consistency, needing no position at all.** The magnetometer must
rotate exactly as the gyroscope says the body rotated; short-window variance must
stay near the noise floor; and a step change correlated with a known internal
event (charge start, audio start, BLE advertisement, a display transition)
attributes itself. **T4 works on a device that has never had a position, and it
is the one worth building first.**

**Where the expected values come from — and the circularity is real, because this
board has no GNSS.**

**(a) The calibration produces `F`, and it must not be the only source.** A46's
tenth parameter is the geomagnetic field strength, obtained *"with no a priori
knowledge of location"*. That is a genuine, cited answer — **for T1's
disturbance-detection job only.** It cannot also police the fit: **a fit that
converged to the wrong ellipsoid emits a wrong `F` and then blesses its own
residuals.** So (a) and (b) run together, with (b) as the calibration-integrity
check and (a) as the disturbance check. Ranking (a) above (b) silently deletes
the only non-circular detector of the failure mode a hand-soldered retrofit is
most likely to produce.

**(b) A position-free global gate.** 22–67 µT total, or ArduPilot's ±65 %. Weak,
non-circular, and enough to catch a magnet or a broken fit. **NOAA gives it a
harder floor than the total-field bound does:** a **Blackout Zone** at
`H < 2000 nT` where *"compasses are not accurate and should not be relied on for
navigation"*, and a **Caution Zone** at `2000 ≤ H < 6000 nT` (N1).

**(c) `F` is answerable from the fit; the dip is not.** A46's tenth parameter is
**strength, not inclination**. The device can compute its own dip from gravity
and field at calibration time, but that is (a) again and inherits (a)'s
objection. **T2 has no position-free expected value, and that is an open
question, not a solved one** (Q6).

**(d) Coarse position helps `F` and does not rescue the dip gate.** ArduPilot
compiles the WMM in as a table with `LAT_TABLE_SIZE 19` and `LON_TABLE_SIZE 37` —
a 10° grid. **Grid spacing is the table's sampling interval, used with
interpolation; it is not a tolerance for error in the position you look up
with.** Checked against the thresholds above it fails twice: by the axial-dipole
relation `tan I = 2 tan(lat)`, 5° of latitude near 55°N is ≈ 3.2° of inclination
(`ESTIMATED`, dipole approximation) — most of T2's own 5° budget spent before any
sensor error; and NOAA states *"Some local, regional, and temporal magnetic
declination anomalies can exceed 10 degrees"*, which no grid resolution captures.
**Coarse position is adequate for the intensity gate and inadequate for the dip
gate at the threshold proposed.** The table must live on device — this project
must work with no network, ever.

**(e) Declination cannot be manufactured, and this exposes a gap in ADR-0009.**
`F` and dip can be estimated locally; the angle between magnetic and true north
cannot — it needs a position. Without one the heading is **magnetic** north.
ADR-0009's struct documents `uint16_t centideg; // 0..35999, true north` and
carries **no way to say "magnetic north, declination unknown"**. Either
`HeadingSource`/`ReferenceFrame` gains that distinction or the Navigator must
refuse to label the arrow. **This is a real finding the retrofit forces, it is
cheap now and expensive later** — the same argument ADR-0004 §2a makes with
Meshtastic's PR #3157.

**How the verdict is carried, and a vocabulary problem to settle first.** The
obvious move is to mirror ADR-0011 §5 — `TrustState {Trusted, Degraded,
Untrusted}` plus reason codes, hysteresis, timestamps and a bounded transition
log. **That would be a third vocabulary, not a second.** ADR-0009's `Heading`
already carries `Validity {Valid, Stale, Uncalibrated, NoMotion, Invalid}` **and**
a `confidence` of 0..100. Adding `TrustState` gives one angle three overlapping
verdict fields with no stated precedence. **Either `Validity` absorbs the new
reason codes, or the amending ADR states which field wins and why** (Q9). What is
not in doubt is that a boolean will not do — ADR-0011's sentence applies
unchanged: *"a user-facing string, an app's decision to hide the compass, and a
diagnostic screen are three different consumers of the same evidence, and a
collapsed boolean serves none of them."*

### 7.6 User-visible states

These are **not** a single state machine. There are three axes and they compose:
**availability** (is there a compass, is it on), **validity** (is this reading
usable), and **frame/reference** (what is the angle measured against). Rendering
them as one flat list is the collapse ADR-0009's Alternatives section rejects.

**Availability**

| # | State | Sentence | Remedy |
|---|---|---|---|
| 1 | No compass fitted | "This watch has no compass." | none — a fact, not an error. Heading falls back to GNSS course-over-ground in `CourseOverGround`, from a node if attached |
| 2 | Off | *(compass idle)* | none — the duty-cycled state §4.2 assumes. `Availability::Off` already exists in the enum |
| 3 | Not responding | "The compass is not responding." | check the connection. Only a unit that declared a fitment can ever show this |

**Validity**

| # | State | Sentence | Remedy |
|---|---|---|---|
| 4 | Warming up | *(brief, non-blocking)* | wait. The window after power-up while set/reset or offset cancellation runs. **Its duration is `UNKNOWN` and is a settling interval final §26 forbids inventing** |
| 5 | Never calibrated | "The compass has not been set up yet." | the wizard, offered inline. The value is still shown, marked — ADR-0009 §5's `Uncalibrated` row |
| 6 | Calibration no longer valid | "Something magnetic changed — the compass needs setting up again." | recalibrate. Fires on a final §27 identity change **and** on a disturbing-field event. Never silently reuse the old record |
| 7 | Calibrating | coverage and quality, and which orientations are still missing | — |
| 8 | Trusted | the rotating wrist-relative arrow | — the only state that draws it |
| 9 | Degraded, internal cause named | "Compass paused while charging." / "Compass paused while sound is playing." | wait, or unplug |
| 10 | Degraded, external and unattributable | "Something magnetic nearby is confusing the compass." | "move away from it and try again" — in the same breath, never a bare warning triangle |
| 11 | Saturated | "The compass was overloaded by a strong magnet." | move away; the driver runs set/reset; the calibration may need redoing. Distinct from 10 because the remedy differs and the sensor, not the algorithm, is reporting |
| 12 | Tilt too large | "Hold the watch flatter." | one second |
| 13 | Too much movement | "Hold still for a moment." | one second. **Separate from 12: different cause, different remedy** |

**Frame — orthogonal flags, true or false alongside any of the above**

| # | Flag | Sentence |
|---|---|---|
| 14 | Magnetic north only | "Pointing to magnetic north — the watch does not know where it is." No position means no declination. §7.5(e) |
| 15 | Magnetic blackout region | "A magnetic compass does not work reliably here." `H < 2 µT`, NOAA blackout zone |

**In every non-Trusted state the fallback is ADR-0009 §1 unchanged:** draw the
bearing against a fixed north-up reference **and say so**. Never a rotating
arrow, never 0°, never course-over-ground dressed as heading.

### 7.7 Quiet windows cover charging and audio. They cannot cover the display

Final §28 makes the compass its worked example: *"Examples: magnetometer sample /
GNSS acquisition. No app should contain: disable vibrator / delay(100) / read
compass. The service asks the coordinator."*

**Every non-calibratable source on this board is internally knowable**, which is
what makes the mechanism work: the AXP2101 knows whether it is charging and at
what current; the audio path knows whether it is playing; the BLE stack knows
when it advertises. So the coordinator can schedule the sample away from the
disturbance, and when it cannot, the device can **name** the cause. That is the
difference between "compass paused while charging" and an unexplained wobble.

**The display is the exception and it is not a small one.** A compass is looked
at with the screen on. **There is no window in which the screen is off and the
user is reading the compass**, so the display term must be *calibrated or
measured*, not scheduled around. Two facts make that harder: the panel's supply
current is `UNKNOWN`, and the day theme's gamma-decoded emissive load is 2.622
against the night theme's 0.188 — a factor of 13.9 on the same pixels
([WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §1, `ESTIMATED`).

> **That ratio is an emissive ratio, not a supply-current ratio**, and the
> difference matters. An AMOLED module's supply current includes driver silicon,
> boost-converter quiescent and conversion losses, and scan overhead that do not
> scale with emission. **The in-band current step from a theme switch is
> `UNKNOWN`.** Whether the current path runs under the sensor is also
> unestablished — the panel's supply routing geometry is recorded nowhere.

**The user-facing consequence:** state 9's *"Compass paused while sound is
playing"* has no display counterpart that can be written, because *"compass
paused while the screen is on"* is not a sentence a product can show.

**Settling intervals are measured or they do not exist.** Final §26 *"Do not
invent settling intervals"* and §28 *"Delays/settling periods must be measured
and board/provider-specific."* That governs the post-unplug interval, the
post-buzz interval, the warm-up window of state 4, **and** the debounce interval
that stops a cracked joint flickering the arrow between north and "not
responding". All `UNKNOWN`.

---

## 8. What this does to the capability model

**This section states questions an ADR must answer. It does not answer them, and
a research note is not the place to.** What it can do is establish that the
existing model does not cover the case, and record the constraints that were
verified in code so that whoever writes the ADR does not rediscover them the
expensive way.

### 8.1 The model has two sources of a capability and this is a third

**Not the board case.** ADR-0007 §1 defines the hardware inventory as *"per
**board**, contributed by the BSP"* and says of the predicate: *"`present()` is
honest and narrow: it is a fact about a board, it does not change while running,
and no node ever makes it true."* `board_profile.h`:16-18 says the same in code:
*"It describes a board *variant*. A T-Watch S3 Plus with a CC1101 and one with an
SX1262 are two profiles."* **A variant is a purchase-time SKU difference — a
fact true of every unit that shipped that way.** `kWaveshareFeatures` is a
`constexpr std::uint32_t` shared by every ESP32-S3-Touch-AMOLED-2.06 in the
world; setting the magnetometer bit asserts something **false of the board and
true of one unit**. A third profile is the same claim with a longer name, because
a profile is selected by board identity and the two units *are* the same board.

**Not the node case.** ADR-0004 §2 fixes the axis at two values —
`enum class Origin : uint8_t { Local, Node };` — with the invariant
*"`Unprovisioned`, `Unreachable` and `Incompatible` imply a remote provider. A
local capability can never be in them."* A soldered part is `Local`: it cannot
walk away. ADR-0004 §2a excludes it from the other side too: *"a device that
never had a magnetometer does not acquire one because a node said so, it acquires
a *provider*, and that is a different edge."* **A retrofit is neither edge. The
device genuinely acquires the sensor.**

**The decisive citation: this exact question was asked and then recorded as
closed by answering a different one.** [ADR-0001](../adr/0001-capability-model.md)
"Open", lines 220-229:

> *"**Open.** ~~Whether `traits` should be extensible at runtime for external
> sensors over the expansion connector — deferred until such a sensor exists.~~
> **Closed, sooner than expected.** That question arrived within the day, and not
> over the expansion connector: an Attadipa node provides LoRa and GNSS over a
> link. ... Resolved in ADR-0004."*

**The struck-through question is this question.** ADR-0004 resolved the node case
only. **The expansion-connector/soldered case has never been decided**, and the
strike-through is why nobody noticed. Two other places enumerate the routes and
omit this one: `research-integration.md`:416 — *"via an Attadipa node or a future
board"* — and `MAGNETOMETER_BACKLOG`:90, where A5 offers *"a variant board, a
daughterboard, a different unit"*. Three options, none of them "soldered onto
the unit we already have".

### 8.2 What already works, and what the question is actually about

**Everything from `Capability::Heading` upward needs no new concept.**
`capability_registry.cpp`:103-104 already prefers a magnetometer over GNSS
course-over-ground, and ADR-0009's Consequences already promised *"Adding a
magnetometer later is a new `HeadingSource` and a calibration record; nothing
above `LocationService` changes."*

**The unresolved question is strictly below the capability registry: who declares
the bit, and how the declaration survives being wrong.**

### 8.3 Constraints any answer must satisfy — verified in code, not inferred

The obvious design is an add-only fitment overlay on the inventory, read at boot,
with `present()` becoming `profile_->present(f) || (fitment.added_mask & bit)`.
**It does not work as written, and the reasons were checked at source rather
than reasoned about.**

1. **Two of the three `present()` call sites bypass an overlay.**
   `profile_inventory.cpp`:13 (constructor) reads
   `states_[i] = profile.present(feature) ? HardwareState::Untouched : HardwareState::Absent;`
   — `profile.present`, not `this->present`. Line 36 (`set_state`) reads
   `if (index >= kHardwareFeatureCount || !profile_->present(feature)) { return; }`.
   **A fitted unit would report `present() == true` and `state() == Absent`
   forever**, and the BSP's `set_state(MagnetometerSensor, Ready)` would silently
   return. That pairing is explicitly illegal — `hardware_feature.h`:65:
   *"`Absent` — `present() == false`. There is no driver to have a state."* — and
   `capability_registry.cpp`:46 maps `Absent → Unsupported`, the **terminal**
   value.
2. **On a T-Watch that design would *remove* a working heading.**
   `capability_registry.cpp`:103-106 is **first-present-wins**, not
   first-`Ready`-wins: it returns `by_feature(MagnetometerSensor)` whenever the
   part is present and never reaches the `GnssReceiver` line. A present-but-
   `Failed` magnetometer therefore suppresses course-over-ground entirely — which
   is the cracked-solder-joint case a hand-soldered retrofit makes likely.
   **ADR-0007 §4's ordering is a *fallback* order and the code implements it as a
   *precedence* order.** On the Waveshare the bug is currently invisible (no GNSS
   receiver, and a `Ready` node still wins); that is luck, not structure. **This
   fix belongs with the ADR, not after it.**
3. **The simulator cannot reach the new states, and the `--radio` precedent does
   not transfer.** `sim/options.cpp`:210 writes `out.board.radio = ...` — a field
   **inside** `BoardProfile`. A fitment record is deliberately **outside** it,
   which is the load-bearing premise of the whole design. `ProfileInventory`'s
   only constructor is `explicit ProfileInventory(const BoardProfile&)`, and
   `sim/main.cpp`'s `bring_up()` loops every present feature and sets it `Ready`,
   so a simulated `failed` or `mismatch` fitment is overwritten on the next line.
   **Reaching the four states needs changes to `Options`, the constructor, the
   composition root and `bring_up()`** — which matters, because ADR-0004
   committed to *"Every state is reachable in the simulator without a rebuild"*
   and that commitment is also the strongest argument against the Kconfig
   alternative.
4. **There is no reason channel, and the retrofit's most probable fault class
   needs one.** `hardware_inventory.h`:26 exposes
   `virtual HardwareState state(HardwareFeature) const = 0;` — a bare six-value
   enum. "Nothing answered at the address" and "something answered but the
   identity register does not match" would render identically, so Diagnostics
   cannot show declared-versus-detected in the sense ADR-0007 committed to.
   **Either `HardwareState` gains a value or `HardwareInventory` gains a
   per-feature reason accessor** — an interface change.
5. **The typed-descriptor pattern is not available for free.**
   `HardwareInventory` has exactly one, `virtual const RadioInfo* radio() const`,
   and `ProfileInventory::radio()` derives it from a `BoardProfile` field.
   Following the `RadioInfo`/`RadioChip::Unknown` pattern means either putting a
   `MagnetometerInfo` into `BoardProfile` — which the whole design forbids — or
   adding a second descriptor mechanism **and a new virtual**.
6. **A generic add-only mask is a hazard beyond the magnetometer.**
   `ProfileInventory::radio()` consults `this->present()`, so a record that set
   the `Radio` bit on a Waveshare would turn `radio()` from `nullptr` into a
   pointer to a struct `board_profiles.cpp`:104 documents as *"no radio is
   fitted; the struct is meaningless"*. `MeshMessaging` happens to stay
   `Unsupported` because a zeroed `RadioInfo` has no modulations — **luck
   again.**
7. **The bench configuration this document recommends is unrepresentable.**
   §3.1 says to put **both** ordered magnetometers on the bus at once. The
   inventory holds `HardwareState states_[kHardwareFeatureCount]` — **one state
   per feature, not per instance.** There is no way to say "two magnetometers,
   one Ready and one Failed". **The very first physical configuration the design
   exists to describe is one it cannot describe.**
8. **The apps-never-learn invariant is not protected "by construction", and that
   claim should be withdrawn before an ADR leans on it.** The link boundary is
   real — `apps/` does not link `attadipa_platform`, and
   `tests/boundary/app_reaches_for_hardware.cpp` checks it — but it protects
   `HardwareFeature` and `present()`, **not the `Availability` values derived
   from them**, which `apps/` consumes directly. `capability_registry.cpp`:165-186
   shows `node_availability()` can never return `Failed` or `Off`, so for
   `Capability::Heading` on a Waveshare **those two states are reachable only
   from a locally fitted magnetometer**, and `availability()` surfaces them to
   `apps/src/app_manifest.cpp`. **An application can therefore distinguish a
   retrofitted unit from a stock one of the same board type.** The availability
   axis already leaks *board type*; what is new is the **granularity** — unit,
   not board — which is exactly the line this design says must stay below the
   registry. An ADR must take a position on whether unit-level fitment may be
   inferable from a remedy state.

### 8.4 Detection may demote. It may never promote

**A probe informs `state()` and never `present()`.**

**What a probe may conclude:** that something acknowledged an address, and — if
an identity register is read — that the byte is consistent with the declared
part. Nothing more. ADR-0001's rejection stands unscoped: *"an I2C address that
answers does not prove which chip answered."*

**What it may not conclude, and why each is real here:** a silent address is
produced by a cold or cracked solder joint (**the likeliest failure on a
hand-soldered retrofit inside a 13.60 mm case worn on a wrist**), swapped
SDA/SCL, a floating address pin, a rail that is down, a bus wedged by another
device, a part held in reset, and a unit where nobody soldered anything. **Seven
sentences, one NACK.**

**The terminality argument is decisive.** ADR-0004 §2a: *"`Unsupported` terminal.
Nothing may leave it. Ever."* If `present()` were probe-derived it could flip at
runtime. That is not hypothetical — this repository already read it out of
upstream history: Meshtastic shipped GPS as a two-state boolean, retrofitted
`NOT_PRESENT` in PR #3157, and **two years later** committed
`4a534f02a48626f2addf742dced2f9e8321d5e16`, *"fix(gps): prevent GPS
re-enablement in NOT_PRESENT mode"* — *"a hardware switch could still drag a
device out of the state that means *this device does not have one*."* **A
boot-time I2C probe wired to `present()` is that hardware switch, in this
codebase, by design rather than by accident.**

**Detection's actual job** is the one ADR-0001 and ADR-0007 already committed to
twice: *"declared and detected should be compared, and a mismatch is a finding
worth surfacing"*, and *"Diagnostics that can show both layers side by side ...
because a descriptor that disagrees with the hardware is worse than no
descriptor."* **The retrofit is the first case where that commitment does real
work**, because it is the first part whose declaration was made by a person with
a soldering iron rather than by a schematic.

**"Present at boot, gone at runtime" needs no new state**: it is
`Ready → Failed`, which ADR-0004's table already permits. It needs a **debounce
interval**, whose value is `UNKNOWN` and `MEASURED`-or-nothing.

### 8.5 A compile-time flag is not the answer, and the reason is narrow

**A Kconfig option must never assert unit-level presence.** It is ADR-0001's
rejected alternative — *"Feature flags at compile time. Rejected: it produces a
separate binary per board *variant* ... It also makes the simulator unable to
present configurations it was not compiled for"* — and it is **worse** than the
case that was rejected, because it produces a binary per **soldering job**, and
soldering jobs cannot be enumerated the way SKUs can. It also cannot carry the
data: the fitment is not a boolean but a part identity, a bus, an address, a rail
and **an axis map produced by how the part happened to be glued down** (§5.10).

**And it would set the precedent rather than follow one:** there is **no ESP-IDF
build in this repository today** — no `Kconfig`, no `sdkconfig`, no `boards/`
directory; `find_board_profile()` is called only from `sim/` and `tests/`. The
first Kconfig option in the project should not be one that decides a hardware
fact.

**What Kconfig legitimately will own,** so this is not over-read: selecting which
BSP an image is built for, and trimming optional driver families for image size.
Both are properties of the image. **The line is: Kconfig may decide what code is
in the image; it may never decide whether a part is on a device.**

### 8.6 The questions an ADR has to answer

1. Is there a **third declarer of presence** — board profile, unit fitment, node
   provider — while `Origin` stays two-valued? Or does something else give the
   right shape?
2. **Who may write the fitment record**, and why is a reboot enough to make a
   user-writable presence declaration legitimate when §8.4's Meshtastic case
   says a runtime switch is not? Note that neither `SettingsService` nor
   `StorageService` exists yet.
3. **May unit-level fitment be inferable from an `Availability` value seen by an
   application?** (§8.3 item 8.)
4. **How is instance multiplicity represented**, given that the bench
   configuration puts two magnetometers on one bus? (§8.3 item 7.)
5. **What invalidates a calibration record?** Final §27 says *"Changing
   sensor/provider may invalidate calibration"*; the retrofit adds at least two
   more triggers — **a re-solder** (same part identity, different axis map) and
   **reassembly**, because the speaker lives in the removable back cover and
   comes off by desoldering, moving the dominant hard-iron source by some
   fraction of a millimetre against a 13–32 µT/mm gradient. **Replacing the
   battery (#64) invalidates the soft-iron matrix, not merely the offset.**
6. Which ADRs are amended, and how. Candidates: **0007** (the `present()`-is-a-
   board-fact framing, and the `Heading` mapping row), **0009** (two factual
   asides and one rejected alternative; **§3 is untouched and sharpened**, since
   a soldered part is the first thing that can populate `WatchBody`), **0001**
   (a second notice on the struck-through Open item — additive, per the repo's
   convention that an amending ADR does not replace the amended one), **0004**
   (scoped, not overturned; and its rejected "boot-time-only discovery" must be
   explicitly scoped to *radio range*, so a reboot-on-solder does not read as
   re-adopting it), **0006** (a fourth scope row, and the first setting in the
   system that asserts a **fact** rather than a preference).

Untouched: **0002**, **0003** (and it supplies the `Unknown`-is-legal pattern),
**0005**, **0008**, **0010** (with the ordinary consequence that every new string
ships in both languages in the same commit), **0011** (its magnetometer sentence
is T-Watch-and-BMA423-specific and remains true), **0012**.

---

## 9. Every place in this repository that asserts the board has no magnetometer

**Listed so the retrofit does not leave half of them wrong.** Line numbers are
from a fresh scan on 2026-08-22 and will drift; the quoted anchor is the durable
part. **None of these should be edited until an ADR has answered §8.6**, because
several of them are correct about the *board* and wrong only about *this unit*,
and the distinction is the thing being decided.

**Verdict key:** `FALSE` — false as written · `UNIT` — true of the board type,
needs a board-versus-unit requalification · `TRUE` — stays true, listed so it is
not "fixed" by mistake.

### Specification and owner documents — do not edit

| File | Note |
|---|---|
| `docs/master-prompt-final.md`:930 | *"Neither current target board appears to contain a magnetometer."* The hedge is the owner's own. Product requirements binding, technical claims not — its own §1. **Do not edit.** |
| `docs/master-prompt.md`:507 | Superseded history. CLAUDE.md: *"Do not fix anything in them."* Listed for completeness. |

### Canonical fact records

| File:line | Verdict | Anchor |
|---|---|---|
| `docs/research/VERIFIED_FACTS.md`:90 | **FALSE** | *"### Neither board has a magnetometer"* — the record everything else cites |
| `docs/research/VERIFIED_FACTS.md`:353 | **TRUE** | *"The T-Watch has no magnetometer — now from the schematic"*. Exhaustive part-family search of six sheets. T-Watch only |
| `docs/research/VERIFIED_FACTS.md`:340-348 | **TRUE**, and it is the precedent | *"a fact about a board and a fact about a device are different claims, and this line turned one into the other without noticing"* |
| `docs/research/HARDWARE_MATRIX.md`:31 | **UNIT** | `\| Magnetometer \| absent \| absent \|` — needs a retrofit column, not a correction |
| `docs/research/HARDWARE_MATRIX.md`:46 | **UNIT** | *"Neither board has a magnetometer. The magnetometer work ... is therefore architectural only"* |
| `docs/research/HARDWARE_MATRIX.md`:415 | **UNIT** | `\| MAGNETOMETER \| ❌ \| ❌ \| simulated \|` |
| `docs/research/HARDWARE_MATRIX.md`:394 | **TRUE** | *"IMU / magnetometer \| scripted motion and field"* — simulator provision, unaffected |

### ADRs

| File:line | Verdict | Anchor |
|---|---|---|
| `docs/adr/0009-heading.md`:30 | **FALSE** | *"Neither board has a magnetometer ... so today the *only* possible source is GNSS course-over-ground"* |
| `docs/adr/0009-heading.md`:178 | **FALSE** | *"Use the accelerometer for tilt-compensated heading. Not possible — it needs a magnetometer to compensate"*. §7.4 |
| `docs/adr/0009-heading.md`:167 | **TRUE** | an argument about the enum, not a hardware claim |
| `docs/adr/0009-heading.md`:110 | **TRUE** | the A6 paragraph — the *node* question is unaffected |
| `docs/adr/0009-heading.md`:215-216 | requalify | A5 answered by events; **A6 remains open** |
| `docs/adr/0007-two-capability-layers.md`:56-58 | **FALSE** | *"Heading would be gated on `Magnetometer`, which is `false` on both boards"* |
| `docs/adr/0007-two-capability-layers.md`:217 | **FALSE** | `\| Heading \| MagnetometerSensor (neither board has one) · ... \|` |
| `docs/adr/0007-two-capability-layers.md`:88-89 | requalify | the inventory layer is declared *per board* |
| `docs/adr/0001-capability-model.md`:153 | **UNIT**, and it predicted this | *"`Magnetometer` exists in the enum even though neither board has one — so that adding one later changes an answer, not an interface"* |
| `docs/adr/0001-capability-model.md`:220-229 | **the critical one** | the struck-through Open item, §8.1 |
| `docs/adr/0001-capability-model.md`:147 | **TRUE** | illustrative sentence |
| `docs/adr/0004-capability-sources.md`:196-200 | **TRUE** | and the retrofit now falls outside it — §8.1 |
| `docs/adr/0004-capability-sources.md`:111-113 | **TRUE** | illustrative |
| `docs/adr/0011-gnss-integrity.md`:37 | **TRUE** | T-Watch/BMA423-specific |

### Architecture, hardware and backlogs

| File:line | Verdict | Anchor |
|---|---|---|
| `docs/architecture/ARCHITECTURE.md`:270 | **FALSE** | duplicate of the ADR-0007 mapping table — **both copies must change** |
| `docs/architecture/ARCHITECTURE.md`:580-583 | **FALSE** for the compass half | *"cannot occur on either board, because neither board has a compass"*. The motor half stays true on this unit (T-097) |
| `docs/architecture/ARCHITECTURE.md`:248-250 | **TRUE** | illustrative |
| `docs/architecture/ARCHITECTURE.md`:444-447 | **TRUE**, and it is this file's own precedent | *"the same weak argument-from-absence the magnetometer claim used to rest on"* |
| `docs/hardware/INTERFERENCE_MATRIX.md`:32,37,40,42 | row states change | §6 — two of four |
| `docs/hardware/INTERFERENCE_MATRIX.md`:44-52 | **FALSE** | the four-`NOT MEASURABLE` rationale, which contains its own trigger |
| `docs/hardware/INTERFERENCE_MATRIX.md`:54-58 | **FALSE** | *"the pair the master plan uses to motivate the whole coexistence architecture"* |
| `docs/hardware/INTERFERENCE_MATRIX.md`:119-123 | **FALSE** | *"unlike the magnetometer rows, which cannot be measured on any targeted hardware at all"* |
| `docs/hardware/INTERFERENCE_MATRIX.md`:103 | half false | *"Empty. No hardware has been measured. No board is present."* — sentence 2 is now false, sentence 1 must stay true |
| `docs/hardware/MAGNETOMETER_BACKLOG.md`:7 | **FALSE** | *"Neither target board has a magnetometer."* |
| `docs/hardware/MAGNETOMETER_BACKLOG.md`:10,13-17 | **UNIT** | the per-board IMU list |
| `docs/hardware/MAGNETOMETER_BACKLOG.md`:41-49 | **FALSE** | *"design-only, and honest about why"* |
| `docs/hardware/MAGNETOMETER_BACKLOG.md`:58-63 | gates change | G-06, G-07, G-09, G-10 unblock; **G-08 does not**; G-03 changes shape |
| `docs/hardware/MAGNETOMETER_BACKLOG.md`:71-78 | **FALSE, and doubly stale** | *"Both boards have the buzz"* — this unit has **no motor fitted** either (T-097), and it conflates the motor with the `AAC210602A1` (T-105). §6 |
| `docs/hardware/MAGNETOMETER_BACKLOG.md`:90,92-93,95-97 | A5 answered | G-14/G-15 become live; G-15's answer is yes — `IO15`/`IO14` |
| `docs/hardware/COEXISTENCE_BACKLOG.md`:28,47,48,72-76 | **FALSE** | C-09 unblocks; **C-08 does not** |

### Research notes

| File:line | Verdict | Anchor |
|---|---|---|
| `docs/research/OPEN_QUESTIONS.md`:38 | A5 answered by events | and the row's premise is false |
| `docs/research/OPEN_QUESTIONS.md`:39 | **TRUE** | A6 unaffected |
| `docs/research/OPEN_QUESTIONS.md`:205 | Q2 answered by events | |
| `docs/research/OPEN_QUESTIONS.md`:216-219 | **FALSE** | *"Either the node carries one ... or 'compass' means GNSS course-over-ground"* — a false dichotomy once a part is soldered on |
| `docs/research/OPEN_QUESTIONS.md`:305 | **FALSE as written** | survey-findings list |
| `docs/research/REUSE_LEDGER.md`:458 | requalify | *"### GNSS parsing and heading without a magnetometer"* |
| `docs/research/REUSE_LEDGER.md`:630-661 | **revisit trigger fires** | *"Revisit when: an external magnetometer is decided"*. §7.4 |
| `docs/research/REUSE_LEDGER.md`:513 | requalify | |
| `docs/research/TAGS_TRACKS_RECKONING.md`:286-292 | **FALSE for the Waveshare** | *"neither board has one"* — a magnetometer **is** that absolute reference |
| `docs/research/TAGS_TRACKS_RECKONING.md`:294-298 | **TRUE**, requalify | *"DR consumes odometry, an anchor and — where it exists — `Heading`. It never manufactures one."* The rule stands; *"where it exists"* now has a case |
| `docs/research/OWNER_DECISIONS.md`:326 | **TRUE** | BMA423-specific |
| `docs/research/PEDOMETER_PARTS.md`:351 | now larger | *"Neither board's IMU orientation is recorded yet"* — §5.10 |
| `docs/research/RECONCILIATION_2026-08-21.md`:25,48 | **TRUE** | historical |
| `docs/node/NODE_PROFILE.md`:43 | requalify | N3 — A5/Q2 answered by a route it does not contemplate |
| `docs/upstream/research-integration.md`:389-405 | **basis FALSE for this unit** | the Madgwick/Mahony `REJECT` |
| `docs/upstream/research-integration.md`:414-419 | incomplete | *"via an Attadipa node or a future board"* — two routes named, this one is not among them |
| `docs/upstream/research-integration.md`:92 | gate changes | §11 *"not yet applicable"* |

### Code, tests and agent instructions

| File:line | Verdict | Anchor |
|---|---|---|
| `platform/include/attadipa/platform/hardware_feature.h`:37 | **FALSE** | *"neither shipping board has one; the seat exists anyway"* |
| `platform/src/board_profiles.cpp`:21 | **TRUE** | T-Watch block |
| `platform/src/board_profiles.cpp`:44-60 | **UNIT** | `kWaveshareFeatures` — the executable assertion, and §8.1 argues it should **not** change |
| `core/src/capability_registry.cpp`:96,103-106 | not an assertion | the already-correct preference — **but see §8.3 item 2** |
| `core/include/attadipa/core/trust.h`:239 | **TRUE** | BMA423-specific |
| `core/include/attadipa/core/availability.h`:10 | **TRUE** | illustrative |
| `sim/labels.cpp`:65 | not an assertion | the label already renders |
| `tests/test_capability_registry.cpp`:111-113 | **UNIT** | comment at 111, assertions at 112-113. **Under §8.1's reading this test never fails**, because `kWaveshareFeatures` never gains the bit; the comment narrows to *"neither board **type**"* |
| `tests/test_capability_registry.cpp`:275,282,296 | premise and value change | *"Waveshare: a six-axis IMU ... but no magnetometer"*, and `CHECK_AVAIL(..., Heading, Unprovisioned)` at 296 |
| `tests/test_position.cpp`:186 | **FALSE as written** | the dead-reckoning caveat comment |
| `CLAUDE.md`:41 | **FALSE** | *"neither board has a magnetometer;"* — in the never-trust-verify list every agent reads first |
| `.claude/agents/hardware-fact-checker.md`:45 | **FALSE** | this agent would **enforce the stale fact against a correct change** |
| `.claude/agents/researcher.md`:53 | **FALSE** | |

### Public-facing, and the bilingual rule applies

| File:line | Verdict | Anchor |
|---|---|---|
| `README.md`:120-121,128 | **FALSE as written** | *"neither has a magnetometer"* |
| `README.ru.md`:125-128 | **FALSE as written** | *"магнитометра нет ни у одной"* |
| `docs/community/seed-discussions/1-offline-friend-location.md`:134-135,151 | **FALSE as written** | both languages |
| `docs/community/seed-discussions/2-find-my-camp.md`:107,123,137 | **FALSE as written** | both languages, one file |

> **`README.md` and `README.ru.md` change in the same commit.** CLAUDE.md:
> *"A README that is current in one language and stale in the other is worse
> than one language alone."* The same applies to the two seed discussions, which
> carry both languages inside one file each.

### Volatile

| File:line | Note |
|---|---|
| `STATUS.md`:463 | **FALSE as written** — *"arriving hardware does not help ... not measurable on either of them in any configuration"*. It **does** help, for two of four rows (§6) |
| `STATUS.md`:470-471 | A5 answered; A6 unaffected |
| `STATUS.md`:690 | historical record of #21; true as history, premise narrows |
| `TASKS.md`:410 | **FALSE as written** — the Fusion reuse-ledger task |
| `TASKS.md`:1704 | **FALSE** — T-014's *"cannot be run on either target board"* |
| `TASKS.md` T-011 blocker | **FALSE as written** — *"neither board has a magnetometer, so the haptics-versus-compass case cannot be measured"*. **The conclusion for that one pair survives by accident and for a different reason** (no motor, T-097); the sentence does not |
| `TASKS.md` T-012, T-071 | requalify — A5 answered; T-071's rule stands, its factual basis narrows |

---

## 10. Status of every claim in this document

| Claim | Status |
|---|---|
| Main I2C bus is SDA = IO15, SCL = IO14; six devices ACK | **VERIFIED** — schematic and physical unit |
| Which of `0x6A`/`0x6B` the IMU occupies | **CONFLICTING** — schematic says `0x6B`, QMI8658C Rev 0.6 says `0x6A`. One bus scan settles it. `HARDWARE_MATRIX`:318 and :327 disagree with each other |
| Both I2C pins brought out on the ten-pad expansion row, with GND and 3V3 | **VERIFIED** |
| Whether the `+3V3` pad is always-on or `ALDO1`-switched | **UNKNOWN** — decides whether `Availability::Off` is physically reachable |
| QMI8658 is 6-axis, no magnetometer; board-frame axes silkscreened | **VERIFIED** |
| `AAC210602A1` is in the back cover on solder pads, with a grille slot | **VERIFIED** — owner photographs |
| Whether it is a speaker or a haptic actuator | **UNKNOWN** — **T-105** |
| Its magnetic moment, position and axis orientation | **UNKNOWN** — the §0 measurement |
| `P1`/`P2` motor pads are bare; GPIO18 → R12 → Q1 → BLDO2 drive circuit present | **VERIFIED** — **T-097** |
| Case envelope 50.80 × 42.00 × 13.60 mm; rear plate 34.60 × 25.80 mm | **VERIFIED** — vendor drawing |
| Internal cavity dimensions | **UNKNOWN** — no section view, no STEP/STL/DXF anywhere |
| Achievable magnet-to-sensor separation ≈ 20–40 mm | **ESTIMATED** — bounded above by the envelope, not by a cavity drawing |
| Cell capacity ~300 mAh | **ESTIMATED** — three converging methods; the 400 mAh label is a label |
| Charge current 150–200 mA | **owner's stated intent**, not a measurement |
| AMOLED supply current | **UNKNOWN** — no figure exists in this repository |
| Day/night emissive ratio 13.9× | **ESTIMATED** — a pixel-value derivation; **not a supply-current ratio** |
| All datasheet electrical figures for both ordered parts | **VERIFIED against the datasheet**, `NOT MEASURED` on hardware |
| AK09911C register address map | **UNKNOWN from a primary source** |
| AK09911C noise floor and orthogonality | **not specified by M1** |
| Disturbing-field threshold for either ordered part | **UNKNOWN** |
| AK09911C ≈ 220 µA at 10 Hz | **ESTIMATED** — duty-cycle model, validated against AKM's own 100 Hz figure |
| ESP32-S3 light sleep 240 µA + 140 µA octal PSRAM | **PUBLISHED-SPEC** |
| BLE TX 193 mA peak @ +9 dBm | **PUBLISHED-SPEC** |
| Whether an always-on tilt-compensated compass is affordable | **UNKNOWN** — system term missing |
| Cost of one SoC wake at 10–20 Hz | **UNKNOWN**, and probably decisive |
| ULP-RISC-V cannot reach IO15/IO14 | **VERIFIED** — Espressif RTC I2C pin restriction |
| QMI8658 Mag Mode is documentation-dead | **VERIFIED** — M3 publishes no `mDEV` encoding; M5's revision history records the deletion |
| Current-trace arithmetic, single conductor | **VERIFIED** — A47 Eq. 4, reproduces A47's own worked example |
| Where that arithmetic stops applying (loop closure, > ~20 mm) | **ESTIMATED** — geometry-dependent; the loop dimensions are not established |
| Speaker field at 20 mm | **ESTIMATED, bracket ≈ 1.5–293 µT** — two orders of magnitude. The extrapolation is invalid and the measured data exists unread in S1 Figure 6 |
| Inductor field, dipole model | **ESTIMATED** — no part numbers known |
| Earth field 22–67 µT total, 0–42 µT horizontal | **VERIFIED** — A47 §2 |
| `B_h` at the owner's location | **UNKNOWN** — no WMM/IGRF evaluation has been run |
| Dip at the owner's location | **UNKNOWN** — and it sets the `tan δ` amplification |
| NdFeB tempco −0.11 to −0.12 %/°C | **ESTIMATED** — citation unreconciled between strands, and the quoted 20–100 °C band does not cover the use case |
| Stability budget understated by ≈ 60 % | **ESTIMATED** — three thermal terms of the same order, only one of which was originally counted |
| AMOLED backing plate: magnetic grade or austenitic | **UNKNOWN** — decides whether the soft-iron fit is well-conditioned |
| Every interference measurement | **NOT EXECUTED — HARDWARE REQUIRED** |
| Every settling and debounce interval | **UNKNOWN** — final §26 forbids inventing them |
| `present()`/`set_state()` overlay bypass, first-present-wins, missing reason channel, simulator reachability | **VERIFIED in code**, 2026-08-22 |

---

## 11. Open questions, each shaped as an issue

**Q1 · Which of the two ordered parts does the survey select?**
*Not "which part did the owner order" — that is answered.* #83 records two: a
CJMCU-9911 (AK09911C) and a GY-271 (QMC5883L), with the choice open.

> **This declines the framing that the first open question is which part was
> ordered.** It is answered, and had it been left open the whole document would
> be contingent on it. What is genuinely open is the *selection*, and §3.3 says
> the discriminator is the field at the mounting position, not a datasheet
> comparison. Blocked on Q2. `owner + measurement`.

**Q2 · Where is the speaker magnet, and which way does its axis point?**
Locate it through the closed back cover with a compass needle or a magnetometer
on a wire. Then map the field at 3–5 candidate positions under the state matrix
in §5.9. **Numbers to beat: static offset ≲ 100 µT, state-dependent delta
≲ 1–2 µT, no axis within 20 % of full scale.** This is the first task and it
blocks Q1, the placement, the part choice and — through the shared cavity —
[#64](https://github.com/hleserg/Attadipa/issues/64). `needs-hardware`, not
blocked on the parts arriving.

**Q3 · What are F, H, inclination and declination at the owner's coordinates?**
A WMM or IGRF evaluation. It sets every heading-error denominator, the `tan δ`
tilt amplification, and the T1/T2 thresholds. Until it exists, every angle in
this document is a latitude band. **A small job that gates the trust tests.**
`research`.

**Q4 · What do S1 Figure 6's measured values at z = 15, 20 and 25 mm say?**
Reading one figure collapses the 200× bracket in §5.2 to a measurement.
`research`, unblocked, cheaper than any of the reasoning it replaces.

**Q5 · Is the `+3V3` expansion pad always-on or `ALDO1`-switched?**
It decides whether `HardwareState::RailOff` and therefore `Availability::Off` are
physically reachable for a retrofitted part — and so whether §7.6 state 2 exists
and whether the quiet-window coordinator can cut a rail or only reschedule
sampling. A targeted schematic re-read. `research`.

**Q6 · Where does the expected dip come from on a device with no GNSS?**
§7.5(c): A46's tenth parameter is field *strength*, not inclination, so T2 has no
position-free expected value. Candidates: a retained last position from a node,
or an ADR-0006 region picker — **which is not a workaround but the honest
fallback for a device with no receiver**. `design`.

**Q7 · Reconcile the NdFeB temperature coefficient into `docs/research/`.**
One strand cites the Eclipse Magnetics datasheet by grade; another records the
fetch as 404'd. The whole calibration-expiry budget rests on it, and the quoted
20–100 °C band does not cover a wrist at 33 °C or a street at 0 °C. `research`.

**Q8 · Add a Wi-Fi/BLE TX × Magnetometer row to `INTERFERENCE_MATRIX.md`.**
193 mA peak, `PUBLISHED-SPEC`, switched, with an advertising envelope inside the
detectable band (§5.6). The matrix has no radio-current row for the radio this
board actually has. While there: reconcile `NOT MEASURABLE` against final §29's
`NOT MEASURABLE ON CURRENT HARDWARE`, and note that the candidate-pairs table is
split by prose. `docs`.

**Q9 · Which verdict field owns heading trust?**
`Validity`, `confidence` and a possible `TrustState` are three overlapping
fields on one angle with no stated precedence (§7.5). Settle it in the amending
ADR before three of them ship. `design`.

**Q10 · How does a heading say "magnetic north, declination unknown"?**
ADR-0009's struct documents `centideg` as true north and has no field for it
(§7.5(e)). Either the model gains the distinction or the Navigator must not
label the arrow. Cheap now, expensive later. `design`.

**Q11 · The ADR for a part that is neither the board's nor a node's.**
§8.6 lists six questions it must answer and §8.3 lists eight verified
constraints any answer must satisfy. It can be written and merged **before the
part arrives**, with no default changed and every existing test passing —
which is the argument ADR-0004 §2a already makes: *"the cost of runtime-
extensible capabilities is almost entirely in the *contracts* around them ...
cheap to decide with no code and expensive to retrofit."* `design`.

**Q12 · Fix first-present-wins in `capability_registry.cpp`:103-106.**
A present-but-`Failed` magnetometer suppresses the GNSS course-over-ground
fallback. Invisible on the Waveshare, a real regression on a T-Watch retrofit.
**Belongs with Q11, not after it.** `bug`.

**Q13 · Design Child Mode for a compass that exists on one unit.**
§7.3: both the fitted and un-fitted cases, an adult-gated wizard, what happens to
the Navigator and SOS during a one-to-two-minute off-wrist calibration, and
whether calibration can be interrupted and resumed. `design`.

**Q14 · Should the speaker be removed?**
It is the dominant hard-iron source **and** it occupies the cavity #64 is trying
to size. Removing it costs `Capability::AudioOut` — or `Capability::Haptics`, if
T-105 says it is an actuator, in which case this unit has neither. **Owner
decision**, coupled to T-105, T-097 and #64. `needs-owner`.

**Q15 · Reconcile `HARDWARE_MATRIX`:318 against :327.**
One says the IMU address is `CONFLICTING` between `0x6A` and `0x6B`; the other
says *"nothing collides and `0x6A` is free"*. Both cannot be relied on. One bus
scan settles it and the file should say so until then. `docs`.

**Still open and untouched by any of this: A6 — does the Attadipa node carry a
magnetometer?** A5 and Q2 are answered by events, by a route none of the three
documents that enumerate the routes contemplated. **A6 is a different question
and ADR-0009 §3 stands unchanged**: a node's heading is node orientation, and it
is never presented as watch orientation.

---

*Facts here are datasheet- and application-note-derived and marked with their
source. **Nothing has been verified on hardware, and nothing in this document is
a `PASS`.** See [VERIFIED_FACTS](VERIFIED_FACTS.md) for the standard this has
not yet met.*
