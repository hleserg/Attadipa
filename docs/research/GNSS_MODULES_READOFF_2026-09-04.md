# The two bench GNSS modules, read off the parts — 2026-09-04

Both receivers the owner bought on 2026-09-02 have now been powered, read and
identified from what they say about themselves. This report replaces the
listing-derived `UNKNOWN`s in
[BENCH_DEVICES](BENCH_DEVICES.md#two-gnss-modules-delivered-2026-09-02--read-off-2026-09-04)
and answers [H18](OPEN_QUESTIONS.md#hardware--measurement-required) on identity,
baud, protocol and bands. **It does not close H18**, which stays `PARTIAL`: its
supply half — a regulator on each carrier, each module's TX idle voltage, and
the GT-U12's back-drive path in §2.4 — is untouched, and §4 is exactly where that matters, because those pins are now
named. It also
carries the one desk-resolvable item [#427](https://github.com/hleserg/Attadipa/issues/427)
attached to the same bench trip: the GPIO numbers behind the Waveshare
expansion pads, in §4.

Bench logs stay under `~/attadipa-bench/i427/` and are never committed: they
carry the owner's real position, and `hleserg/Attadipa` is public. So does the
per-unit identity each part prints — chip ids, unique ids and the trailing
serial fields of the module and product strings are read on the bench and left
there.

## 0. Which board is which — read this first

**The receiver measured through most of 2026-09-04, and called "the M10"
throughout the bench notes, is the QUESCAN AN3126.** The GT-U12 came onto the
wire at about 18:00 UTC. Anything written before that hour describes the
AN3126, and the two parts turned out to be nothing like each other.

| | GT-U12 | QUESCAN AN3126 |
| --- | --- | --- |
| Die, from the part | **ALLYSTAR `HD8041D`** | **u-blox M10**, `hwVersion 000A0000` |
| Module, from the part | `HYT-1010` family | not separately reported |
| Firmware, from the part | `3.018.a3f23db` | ROM SPG 5.10, `PROTVER=34.10` |
| Bands | **L1 + L5/E5a/B2a — dual** | **L1/E1 only — single** |
| Default baud | 115200 | 38400 |
| Binary protocol | ALLYSTAR, sync `F1 D9` | UBX, sync `B5 62` |
| NMEA as shipped | 4.00 (`CFG-NMEAVER=02`) | 4.11 |
| Config storage | not probed | **`UNKNOWN`** — the BBR/Flash NAKs are the documented answer for a key never written there |
| Price paid | 1 596 ₽ | 791 ₽ |

## 1. AN3126 — a u-blox M10, and it is single band

### 1.1 The #427 checklist

| Item | Result | Label |
| --- | --- | --- |
| Carrier regulator present/absent, and what `VCC` wants | **UNKNOWN** — never inspected. The bridge's `3V3` pin held 3.3 V under the module's load, which says the *dongle* regulates, not the carrier | UNKNOWN |
| TX idle voltage | **never reported**. Bounded only by the module's own `VCC`-`GND` = 3.26 V measured at its header | NOT MEASURED |
| Baud rate | 38400. 4800, 9600, 19200, 57600, 115200 and 230400 all returned framing garbage; 38400 returned 26 600 bytes with no corrupt character | MEASURED |
| Identity | `UBX-MON-VER`, and the power-up `$GNTXT` banner, agreeing | MEASURED |
| Sentence set | GGA, GSA, GSV, RMC, VTG, GLL at 1 Hz; talkers GP, GL, GA, GB, GQ. **No `$GIGSV`** — no NavIC | MEASURED |
| Non-NMEA framing | UBX, both directions. `CFG-UART1` reports `UBX 1, NMEA 1` in and out | MEASURED |
| Fix acquired, and cold TTFF | yes. **Cold 40.0 s**, guarded — the one TTFF figure this bench earned. **Hot is `UNKNOWN`**: both numbers it produced are unusable, one from a field caught lying here and one by an unrecorded method (§1.3). **Warm — ephemeris dropped, almanac kept — was never run** | **MEASURED** for cold; **`UNKNOWN`** for hot; **NOT MEASURED** for warm |
| Position, satellites, HDOP | logged on the bench; the position itself is not committed | MEASURED, off-repo |
| Raw log attached | **no** — see the note on coordinates above | not met, deliberately |

### 1.2 Identity, said twice

Power-cycling the module produced the full startup banner after 3.4 s of
silence:

```
$GNTXT,01,01,02,u-blox AG - www.u-blox.com
$GNTXT,01,01,02,HW UBX 10 000A0000
$GNTXT,01,01,02,ROM SPG 5.10 (7b202e)
$GNTXT,01,01,02,FWVER=SPG 5.10
$GNTXT,01,01,02,PROTVER=34.10
$GNTXT,01,01,02,GPS;GLO;GAL;BDS
$GNTXT,01,01,02,SBAS;QZSS
$GNTXT,01,01,02,ANTSUPERV=
$GNTXT,01,01,02,ANTSTATUS=DONTKNOW
```

`UBX-MON-VER` returned the same strings over the binary channel, and
`UBX-SEC-UNIQID` returned an id whose bytes sit inside the `CHIPID` the banner
printed. Two independent reads, one part.

`ANTSTATUS=DONTKNOW` with an empty `ANTSUPERV=` means no antenna supervisor is
wired on this carrier, so `antStatus` and `antPower` in `UBX-MON-RF` carry no
information here. They are *unknown*, not *ok*.

This is the same generation as the T-Watch's own receiver — same ROM SPG 5.10,
same `PROTVER=34.10`
(`docs/research/TWATCH_GNSS_READOFF_2026-09-05.md:1` — "The T-Watch GNSS module, read off the part — 2026-09-05")
— but a different module, and this one is the bench reference rather than the
part under test.

### 1.3 What it receives

All measured at one window position with the antenna on a lead, RAM-layer polls
only, nothing written that survives a power cycle.

| Measurement | Value | Conditions |
| --- | --- | --- |
| Bands | L1/E1 only. `UBX-NAV-SIG` reports GPS L1C/A ×9, GAL E1 ×9, BDS B1C ×8, GLO L1 ×7, SBAS L1 ×4 — **no L5, E5a, B2a or L2 anywhere** | 37 signals, 17 used |
| Constellations | GPS, GLONASS, BeiDou, Galileo all **enabled**, and all four appear in the `NAV-SIG` snapshot above. `MON-GNSS` also reports `simultaneous = 3`, which did **not** stop four being tracked at once here — read it as the field's own capability figure, whose exact meaning on this firmware is `UNKNOWN`, not as a cap #429 should configure around | `UBX-MON-GNSS` |
| Horizontal precision | **rms 4.07 m**, median 3.96, 68 % 5.16, 95 % 6.66, max 7.02 | 60 s static, 12 sats, quality 2 |
| `hAcc` vs its own scatter | receiver claimed **4.28 m** against 4.07 m measured — 0.21 m on 4.07 m, 5.2 % | same window; this is scatter, not true error |
| Best C/N0 | 42–44 dBHz | balcony |
| **Hot** TTFF | **`UNKNOWN`.** The receiver reported **820 ms** in `UBX-NAV-STATUS.ttff` — **not** `NAV-PVT`, which carries no such field — and a guarded run separately reported 0.1 s by a method nobody recorded | **ephemeris retained**, so this is u-blox's hot start and not warm. Neither number is usable. **`NAV-STATUS.ttff` is the one field this bench caught lying**: it reported `ttff = 163152 ms` in a frame whose own `gpsFix` was `0`, so it is not trustworthy without a guard, and 820 ms comes from it. And 0.1 s is below this part's 1000 ms measurement period, which is the floor on any true TTFF |
| Cold TTFF | **40.0 s** | `UBX-CFG-RST`, BBR wiped, antenna secured and guarded |
| **Warm** TTFF | **NOT MEASURED** | almanac and position kept, ephemeris dropped — never run |
| Solution rate | 1 Hz — `CFG-RATE-MEAS 1000 ms`, `CFG-RATE-NAV 1` | `CFG-VALGET`, RAM |

Three things this does **not** establish.

**The scatter is repeatability, not accuracy.** A constant offset of the whole
cluster is invisible to the method, and the bench has no surveyed point. The
north–south spread is 2.5× the east–west one because a window frame cuts part
of the sky — the DOP components show the same skew.

**So the `hAcc` comparison is bounded the same way.** What 4.28 m claimed
against 4.07 m measured — 0.21 m on 4.07 m, 5.2 % — shows is that the receiver's estimate tracks its own
*scatter* honestly, at one window, over one minute, in one sky. It is **not**
evidence that `hAcc` bounds true error, which this method cannot see. For
[#429](https://github.com/hleserg/Attadipa/issues/429) that is still the useful
result — it means `hAcc` can be carried into `PositionValidity` as the receiver
reports it, rather than being scaled by a fudge factor invented here — but the
claim is "not optimistic about its own noise", not "accurate to 4 m".

**And there is one TTFF figure here, not three.** 40.0 s is a guarded cold
start. The hot figure is `UNKNOWN` for the reasons the row above gives, and the
**warm** case — almanac and position kept, ephemeris stale — was never run.
Warm is the case a duty-cycled watch actually wakes into, so the number that
matters most to a power budget is the one entirely absent.

### 1.4 Interference and spoofing — one detector, one monitor, and it ships off

Traced to **u-blox M10 SPG 5.10 interface description, UBX-21035062 R03** —
the exact firmware and protocol version the banner reports — and then polled.

- `UBX-NAV-STATUS.spoofDetState` read **1, "No spoofing indicated"**, not 0.
  The detector is running. The document's own caveat is the reason this is not
  protection: "a value of 1 - No spoofing indicated does not mean that the
  receiver is not spoofed, it simply states that the detector was not triggered
  in this epoch."
- `UBX-MON-RF.jammingState` read **0** — which the document defines as "unknown
  or feature disabled or flag unavailable". `CFG-ITFM-ENABLE` read `0x00`, and
  the **Default layer** returns `0x00` too, so **the interference monitor is off
  as u-blox ships it**, not off because of anything on this board.
- Enabling it (`CFG-VALSET`, layers = `0x01`, RAM only, `ACK`ed, read back)
  moved `jammingState` to **1, "ok"** immediately and it stayed there.
  `cwSuppression` 9–11 of 255, `agcCnt` 558 of 8191 (6.8 %) and constant,
  `noisePerMS` 61–85. `UBX-MON-SPAN` showed the front-end filter shape across
  1586 MHz ± 32 MHz with no CW spike. **No interference at this bench.**
- **Absent in this firmware:** `UBX-SEC-SIG` and `UBX-SEC-SIGLOG`. Class `0x27`
  contains only `UBX-SEC-UNIQID` here; the richer reporting arrived in SPG 5.20
  and 5.30. No Galileo OSNMA — that is F9/F10 territory and this part is single
  band anyway.

So "built-in spoofing protection" on this part is **one per-epoch detector bit
and an interference monitor that ships disabled**. Both are UBX-only: nothing
about either can be read over NMEA. Recorded against ADR-0011's spoofing axis
as a fact, not as work.

The monitor was left **on in RAM only**; unplugging the module reverts it. No
save command was sent at any point.

### 1.5 It runs from ROM — and what the layer probe does *not* show

`UBX-CFG-VALGET` names the layer it reads, so the same key was asked of all
four:

```
layer 0 RAM     -> ANSWERED 0x01      (our RAM enable)
layer 1 BBR     -> NAK
layer 2 Flash   -> NAK
layer 7 Default -> ANSWERED 0x00      (the control)
```

The layer-7 control shows the frames were well formed: Default lives in ROM and
answered, so the two NAKs are not the script.

**But an earlier version of this section read them as "the storage is not
there", and that is retracted — it steps past what the probe licenses.** On
gen-9/gen-10 u-blox, `CFG-VALGET` returns an item from a *storage* layer only
if something was written there, and NAKs otherwise; RAM and Default hold every
key unconditionally. So the two layers that answered are the two that always
answer, and the two that NAKed are the two that answer only for saved keys —
and `CFG-ITFM-ENABLE` was never saved to either, by design: the only write this
session made was `layers = 0x01`, RAM. **A part with perfectly good BBR and
flash returns exactly this pattern for an unwritten key.** Whether this module
has usable BBR or flash is therefore **`UNKNOWN`**, not "none". The next action
is one reversible step — `CFG-VALSET` of a harmless key to layer 1 and re-poll
it — and it is still **not** a save command in the sense H18 forbids.

**The firmware conclusion does not rest on that probe and stands.** The banner
and `UBX-MON-VER` both report `ROM SPG 5.10`: u-blox names a ROM-based part
`ROM` and a flash-based one `EXT`, and `UBX-MON-PATCH` reports two patch
entries, both activated, both `location = eFuse` — the corrections on top of the
ROM were burned at the factory, not loaded from a writable store.

A firmware update in the ordinary sense therefore has nowhere to go. The route
that remains is u-blox's for ROM parts — the host pushes an image into RAM at
every startup — and that is not ruled out by anything here, but it needs an
image u-blox distributes privately and a new upload path on the watch's battery
budget, to gain `UBX-SEC-SIG` on a receiver that already fixes. Recorded, not
proposed.

## 2. GT-U12 — an ALLYSTAR HD8041D, and it is dual band

### 2.1 The #427 checklist

| Item | Result | Label |
| --- | --- | --- |
| Carrier regulator present/absent | **UNKNOWN** — the carrier back carries a u.FL connector, a round can (backup cell or supercapacitor) and passives marked 101/511/511; no regulator identified | UNKNOWN |
| TX idle voltage | never reported | NOT MEASURED |
| Baud rate | 115200 | MEASURED |
| Identity | `HD8041D` die, `HYT-1010` module, firmware `3.018.a3f23db`, from the part's own polls | MEASURED |
| Sentence set | GGA, GSA, GSV, RMC, VTG at **1.000 Hz** (30 GGA epochs over 29.0 s of receiver UTC); talkers GP, GL, GA, BD; plus `$GNTXT,01,01,01,ANT_OK` once a second | MEASURED |
| Non-NMEA framing | ALLYSTAR binary, sync **`F1 D9`** | MEASURED |
| Fix acquired | yes, autonomous quality 1 indoors near a window | MEASURED |
| Cold TTFF | never run on this part | **NOT EXECUTED — HARDWARE REQUIRED** |
| Static scatter / accuracy | never run on this part | **NOT EXECUTED — HARDWARE REQUIRED** |
| Raw log attached | **no** — coordinates | not met, deliberately |

Off the part itself, from the owner's photographs: carrier silkscreen
`GOOUUU-GPS+BD`, shield can `GOOUUU TECH / GT-U12`, header `VCC GND TX RX PPS`,
and a separate ~25 mm ceramic patch on u.FL. **`GT-U12` is GOOUUU's carrier-board
name, not a chipset**, which is why it appears nowhere in the part's answers.

### 2.2 The sync word is `F1 D9`, and that is why twenty-four probes were silent

Twenty-four vendor protocols were sent at this module and every one was silent
— CASIC, MTK, Quectel, Airoha, Unicore in both its NMEA and OEM-ASCII forms,
u-blox, ST, Goke, plain NMEA queries, and a deliberately malformed sentence to
draw a NACK out of anything that parses at all. Each sweep carried a control
whose effect needs no reply (a rate command, checked against the observed GGA
rate), so the silences were recorded as the probe's rather than the module's,
and the harness was proven separately: with both signal leads pulled off the
module's own header and joined — connector shells inside the loop — the bridge
echoed its own test strings at 9600, 38400 and 115200. The wire was never the
fault.

What identified the part was its own output. It had been naming itself once a
second the whole time:

```
$GNTXT,01,01,01,ANT_OK*50
```

That sentence, checksum included, is the antenna-status output documented for
the **ALLYSTAR TAU series** — Table 25 of the TAU1113 datasheet gives
`ANT_OK*50`, `ANT_SHORT*06` and `ANT_OPEN*40`. Byte for byte. The same
datasheet gives the reason the probes failed: **ALLYSTAR's binary sync word is
`F1 D9`, where u-blox uses `B5 62`.** Everything after it is the UBX frame
shape — class, id, little-endian length, payload, Fletcher-8 checksum — which
was verified by rebuilding the datasheet's own example byte for byte before
anything was sent.

With the right sync word the part answers 31 message types, and named itself:

```
F1 D9 0A 04 -> firmware 3.018.a3f23db, hardware HD8041D.<per-unit suffix>
F1 D9 0A 05 -> module   HYT-1010-<per-unit suffix>
F1 D9 0A 07 -> product  <per-unit string>
```

Polls only — zero-length payloads. `06 09` (save/clear), `06 40` (reset) and
`06 04` were excluded by name from every sweep and never sent.

### 2.3 Dual band, measured three ways

An earlier reading here concluded "single band" and was **wrong**; it is
retracted. It rested on counting records in `01 30`, which ALLYSTAR's own
protocol specification defines as `NAV-SVINFO`, **one record per satellite** —
a dual-band receiver produces exactly the same count there, so the count was
evidence of nothing.

What settles it, three independent ways:

1. **The configuration mask it ships with.** `CFG-NAVSAT` (`06 0C`) returned
   `37 82 10 04` = `0x04108237`, which decodes against ALLYSTAR's binary
   protocol specification V2.3 as `ON: GPS L1, GLONASS G1, BEIDOU B1, GALILEO
   E1, QZSS L1, GPS L5, BEIDOU B2A, GALILEO E5A`. The second band was enabled
   out of the box.

   **One bit of that mask is unidentified here, and it is recorded rather than
   glossed.** `0x04108237` has **nine** bits set — 0, 1, 2, 4, 5, 9, 15, 20 and
   26 — and the list above names eight signals. Benign readings exist (SBAS, or
   one name covering both B1I and B1C) and so do readings that matter: a ninth
   enabled signal, or **IRNSS**, which this part's listing claimed and which is
   the ALLYSTAR tell this trip went looking for — and which §2.1 records as
   absent from the talker list. ALLYSTAR's specification is not in this
   repository, so neither a reader nor a later session can re-derive the mapping
   from what is here. **Do not size what this receiver emits from the
   eight-name list.** Nothing in the dual-band conclusion rests on it: items 2
   and 3 are independent of the mask, and item 2 bounds each constellation to
   two signals on the air.
2. **The air.** `CFG-NMEAVER` (`06 43`) read `02` = NMEA V4.00, which is why
   GSV carried no `signalId`. Set to `03` = V4.10 — RAM only, output-format
   only, **restored to `02` and the restore verified by re-poll** — the
   receiver names the signal of every satellite itself. In a 20 s window
   indoors, 6 of 41 satellites appeared on two signalIds each: GPS 25 on 1 and
   8, which the NMEA 4.11 table names **L1 C/A and L5-Q** — a naming this part does
   not follow throughout, see below;
   Galileo on 2 and 6, BeiDou on 1 and 4. GLONASS showed one signal only —
   exactly as the mask says, G1 on and G2 off. Every constellation whose second
   band is enabled shows two; the one whose second band is disabled shows one.
3. **The antenna.** Photographed: two stacked ceramic resonators, larger below
   and smaller above, one feed pin through both — the standard L1+L5 stacked
   patch. Die, configuration and antenna are all dual band.

The Galileo and BeiDou signalId pairings are left as ALLYSTAR's own numbering
rather than mapped onto the u-blox 4.11 table, which they do not fit cleanly.
It changes nothing: the mask names which two signals are enabled per
constellation, and only those appear.

**The marketplace listing was accurate.** It claimed "dual-band GPS L1 L5" and
the part delivers it. That makes the GT-U12 the only receiver on this bench
that can reject multipath by frequency — the failure mode that dominates in
cities and under canopy, which is the case the owner bought it to characterise.

Still open on this part and nothing else: **it has no accuracy figure and no
cold-start TTFF.** The AN3126 has 4.07 m rms and 40.0 s; the GT-U12 has neither,
so the two are not yet comparable where it counts.

### 2.4 Unexplained: it does not stop when its `VCC` is unplugged

Three guarded windows — 200 s, 300 s, and 300 s with the host's TX held low to
remove any phantom feed through the module's `RX` pin — recorded **zero
interruption** in the NMEA stream while the owner reports pulling the `VCC`
lead. GGA timestamps run one per second with no gap and no backward jump across
all three. Holding TX low changed nothing, so a phantom path through `RX` is
genuinely disproved.

**This is not a curiosity, and calling it non-blocking would be wrong.** §2.1
photographed the likely answer — "a round can (backup cell or supercapacitor)"
on the carrier back — and if that can holds the module up, then the module can
drive its `TX` while the host rail is down. That is a back-drive case, and it
sits directly on the gate §6 names: a single TX idle-voltage reading taken with
everything powered does not cover it. So this `UNKNOWN` is **part of** the
supply half of H18, not orthogonal to it, and it has to be answered before
either module's `TX` reaches GPIO 44.

## 3. What #429 must not do — the parser hazard, measured

The two receivers disagree about NMEA field layout in a way that breaks the
obvious parser:

GSA is fixed-length and both parts agree on it:

```
GT-U12      GSA  19 fields in all 120 sentences,  systemId PRESENT
AN3126      GSA  19 fields in all 351 sentences,  systemId PRESENT
```

**GSV is not fixed-length, and that is the trap.** A GSV sentence carries four
header fields and then a four-field block per satellite, so its length is
`4 + 4n` without `signalId` and `5 + 4n` with it. **`n` is 0 to 4**: 4 is the
block limit, the last sentence of a group carries only the satellites left
over, and **0 is real** — a u-blox M10 emits one GSV per *enabled*
constellation whether anything is visible in it or not. The AN3126 capture
contains `$GQGSV,1,1,00,0*64`, five fields, QZSS with nothing in view, and the
watch's own MIA-M10Q does the same
(`docs/research/TWATCH_GNSS_READOFF_2026-09-05.md:124` — "  sentence GAGSV      sentence GNGSA      sentence GPGSV").
A parser that bounds `n` below at 1 rejects one to four well-formed sentences
per epoch and makes a correct receiver look like a stream of parse errors.
Counted across the two captures:

```
GT-U12      GSV field counts  {8, 16, 20}          signalId ABSENT
AN3126      GSV field counts  {5, 9, 13, 17, 21}   signalId PRESENT
```

NMEA 4.10 introduced GSA `systemId` and GSV `signalId` together, so the natural
rule — "this talker is 4.10, therefore GSV carries `signalId`" — is true of the
AN3126 and **false of the GT-U12 sitting on the same bench**. Applied to the
GT-U12 it would read the SNR column as a signal id and shift every satellite
record by one.

The rule that replaces it must be **`4 + 4n` versus `5 + 4n`, not a pair of
example lengths.** `4 + 4n = 5 + 4m` has no integer solution, so the two forms
never collide and the residue decides:

```
(fields - 4) % 4 == 0  ->  no signalId   (GT-U12: 8, 16, 20)
(fields - 4) % 4 == 1  ->  signalId last (AN3126: 5, 9, 13, 17, 21)
```

An earlier version of this section gave the counts as "20" and "21" and is
**retracted**: those are the two *full* sentences, and a parser matching on them
has no answer for the tail sentence of every group — which is one sentence in
three here. That mistake is the same record-shifting bug this section exists to
prevent, one level further in.

The cause is documented rather than broken: the GT-U12 was in `CFG-NMEAVER=02`
= V4.00, and ALLYSTAR puts `systemId` in GSA at **both** V4.00 and V4.10. An
earlier note here called it a partial implementation; that was wrong and is
retracted. The rule it produced survives the correction intact:

> **A parser decides the field layout per sentence type, from that sentence's
> own field count — and where a sentence is variable-length, from the residue
> its grammar defines, never from an example length. It never infers one
> sentence type's layout from another's, and never from a version inferred from
> a talker.**

This is the first hardware evidence in the project that the receiver spread is
wide enough to break a version-inferring parser, and it is worth more to #429
than the band question. `TWATCH_GNSS_READOFF_2026-09-05.md` already carries the
same rule for the watch's own module.

A second note for #429, from §1.3: on a u-blox part `UBX-NAV-PVT` carries time
and its validity, `fixType`, `numSV`, position, `hAcc`, `vAcc` and `pDOP` in one
checksummed 92-byte frame — strictly more than the NMEA sentences, and it maps
onto `GnssObservation` and `PositionValidity` without sentence assembly. That is
an argument for a driver, not a decision, and the two bench parts speak
different binary protocols anyway.

### 3.1 How much a receiver says in a second — MEASURED, and a buffer depends on it

A driver that reads the UART from an existing tick has to size two things
against each other: the ring the ESP-IDF driver fills, and how long the tick may
be away before what is in that ring stops being current. The second is a
3-second flush in `firmware/main/local_gnss.cpp`, so the number the first needs
is **the most bytes any three consecutive seconds of these captures contain** —
measured directly, not a peak second multiplied by three.

| Captures | Module | Satellites in view | Worst 1 s | Worst 3 s |
| --- | --- | --- | --- | --- |
| `boot-…090718Z`, `cap-38400-…`, `live-…`, both `waitfix-…` | AN3126 | 0–3 | 452–460 | 1356–1380 |
| `fix-…1353Z`, four `boot…-1400…` | AN3126 | 8–38 | 1064–1655 | 3188–**4127** |
| six `gtu12-…` | GT-U12 | 38–40 | 1055–1111 | 3133–3332 |

One epoch a second in all sixteen. **The worst three-second window in any of
them is 4127 bytes**, in `boot4-…140439Z` — the capture taken across a power
cycle. Its three epochs are 1218, 1254 and 1655 bytes, and **442 bytes of the
last one are the twelve-line u-blox `$GNTXT` startup banner**. That burst
belongs in the number rather than beside it: a module coming back up while the
host has been asleep is exactly the case where a long gap and a large burst
arrive together.

4127 is a floor, not a ceiling. One line of that capture is torn — the capture
tool spliced an annotation into a `$GNGLL` — so about twenty bytes the receiver
did send are not in the file to be counted. The margin below absorbs it; a
count that included the tool's own annotation text as receiver bytes would not
be a measurement of the receiver.

**What drives the spread is satellites in view, not the module and not the
protocol version.** The two AN3126 rows are the same part at the same
`PROTVER`, sending the same sentence set every second — `GNRMC GNVTG GNGGA
GNGLL`, five `GSA`, and one `GSV` block per constellation. Only the lengths
change: with no satellites in view `$GNRMC` is 27 bytes and `$GNGGA` is 33, each
`GSV` is a 20-byte stub, and the epoch is 436; with a sky those become 70 and
75, the `GSV` stubs become populated blocks, and the epoch passes 1200. The
heaviest window with no banner in it is 3722 bytes, from the AN3126; the
heaviest the GT-U12 produced is 3332. Neither part is the reason for the
spread, and the two figures are not a like-for-like comparison of the parts:
the GT-U12 reports 39–40 in view in every epoch of every capture, including one
taken across a power cycle where it was using none of them, so that column is a
tracking-channel or almanac count for that receiver and not a sky.

What this decides in `firmware/main/local_gnss.cpp` — "constexpr int kRxRing = 8192;":
a gap *shorter* than the 3-second flush keeps its bytes, so the ring has to have
been able to hold them. **4096 would not have: it is 31 bytes under the worst
window measured here**, and a ring that overflows inside that window hands the
next tick a backlog whose newest sentence is already seconds behind — an old
epoch stamped with the time it was noticed, which is the one thing this driver
exists to refuse. 8192 clears the measured worst case by 98%.

**Counted from the captures in `~/attadipa-bench/i427/`, which are not
committed** — they carry the owner's real position. Reproducible from any
capture of the same modules: the capture tool's own `###` annotations removed
wherever they appear, then sentence bytes including CRLF, split into epochs at
each `RMC`, summed over every window of three consecutive epochs.

## 4. The Waveshare expansion pads: `RXD` = GPIO 44, `TXD` = GPIO 43 — VERIFIED

§1.5 of [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) records the pad
row from the silkscreen and identifies `RXD`/`TXD` as the only uncommitted
channel on it, but the GPIO numbers behind those two pads were recorded nowhere.
#429 needs them. They resolve from the schematic in three links:

1. **Pad → net.** The "Parameter Set" test-point block of
   `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0` sheet 1 lists ten test points —
   `TP3 VCC3V3`, `TP4 GND`, `TP13 U0TXD`, `TP6 U0RXD`, `TP5 ESP32_SCL`,
   `TP7 ESP32_SDA`, `TP12 USB'_N`, `TP18 USB'_P`, `GND`, `VBUS`. That is the
   exact mirror of the ten silkscreened pads §1.5 records, 10 of 10, which is
   also the evidence that this sheet is this board — corroborating §1.1, where
   the silkscreen revision was read off the unit directly. So **`TP13` is the
   `TXD` pad and `TP6` is the `RXD` pad.**
2. **Net → chip pin.** `U2` is a bare `ESP32-S3R8` in QFN56, not a module. The
   nets land on **pin 49, symbol name `U0TXD`**, and **pin 50, symbol name
   `U0RXD`**. The symbol's numbering was cross-checked on four unrelated pins
   before this was read off it — `MTMS` on 48, `MTDI` on 47, `MTDO` on 45,
   `MTCK` on 44 — which match GPIO 42, 41, 40 and 39 on the S3's published
   pinout.
3. **Chip pin → GPIO.** ESP-IDF vendor source,
   `components/soc/esp32s3/include/soc/uart_pins.h`: `#define U0RXD_GPIO_NUM 44`
   and `#define U0TXD_GPIO_NUM 43`.

**So pad `TXD` is GPIO 43 and pad `RXD` is GPIO 44.**

They are genuinely free on this firmware. The Waveshare console is the SoC's
native USB, not a UART bridge
(`firmware/sdkconfig.defaults:65` — "CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y"),
so UART0 is not carrying the console, and nothing under `platform/` or
`firmware/` claims 43 or 44 — `boards/`, which `AGENTS.md` names for board
code, does not exist in this tree yet.

**One caveat #429 has to design around.** The first-stage ROM bootloader prints
its own boot message on UART0 independently of what the application console is
set to. The eFuse that controls it defaults to on: ESP-IDF's
`components/efuse/esp32s3/esp_efuse_table.csv` describes `UART_PRINT_CONTROL`
(BLK0 bit 134, 2 bits) as `Set the default UART boot message output mode
{0: "Enable"; 1: "Enable when GPIO46 is low at reset"; 2: "Enable when GPIO46
is high at reset"; 3: "Disable"}`, and USB printing is a **separate** bit
(`DIS_USB_SERIAL_JTAG_ROM_PRINT`, BLK0 bit 130), so selecting the USB console
does not silence the UART one. A GNSS module wired to these pads will therefore
see boot chatter on its `RX` at every reset, and the only way to stop it is to
burn an eFuse, which `AGENTS.md` forbids. The receiver must tolerate it, or the
pad must not be wired to the module's `RX` at all.

Two things this section does **not** establish. The baud of that ROM message is
`UNKNOWN` here — not read off, and not asserted. And the eFuse state of *our*
unit was not read: the Waveshare was not on the bench during this session, so
the default above is the vendor's documented default, not a measurement of this
board. `espefuse.py summary` is a read-only command and would settle it.

## 5. Method — a bench measurement needs a guard on its physical variable

Three cold-start readings were lost before one survived, and all three failed
the same way: the antenna moved. A patch antenna on a long lead was knocked
over twice, and the receiver's data stayed perfectly plausible while meaning
nothing — "no fix in 208 s with 43 dBHz available" and "191 s" were both an
antenna lying on its side, and both are **void**.

The tell is worth keeping: **strong SBAS with zero GPS is a patch antenna on its
side.** The geostationary SBAS satellites sit ~25–30° over the southern horizon
at this latitude and fall into the side lobe, while the overhead GPS
constellation lands in the null. SBAS and GPS share 1575.42 MHz, so a receiver
hearing one at 41 dBHz and the other not at all is being pointed, not broken.

The fourth run carried its own guard — it counted unique GPS satellites per 10 s
window through the warm stage and would have **aborted rather than reported** if
that count halved — and produced the 40.0 s figure §1.3 records. The guard never
tripped.

The rule this leaves: any bench measurement longer than a few seconds needs a
guard on the physical variable it depends on, reporting an abort rather than a
number. A contaminated reading that looks plausible is worse than no reading.

A separate scare on the same day is recorded so it is not misread later: a
static discharge during rewiring killed the USB host controller (recovered by
unbinding and rebinding `xhci-hcd`), and the AN3126 went silent at the same
moment. **The module was not damaged.** The fault was one loose Dupont socket,
found by elimination after a harness loopback that had bypassed the connector
shells; replacing it restored the link, the UBX reverse channel and the fix at
once. No later behaviour of that part should be attributed to the discharge.

### 5.1 The repeat-rate capture sat still — owner-attested, not MEASURED

The parser rule in §3 rests on a repeat rate, and a repeat rate only carries
weight if the receiver was standing still while it was taken: a receiver being
carried about cannot repeat a coordinate to the last digit, so a capture in
motion would measure the easy half of the hazard. For the **GT-U12** half the
condition is recorded here rather than left to inference — the module was set
down on a bench and left there, and **the owner confirmed on 2026-09-05 that it
had sat untouched overnight**.

That is an **owner attestation, not a MEASURED quantity**. Nothing in the
capture proves it: there is no motion sensor in the harness and a stationary
receiver and a wedged one write the same repeat. What stands behind it is the
person who put the module down. It is written here because a number that
depends on a physical condition is worth what the record of that condition is
worth, and a condition that lives only in a source comment is one no reader can
audit — §5's own rule, applied to a run that predates it.

The **AN3126** half of the 6.5 MB carries no such record and must not be given
one: its physical condition during capture is **UNKNOWN**. The repeat figures
in §3 are quoted for both modules, so a future reader tightening that threshold
has one half attested and one half not.

## 6. What is still UNKNOWN

- **Both carriers' regulators**, and what each `VCC` actually wants. Neither
  board was inspected for one; both were run from the bridge's regulated 3.3 V,
  which works but proves nothing about the carrier.
- **Three things, matching the H18 row: both carriers' regulators (above), both
  modules' TX idle voltage, and the GT-U12's back-drive path in §2.4.** The last
  two are one measurement, not two: an idle-voltage
  reading taken with everything powered does not cover a module that keeps
  driving its `TX` after its `VCC` lead comes off. This is the half of H18 that
  guards an ESP32-S3 pin, and §4 is where those pins now are — so it has to be
  answered before either module is wired to the Waveshare pad row, not before
  the next bench capture.
- **The GT-U12's accuracy and cold-start TTFF.** `NOT EXECUTED — HARDWARE
  REQUIRED`, and the reason the two parts cannot yet be compared where it
  matters.
- **Whether an `HD8041D` can be driven into other band combinations.** The
  shipped mask is decoded; ALLYSTAR publishes no part brief naming `HD8041` at
  all, and the dual-band TAU1202 brief's `HD804XD` is a family resemblance, not
  an identification.
- **The ROM boot-message baud on UART0, and this Waveshare unit's eFuse state.**
  §4.

## 7. What this changes elsewhere

- `BENCH_DEVICES.md`'s module section is no longer the listing; it points here.
- `OPEN_QUESTIONS.md` **H18 moves to `PARTIAL`**: identity, baud, protocol and
  bands are answered; its supply half — a regulator on each carrier, each
  module's TX idle voltage, and the GT-U12's unexplained power path in §2.4 —
  is not, and that half is the gate on wiring either part to the pads in §4.
- `HARDWARE_MATRIX.md`'s expansion pad row and
  `WAVESHARE_BOARD_RECEIVED.md` §1.5 both gain the GPIO numbers from §4.
- [#429](https://github.com/hleserg/Attadipa/issues/429) is unblocked on the
  pin question and inherits the parser rule in §3, the `hAcc` result in §1.3 and
  the ROM-chatter caveat in §4.
- ADR-0011's spoofing axis gains a concrete source on one part (§1.4) and none
  on the other: nothing equivalent was read from the ALLYSTAR.
- **A driver that wants a jamming indication from an M10 inherits a constraint
  §1.4 and §1.5 only imply separately.** `CFG-ITFM-ENABLE` is `0x00` on the
  Default layer, and whether an override can be persisted is `UNKNOWN` (§1.5),
  so until somebody shows otherwise the monitor must be re-enabled **in RAM at
  every boot** — and until it is, `jammingState = 0` means *not looking*. It must
  never be read as *ok*.
