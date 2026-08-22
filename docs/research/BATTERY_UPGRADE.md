# Battery: what can replace the fitted cell, and what has to be measured first

Status: **research complete for everything that does not need the board;
blocked on four measurements only the owner can take.** Raised by the owner
2026-08-22. Decision tracked as [#64](https://github.com/hleserg/Attadipa/issues/64);
[OPEN_QUESTIONS](OPEN_QUESTIONS.md) D2.

> ## PAUSED — 2026-08-22, owner's decision
>
> **Calipers are on order and have not arrived**, so none of §4's measurements
> can be taken yet. The owner has also ordered a **magnetometer** to solder onto
> this unit, and wants its size known before the cell is chosen — because
> whatever the magnetometer occupies comes out of the same cavity. **The two
> questions are now coupled and the battery is downstream of the magnetometer** —
> [#83](https://github.com/hleserg/Attadipa/issues/83), and this issue is
> [#64](https://github.com/hleserg/Attadipa/issues/64).
>
> **One thing the owner did establish, by handling the unit** — recorded as
> `OBSERVED`, not `MEASURED`, because it is a judgement by eye and hand and not a
> caliper reading:
>
> > *"место под крышкой есть, текущие 400 mAh не упираются ни по толщине, ни по
> > ширине, ни по длине. Просто китайцы сэкономили."*
>
> There is room under the cover, and the fitted cell butts against nothing in any
> of the three axes. **That is a real finding and it changes the odds**: Row A of
> §5 — drop-in only, no spare depth — is now the *unlikely* outcome rather than
> the default one, and Rows B through D become worth taking seriously. It does
> **not** substitute for M1: "not touching" and "has 1.1 mm of clearance" are
> different statements, and only one of them sizes a cell. Nothing in §5 has been
> promoted on the strength of it.

## 0. What this note is

Four source fan-outs fed it — the Waveshare schematic and wiki, the AXP2101
datasheets cross-checked against XPowersLib, the vendor's own ESP-IDF example,
and 51 cells from four manufacturers' published datasheets — and then **three
adversarial passes attacked the draft that came out of them: mechanical,
electrical and procurement. All three refuted it.** Their corrections are
applied throughout, and §11 lists the claims that were dropped, with the reason.
The earlier version of this file said its adversarial pass had not run. It has
now, and four of its findings changed an answer rather than a wording: the
standing charge current, the currents in the larger-cell rows, what the
no-measurement default may specify, and which AXP2101 is on the board.

Two facts in this note were checked first-hand in this session rather than taken
from a fan-out, and are marked `(this session)` where they appear: the battery
connector's designator, extracted from the schematic's own word coordinates, and
the case dimensions, rendered from the vendor drawing at 150 and 300 dpi.

**D2's three open charge-path items are now answered**: the `TS`/NTC termination
is traced (§1.1), the connector is identified as `J1` with its pitch still
`LIKELY` (§1.1, §1.3), and the vendor firmware's charge current is found — and
rejected (§6).

---

## 1. The battery path, by evidence class

### 1.1 `VERIFIED`

Everything here is traced to the vendor schematic, the vendor mechanical
drawing, vendor source code, or the owner's own unit. Nothing here rests on a
marketplace listing.

| Fact | Source |
|---|---|
| **The battery connector is `J1`**, not `BAT1`. Pin 1 = net `VBAT1` = **positive**; pin 2 = `GND`; a third pad numbered 0 also goes to `GND` as a mechanical anchor. Bottom silkscreen prints `BAT` beside it with a `+` on the pin-1 side. | Schematic p1 + p3. **`(this session)`**: word-coordinate extraction puts `J1` at (267.4, 193.8) directly beside `VBAT1` at (297.2, 189.9), while the motor block — `MOTOR`, `R12 4.7K`, `R13 47K`, `Q1`, `R7 0R`, pads `P1`/`P2` — sits together at x ≈ 154–205. The designator list contains `COJ1`, `COJ2`, `COP1`–`COP6` and **no `BAT1` anywhere** |
| **Net `VBAT1` has exactly three connections on the whole sheet**: `J1` pin 1, `CP5` 2.2 µF to `GND`, and AXP2101 pin 33 (`BAT`). **No protection FET, no fuel-gauge IC, no load switch, no series sense resistor, no disconnect switch.** | Schematic p1; `VBAT1` occurs 4× page-wide, one of which is the rail-summary table row `VBAT1 → CHG BAT` **`(this session)`** |
| **The AXP2101 `TS` pin (31) goes through `RP2`, a 10 kΩ part, to `GND` and nowhere else.** It does **not** reach `J1`. The cell has two wires, so the charger never sees cell temperature — only whatever `RP2` reports about the board near the PMU. | Schematic p1; net `TS` occurs exactly twice (the wire label and the pin name). Value string reads `Thermistor 10K` **`(this session)`** |
| **There is no charge LED.** The `CHGLED` net (pin 1) terminates in open space — no port, no component. | Schematic p1 |
| **There is no RTC coin cell.** `VBACKUP` (pin 27) carries only a 2.2 µF cap; the PCF85063 RTC is held up from the main battery through `RTCLDO` (pin 28). **A battery swap will reset the RTC** — the sibling 1.8 board has a backup-battery pad, this one does not. | Schematic p1; Waveshare wiki |
| **Case external envelope**: 50.80 mm across (including the strap-lug bars), 42.00 mm body height, **13.60 mm total thickness with a 12.90 mm body step**, and the rear face carries a central rounded-rectangle plate dimensioned **34.60 × 25.80 mm with `R4.5`** on its corner. | `Esp32-s3-touch-amoled-2_06_dimensions.pdf`. **`(this session)`**: rendered at 150 dpi whole-page and 300 dpi on the rear view; the `R4.5` leader lands on that plate's lower-right corner, not on the case body's |
| **The fitted cell is labelled `402728`, 3.7 V, 400 MAH**, on red/black leads in a 2-pin white plug that mates with a through-hole header. Not soldered. | Owner's unit, [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.2 |
| **Waveshare's demo sets the charger to 400 mA CC**, 4.2 V, precharge 50 mA, termination 25 mA, `TS` measurement disabled. This was a deliberate change: upstream XPowersLib's copy of the same file sets 200 mA / 4.1 V. | `examples/esp-idf/01_AXP2101/main/port_axp2101.cpp`, vendor repo; diff against `/root/upstream/XPowersLib` |
| **Waveshare's BSP component configures the charger not at all**, and the shipped `xiaozhi` factory `.bin` is opaque. | Vendor repo |
| **Waveshare never states the fitted cell's capacity.** The only number they publish is a *recommendation*, in one FAQ line: *"The recommended battery specification is 4\*27\*28 400mAh."* The header is named *"3.7 V MX1.25 lithium battery recharge/discharge header"*; sibling boards' pages spell it *"MX1.25 2P connector"*. | Wiki FAQ, product page, sibling 1.75 / 1.8 / 2.16 pages |
| **No standalone replacement cell is sold** for this board — the cell exists only as a bundled SKU option. Waveshare's own advice: *"be sure to choose a lithium battery product that is safe and compliant, has protective functions."* | Product page, wiki |

### 1.2 `ESTIMATED`

| Estimate | Basis |
|---|---|
| **The fitted cell holds ~250–310 mAh, not 400 mAh. Working figure: 300 mAh.** | §3. Three independent lines — volumetric comparison against datasheeted cells, gravimetric, and pouch-seal overhead — converge there |
| **Density band for this class: 87–102 mAh/cm³** at footprints ≤ 32 mm; 77–109 across all footprints, median 91.4. | 51 cells with published datasheets from EEMB, LiPol Battery Co, PKCELL and Ufine Battery |
| **Mass band: 1.74–2.26 g/cm³ and 161–201 Wh/kg.** | Same sample |
| Vendor runtime claims — ≈1 h at full brightness, 3–4 h "screen backlight off", ≈6 h low-power. | `ESTIMATED-by-vendor`: no method, no brightness, no radio state, and the phrase *"screen backlight"* describes something an AMOLED does not have. See §10 |
| The community 900 mAh case-mod: 36 × 30 × 8 mm cell, case ~4.5 mm taller, **speaker removed**, screws and strap reused. | MakerWorld model 2763002. `makerworld.com` returned HTTP 403 to direct fetch; details came from search-engine extraction, obtained twice from two independently worded queries with consistent content. **Credible community report, not a verified fact**, and the cell was marketplace-sourced |

### 1.3 `UNKNOWN`

Everything in this list blocks something in this note, and none of it can be
closed from a document.

- **Every internal dimension of the case.** No section view, no cavity
  dimension, no STEP / STL / DXF / DWG anywhere in the wiki's Resources list.
  The drawing's own PDF title says it was printed from a 3D model that Waveshare
  does not publish.
- **Whether there is a battery "bay" with a floor and walls at all**, and which
  side the cell rests against. The `J1` header is on the back-facing PCB face,
  which also carries `U2` (the ESP32-S3, 7 × 7 mm QFN56, roughly 9 mm away),
  `U3` (the GD25Q256 SOP-8), a large `J2` footprint, `U5`, `U6`, `X1`, `Y1`,
  `IND2`, `L1`, `L2`, `MIC1`, `MIC2`, the `P1`/`P2` motor pads, the `P5`/`P6`
  speaker pads and roughly twenty-five passives. The speaker is mounted in the
  back cover. Whether the pouch lies across that populated face or sits in a
  moulded pocket in the cover is not established — and the two cases need
  different measurements. This is **M0** in §4.
- **The plug pitch on this particular cell.** `LIKELY` 1.25 mm: Waveshare name
  the header `MX1.25`, and a scale-calibrated reading off their own PCB drawing
  gives 1.30 ± 0.05 mm (1.0 mm would read 12.3 pt and 1.5 mm 18.5 pt on that
  page — both excluded by many times the jitter). The plug itself has never been
  calipered. `MX1.25` is generic Chinese-market usage for a PicoBlade-geometry
  part; it does not mean genuine Molex, and **no SKU is asserted here**.
- **Whether the fitted cell carries a PCM.** Not visible. It decides whether a
  replacement of the same code arrives 28 mm or ~30 mm long.
- **Which AXP2101 variant is fitted**, and this is not a footnote — see §6.
  Evidence leans towards the **SWcharge** (switching-charger) part, whose charge
  current goes to 1500 mA, not the linear part that tops out at 1000 mA.
- **Whether `RP2` is a real NTC or a plain 10 kΩ.** The symbol is a plain zigzag
  identical to `RP1`/`RP3`/`RP4` with no NTC marker; the value string says
  `Thermistor 10K`. `CONFLICTING`. It changes nothing about which cell to buy —
  either way `RP2` is on the PCB and reports board temperature — but it changes
  what §7's interlock can use as an input.
- **Five eFuse-defaulted registers with no datasheet-stated POR value**: `0x62`
  (CC charge current), `0x50` bits 4 and 3:2 (does `TS` gate the charger; is its
  current source on), `0x58` (JEITA enable), `0x12` bit 3 (BATFET when powered
  off on battery), `0x69` bits 2:1 (CHGLED mode — moot, that net goes nowhere,
  but read it anyway). One I²C burst at `0x34` settles all five.
- **The board's peak current draw.** Needed to state a PCM over-current
  acceptance criterion (§8) and never measured.
- **Stock availability of every geometry named in §5.** Datasheet existence was
  verified; orderability was not. Ufine state no MOQ on "conventional" cells and
  3000 pcs on customised ones, without saying which of these are which; LiPol's
  in-stock page lists none of the parts named here — which is not proof of
  unavailability, but is proof that nobody checked.

### 1.4 Wrong in the repository, and wrong in the briefing

Recorded here as findings. **This note does not edit those files** — that is a
separate task, §12.

- **`HARDWARE_MATRIX` line 304 and `VERIFIED_FACTS` say "connector `BAT1`".**
  There is no `BAT1` on this board, in the schematic or on either silkscreen
  layer. It is `J1`, silkscreened `BAT`.
- **`HARDWARE_MATRIX`'s Waveshare vibration-motor row says the motor is "on
  connector `J1`".** It is not: the motor lands on two single pads `P1`/`P2`,
  silkscreened `MOTOR` with a `+`, beside an empty coin-motor outline.
- **The same row gives the motor rail as `BLDO2`.** The schematic says `ALDO3`,
  through `R7` (0 Ω 0402) to `P1`.
- **That row also omits `R13`**, a 47 kΩ base-to-`GND` pulldown on `Q1`.
- **One fan-out source claimed `J1` is the motor connector.** It is not; that
  reading came from flat text extraction. Two independent coordinate-level
  readings and the bottom silkscreen agree it is the battery. `J2` is a separate
  connector elsewhere on the sheet.
- **This task's own briefing carried four T-Watch S3 Plus facts as if they were
  this board's** — `R182`/CHGLED, the `MSK12C02-HB` disconnect switch, the
  `MS412FE` coin cell and 940 mAh. All four are T-Watch rows in
  `HARDWARE_MATRIX`. This board has none of those parts.

---

## 2. The safe default that needs no measurement

**The parent asked for a pick that needs no measurement. Here it is — but the
draft's version of this section was refuted and the repaired version is weaker
than it sounds, so read the whole of it.**

**Refuted:** the draft said *"a pack whose finished outline is at or under
4.0 × 27 × 28 mm … cannot be larger than what is in the case already, so it fits
by construction."* That is false, for three compounding reasons:

1. Manufacturers in this class quote **nominal plus tolerance, not maxima**.
   LiPol quote packs in these geometries as `4.0 ±0.3 × 30 +0.5/−0.5 × 35 ±1.0`;
   EEMB specify their 4.0 mm-class `LP402535` as `T ≤ 4.3`. A fully compliant
   "4.0 × 27 × 28" pack can arrive at 4.3 × 27.5 × 29.
2. Run that through this note's own swell rule: 4.3 × 1.1 = **4.73 mm**, which
   is thicker than the bay was ever designed around.
3. **The baseline is itself unmeasured.** `402728` is a supplier's marking on an
   unbranded pouch, not a caliper reading. Comparing an untoleranced order
   against an unmeasured reference is not a fit guarantee.

**The repaired no-measurement pick**, stated as five clauses to the supplier
before money moves:

1. **Maximum finished PACK dimensions, inclusive of all tolerances, ≤ 4.0 × 27 ×
   28 mm** — the *pack*, not the cell code, and *maximum*, not nominal. In
   practice that means ordering the **3.5–3.8 mm nominal thickness class**, not
   the 4.0 mm class, so that the tolerance band still lands under 4.0 mm.
   **The trap in this size class: a PCM adds 0–2 mm of LENGTH, never thickness.**
   Ufine ship their `502728` as a 5 × 27 × **30** pack and their `403030` as a
   4 × 30 × **32** pack. A cell ordered as "402728" can arrive 30 mm long and not
   go in.
2. **3.7 V nominal, 4.20 V ±50 mV termination** — ordinary LiCoO₂, not "HV".
3. **Integrated PCM**, with over-charge, over-discharge and over-current trip
   points printed on the datasheet (acceptance criterion in §8).
4. **2-pin housing on 1.25 mm pitch**, or bare tinned leads — see §8's connector
   rule, which prefers bare leads.
5. **A real manufacturer datasheet before purchase.** No datasheet, no order —
   and see §3 for what to do when that rule rejects everything you can find.

**Expect 250–300 mAh, `ESTIMATED`.** Against the fitted cell's *real* capacity
that is **0.83–1.0×**. Against its sticker it is 0.63–0.75×. **This pick buys no
energy.** What it buys is a cell with a published cycle life, a published charge
rate and a documented protection board — which is what lets firmware set
`REG 0x62` from a real number instead of a sticker, and gives a second cell so
the first can be tested without bricking the watch.

**M3 before ordering is strictly better than this default**, costs one caliper
and one kitchen scale, and takes ten minutes. The no-measurement pick exists
because it was asked for, not because it is the recommended path.

---

## 3. Spotting a fake listing — and what to do when the screen rejects everything

### The screen

`402728` is 4.0 × 27 × 28 mm = **3.024 cm³**. At 3.7 V nominal, 400 mAh is
1.48 Wh, which implies **132.3 mAh/cm³ (489 Wh/L)**.

Across 51 datasheeted cells from EEMB, LiPol, PKCELL and Ufine, the observed
band at footprints ≤ 32 mm is **87–102 mAh/cm³**, median 91.4 across the whole
sample. 132.3 is **+22 % on the densest cell found in any footprint** and
**+52 % on the median** for comparable small-footprint parts.

> **The screen: `T × W × L (mm) ÷ 1000` = cm³, then × 90 = the honest
> expectation.**
>
> - **80–100 mAh/cm³** — normal, believable.
> - **100–110** — top of the published range; only credible for a
>   current-generation cell.
> - **> 110 in a footprint under ~32 mm** — suspect. No datasheet in the sample
>   supports it.
> - **> 125 anywhere in this size class** — supported by no manufacturer
>   datasheet found. The fitted cell's implied 132.3 sits here.

Three cross-checks make the same point without arithmetic on a listing:

- **Volumetric.** Ufine's `502728` is the *same footprint*, 25 % *more* volume,
  and rated **360 mAh**. LiPol's `LP402933` is the *same 4.0 mm thickness* with a
  27 % *bigger* footprint and rated **320 mAh minimum**. EEMB's `LP542730`
  (5.4 × 27 × 30) is 380 nominal / 350 minimum. **What 400 mAh actually costs in
  a 27 × 30 footprint is 5.5 mm of thickness** — LiPol's `LP552730`.
- **Gravimetric.** A genuine 402728 should weigh 5.4–6.8 g and hold 275–330 mAh.
  1.48 Wh at the best density observed anywhere (201 Wh/kg) would need 7.4 g,
  i.e. 2.43 g/cm³ — denser than every pouch in the sample.
- **Seal overhead.** At 27 × 28 mm the pouch seal margins and tab area take
  roughly 40 % of the plan area and ~0.2 mm of thickness, leaving ~1.6–1.7 cm³
  of active stack. 1.48 Wh there means ~880 Wh/L at stack level. That is why the
  same makers only reach 117–122 mAh/cm³ once the footprint passes 34 × 42 mm.

**Verdict on the fitted cell: `400 mAh` is optimistic to the point of not being
credible as a datasheet-grade rating.** Not a physical impossibility; simply not
something any manufacturer that publishes datasheets sells in this envelope.

### What to do when the screen rejects everything — and it will

**Every listing you will find for this size code says "402728 400mAh".** That is
132.3 mAh/cm³ and the screen rejects all of it. No sampled maker publishes a
datasheet at 4 × 27 × 28 at all — that absence is itself the finding. Applied
literally, "no datasheet, no order" leaves exactly one fully conforming
candidate in the drop-in class, a 250 mAh part, which is *smaller* than what is
already fitted.

So the screen is not a purchase filter. It is a **truth filter**, and the
fallback is stated here so that nobody quietly overrides it at a checkout:

- **Path A — buy the marketplace cell anyway.** Legitimate, and it costs
  nothing, *because the firmware never trusts the sticker either way*. Treat its
  capacity as **~300 mAh `ESTIMATED`**, set `REG 0x62` from that (§6), record in
  the purchase note that the capacity claim failed the density screen, and
  measure it (M3, then the discharge test) before any figure derived from it is
  written down anywhere.
- **Path B — go to a datasheeted geometry** from §5 and accept what it brings:
  a different length, possibly a different housing, and stock status that has to
  be confirmed rather than assumed.

Both are acceptable. Silently believing a sticker is not.

---

## 4. The measurements

### M0 — which side does the cell sit against, and is there a bay at all?

**Take this before M1 and M2, because it decides what they mean.** `J1` is on
the populated back-facing PCB face; the speaker is in the back cover. Answer, in
one or two sentences and one photograph: does the pouch lie **across the
component side of the PCB**, or in a **moulded pocket in the back cover**? If
the former, "clearance" is not a number but a field over a landscape of
components, and M1's method changes accordingly.

### M1 — closed-case clearance over the cell's volume

**Not the depth of a recess, and not measured with the cover off.**

- If M0 says **pocket in the cover**: remove the cell, place three ~6 mm balls
  of plasticine — one at the cell's centre, one near each end — lay thin paper
  over them so they do not stick, fit the cover and tighten the screws to normal
  torque, reopen, and caliper each squashed ball at its centre. **The smallest of
  the three is M1**, to 0.1 mm.
- If M0 says **across the component side**: three points along one axis can miss
  the true minimum entirely. Map the tall parts first — height of `U2`, `U3`,
  `X1`, `Y1`, the inductors and any connector shell that falls inside the
  footprint — then use a grid of at least six putty balls covering the whole
  intended footprint, not three in a line.

**Acceptance check on the reading itself.** Stiff putty holds the cover proud
instead of compressing, and that error runs the wrong way — it makes M1 read
*larger* and orders a cell too thick. The cover must sit fully seated, screws
bottomed, no visible gap and no bow between screws. If in doubt, repeat with
smaller, softer balls.

**Two subtractions before the swell rule, both measured rather than guessed:**

1. **Retention.** Whatever holds the new cell down — foam pad, double-sided
   tape — has a thickness, and it comes off M1 *before* the ÷1.1.
2. **Speaker pair.** If the wires genuinely cannot be routed clear of the cell's
   footprint (§9), caliper the pair's diameter and subtract that too.

**Sanity bound.** Total body thickness is 12.90 mm and it contains the lens, the
AMOLED module, the PCB, the back-side components and two cover walls. An M1
above roughly 6 mm should be re-measured before it is believed.

### M2 — the largest rectangle that will actually lie flat

Not a cross of two clearances. With any internal corner radius, a sharp-cornered
pouch of the measured width × length will foul the corners before it lies flat —
and the pouch's **sealed flange** is stiffer and thicker than its body, so it is
the flange that meets the corner first.

**Method: card templates.** Cut card at the candidate outlines, lay each in the
cavity in the orientation the cell will actually sit, and close and screw the
cover. That is the measurement; the calipers only tell you which templates to
cut.

Record, all of it:

- **(a)** the clear rectangle's two sides, to 0.5 mm, measured to the nearest
  obstruction on each side — wall, screw boss, connector body, component — not
  to the widest point of the cavity;
- **(b)** the clear distance from the **face of the `J1` header** to the opposite
  wall;
- **islands**: any boss, rib or post standing *inside* the footprint, and where.
  A boss in the middle cannot be described by any rectangle;
- **the internal corner radius**, which will be at least the moulding tool's;
- **which case axis each cell dimension will lie along.** The body is 50.80 ×
  42.00 mm externally — the two axes differ by nearly 9 mm, so for every
  non-square candidate in §5 the orientation decides the answer.

> **The length rule, and the draft got it wrong three times.** The plug plus the
> loop of wire needs 4–6 mm the cell cannot use. So
> **maximum orderable cell length = M2(b) − ~5 mm − (0–2 mm if the pack carries
> its PCM at the tab end).** A 30 mm pack therefore needs **M2(b) ≥ 35 mm**, and
> **≥ 37 mm** with a tab-end PCM. Not 30.

**Which axis the loop taxes is itself unverified** — on a watch the leads often
fold back over the *face* of the cell, in which case they eat M1 and the swell
allowance instead of M2(b). See M3's first instruction.

### M3 — the factory cell: how its leads are dressed, then dimensions, then mass

**Before anything is unplugged**, record and photograph **how the leads are
dressed**: folded back over the pouch face, or run along its length? That
observation is free now and gone the moment the cell moves.

**Also photograph the plug seated on `J1`**, showing which side of the housing
the red wire occupies relative to a fixed board feature (the USB-C edge). The
silkscreen `+` is *covered* by the plug once seated, so this is the cheapest
polarity reference in the whole operation and it only exists before disassembly.

Then, off the board:

- **Thickness at the centre of the pouch, not at the sealed edge** — the seal is
  thinner and flatters the reading. **Measure between two rigid flat plates and
  subtract their thickness**; hard caliper jaws on a soft pouch either indent it
  or read low, and indenting a lithium cell is not a cosmetic problem.
- **Width.**
- **Length twice**: across the pouch body, and from the far edge to the very end
  of whatever is at the tab end. **Feel for a hard rectangular lump there and say
  whether you find one** — that is the PCM question.
- **Mass, plug attached, to 0.1 g.**

> **Mass is the lie detector.** Pouches in this class run 1.74–2.26 g/cm³ and
> 161–201 Wh/kg. **6.0–6.5 g** is consistent with 280–330 mAh and confirms the
> sticker is optimistic. **7.5–8 g** would be the only mass consistent with a
> genuine 400 mAh — and no sampled pouch reaches that density, so a heavy
> reading more likely means the cell is thicker than 4.0 mm, which the caliper
> then settles.

### Plug pitch

Caliper across the **crimp centres** of the two contacts, not across the
housing. Expect 1.25 mm. One reading closes a row `HARDWARE_MATRIX` carries as
`LIKELY` and `STATUS.md` lists as `CONFLICTING`.

### On the powered board, whenever convenient

- **One I²C read burst at `0x34`** covering `0x62`, `0x50`, `0x58`, `0x12`,
  `0x69` — the five eFuse-defaulted registers of §1.3. Until then every
  "default" claimed for them is `UNKNOWN`.
  **Watch for one pathological combination**: `0x50` bit 4 = 0 *and* bits 3:2 =
  00 (current source off) makes `TS` read 0 V through `RP2`, which the part sees
  as "battery far too hot" and refuses to charge. With the source at its 50 µA
  default, `RP2`'s 10 kΩ puts `TS` at 500 mV — exactly the datasheet's 25 °C row
  and comfortably inside the [176 mV, 1312 mV] window.
- **`RP2`: NTC or plain resistor?** Measure cold, warm the board with a hot-air
  pencil, measure again. An NTC falls; a resistor does not.
  `NOT EXECUTED — HARDWARE REQUIRED`.
- **Which AXP2101 variant?** Put a scope on the `LP2` / `SW`-node while the
  battery is actively charging. The SWcharge part switches there during charge;
  on the linear part `DCDC5` is disabled (the sheet's own rail table says `NC`,
  and feedback resistor `RP4` is not fitted) and the node should be quiet.
  **Do not attempt to settle this by writing high `REG 0x62` codes to see what
  happens — that experiment *is* the fire path.**
  `NOT EXECUTED — HARDWARE REQUIRED`.
- **The factory cell's real capacity**: shunt in the battery lead, constant load,
  4.20 V down to 3.00 V, integrated. **Safety conditions in §9 — this is not a
  bare constant load on an unattended bench.** Do it on the factory cell before
  it is replaced and again on the replacement.

---

## 5. The sizing table, keyed on M1 and M2

**The go/no-go, and it outranks every row below.** With the cell in place and
the leads in their final routing: **every cover screw starts by hand, and the
cover pulls flat with no bow between screws.** If that is not true, the cell is
wrong, whatever the table said.

Two rules behind every row:

> **Maximum orderable pack thickness = (M1 − retention − speaker pair) ÷ 1.1.**
> The ÷1.1 is a 10 % swell allowance and it is not optional: a pouch that
> exactly fills its space bows the cover inside a year, and on a screwed cover
> that shows up as a cracked boss.
>
> **Maximum orderable cell length = M2(b) − ~5 mm − (0–2 mm for a tab-end PCM).**

**No row uses a diagonal test.** The draft rejected candidates by comparing a
rectangle's diagonal against "a 42 mm body" — the test for a *circular* cavity.
This case is a rounded rectangle, 50.80 × 42.00 mm, and in a rectangular cavity
a rectangle fits or does not fit **side against side, in some orientation**. All
such verdicts are withdrawn (§11). **M2's card templates are the fit test.**
The rear-face plate's 34.60 × 25.80 / `R4.5` are the only published dimensions
anywhere near the interior — and they are **not** cavity dimensions and must not
become a new veto: the fitted cell is 27 × 28 mm, which already exceeds 25.80 in
one axis and sits inside the case regardless.

Every capacity below is the maker's own minimum/typical figure unless marked.
**Every gain factor is against the fitted cell's ~300 mAh `ESTIMATED` real
capacity** and collapses if M3 says otherwise. **Stock status is `UNKNOWN` for
every geometry named** (§1.3). **No SKU here is asserted as the part to buy** —
they are geometries with published datasheets, named so the numbers can be
checked.

| Row | Gate | Order class | Expect | Gain vs real ~300 mAh | `REG 0x62` |
|---|---|---|---|---|---|
| **A — drop-in** | M1 ≥ 4.4 mm after subtractions; M2 clears ~27 × 28 | 3.5–4.0 mm class, max finished pack ≤ 4.0 × 27 × 28 **inclusive of tolerance**. Ufine `402525` (250 mAh, PCM) — a **manufacturer product-page figure, not a datasheet PDF** — is the only survivor of this row's own gate — and it is a *smaller* cell. Otherwise Path A of §3 | 250–310 mAh | **0.83–1.03×** | **125 mA**, code 5 → 0.50C at 250 mAh |
| **B — ~1 mm spare depth** | M1 ≥ 5.1 mm; M2(b) ≥ 33 mm | 4.6 mm class, `462528` geometry — Ufine publish 330 mAh, PCM, 6.7 g, 500 cycles ≥ 80 % (**manufacturer product-page figure, not a datasheet PDF**). Only 0.6 mm thicker than stock | 330 mAh | **1.1×** | **150 mA**, code 6 → 0.45C |
| **B′ — 5.0 mm branch** | M1 ≥ 5.5 mm **and** M2(b) ≥ 35 mm (the pack is 30 mm long, not 28) | `502728` geometry — Ufine datasheet, 360 mAh, PCM, 7.8 g | 360 mAh | **1.2×** | **50 mA**, code 2 — see §6, this part's own standard charge is 72 mA (0.2C) |
| **C — ~2 mm spare depth** | M1 ≥ 6.1 mm **and** M2(b) ≥ 35 mm | 5.5 mm class at 27 × 30, `552730` geometry — LiPol `LP552730` datasheet: 400 mAh min / 430 typ, PCM inside the outline, **max charge 200 mA** | 400–430 mAh | **1.3–1.4×** | **175 mA**, code 7 → 0.44C |
| **C′ — tall branch** | M1 ≥ 7.2 mm **and** M2(b) ≥ 35 mm | 6.5 mm class at 25 × 30, `652530` geometry — LiPol `LP652530`: 500 min / 530 typ, 108.7 mAh/cm³, PCM, **max charge 250 mA**. The densest part in the whole sample | 500–530 mAh | **1.7×** | **200 mA**, code 8 → 0.40C |
| **D — spare footprint, no spare depth** | M1 still 4.4–5.0 mm; M2 clears a rectangle *larger* than 27 × 28, template-checked | 4.0 mm class, larger plan area, sized to the template that actually lies flat. `402933` (LiPol 320/330, max 160 mA) needs M2(b) ≥ 38 mm · `403035` (LiPol 400/430, max 200 mA) needs ≥ 40 mm · `403040` (LiPol 420/430, max 215 mA) needs ≥ 45 mm | 320–430 mAh | 1.1–1.4× | 150 mA (code 6) for 320–330 · **175 mA** (code 7) for 400–430 |
| **E — out of scope, recorded not recommended** | Only after the speaker is removed and the cover replaced with a taller one | 36 × 30 × 8 mm, per the MakerWorld community case (§1.2). 8.64 cm³; the claimed 900 mAh is 104 mAh/cm³ — top of the band but, unlike the fitted cell's 132, *not* outside it, because that footprint is big enough to amortise the seal overhead | 700–800 mAh `ESTIMATED` | **2.3–2.7×** | **300 mA**, code 9 → 0.38–0.43C |

**Notes that bind across rows.**

- **EEMB's 4 mm-class parts are excluded** from every row despite fitting the
  geometry: `LP402535` and `LP542730` are **bare tabbed cells**, and §8 makes an
  integrated PCM non-negotiable. They appear in §3 as comparators only.
- **Row E deletes a capability.** Removing the speaker takes away something the
  capability registry currently assumes is present. That is a product decision
  for the owner, not a battery decision.
- **If M1 comes back below 4.4 mm**, the fitted cell is already outside its own
  swell margin. Order the 3.5 mm class and expect the low end of the capacity
  range.
- **Connector, per row.** LiPol ship some parts on Molex `51021-0200` and some
  on Molex `78172` — **both are PicoBlade, 1.25 mm pitch**, i.e. the geometry
  this board's header wants, so `LP403035` and `LP602530`-class parts are the
  ones most likely to plug straight in. Only JST `PHR-2` (2.0 mm, on some
  `403040` parts) forces re-termination. §8 still requires the meter.

---

## 6. Charge current, as a C-rate, with the register named

**`REG 0x62[4:0]` (`ICC_CHG_SET`) sets constant-current charge.** 25 mA steps
from 0 to 200 mA (codes 0–8), then it **jumps straight to 300 mA** (code 9) and
rises in 100 mA steps. The datasheet prints the formula itself: `25 × N` mA for
N ≤ 8; `200 + 100 × (N − 8)` mA for N > 8.

**There is no code between 200 mA and 300 mA**, and that gap is load-bearing: a
500 mAh cell's 0.5C is 250 mA, which does not exist, so it gets **200 mA (0.40C)
and never 300 mA (0.60C)**.

> ### The rule
>
> **`I_CC` = the lowest of —**
> **(a)** `0.5 × C_real`, where `C_real` is *measured*, never the sticker;
> **(b)** the cell datasheet's own **standard** charge current, where one is
> given — that is the current at which the maker measured the rated capacity and
> quoted the cycle life, and exceeding it trades cycle life for time;
> **(c)** strictly **below** the datasheet's **maximum** charge current, never at
> it;
> **— then rounded DOWN to an available `REG 0x62` code.**
>
> **Before any measurement exists: 125 mA, code 5.** That is 0.50C on the low end
> of the estimate and less on everything larger. **Holding 125 mA for every row
> is always valid**, costs only charge time, and is the right answer whenever any
> input is uncertain.

Worked, per row: **A** 125 mA = 0.50C at 250 mAh, 0.40C at 310 · **B** 150 mA =
0.45C (rule (b) gives 165, rounded down) · **B′** 50 mA = 0.14C — rule (b), the
maker's own standard for that part is 72 mA; if that is unacceptably slow, the
departure must be *recorded as a departure* and capped far below the 360 mA
maximum, and this note does not recommend it · **C** 175 mA = 0.44C, one code
below the part's 200 mA maximum · **C′** 200 mA = 0.40C, below its 250 mA
maximum · **D** 150 mA = 0.47C at 320 mAh, 175 mA = 0.44C at 400 · **E** 300 mA
= 0.38–0.43C.

### Neither available default is correct, in any row

- **The vendor demo sets 400 mA.** On the real ~300 mAh cell that is **1.33C**,
  above the 1.0C absolute maximum for this pouch class. Even on a genuine
  400 mAh cell it is 1.0C — twice the 0.5C standard charge every datasheet in
  this class specifies. It exceeds 0.5C for **every** cell in **every** row.
- **The POR default is eFuse-trimmed** — the datasheet prints it
  `{EFUSE, 0b, EFUSE}`, so there is no silicon constant to quote. X-Powers'
  prose gives the intended value as 300 mA, which is **1.0C** on the real cell.
  It has never been read on this board.

**So: read `REG 0x62`, then write it explicitly at every PMU init.** Not because
the value might be wrong — because we do not know what it is.

### Which AXP2101 is on this board is `UNKNOWN`, and it matters here

The draft asserted the **linear** part on the grounds that the schematic's pin
numbering matches the linear datasheet and pin 35 `SW` drives `LP2` as the
`DCDC5` inductor. **That reasoning does not discriminate**: pins 30–41 are
identical in both documents, and the only difference is pin 35's *description* —
"Inductor pin for DCDC5" (linear) versus "Inductor pin for Buck" (SWcharge). The
argument assumed the linear datasheet in order to read the pin, then used the
reading as proof. Claim withdrawn (§11).

The board evidence leans the other way: **Waveshare publish
`X-power-AXP2101_SWcharge_V1.0.pdf` as the AXP2101 datasheet in this board's own
Resources list**; the SWcharge part is described as a *"high efficiency 1 µH
inductor buck mode switch charger, 85 %@5V-3.8V-1A, Ichg up to 1.5 A"*; the
board fits `LP2` = 1 µH 2.5×2×1.2 3.5 A on pin 35 into the `VSYS` node through
`RP3` 0.01 Ω 1 %; and `DCDC5` is listed `NC` in the sheet's own rail table with
its feedback resistor `RP4` not fitted. Populating a power inductor on an
explicitly unused converter, and tying its switch node to the live `VSYS` rail,
is not a credible reading. `grep DCDC5` on the SWcharge datasheet returns
nothing — that variant has no `DCDC5` at all.

**Three consequences, and none of them change a single current chosen above** —
codes 5 through 9 decode identically in both parts:

1. **"Everything above code 16 is reserved" is true only of the linear part.**
   In the SWcharge variant, `10001b`–`10101b` are **1100–1500 mA**. Since this
   note prescribes *raw* register writes for the safety timers, the JEITA block
   and the `TS` thresholds — XPowersLib exposes no helper for any of them — a
   raw write of a high code from a bad cast or a copied snippet could deliver
   **1.5 A into a ~300 mAh cell, i.e. 5C**, in a state the draft documented as
   impossible. XPowersLib's own `setChargerConstantCurr()` does clamp; the raw
   path does not.
2. **The linear thermal model may be the wrong model.** `(VBUS − VBAT) × I_CC`
   dissipated in the package is a linear-charger calculation; through an 85 %
   buck it is roughly an order of magnitude lower. The conclusions stay
   conservative either way at these currents, but the reasoning must not be
   presented as established.
3. **"Ignore the 1.5 A figure" was wrong.** Treat 1.5 A as possibly real until
   the scope test in §4 says otherwise.

### The other four registers

- **`REG 0x61` (precharge): set code 1 = 25 mA.** The POR default is 125 mA,
  which on a ~300 mAh cell is 0.42C — four times the ≤0.1C convention for a cell
  sitting below 3.0 V, which is exactly the state where excess current is
  genuinely hazardous rather than merely unkind. **Waveshare's demo sets 50 mA,
  and 50 mA fails the same rule** (0.17C); take the demo's termination figure,
  not its precharge figure.
- **`REG 0x63[3:0]` (termination): 25 mA**, and keep termination **enabled**
  (`REG 0x63[4]`). The 125 mA POR default stops the charge early and leaves the
  cell part-full while the gauge reads 100 %.
- **`REG 0x64` (CV target): write `011b` = 4.2 V explicitly and read it back.**
  The draft said "leave it at the POR default", which is indefensible next to its
  own doctrine for `REG 0x62`. **The PMU never sees a POR while the cell is
  connected** — it is powered continuously from `VBAT1`, which has no disconnect
  switch, and it holds the RTC. An ESP32 reset, a reflash or a watchdog reboot
  therefore does *not* reset this register, and whatever the opaque factory image
  or a vendor Arduino example last wrote to it persists into our boot. If that
  were `100b` or `101b`, we would charge a 4.2 V cell to 4.35 or 4.4 V silently
  and permanently. **Never write `000b`**: datasheet V1.0 prints that code
  *reserved*, V1.4 prints it *5.0 V*. `REG 0x63[4]` gets the same
  write-and-verify treatment for the same reason — if it is ever found clear,
  the charger holds CV indefinitely, float-charging a Li-ion.
- **`REG 0x16` (input current limit): set `001b` = 500 mA** until enumeration
  says otherwise. The POR default is `100b` = **1500 mA**, which an unconfigured
  AXP2101 will pull from a host that granted 500 mA and never finished
  enumerating. *Variant caveat*: the draft justified this partly as capping die
  dissipation, which is a linear-part argument. Through a buck front end, 500 mA
  at 5 V supports roughly 560 mA of charge current — so **`REG 0x62` is the real
  bound on charge current**, and `REG 0x16` is a USB-compliance measure.

**Timers are not near a limit in any row.** The CC safety timer
(`REG 0x67[5:4]`) defaults to 12 h; 430 mAh at 175 mA is about 3 h, 800 mAh at
300 mA about 3.5 h. If one ever expires the charger drops to a fixed 10 mA
"battery safe mode" and raises an IRQ; rewriting the enable bit is one of the
documented exits.

---

## 7. Temperature: what disabling `TS` removes, and what has to replace it

**`REG 0x50` bit 4 = 1 at every PMU init, in every row.** `TS` carries no
battery-safety information on this board: `RP2` is a fixed 10 kΩ on the *PCB*,
reporting board temperature near the PMU and never cell temperature, and no
2-wire replacement can change that. Do not inherit the eFuse value — the
datasheet prints "Default: EFUSE" for that bit and states no number. Waveshare's
own demo already does this, with the comment *"It is necessary to disable the
detection function of the TS pin on the board without the battery temperature
detection function, otherwise it will cause abnormal charging."* Somebody was
bitten by this.

**But the draft stopped there, and that is a hole.** Decoupling `TS` removes
*every* temperature interlock, cold and hot, and the draft put nothing in its
place. As written it would charge a lithium pouch at 0.5C at any temperature.
Charging Li-ion below 0 °C plates metallic lithium on the anode — the principal
non-abuse route to a pouch fire — and a watch charged from a power bank outdoors
is exactly that exposure.

**This is not a theoretical limit. It is on the datasheets of the parts named in
§5:**

| Part | Charge temperature window |
|---|---|
| EEMB `LP402535`, `LP603030`, `LP542730` | 0–45 °C, **and ≤ 0.3C between 0 and 20 °C** |
| Ufine `502728` | 0–45 °C |
| LiPol `LP503035` | **+10 to +45 °C** |

So the standing 0.5C exceeds the EEMB parts' own limit at any ambient below
20 °C — ordinary indoor temperature in an unheated room.

**Required, before any replacement cell is charged unattended:**

- **Inhibit charging below 0 °C** outright (below +10 °C if a LiPol part is
  fitted), **derate to ≤ 0.3C between 0 and 20 °C**, and **inhibit above 45 °C**.
- **Route (a), which works regardless of what `RP2` is, and should be primary**:
  the AXP2101's own die-temperature ADC channel plus the QMI8658C's on-die
  temperature register. Both are already on this board; neither needs the `TS`
  pin. Both read *warmer* than the cell, so they are conservative on the hot side
  and optimistic on the cold side — which is the wrong way round, and is why
  route (b) is worth the bench check.
- **Route (b), better if it checks out**: bit 4 = 1 does **not** require giving
  up the reading. It makes `TS` "the external fixed input", i.e. a plain ADC
  channel — so the current source could be left on (`REG 0x50[3:2]` = 01 or 11)
  and `REG 0x30` bit 1 left set, letting firmware read `TS` and make the JEITA
  decision itself. For a cold, isothermal watch, board temperature is a good
  proxy for cell temperature. Both datasheets support this in principle; no
  document or code demonstrates the combination.
  `NOT EXECUTED — HARDWARE REQUIRED`.
- **Do not use XPowersLib's `disableTSPinMeasure()` blindly.** It writes `0x10`
  to `REG 0x50` — bit 4 = 1, *and* current source off — *and* clears `REG 0x30`
  bit 1. It conflates "decouple `TS` from the charger" with "turn the `TS` ADC
  off", and in doing so destroys route (b).

**Enclosure touch temperature is a separate limit nobody has considered.** This
is a skin-contact wearable in a sealed case; prolonged-contact guidance for a
plastic enclosure is around 48 °C. The draft considered only the PMU die
(`REG 0x65`, 100 °C foldback), which is not a wearable limit, while recommending
*higher* currents in the bigger-cell rows. Waveshare's own wiki records the
AXP2101 die reading **46 °C over 30 minutes at 20 °C ambient** while charging
with radios on. That die runs warmer than the cell, so it is not proof the cell
exceeds its 45 °C window — but the cell's ceiling is entirely unmonitored while
the vendor's own reading already sits above it.

---

## 8. Non-negotiable

- **4.2 V chemistry only.** Nothing marked "HV", "high voltage", 4.35 V or
  4.4 V. Their advertised capacity assumes a termination this board will never
  apply, so the energy is unreachable — and a mismarked HV cell in a system whose
  firmware someone later "optimises" upward is a fire path with a plausible
  commit behind it. **Never write `REG 0x64 = 100b` or `101b`; write `011b` and
  read it back (§6); never write `000b`.**
- **PCM mandatory, integrated in the pack**, with over-charge (~4.28 V),
  over-discharge (~2.9 V) and over-current trip points printed on the datasheet.
  This board has **no protection FET, no fuel-gauge IC, no load switch and no
  series sense resistor** — `VBAT1` has exactly three connections. The PMU is the
  only other line of defence and it cannot protect against a short across the
  cell's own leads. **Acceptance criterion: the over-current trip must sit
  comfortably above the board's peak draw** — a pack that nuisance-trips on an
  AMOLED-plus-WiFi-TX peak leaves the watch dead. **The board's peak draw has
  never been measured** (§1.3, §12), so this criterion cannot yet be given a
  number, and inventing one would be worse than saying so.
- **Polarity, verified with a multimeter, against the board silkscreen, before
  the plug goes anywhere near the header.** Pin 1 = positive = the pin the bottom
  silkscreen marks `+` beside `BAT`. **Never on wire colour alone** — pre-wired
  cells ship with polarity effectively at random, and reverse polarity into the
  `BAT` pin is not protected on this board. Because the silkscreen `+` is covered
  once a plug is seated, photograph the factory plug in place first (M3).
  **If the meter reads nothing, nothing goes in.** A pack whose PCM has tripped
  or that shipped over-discharged reads ~0 V and gives you no polarity at all; a
  healthy pack arrives at roughly 3.6–3.9 V storage charge, and anything below
  ~3.0 V is a reason to return it, not to charge it.
- **Transplant the original pigtail, or buy the cell with bare tinned leads.**
  The housing that is known to mate with `J1` is the one already on the factory
  cell. **Preferred, and reversible: de-pin it** — lift the retention tang with a
  fine pin, withdraw both crimp terminals, transfer them (or re-crimp onto the
  new leads), re-insert. No heat, no blade, nothing exposed, and undoable if you
  get it wrong. **Fallback, if the crimps will not come out: cut and solder — one
  wire at a time**, insulate it, then the other. Cutting both at once shorts the
  pouch across the blades of the cutter. Buying a bare-lead cell and fitting the
  original housing avoids trusting a stranger's crimping *and* a stranger's
  polarity.
- **10 % swell margin, and it comes after the subtractions.** Maximum pack
  thickness = (M1 − retention foam or tape − speaker-pair diameter if unavoidable)
  ÷ 1.1.
- **The cell must not bear on anything hard.** No wire under the pouch — a pouch
  resting on a wire is a point load and a pouch punctured by one is a fire, not a
  fault — and equally **no QFN package edge, crystal can, inductor or connector
  shell under it**: the same failure mode with a harder object. If the pouch has
  to sit over the component side at all, it sits on a compliant pad, not on
  parts. And it should not sit in contact with the ESP32-S3, whose heat then has
  nowhere to go but the cell.
- **The cell must be retained.** A pouch loose on a 1.25 mm-pitch header in a
  wrist-worn device works the header pins and the leads with every arm swing.
  Foam pad or double-sided tape, and its thickness comes off M1 first.
- **A third (NTC) wire cannot be used and must not be left loose.** Some parts in
  these geometries ship with one — `LP602530` carries a 10 kΩ 1 % B3380 NTC. The
  header is 2-pin and `TS` is tied to `GND` through `RP2` on the PCB, so the lead
  has nowhere to go. Insulate it and tape it to the pack. It is not a reason to
  reject the cell.
- **Charge current is never inherited** (§6), and **temperature interlocks are
  not optional** (§7).
- **Density sanity check on every listing** (§3), with the fallback stated there
  rather than improvised at a checkout.
- **The fuel gauge takes the measured capacity, not the sticker.** The AXP2101's
  percentage is a voltage-based estimator with no coulomb counting and no gauge
  IC — Waveshare warn about it themselves: *"battery capacity does not vary
  linearly, so large percentage fluctuations may occur."* Feeding it a design
  capacity 22 % above the best cell ever measured in this class propagates that
  error into every figure a user is shown.

---

## 9. Handling: opening, fitting, testing, arrival

- **Power down and unplug USB before touching the battery header.** First line of
  the procedure, and it was missing from the draft.
- **The flex cables, not the speaker pair, are the fragile expensive thing.**
  Waveshare's warning is about the cable area generally, and the draft
  mis-attributed it to the speaker wires: *"When disassembling, special attention
  should be paid to protecting the cable area. This part is vulnerable and prone
  to breakage due to pulling, twisting or forceful operation… Support the
  mainboard or cables to keep them in a natural and tension-free state."* One
  thing is known about the display flex: **`J3`, the 34-pin AMOLED connector, is
  on the display-facing side of the PCB**, so it does not obviously cross the
  battery volume. Its fold path, the touch flex and the board's edge notch are
  unverified. Rule: never let the board hang on a flex, never fold a flex to a
  sharp crease, and never route a cell lead across one.
- **The speaker pair.** Route it around the **perimeter** of the cell's footprint
  and hold it there with Kapton or a dab of RTV. Never trap a wire between the
  cell and the cover, or under a screw boss. Take M1 with the wires in their
  final routing. If they cannot be routed clear, caliper the pair and subtract it
  from M1 (§4).
- **Arrival inspection, before the cell goes anywhere near the case.** Reject and
  return, do not fit: puffed or pillowed, dented or creased, warm to the touch,
  damaged or wrinkled tab seal, tape lifting over the PCM, leads with nicked
  insulation. In a category where the paperwork is expected to be optimistic, the
  physical inspection is the last check before a lithium pouch goes into a sealed
  case worn on a wrist.
- **If the new pack reads "absent".** That is the most likely first symptom of a
  replacement cell. `REG 0x68[0]` = 0 disables battery detection and forces the
  PMU to proceed as if a battery is present; it also stops spurious
  `BAT_INSERT`/`BAT_REMOVE` IRQs. It is the documented escape hatch. Check the
  plug, the polarity and the pack voltage first — the datasheet does not document
  *how* the part decides, so any claim about how a marginal cell is classified
  would be a guess.
- **The capacity-discharge test is not a bare load on an unattended bench.**
  Required: an **independent hard low-voltage cutoff at 3.00 V** (a constant load
  alone will overshoot, and the factory cell's PCM status is `UNKNOWN`), a
  defined current, a thermal watch, and a fire-safe container. **The shunt sits
  inside the PMU's own voltage-sense path** — `VBAT1` has no sense resistor and
  runs straight from the header to the `BAT` pin — so its drop biases the
  reported percentage and the low-battery cutoff: use a small value, or a
  high-side / clamp method. **Insulate the shunt: shorting its leads shorts the
  cell.**
- **Do not section a cell.** The draft suggested the factory cell could be
  "weighed, sectioned or run to exhaustion". Cutting open a charged Li-polymer
  pouch is an ignition event, not a measurement. Struck.

---

## 10. No runtime figures, and why

**Nothing in this note says how many hours anything lasts, and that is
deliberate.** Hours need a **measured panel current at a known average picture
level**, which does not exist — **T-095**. A capacity and a per-frame emissive
estimate do not multiply into hours: they ignore the driver, the regulator's
efficiency curve and whatever the CO5300 does with its own idle modes.

What this note offers instead is the **capacity gain factor** in §5, against an
`ESTIMATED` baseline that M3 can overturn.

Two things worth holding next to each other:

- The vendor's runtime claims are `ESTIMATED-by-vendor` with no stated method,
  and their wording refers to a *"screen backlight"* an AMOLED does not have.
- **The cell is not the largest lever.** The vendor's own GitHub issue #6 reports
  ~36 % per hour fully idle, with one commenter reaching ~10 %/hour by fixing
  rails, forcing the display genuinely off, dropping to 80 MHz and disabling
  unused `ALDO`/`BLDO` rails. A 1.4× cell against a 3.6× firmware saving is the
  wrong end of the problem to optimise first — though the cell is the cheaper of
  the two to buy. **One conflict in that thread must not be adopted unverified**:
  a commenter identifies GPIO13 as `LCD_EN`, a boost-converter enable worth
  ~30 mA screen-off, while Waveshare's own GPIO table assigns GPIO13 to
  `LCD_TE`. One of the two is wrong, and it must be resolved against the
  schematic and on hardware before any firmware drives that pin.

The measurement that closes both questions is the same one: **a shunt in the
battery lead, day theme and night theme, same screen** — and the plug-in header
makes it easy, because the cell unplugs rather than desolders.

---

## 11. What the draft claimed, what was refuted, and where I disagree

Three adversarial passes ran. All three refuted the draft. The corrections are
applied above; these are the claims that were **dropped**, with the reason, so
that nobody reinstates them from the earlier text.

1. **"A pack ≤ 4.0 × 27 × 28 mm fits by definition."** Refuted and accepted.
   Manufacturers quote nominal plus tolerance, the baseline is unmeasured, and
   4.3 mm × 1.1 breaks the bay. Repaired in §2 as *maximum finished dimensions
   inclusive of all tolerances*, in practice the 3.5–3.8 mm class, with M3-first
   named as the strictly better path.
2. **"This board carries the linear AXP2101; ignore the 1.5 A figure."** Refuted
   and accepted. Pin numbering does not discriminate between the two documents,
   and the reasoning was circular. Variant is now `UNKNOWN`, evidence leaning
   SWcharge, with a scope test named and an explicit warning never to probe it by
   writing high `REG 0x62` codes (§6). **No current chosen in this note changes**
   — codes 5–9 decode identically in both parts.
3. **The diagonal-versus-diameter fit test**, and every `FITS` / `DOES NOT FIT`
   verdict derived from a "~40 mm round internal body". Refuted and accepted: the
   case is a rounded rectangle, 50.80 × 42.00 mm. The test failed in both
   directions — it wrongly rejected `402933`, `403035` and others, and it let
   others through on unsound reasoning. Withdrawn entirely; M2's card templates
   replace it (§5).
4. **"M2(b) ≥ 30 mm" as the gate for a 30 mm cell**, in three separate rows.
   Refuted and accepted — the draft's own M2 definition charges 5–7 mm to the
   plug and wire loop, so a 30 mm pack needs 35–37 mm. Corrected in §4 and every
   row of §5.
5. **`REG 0x64` "leave it at the POR default".** Refuted and accepted: the PMU
   never sees a POR while the cell is connected, so an opaque previous firmware's
   value survives our boot. Now write-and-read-back (§6).
6. **150 mA as the standing default**, which is 0.60C at the low end of the
   draft's own capacity estimate. Refuted and accepted — 125 mA (code 5) is the
   standing default, and the rule now sizes from the *minimum* of the range, not
   its midpoint (§6).
7. **200 mA in the 400–430 mAh rows**, which is those parts' datasheet
   *maximum*. Refuted and accepted: 175 mA, code 7 (§6).
8. **"Precharge 25–50 mA."** Refuted and accepted: 50 mA is 0.17C and fails the
   draft's own ≤0.1C rule. Code 1 = 25 mA only (§6).
9. **"Sectioned"** as a way to examine the old cell. Refuted and accepted;
   struck (§9).
10. **The connector-mismatch warning that lumped Molex `51021-0200` in with JST
    `PHR-2`.** Refuted and accepted: `51021-0200` and `78172` are PicoBlade,
    1.25 mm — the geometry this header wants. Only `PHR-2` is 2.0 mm (§5, §8).
11. **Ufine `462528` / `402525` / `552730` figures presented as datasheet
    figures.** Refuted and accepted: they are manufacturer *product-page* figures.
    Better than a marketplace listing, not the same as a datasheet PDF, and this
    note's own discipline forbids conflating them. Labelled in §5.
12. **"400 mA is 1.0C on a 430 mAh cell."** Refuted and accepted: 0.93C. The
    conclusion is unchanged.
13. **Waveshare's "protect the cable area" warning attached to the speaker
    wires.** Refuted and accepted; re-attached to the flex cables, where it
    belongs (§9).

**Where I disagree with a verifier: nowhere on substance.** I have no primary
source that contradicts any of the thirteen, and manufacturing a disagreement to
fill this section would be worse than having none. Two points of emphasis rather
than dissent:

- One verifier's arithmetic on the rear-face plate suggests its second axis
  scales to ~27 mm, which the fitted 27 × 28 cell would already exceed. Both
  mechanical verifiers reached the same conclusion from it — **that outline is
  not the cavity** — and I adopt their guard: 34.60 / 25.80 / `R4.5` are recorded
  in §1.1 as the only published dimensions near the interior and are explicitly
  *not* a fit veto.
- One verifier noted that the `TS` decoupling instruction is safe only if `RP2`
  is a plain resistor, and that if it is a genuine NTC the board reading is a
  usable cold-weather proxy. I agree, and that is exactly why §7 keeps route (b)
  open and flags `disableTSPinMeasure()` for closing it.

**Nothing in this note is `PASS`, `VERIFIED` or `MEASURED` on the strength of the
note alone.** Every cavity dimension is `UNKNOWN`; every capacity for the fitted
cell is `ESTIMATED`; every hardware test named here is
`NOT EXECUTED — HARDWARE REQUIRED`.

---

## 12. Open questions, each ready to become an issue

1. **Measure the battery bay: M0, M1, M2, M3.** Answer which side the cell rests
   against, the closed-case clearance by compressed shim, the largest rectangle
   that lies flat by card template, and the fitted cell's dimensions and mass.
   Unblocks the whole of §5. `needs-hardware`.
2. **Weigh the fitted cell and settle whether it is 400 mAh.** One kitchen scale
   to 0.1 g against the 1.74–2.26 g/cm³ band; ~6 g means the sticker is
   optimistic. Then confirm with a shunt discharge under §9's safety conditions.
   `needs-hardware`.
3. **Fix four wrong Waveshare rows in `HARDWARE_MATRIX` (and the `BAT1` row in
   `VERIFIED_FACTS`).** The battery connector is `J1`, not `BAT1`; the vibration
   motor is on pads `P1`/`P2`, not `J1`; its rail is `ALDO3`, not `BLDO2`; and
   `R13` (47 kΩ pulldown on `Q1`) is missing from the drive circuit. Evidence in
   §1.1 and §1.4. Documentation only.
4. **Read the five eFuse-defaulted AXP2101 registers on the powered board.**
   `0x62`, `0x50`, `0x58`, `0x12`, `0x69`, in one I²C burst at `0x34`. Until then
   every "default" claimed for them is `UNKNOWN`. `needs-hardware`.
5. **Settle which AXP2101 variant is fitted.** Scope the `LP2`/`SW` node while
   charging: the SWcharge part switches there, the linear part should be quiet.
   Never probe it by writing high `REG 0x62` codes. Decides whether 1.5 A is
   reachable by a raw write. `needs-hardware`.
6. **Is `RP2` a real NTC or a plain 10 kΩ?** Measure cold, then after hot air. An
   NTC falls; a resistor does not. Decides whether §7's route (b) has a real
   temperature signal behind it. `needs-hardware`.
7. **Bench-check reading `TS` as a plain ADC input with the charger decoupled.**
   `REG 0x50` bit 4 = 1 with bits 3:2 ≠ 00 and `REG 0x30` bit 1 still set. No
   document or code demonstrates this combination.
   `NOT EXECUTED — HARDWARE REQUIRED`.
8. **Implement the charge temperature interlock.** Inhibit below 0 °C, derate to
   ≤ 0.3C from 0–20 °C, inhibit above 45 °C, using the AXP2101 die channel and
   the QMI8658C temperature register at minimum. Blocks charging any replacement
   cell unattended (§7).
9. **Measure the board's peak current draw**, so the PCM over-current acceptance
   criterion in §8 can be given a number instead of a sentence.
   `needs-hardware`.
10. **Caliper the plug pitch on the fitted cell.** Closes a `LIKELY` row in
    `HARDWARE_MATRIX` and a `CONFLICTING` row in `STATUS.md` with one reading.
    `needs-hardware`.
11. **Confirm stock and shipping before naming any part as the one to buy.**
    Datasheet existence was verified; orderability was not, and a standalone
    lithium cell is a UN38.3 / IATA dangerous-goods shipment that many carriers
    will not take to a private address — which is a filter on the sales channel,
    not just the part.
12. **T-095 — measure the panel current, day theme and night theme, same
    screen**, with the shunt already in the battery lead. It is the reason the
    bigger cell is being considered, and no runtime figure exists until it runs.
    `needs-hardware`.
13. **Resolve GPIO13: `LCD_TE` or `LCD_EN`?** Waveshare's GPIO table and a
    community report disagree, and ~30 mA of screen-off current rides on it.
    Resolve against the schematic, then on hardware, before any firmware drives
    that pin (§10).
