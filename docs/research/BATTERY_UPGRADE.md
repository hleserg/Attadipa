# A bigger cell for the Waveshare — what to order, and what has to be true first

Status: **research complete for the parts that do not need the board; blocked on
three measurements only the owner can take.** Raised by the owner on 2026-08-22.
Decision tracked as [#64](https://github.com/hleserg/Attadipa/issues/64);
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) D2.

## 0. What this note is, and one thing it is not

It was produced by a fan-out over four sources — the Waveshare schematic and
wiki, the AXP2101 datasheet cross-checked against XPowersLib, the vendor's own
ESP-IDF example, and 51 cells from four manufacturers' published datasheets.

**Its adversarial pass did not run.** Three verifiers (mechanical, electrical,
procurement) and the synthesis step were cut off by an account spend limit, so
what follows has been checked *for sources* but not *attacked*. Every number
carries its provenance; treat the reasoning between them as unreviewed. That is
recorded here rather than quietly omitted.

## 1. The headline, and it is not the one that was expected

**The fitted cell almost certainly does not hold 400 mAh.**

`402728` is 4.0 × 27 × 28 mm = **3.024 cm³**. At 3.7 V nominal, 400 mAh is
1.48 Wh, which implies **132.3 mAh/cm³**.

Across 51 cells with published datasheets from **EEMB, LiPol Battery Co, PKCELL
and Ufine Battery**, at footprints ≤ 32 mm, the observed band is **87–102
mAh/cm³**. 132.3 is **+22 % on the densest cell found in any footprint** in that
sample and **+52 % on the median** for comparable small-footprint parts.

So the honest expectation for that envelope is **250–310 mAh**, and the physical
ceiling is about **310 mAh**. `ESTIMATED` — see §6 for the measurement that
converts it into a number.

This changes what "upgrade" means. A same-size replacement buys **no capacity**.
What it buys is a cell with a published cycle life, a published charge rate and a
documented protection board — which is what lets firmware set the charge current
from a real number instead of a sticker. That is a legitimate reason to do it,
and it is not the reason the owner asked.

## 2. The safe default — needs no measurement at all

A pack whose **finished outline, including the PCM, the tape and the tab fold**,
is at or under **4.0 × 27 × 28 mm**. It cannot be larger than what is in the case
already, so it fits by construction and carries exactly the swell risk Waveshare
already accepted.

Specify all five clauses to the supplier before money moves:

1. **Finished PACK dimensions ≤ 4.0 × 27 × 28 mm** — stated as the *pack*, not
   the cell code. **This is the trap in this size class: a PCM adds 0–2 mm of
   LENGTH, never thickness.** Ufine ship their `502728` as a 5 × 27 × **30** pack
   and their `403030` as a 4 × 30 × **32** pack. A cell ordered as "402728" can
   arrive as a 4 × 27 × 30 pack that will not go into a bay the factory cell just
   fits.
2. **3.7 V nominal, 4.20 V ±50 mV termination** — ordinary LiCoO₂.
3. **Integrated PCM**, with over-charge, over-discharge and over-current trip
   points stated on the datasheet.
4. **2-pin housing on 1.25 mm pitch**, red lead on pin 1.
5. **A real manufacturer datasheet before purchase.** No datasheet, no order.

Expect **250–300 mAh**, `ESTIMATED`.

## 3. The sizing table, keyed on a measurement nobody has taken

`M1` is defined in §6 and is **not** the depth of the recess. The rule behind
every row:

> **max orderable PACK thickness = M1 ÷ 1.1**

The ÷1.1 is a 10 % swell allowance and it is not optional. A pouch that exactly
fills its bay bows the back cover inside a year, and on a screwed cover that
shows up as a cracked boss.

| If M1 and M2 come back… | Order | Expect | Against the real ~300 mAh | Charge current |
|---|---|---|---|---|
| **A — drop-in.** M1 4.0–5.0 mm, footprint ≤ 27 × 28 | 4.0 mm class, pack ≤ 4.0 × 27 × 28. Geometries `402728`, `402525`, `402530` (the last only if M2(b) clears 30 mm) | 250–310 mAh | **0.85–1.05×** | **150 mA**, code 6 → 0.50C |
| **B — ~1 mm spare depth.** M1 ≥ 5.1 mm | 4.6 mm class. **Ufine `462528`: 4.6 × 25 × 28, 330 mAh, PCM fitted, 6.7 g, IR < 200 mΩ, 500 cycles ≥ 80 %** — 102.5 mAh/cm³, the densest sub-30 mm part in the whole sample, 0.6 mm thicker than stock | 330 mAh | **1.1×** | **150 mA**, code 6 → 0.45C |
| **C — ~2 mm spare depth**, *and* ≥ 30 mm of clear length | 5.5 mm class at 27 × 30. **LiPol `LP552730`: 400 mAh min / 430 typ, PCM inside the quoted outline, max charge 200 mA** | 400–430 mAh | **1.3–1.4×** | **200 mA**, code 8 → 0.50C |
| **D — spare footprint, no spare depth.** M1 still 4.0–5.0 mm | 4.0 mm class, larger plan area, sized to whatever M2 clears. `LP402933` 320/330 · `LP403035` 400/430 · `LP403040` 420/430 — all PCM-fitted packs | 320–430 mAh | 1.1–1.4× | 150 mA (≤330) / 200 mA (≥400) |

**What 400 mAh actually costs in this footprint is 5.5 mm of thickness, not 4.0.**
EEMB's `LP542730` (5.4 × 27 × 30) rates 380 nominal / 350 minimum, which brackets
the same conclusion from a second manufacturer.

**If M1 comes back ≥ 7.2 mm**, `LP652530` opens: 500 mAh minimum / 530 typical,
108.7 mAh/cm³, PCM, on a 25 × 30 footprint — about **1.7×** the real factory
cell, and the largest part found that will lie flat in a body this size. Note its
0.5C is 250 mA, which **the register cannot express** (§4).

**If M1 comes back below 4.4 mm**, the factory cell is already outside its own
10 % swell margin. Order 3.5 mm, not 4.0.

**Geometry warning for row D.** The case body is **42.00 mm** externally and
**12.90 mm** thick for the entire stack — AMOLED, PCB, cell and two cover walls
(VERIFIED, Waveshare's own `Esp32-s3-touch-amoled-2_06_dimensions.pdf`). A
rectangle that fits on paper can still fail to lie flat in a round body, so M2
asks for the **diagonal** too.

## 4. Charge current is never inherited, and both available defaults are wrong

`REG 0x62[4:0]` sets constant-current charge. **25 mA steps from 0 to 200 mA
(codes 0–8), then it jumps straight to 300 mA (code 9)** and rises in 100 mA
steps to 1000 mA. `25 × N` mA for N ≤ 8; `200 + 100 × (N − 8)` mA for N > 8.

**There is no code between 200 mA and 300 mA**, and that gap is load-bearing: a
500 mAh cell's 0.5C is 250 mA, which does not exist, so it gets **200 mA (0.40C)
and never 300 mA (0.60C)**.

> **The rule: `I_CC = 0.5 × C_real`, rounded DOWN to an available code.**

**Do not inherit the vendor demo.** Waveshare's
`examples/esp-idf/01_AXP2101/main/port_axp2101.cpp` sets **400 mA**. On the real
~300 mAh cell that is **1.33C**, above the 1.0C absolute maximum for this pouch
class — and it was a *deliberate* change: upstream XPowersLib's copy of the same
file sets 200 mA / 4.1 V. Even if the sticker were honest, 400 mA is 1.0C, twice
the 0.5C standard charge every datasheet in this class specifies (`LP552730`:
200 mA max. `LP402933`: 160 mA max. Ufine `502728`: 72 mA standard, 360 mA
absolute maximum).

**Do not inherit the POR default either.** `REG 0x62`'s reset value is
eFuse-trimmed — the datasheet prints it `{EFUSE, 0b, EFUSE}`, so there is no
silicon constant to quote. X-Powers' prose gives the intended value as 300 mA,
which is 1.0C on the real cell. **It has never been read on this board**;
`UNKNOWN` until one I²C read says otherwise, and it must be **written explicitly
at every PMU init** regardless, because the Waveshare BSP component configures
the charger not at all.

**Two more registers are wrong in the other direction.** `REG 0x61` (precharge)
and `REG 0x63[3:0]` (termination) both default to **125 mA**, which on a ~300 mAh
cell is 0.42C — four times the ≤0.1C convention for precharging a cell below
3.0 V, and a termination current high enough to stop the charge early and leave
the cell part-full while the gauge reads 100 %. Set **precharge 25–50 mA,
termination 25 mA**. This is the one thing in Waveshare's demo worth keeping: it
already sets 50 mA / 25 mA.

**And the input limit.** `REG 0x16` defaults to `100b` = **1500 mA**, which is not
USB-compliant on a port that granted 500 mA. An unconfigured AXP2101 pulls 1.5 A
from a host that never finished enumerating. Set **500 mA** until enumeration
says otherwise; on a linear charger this also caps die dissipation, which matters
in a sealed watch case.

## 5. Non-negotiable, and two of these destroy hardware

- **4.2 V chemistry only.** Nothing marked "HV", "high voltage", 4.35 V or 4.4 V.
  Their advertised capacity assumes a termination this board will never apply, so
  the energy is unreachable — and a mismarked HV cell in a system whose firmware
  someone later "optimises" upward is a fire path with a plausible commit behind
  it.
- **Never write `REG 0x64 = 100b` (4.35 V) or `101b` (4.4 V).** Leave it at
  `011b` = 4.2 V. **And never write `000b`**: datasheet V1.0 §6.13.2.62 prints
  that code *reserved*, V1.4 prints it *5.0 V*. One of those documents is wrong
  about what the silicon does, and finding out empirically costs a cell.
- **PCM mandatory, integrated in the pack.** This board has **no protection FET,
  no fuel-gauge IC, no load switch and no series sense resistor**: net `VBAT1`
  has exactly three connections on the whole schematic — header pin 1, a 2.2 µF
  cap, and the AXP2101 `BAT` pin. The PMU is the only other line of defence and
  it cannot protect against a short across the cell's own leads.
- **Polarity, verified with a meter, every time.** Pin 1 = positive = the pin the
  board's silkscreen marks `+`. **Never on wire colour alone** — pre-wired cells
  ship with the polarity effectively at random, and reverse polarity into the
  `BAT` pin is not protected on this board. If the pack arrives on a different
  housing (LiPol ship Molex 51021-0200 on some parts and JST PHR-2 on others),
  do not force it: specify the housing at order time, or re-terminate — and
  re-terminating means **cutting one wire at a time**, insulating it, then the
  other. Cutting both at once shorts the pouch across the blades of the cutter.
- **A third (NTC) wire cannot be used and must not be left loose.** Some parts in
  these geometries ship with one — `LP602530` carries a 10 kΩ 1 % B3380 NTC. The
  header is 2-pin and the AXP2101's `TS` pin is tied to ground through `RP2` on
  the PCB, so the lead has nowhere to go. Insulate it and tape it to the pack. It
  is not a reason to reject the cell.
- **Set `REG 0x50` bit 4 = 1 at every PMU init, for every row.** `TS` carries no
  battery-safety information on this board: `RP2` is a fixed 10 kΩ on the *PCB*,
  reporting board temperature near the PMU and never cell temperature, and no
  2-wire replacement can change that. Leaving `TS` gating the charger buys
  nothing and adds a failure mode. Waveshare's own demo already does this, with
  the comment *"It is necessary to disable the detection function of the TS pin
  on the board without the battery temperature detection function, otherwise it
  will cause abnormal charging"* — somebody was bitten by this.
- **Density sanity check on every listing.** `T × W × L (mm) ÷ 1000` = cm³, × 90
  = the honest expectation. 80–100 mAh/cm³ is normal, 100–110 is the top of the
  published range, **above 110 in a footprint under ~32 mm is suspect**, and
  above 125 anywhere in this size class is supported by no manufacturer datasheet
  found. The factory cell's implied 132.3 sits in that last bracket.
- **The fuel gauge takes the measured capacity, not the sticker.** The AXP2101's
  percentage is a voltage-based estimator with no coulomb counting and no gauge
  IC. Feeding it a design capacity 22 % above the best cell ever measured in this
  class propagates that error into every runtime figure a user is shown.

## 6. The measurements — and M1 is not the obvious one

**M1 — closed-case clearance over the bay.** *Not* the depth of the recess, and
not measured with the cover off. Remove the cell; put three ~6 mm balls of
plasticine in the bay — one at the cell's centre, one near each end; lay thin
paper over them so they do not stick; fit the back cover and tighten to normal
torque; reopen; caliper each squashed ball at its centre. **The smallest of the
three is M1**, to 0.1 mm. Take it **after** the speaker wires are in their final
routing, and note whether the balls sat on or beside them.

**M2 — usable footprint, two numbers to 0.5 mm.** (a) the largest **clear
rectangle**, measured to the nearest obstruction on each side — wall, screw boss,
connector body, component — not to the widest point of the cavity. (b) the clear
distance from the **face of the battery header** to the opposite wall. The plug
and its wire loop need 4–6 mm the cell cannot use, so maximum cell length is
(b) − ~5 mm, and a further 0–2 mm if the pack carries its PCM at the tab end.
Record which of (a)'s dimensions runs along the plug axis, **and the diagonal**.

**M3 — the factory cell itself, and this is the lie detector.** Caliper the
thickness at the **centre** of the pouch, not at the sealed edge. Caliper the
width. Caliper the length twice: across the pouch body, and from the far edge to
the very end of whatever is at the tab end — feel for a hard rectangular lump and
say whether you find one. Then **weigh it, plug attached, to 0.1 g**.

> Pouches in this class run **1.74–2.26 g/cm³** and **161–201 Wh/kg** across four
> manufacturers. **6.0–6.5 g** is consistent with 280–330 mAh and confirms the
> sticker is optimistic. **7.5–8 g** would be the only mass consistent with a
> genuine 400 mAh — and no sampled pouch reaches that density, so a heavy reading
> more likely means the cell is thicker than 4.0 mm, which M3's calipers then
> settle.

**Plug pitch** — caliper across the **crimp centres** of the two contacts, not
across the housing. Waveshare name the header `MX1.25` on the wiki and product
page, and a scale-calibrated reading off their own PCB drawing gives 1.30 ±0.05
mm (1.0 mm would read 12.3 pt and 1.5 mm 18.5 pt on that page; both excluded by
many times the jitter). The pitch of the plug **on this cell** has still never
been measured, and [HARDWARE_MATRIX](HARDWARE_MATRIX.md) carries it as `LIKELY`.

### The speaker wires — five rules, not a measurement

1. **No wire under the pouch, ever.** A pouch resting on a wire is a point load;
   a pouch punctured by a wire is a fire, not a fault. Route the pair around the
   **perimeter** and hold it with Kapton or a dab of RTV.
2. If they genuinely cannot be routed clear, caliper the pair's diameter and
   **subtract it from M1**. Measure it; do not guess it.
3. Never trap a wire between the cell and the cover, or under a screw boss.
4. Take M1 with the wires in their final routing.
5. Waveshare's own warning, verbatim: *"When disassembling, special attention
   should be paid to protecting the cable area. This part is vulnerable and prone
   to breakage due to pulling, twisting or forceful operation… Support the
   mainboard or cables to keep them in a natural and tension-free state."*

### On the board, whenever it is convenient

- **One I²C read burst at `0x34`.** All of these are eFuse-defaulted with no
  datasheet POR value, so every "default" claimed for them is `UNKNOWN` until
  read: `0x62` (CC charge current — the number this whole note is about),
  `0x50` bits 4 and 3:2 (whether `TS` gates the charger and whether its current
  source is on), `0x58` (JEITA), `0x12` bit 3 (BATFET when powered off on
  battery), `0x69` bits 2:1 (CHGLED mode — moot, that net goes nowhere on this
  board, but read it anyway).
- **`RP2`: NTC or plain resistor?** Measure cold, warm the board with hot air,
  measure again. An NTC falls; a resistor does not. The schematic symbol is a
  plain zigzag identical to `RP1`/`RP3`/`RP4` with no NTC marker, but the value
  string reads *"Thermistor 10K"*. `NOT EXECUTED — HARDWARE REQUIRED`. It changes
  nothing about the cell choice — `RP2` reports board temperature either way.

## 7. No runtime figures, and why

Nothing here says how many hours anything lasts. That needs a measured panel
current, which does not exist — **T-095**. The vendor's own claims (≈1 h at full
brightness, 3–4 h screen off, ≈6 h low-power) are `ESTIMATED-by-vendor` with no
stated method, no brightness, no radio state, and boilerplate that refers to a
*"screen backlight"* an AMOLED does not have.

The one measurement that fixes both: a shunt in the battery lead, a constant
load, 4.20 V down to 3.00 V, integrated. Do it on the factory cell **before** it
is replaced and again on the replacement. **While the shunt is in, take the
day-theme and night-theme currents on the same screen** — that is T-095, and it
is what the bigger cell is being bought for.

## 8. Status of every claim here

| Claim | Status |
|---|---|
| Cell is `402728`, marked 400 mAh / 3.7 V, on a removable 2-pin plug | **VERIFIED** — read off the received unit |
| Real capacity ~250–310 mAh | **ESTIMATED** — 51 datasheet cells, four manufacturers. M3 converts it |
| Case 42.00 mm across, 12.90 mm thick | **VERIFIED** — Waveshare mechanical drawing |
| `VBAT1` has three connections and no protection FET | **VERIFIED** — schematic |
| `TS` tied to ground through `RP2` on the PCB | **VERIFIED** — schematic |
| `RP2` is an NTC or a fixed resistor | **CONFLICTING** — symbol says resistor, value string says thermistor |
| `REG 0x62` POR value | **UNKNOWN** — eFuse-trimmed, never read on this board |
| Vendor demo sets 400 mA | **VERIFIED** — vendor source |
| Header is 1.25 mm pitch | **LIKELY** — vendor names `MX1.25`; the plug has not been measured |
| Cavity dimensions | **UNKNOWN** — M1, M2 |
| Any runtime in hours | **UNKNOWN**, and deliberately absent — T-095 |

Nothing above is `PASS`, `VERIFIED` or `MEASURED` on the strength of this note
alone. The adversarial pass did not run (§0).
