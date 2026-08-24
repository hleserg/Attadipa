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
>
> **Extended 2026-08-24** with the two owner corrections from
> [#83, 19:49:50Z](https://github.com/hleserg/Attadipa/issues/83#issuecomment-5382281381)
> that never reached the queue and are re-filed as
> [#182](https://github.com/hleserg/Attadipa/issues/182): the AK09911C's reset
> input (**§2.6**) and the pull-up arithmetic against the I²C sink limit
> (**§4.3.1**–**§4.3.4**). Both are answered from primary sources, and the parts
> of them that are about *the specific modules* stay `UNKNOWN` with the two
> ohmmeter probes that settle each one named.

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
| M1 | **AKM `AK09911` Short Datasheet**, `ShortDatasheet-E-00`, 2014/1, md5 `1d7e1960c86b2a1fb38ecc862196c4a7` — **re-fetched and the md5 re-checked on 2026-08-24**, so §2.6 below is quoting the same eighteen pages as the rest of this document and not a second copy that happens to share a title. It is the right document for the ordered part — `AK09911C` is the only variant named, in the package line and again in the recommended-connection schematic. It is a *short* datasheet: it runs to §9 but contains **no register address map** (see §2.4). It does, however, contain the pin table, the ball map and the reset specification — §2.6 |
| M2 | **QST `QMC5883L` Datasheet Rev. B**, document `13-52-04`, `QST-PD-B002-22`, fetched from `qstcorp.com`, md5 `d13221b15c034c3f9b24befa48c8f4ab`. Rev B, not the Rev 1.0 that most of the internet mirrors |
| M3 | **QST `QMI8658C` Rev 0.6**, md5 `3d2bd7b24172e5d3448f2c9ecf2ef752` — the IMU already on the board, consulted for §5. Marked `ADVANCE INFORMATION — CONFIDENTIAL AND PROPRIETARY` on every page; it is a pre-release document |
| M5 | **QST `QMI8658A` Datasheet Rev A**, doc `13-52-25`, md5 `5a0fef65a358430d6499944a75d22e19`. Admissible here as evidence about **M3's own document lineage** and nothing else: its revision-history rows 0.4, 0.5 and 0.6 are *verbatim identical* to M3's, so it is the same document renamed at 0.7. Used only for what the vendor did to the documentation — **never** for an electrical characteristic, per §5.3 |
| M4 | Owner's photographs of both AliExpress listings, 2026-08-22 — silkscreen, pin labels and the die marking only. A photograph of a module is evidence about *labels*, not about *nets*. The break-out pin lists it yielded are in [#83, 19:13:48Z](https://github.com/hleserg/Attadipa/issues/83#issuecomment-5382126078): **CJMCU-9911 → `VCC GND SCL SDA CAD RST TST`**, GY-271 → `VCC GND SCL SDA DRDY` |
| M6 | **NXP `UM10204`, *I²C-bus specification and user manual*, Rev. 7.0 — 1 October 2021**, 62 pages, md5 `f0e2e0922efd7eed0aa86a6eee40801a`, sha256 `dc91f00f65584e06ef36e26c93bf9d91a95fb3c8a1830a9223e53caf678b36af`, from `nxp.com/docs/en/user-guide/UM10204.pdf`. **This is the document behind "the 3 mA sink limit I²C specifies"** and it is cited by clause below, never carried over on trust. Note that Rev. 7.0 renamed *master/slave* to *controller/target* throughout; the electrical tables did not change with it |
| M7 | **The Waveshare schematic — the same file as `HARDWARE_MATRIX` S6**, `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf`, md5 `b0cdcac0afb0c8605896d995676c4468`, sha256 `6d531fb458863c666210c92294a07204d675bcb7997a54fc219d92fadbbacf9d`, re-read 2026-08-24. **The method is the news, not the file**: S6 was read by *text extraction*, which recovers designators and values but not the wires between them, and that is exactly why the pull-ups were never recovered from it. This reading **rendered** the region around `GPIO14`/`GPIO15` at 900 dpi and read the junction dots. §4.3. **Confirmed 2026-08-24 by a third method and a second context** — see M8 |
| M8 | **The same file again, read a third way — by extracting the PDF's vector paths** rather than its text or its pixels. Done in a context that had not made the M7 reading, on a copy re-downloaded from `waveshareteam/ESP32-S3-Touch-AMOLED-2.06` whose md5 and sha256 both matched M7's byte for byte. This is the strongest of the three methods and the one to reach for next: a schematic PDF stores wires as line segments and junction dots as filled curves, so **connectivity is recoverable as coordinates rather than as a judgement about a picture**. Every value and every net in §4.3.1 reproduced. §4.3.1 |

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
reason to: everything else on the main I2C bus is at 3.3 V. **The part has a
reset pin and its input thresholds are referenced to `VID`, not `VDD`** — §2.6,
which is where the reset material lives because it spans the electrical table,
the ball map and a question about the module.

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

### 2.6 The reset input `RSTN`, and why the answer is five wires

> Filed 2026-08-24 from [#182](https://github.com/hleserg/Attadipa/issues/182),
> which re-files an owner correction made on
> [#83, 19:49:50Z](https://github.com/hleserg/Attadipa/issues/83#issuecomment-5382281381)
> and never carried into the repository. **The correction was right and the
> reason it gave was the weaker of the two available**, which is worth stating
> because it changes what may be relied on.

The owner's correction rested on a comment in `drivers/iio/magnetometer/ak8975.c`
in mainline Linux — *"According to AK09911 datasheet, if reset GPIO is provided
then deassert reset on `ak8975_power_on()`…"* — and the owner marked the
neighbouring supply figure `SECONDARY SOURCE`, adding that *"the primary
datasheet has not been read here."* #182 restated that caution and expected the
short datasheet might be unable to settle it.

**It settles it.** `RSTN` occurs twelve times in M1.

#### 2.6.1 What M1 says, by clause

| Where | What it says |
|---|---|
| §4.3 Pin Function | **`C2` · `RSTN` · input · `VID` domain · CMOS.** *"Reset pin. Resets registers by setting to `L`."* |
| §4.1 block diagram | `RSTN` appears as one of the eight external nets, beside `CAD` and `TST` |
| §5.3.1 DC characteristics | `VIH1` and `VIL1` cover `RSTN` together with `SCL` and `SDA`: high ≥ **70 % `Vid`**, low ≤ **30 % `Vid`**. `IIN1` = **±10 µA** at `Vin` = `Vss` or `Vid` |
| §5.3.2 AC characteristics | **`tRSTL`, reset input effective pulse width (`L`) — 5 µs minimum.** There is no maximum |
| §6.2 Reset Functions | *"AK09911 has four types of reset: (1) Power on reset (POR) … (2) VID monitor … **(3) Reset pin (RSTN). AK09911 is reset by Reset pin. When Reset pin is not used, connect to VID.** (4) Soft reset … by setting `SRST` bit."* |
| §7 recommended connection | `RSTN` is drawn **driven from a host CPU `GPIO`**, with an arrow into the ball. `SDA` and `SCL` get external pull-ups to the `VID` rail; `TST` is drawn as a dotted circle |
| §8.2 pin assignment | the ball map, below |

The eight balls, from M1 §8.2, top view — nothing in this repository had recorded
them and the reset question cannot be discussed without them:

|  | 3 | 2 | 1 |
|---|---|---|---|
| **C** | `SDA` | **`RSTN`** | `VID` |
| **B** | `SCL` | *(no ball)* | `VSS` |
| **A** | `TST` | `CAD` | `VDD` |

So the part's reset input is real, documented, and specified down to a 5 µs
minimum pulse. The driver comment agrees with the datasheet; it is simply no
longer what we are standing on.

#### 2.6.2 The consequence is not "four wires or five" — it removes an option

The owner framed this as *"whether the module ties it high or brings it out to a
pad … decides whether this is four wires or five."* Reading M1 narrows it
differently, because **M1 forbids the third state**:

> *"When Reset pin is not used, connect to VID."*

That is not the same instruction as "leave it alone". `RSTN` is a CMOS input
with 30 %/70 % `Vid` thresholds and ±10 µA leakage — a floating one sits at
neither level, and this is a part whose only defence against a spurious reset is
the level on that ball. So there are exactly three outcomes, and only one of them
is four wires:

| What the module does with `RSTN` | Wires | What we must do |
|---|---|---|
| ties it to `VID` on-module | 4 | nothing. The pad, if present, is a test point |
| brings it out and ties it nowhere | **5** | tie the pad to `3V3` at minimum, or drive it from a GPIO |
| brings it out **and** ties it | 4 | nothing — but do not then drive the pad, or a GPIO fights a resistor or a short |

**Five wires is the default and four is the exception**, and the exception is
conditional on a continuity check nobody has made.

#### 2.6.3 What the module does — `OBSERVED`, and that is not `VERIFIED`

The owner's M4 photograph reading of 2026-08-22 lists the CJMCU-9911's break-out
pins as **`VCC GND SCL SDA CAD RST TST`**. So the pad exists and is silkscreened
`RST`.

**That is a label, not a net**, and M4's own definition in §1 says so. A pad
marked `RST` beside an AK09911C is overwhelmingly likely to reach ball `C2`, and
"overwhelmingly likely" is the standard this repository does not write code
against. It equally cannot say whether the module *also* fits a pull-up to `VID`
behind that pad, which is the whole question.

| Question | Status | What settles it |
|---|---|---|
| Does the AK09911C have a reset input? | **`VERIFIED`** — M1 §4.3, §5.3.2, §6, §7, §8.2 | done |
| Does the CJMCU-9911 break it out to a pad? | **`OBSERVED`**, M4 — a silkscreen `RST` | done to the strength a photograph allows |
| Does that pad reach ball `C2`? | **`UNKNOWN`** | continuity, module unpowered: ohmmeter from the `RST` pad to the `VID`/`VCC` pad and to `GND` |
| Does the module tie `RSTN` to `VID`? | **`UNKNOWN`** | the same two probes. A few kΩ or a short to `VCC` means tied; open to both means floating |
| Wire count | **five unless the check says otherwise** | the above |

`NOT EXECUTED — HARDWARE REQUIRED`. The modules have not arrived. This is two
ohmmeter probes on a bare module and needs no board, no power and no soldering,
which is why it belongs in T-109's acceptance rather than in a blocker.

#### 2.6.4 The module also breaks out `TST`, and M1 says not to connect it

The same M4 list ends `… CAD RST **TST**`. M1 is unusually blunt about that pin:

- §4.3: *"Test pin. Pulled down by 100 kΩ internal resister. **Keep this pin
  electrically non-connected.**"*
- §7, of the dotted circle it draws around `TST`: *"Pins of dot circle should be
  kept non-connected."*
- §5.3.1: `IIN2` on `TST` is **100 µA** at `Vin` = `Vdd` — three orders of
  magnitude above the ±10 µA on every other input, and 33× the part's own 3 µA
  power-down current.

So a `TST` pad on a hand-wired assembly is a hazard with a legend on it: strapping
it high costs more current than the sensor idles at, and what it does to the part
functionally is undocumented. **Leave the `TST` pad unwired and keep it away from
anything it could short to.** Recorded here because a break-out board that
exposes a pin the datasheet says to leave alone is exactly the kind of thing that
looks like a feature.

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
identical prohibition on the AK09911C — see §2.2. The AK09911C also has a
**reset input**, which the QMC5883L has no equivalent of and which decides how
many wires this retrofit takes — §2.6. Neither part is the safe one
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
| `0x18` | ES8311 codec — `VERIFIED` by scan |
| `0x34` | AXP2101 PMU — datasheet-fixed, `VERIFIED` by scan |
| `0x38` | FT3168 touch — answers only after its reset is pulsed on GPIO 9 |
| `0x40` | ES7210 microphone ADC — `VERIFIED` by scan |
| `0x51` | PCF85063ATL RTC — datasheet-fixed, `VERIFIED` by scan |
| `0x6B` | QMI8658 IMU — **`MEASURED`**. `0x6A` does not answer, so the datasheet-revision conflict is settled and `0x6A` is free |

**`0x0C`, `0x0D` and `0x1E` are all free**, measured on 2026-08-23 rather than
inferred — [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §3.1. No
conflict with anything already on the board, for either candidate.

An earlier version of this section listed six predicted addresses and marked the
IMU `CONFLICTING`. The scan replaced prediction with measurement on every row,
which is why the table is longer: it now says what answered, not what should.

The conflict is between the two *candidates*: QMC5883L is `0x0D` and cannot move;
AK09911C can be `0x0C` or `0x0D`. **Strap `CAD` to `VSS`.** Then both parts can
sit on the same bus at the same time — which is not a hypothetical, because
having both present is the only way to compare them against each other in the
same magnetic environment, on the same wrist, in one sitting.

An address is not the only thing two modules bring to a shared bus. Each carries
its own pull-ups, and those land in parallel with the board's.

#### 4.3.1 What the board already has — `R23` and `R49`, **2.2 kΩ**

Nothing in this repository recorded the Waveshare board's own `SDA`/`SCL`
pull-ups until 2026-08-24, and the arithmetic below could not start without them.
From M7, the V1.0 schematic — the revision the received unit's silkscreen matches
([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.1):

| Designator | Value | From | To | Net |
|---|---|---|---|---|
| **`R23`** | **2.2 kΩ** | `VCC3V3` | `ESP32_SCL` | `GPIO14`, aliased `TP_SCL`/`RTC_SCL` |
| **`R49`** | **2.2 kΩ** | `VCC3V3` | `ESP32_SDA` | `GPIO15`, aliased `TP_SDA`/`RTC_SDA` |
| `C34` | **22 pF** | `ESP32_SCL` | `AGND` | — |
| `C35` | **22 pF** | `ESP32_SDA` | `AGND` | — |

Both resistors share one node on `VCC3V3` and drop to the two bus lines; the
junction dots settle which goes where. There is **exactly one pull-up per line on
the whole drawing** — the I²C net labels occur 28 times and all of them are on
sheet 1, and no other resistor on any sheet touches them.

**Every line of that table was re-derived independently on 2026-08-24 from the
PDF's vector paths (M8), in a context that had not made the M7 reading**, and it
agrees. The geometry, in the drawing's own coordinates, because it is the
evidence and not a description of it:

- both resistors' upper leads meet at `(147.55, 438.15)` under a junction dot
  centred `(147.5, 438.09)`, and a stub rises from there to the `VCC3V3` symbol —
  so **both pull to `VCC3V3`**, and it is one node, not two;
- `R23`'s lower lead runs down `x = 142.23` and terminates on the wire at
  `y = 455.44`, dot at `(142.17, 455.39)`. `R49`'s runs down `x = 150.22` and
  terminates on the wire at `y = 452.78`, dot at `(150.16, 452.73)`;
- the `GPIO14`/`ESP32_SCL` label box spans the `y = 455.44` wire and
  `GPIO15`/`ESP32_SDA` the `y = 452.78` one. **So `R23` is the `SCL` pull-up and
  `R49` the `SDA` pull-up** — the assignment the table gives;
- `C34` rises from `x = 171.52` to the `SCL` wire, dot at `(171.47, 455.39)`;
  `C35` from `x = 176.85` to the `SDA` wire, dot at `(176.8, 452.73)`. Their lower
  leads join at `y = 468.74` and drop together to `AGND`;
- the label count is exact: `ESP32_SCL`/`ESP32_SDA` 9 each, `TP_SCL`/`TP_SDA` 3
  each, `RTC_SCL`/`RTC_SDA` 2 each — **28**, and sheets 2 and 3 carry none.

**This is a stronger claim than a rendered reading, and it is worth saying why.**
A render is pixels and a human judgement about whether two marks touch; the
vector paths are the endpoints the drawing tool wrote down. Where a wire crosses
another without connecting, the two are distinguishable by the presence or
absence of a filled curve at the crossing, which is precisely the question a
picture makes hardest. The value of the resistors never depended on this — both
lines see 2.2 kΩ either way, so no number in §4.3.3 moves — but
`HARDWARE_MATRIX` now asserts the designator-to-net mapping, and a hardware
matrix that names the wrong resistor is the kind of error this repository exists
to prevent.

**Two things here contradict what was assumed.** The owner's correction said
*"typically 4.7 kΩ or 10 kΩ"* and explicitly flagged that as typical; the board
is at **2.2 kΩ**, stiffer than either, so the arithmetic starts closer to the
limit than expected. And the **22 pF per line** was unrecorded anywhere — it is a
deliberate lump on top of pin and trace capacitance, and it matters to §4.3.4.

*Why this was not found before:* `HARDWARE_MATRIX` S6 records the schematic as
read "by text extraction", which recovers designators and values but not the
wires between them. The values `2.2k` were in that extraction all along with
nothing to attach them to. This reading rendered the sheet and read the junction
dots — see M7 — and a third reading recovered the same wires as coordinates
(M8).

**And that generalises past this row, which is the part worth carrying away.**
Three methods on one file, in increasing strength: text gives designators and
values; a render gives a picture of the wires; the vector paths give the wires
themselves. S6 has been read only the first way for everything except this bus,
and several questions still open against it are exactly connectivity questions —
`VCC3V3`'s source rail ([OPEN_QUESTIONS](OPEN_QUESTIONS.md) **D13**), what the
display flex carries (**D3**), and which of `Key1`/`Key3`/`PWRON` reaches a
finger (**D5**). None of those is answered here and none should be assumed to
fall out; but **the method that answered this one has not been tried on them**,
and that is a cheaper next move than a meter on a board. Recorded so the next
agent does not conclude from S6's "partially recoverable" that the file has been
exhausted.

#### 4.3.2 What the specification actually requires

The **3 mA** in the owner's correction is real and it is worth quoting rather
than carrying over. M6 is `UM10204` Rev. 7.0:

- **Table 10**, `VOL1`: *"LOW-level output voltage 1 (open-drain or
  open-collector) at 3 mA sink current; `VDD` > 2 V"* — 0 to **0.4 V**, and the
  same figures appear in the Standard-mode, Fast-mode and Fast-mode Plus columns.
- **§7.1**: *"The supply voltage limits the minimum value of resistor `Rp` due to
  the specified minimum sink current of **3 mA** for Standard-mode and Fast-mode,
  or 20 mA for Fast-mode Plus."*
- **Equation 2**, given in full by §7.2.4's worked example — *"with a supply
  voltage of `VDD` = 5 V ± 10 % and `VOL(max)` = 0.4 V at 3 mA, `Rp(min)` =
  (5.5 − 0.4) / 0.003 = 1.7 kΩ"*:

  ```
  Rp(min) = (VDD − VOL(max)) / IOL
  ```

  Note that NXP's own example uses the **top** of the supply tolerance, and
  §4.3.3 does the same.
- §4.2 states the number's provenance in one line: *"NXP devices have a higher
  power set of electrical characteristics than SMBus 1.0 … **I²C-bus = 3 mA**"*.

**The part we are adding is rated at exactly that and no more.** M1 §5.3.1:
`VOL` on `SDA`, condition `IOL ≤ +3 mA` with `Vid ≥ 2 V`, max **0.4 V**. The
AK09911C meets the I²C minimum and does not exceed it, so the specification's
limit *is* this part's limit; there is no headroom hiding in the silicon.

At 3.3 V, `Rp(min)` = (3.3 − 0.4) / 0.003 = **966.7 Ω**.

#### 4.3.3 The arithmetic, done

Pull-ups in parallel, one line at a time, sink current taken at `VOL` = 0.4 V.
Exact arithmetic given the module values — and those values are assumptions, so
read the warning under the table before using a row.

| On the line | `Rp` | `I` at 3.3 V | of 3 mA | `I` at 3.6 V | of 3 mA |
|---|---|---|---|---|---|
| board alone (2.2 kΩ) | 2200 Ω | 1.318 mA | 44 % | 1.455 mA | 48 % |
| + one module 10 kΩ | 1803 Ω | 1.608 mA | 54 % | 1.775 mA | 59 % |
| + one module 4.7 kΩ | 1499 Ω | 1.935 mA | 65 % | 2.135 mA | 71 % |
| + one module 2.2 kΩ | 1100 Ω | 2.636 mA | 88 % | 2.909 mA | 97 % |
| + **both** at 10 kΩ | 1528 Ω | 1.898 mA | 63 % | 2.095 mA | 70 % |
| + one 4.7 kΩ, one 10 kΩ | 1303 Ω | 2.225 mA | 74 % | 2.455 mA | 82 % |
| + **both** at 4.7 kΩ | 1136 Ω | **2.552 mA** | **85 %** | 2.816 mA | 94 % |
| + both at 3.3 kΩ | 943 Ω | 3.076 mA | **103 % — fails** | 3.394 mA | **113 % — fails** |
| + both at 2.2 kΩ | 733 Ω | 3.955 mA | **132 % — fails** | 4.364 mA | **146 % — fails** |

**The answer to the owner's question is that it passes**, for both values named
in the correction and for every mixture of them. Both modules at 4.7 kΩ — the
worse of the two named — puts the bus at 85 % of the limit at 3.3 V. The 3.6 V
column is there because `VCC3V3`'s source rail is
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) **D13**, still open, so its tolerance is not
established: 3.6 V is the top of the AK09911C's whole `VDD` range and the answer
survives even there, at 94 %. **No plausible rail tolerance overturns this.**

**So do not lift the modules' resistors on account of the named values.** The
owner's *conditional* was right — "if it comes out too low, the fix is to lift
them" — and the condition is not met.

**But the values are not read off the parts, and that is the honest status.**
Neither module's fitted pull-up is traceable to a primary source: no schematic is
published for either breakout — the one public GY-271 repository carries
datasheets and no drawing — and the "typically 4.7 kΩ or 10 kΩ" in the correction
was flagged as typical by the owner who wrote it. **`UNKNOWN`, per module,
`NOT MEASURED`.**

Rather than block on it, here is the pass line, so an ohmmeter reading answers it
without anyone redoing the algebra:

> **Threshold.** With the board's 2.2 kΩ fixed, the modules' *combined* pull-up
> on each line must be **≥ 1.72 kΩ** at 3.3 V, or **≥ 2.07 kΩ** at 3.6 V.
>
> - **one module attached:** its resistor must be **≥ 1.8 kΩ** on the E24 series
>   (**≥ 2.2 kΩ** if 3.6 V is assumed)
> - **both attached, equal values:** each must be **≥ 3.45 kΩ** at 3.3 V, so
>   **≥ 3.6 kΩ** on the E24 series — and **≥ 4.3 kΩ** if 3.6 V is assumed
>
> **4.7 kΩ passes. 3.3 kΩ fails. 2.2 kΩ fails badly.** Anything at or above
> 4.7 kΩ passes with the rail still unresolved.

**The measurement, which needs no board and no power:** an ohmmeter across each
module's `SDA` pad to its `VCC` pad, and `SCL` pad to `VCC` pad, module unpowered
and off the bus. Two readings per module. `NOT EXECUTED — HARDWARE REQUIRED`; the
modules have not arrived.

One residual this arithmetic does not cover: it holds every device on the bus to
the specification's 3 mA. If any *fitted* device sinks less than that, it reaches
0.4 V sooner and the margins above are optimistic for it. Of the devices on this
bus only the AK09911C's own figure has been read here; the ES8311, ES7210,
AXP2101, PCF85063ATL, FT3168 and QMI8658 have not been checked against it, and
that is a per-datasheet question this document does not answer.

#### 4.3.4 The constraint that is actually tight, and it is the other one

Pull-up sizing has two limits pulling in opposite directions, and only one of them
was asked about. `Rp(min)` guards sink current, above. `Rp(max)` guards rise
time, and M6 §7.1's Equation 1 is `Rp(max) = tr / (0.8473 × Cb)`, with `tr` from
Table 10 — **1000 ns Standard-mode, 300 ns Fast-mode** — and `Cb` the total bus
capacitance, itself capped at 400 pF.

Turned around, at a given `Rp` that is a ceiling on `Cb`:

| `Rp` on the line | at 100 kHz | at 400 kHz |
|---|---|---|
| 2200 Ω — board alone | 537 pF, so the 400 pF cap binds first | **161 pF** |
| 1136 Ω — both modules at 4.7 kΩ | 1039 pF | 311 pF |

**Adding module pull-ups moves the bus *away* from this limit while moving it
toward the sink limit**, which is why the two must be read together, and why
lifting the modules' resistors is not a free "safe" choice.

What is on the bus today, `ESTIMATED` and deliberately loose: M6 Table 10 caps
`Ci` at **10 pF per I/O pin**, there are six slave pins plus the SoC's, and the
board fits `C34`/`C35` at 22 pF each — so roughly **110 pF of parts alone**, and
**100–150 pF** all in once traces are allowed for, part of it arriving over the
display flex that carries the touch controller
([OPEN_QUESTIONS](OPEN_QUESTIONS.md) D3). **At 400 kHz that is already close to
the 161 pF ceiling**, and two modules on flying leads add two more pins and the
wire.

This costs nothing today: the bus scan that produced the address table above ran
at **100 kHz** ([WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md)
§3.1), where the 400 pF cap binds long before `Rp` does. It is written down
because "put it at 400 kHz, the parts support it" is the obvious next thought —
M1 §2.4 records the AK09911C reaching 2.5 MHz only under 100 pF — and on this
board that thought needs `Cb` measured first. `Cb` is **`UNKNOWN`** and wants a
scope on a rising edge, not a calculation.

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
- **The wiring is not four wires and the plan should stop saying so.** #83's body
  said *"four wires, no bus to invent"*; §2.6 makes five the default, because M1
  forbids leaving `RSTN` floating and only a continuity check on the module can
  show it is already tied. If the AKM part is fitted, the retrofit budget is
  `SDA`, `SCL`, `3V3`, `GND`, `CAD` to ground **and** `RSTN` — and whether the
  last of those reaches a GPIO or just a rail is a driver decision, not a wiring
  one. Nothing above the capability registry learns about it either way.
- **`RSTN` is the AKM part's only recovery path that a soft reset cannot
  provide.** M1 §6.2 lists four resets, and the other three all need the part to
  be answering: `SRST` needs a working bus, the `VID` monitor needs the interface
  rail cycled, POR needs `VDD` cycled — and on this board every rail is behind
  the PMU and shared. A wedged sensor with `RSTN` tied to `VID` can only be
  recovered by cutting a rail that other devices are on. This is the same shape
  as the touch controller's problem on the T-Watch, where the reset pull-up is
  not fitted and the only recovery is cycling a display rail
  ([HARDWARE_MATRIX](HARDWARE_MATRIX.md), the FT6336U row). Worth a GPIO if one
  is free.

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
