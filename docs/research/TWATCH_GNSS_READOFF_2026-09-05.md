# The T-Watch GNSS module, read off the part — 2026-09-05

The module fitted to the T-Watch S3 Plus is a **u-blox MIA-M10Q**. It said so
itself, and this report is the evidence and its limits.

Everything below was produced by the bring-up bridge from
[#436](https://github.com/hleserg/Attadipa/issues/436) running on the bench
watch, USB serial `DC:B4:D9:18:49:40`, on 2026-09-05. Logs are kept off this
repository, under `~/attadipa-bench/i436/`, because bench GNSS logs are where a
real position would land; these particular ones carry none, for the reason
§4 gives.

## 1. What the module answered — MEASURED

A `UBX-MON-VER` poll (`B5 62 0A 04 00 00 0E 34`) returned a 190-byte payload:

```
sw  : ROM SPG 5.10 (7b202e)
hw  : 000A0000
ext : FWVER=SPG 5.10
ext : PROTVER=34.10
ext : MOD=MIA-M10Q
ext : GPS;GLO;GAL;BDS
ext : SBAS;QZSS
```

`MOD=MIA-M10Q` is the module naming its own part number. That is the strongest
form this fact can take short of a decapsulation, and it is strictly stronger
than the listing and the recollection that stood before it.

Three consequences follow immediately and are not separate findings:

- **The Quectel LS550G branch is dead** on this unit. Nothing that exists only
  to serve it needs building.
- **DC4 at 850 mV is not a GNSS requirement here.** That figure is the LS550G's
  core supply. See §3 for what DC4 is actually doing.
- **Assistance, when it is designed, is u-blox AssistNow**, not Quectel's
  mechanism. This report does not design it.

The same reply came back **through the pass-through**, from a host script that
was written against the bench modules and changed in no way — which is the
DoD's fourth item and also the reason the identification can be repeated by
anybody with the watch and a USB cable.

## 2. Where the port is, and how fast — MEASURED

```
rx 42 tx 41 @ every speed:    0 bytes,  0 NMEA          <- both directions wrong
rx 41 tx 42 @  38400      :  369 bytes, 12 NMEA
rx 41 tx 42 @ 115200      : 1309 bytes,  0 NMEA         <- noise, not speech
PORT FOUND: rx 41, tx 42, 38400 baud
```

So **GPIO 41 is the module's TX and GPIO 42 its RX**, and
`docs/research/HARDWARE_MATRIX.md:103` — "UART: TX 42, RX 41" — is written from
the **CPU's** perspective. That row was correct and ambiguous at the same time;
it is now only correct. The speed, 38400, is the one `LilyGoLib@38e6f8d`
`src/LilyGoWatchS3.cpp:220` opens the port at.

The middle line above is the trap this run walked into once and is recorded so
nobody walks into it twice. A wrong baud is **not** silence: the UART frames the
same waveform on the wrong bit boundaries and hands up a byte for every one, so
the *most* bytes arrive at the *most* wrong speed. A sweep that scores on byte
count picks noise over speech, and this one did, and then interrogated the noise.

## 3. Which rail — MEASURED, and narrower than it looks

```
AXP2101: LDO enable 0x17 -> 0x17 (ALDO3 panel+touch, ALDO2 backlight)
AXP2101: LDO enable -> 0x17 (BLDO1 3.3 V, GNSS)
AXP2101: DC enable 0x19 (DC3 off) -- read, not written
```

**D6 is answered: BLDO1.** `REG 0x80` reads `0x19` — DCDC1, DCDC4 and DCDC5
enabled, **DCDC3 clear** — while the module is talking. A rail that is off is
not powering anything, so DC3 is excluded by observation rather than by
argument.

Two honest limits on that:

- **The write did not prove itself.** `REG 0x90` read `0x17` *before* the BLDO1
  write on every boot, and bit 4 was already set. The register is battery-backed
  and survives a CPU reset, so something earlier — vendor firmware, or a
  power-on default — had already enabled it. What is shown is that BLDO1 is
  enabled and the module speaks; **not** that this firmware's write is what
  enabled it. Clearing the bit to watch the module go silent would show that,
  and was not done: D6 asks which rail, and DC3 being off already answers it.
- **DC4 being on says nothing about GNSS.** It is on (`0x19` bit 3) and the
  module is a u-blox, which needs no 850 mV core. What DC4 feeds on this board
  is not established here and is not this issue's question.

## 4. What it is receiving — MEASURED, and it is nearly nothing

Indoors, at a desk, across 12 five-second windows:

```
$GNGGA,,,,,,0,00,99.99,,,,,,*56
$GPGSV,1,1,01,28,,,08,1*67
```

Fix quality `0`, zero satellites used, DOP `99.99` — **no fix**. One GPS
satellite, PRN 28, appears in `GSV` at a C/N0 of 8–12 dB-Hz, which is far below
anything usable and is the level at which a receiver reports a signal it cannot
track. That single number is worth keeping for one reason only: **it is not
zero**, so the antenna is connected and the receiver front end is alive. A
module with a broken antenna feed reports an empty sky, not a weak one.

**Nothing about position quality is claimed from this run.** It was done at a
desk; there was no sky and there were no fixes. §4a is a separate run that did
get a fix, and it measures repeatability, not accuracy. Time-to-first-fix is
unmeasured in both.

Talkers seen: `GP`, `GA`, `GB`, `GQ` — GPS, Galileo, BeiDou, QZSS. **`GL` is
absent**, so GLONASS is disabled in the configuration the module is running,
even though §1 shows the firmware supports it. That is an observation about the
current configuration, not about the part, and nothing was written to change it.

Sentences seen. The instrument prints each distinct id once and every `TXT`
verbatim —
`firmware/main/gnss_bridge.cpp:256` — "// Every distinct five-character sentence id once, plus every TXT verbatim."
Four separate runs printed the same nine ids and no tenth:

```
  sentence GAGSV      sentence GNGSA      sentence GPGSV
  sentence GBGSV      sentence GNRMC      sentence GQGSV
  sentence GNGGA      sentence GNVTG
  sentence GNGLL
```

That is six sentence types — `GGA`, `GLL`, `GSA`, `RMC`, `VTG` and `GSV` — with
`GSV` repeated once per constellation, which is what the talker list above
already says. The logs are `i436-run2.log` through `i436-run5.log` under
`~/attadipa-bench/i436/`, uncommitted for the same reason §4a's scripts are.

**Whether the module sends `TXT` is `UNKNOWN`, and no run here could answer
it.** The instrument does look for it — the same line above says so, and it
prints every `TXT` verbatim — and it printed none. But `TXT` is what a module
says unprompted at start-up, and no window here began at the module's
power-up: `msss` read 4986591 ms, so BLDO1 had been up 83 minutes across
several CPU reboots. An absence over that window
is the window's property, not the module's. **Next action:** cycle BLDO1 with
the bridge listening — the same rail cycle §3 already owes.

## 4a. What the module does under sky — MEASURED, and it is not accuracy

Two runs on a balcony, the second of them alongside a reference receiver.
**No position from either run is reproduced here.** This file already says
bench GNSS logs stay out of the repository because a real fix is the owner's
home; a mean of those fixes is the same address with the noise averaged out,
so only distances appear below, and a distance discloses nothing.

### How these numbers were derived

Every figure is scatter about the run's **own mean**, computed the same way in
both runs: each fixed GGA is converted to degrees, the run's mean latitude and
longitude are taken, and each fix's distance from that mean is
`hypot(dlon·111320·cos(lat), dlat·111320)` metres. `rms` is the quadratic mean
of those distances, `CEP50` their median, `95 %` the 95th percentile. The flat
scale is good to well under a metre over the tens of metres involved. The
scripts are bench-side, under `~/attadipa-bench/i436/`, and are not committed
for the reason above; the formula above is the whole of what they do.

**A mean is not a truth point.** Nothing here knows where the receivers
actually were, so none of this is error against truth — it is how far each
receiver's answers sat from its own centre. Read as accuracy it would be
reading something nobody measured.

### Run 1 — the watch alone

2197 s (36.6 min), 2186 fixes out of 2198 GGA, longest gap between fixes 5.0 s
— so near-continuous, not unbroken:

```
rms 22.04 m   CEP50 13.62 m   95% 45.66 m   max 70.63 m
satellites used 4-7      HDOP 3.70 - 15.56
```

### Run 2 — against a reference receiver, same minutes

30 min, the watch logged simultaneously with a GT-U12 (ALLYSTAR HD8041D,
L1+L5, external active antenna). The reference receiver was moved partway
through, which splits the run in two — its satellite count went **up**
(15.3 → 17.6) and its HDOP **down** (1.55 → 1.14) across that boundary while
its scatter grew sixfold, and a receiver holding 17 satellites at HDOP 1.14
does not drift. So this is two experiments:

| | | rms | CEP50 | 95 % | max | sats |
| --- | --- | --- | --- | --- | --- | --- |
| apart | T-Watch | 15.45 m | 9.30 m | 33.95 m | 51.58 m | ~6 |
| | GT-U12 | **1.84 m** | 1.31 m | 3.75 m | 5.94 m | ~16 |
| together | T-Watch | 13.92 m | 10.72 m | 25.98 m | 32.39 m | ~6.7 |
| | GT-U12 | 11.49 m | 9.18 m | 23.86 m | 30.81 m | ~17 |

In the second half the two receivers' means were 1.4 m apart — effectively the
same place. **There, an external active antenna seeing 17 satellites at HDOP
1.14 was 17 % better than the watch seeing 6.7** — while the same GT-U12 had
been six times better than either, one short move away. The watch is the
control that did not move, and its own scatter barely changed between the
halves (15.45 → 13.92 m).

So the honest conclusion is about placement, not about the part: at a spot
this poor, a far better receiver buys almost nothing. What this run does
*not* establish is how the watch performs at a good spot, because the watch
was never put in one.

### Velocity does not converge the way position does

Both receivers stationary, NMEA RMC speed over ground:

| | apart | together |
| --- | --- | --- |
| T-Watch | mean 1.34 kn, max 4.40 kn (8.2 km/h) | mean 1.26 kn, max 5.28 kn (9.8 km/h) |
| GT-U12 | mean 0.05 kn, max 0.47 kn | mean 0.14 kn, max 1.16 kn (2.1 km/h) |

Co-located, position scatter came within 17 % and velocity noise stayed about
ninefold apart. The watch also emits **no course at all** — RMC field 8 is
empty — so it has a speed it will not give a direction to. Anything on the
watch derived from GNSS velocity is reading this, and a device lying still
reads up to 9.8 km/h.

## 4b. Interference and spoofing — MEASURED, polled read-only

Asked of the module over the bridge with UBX polls only: `MON-RF`,
`NAV-STATUS`, `CFG-VALGET` on the RAM layer, `NAV-SIG`. No `CFG-VALSET` and no
`UBX-CFG-CFG`, so nothing was written and nothing saved.

```
UBX-MON-RF   block 0 (L1): jammingState 0, antStatus 2 (OK), antPower 1 (ON),
                           noisePerMS 85, agcCnt 1488/8191 (18 %),
                           cwSuppression 5/255
UBX-NAV-STATUS   spoofDetState 1, ttff 320592 ms, msss 4986591 ms
UBX-CFG-VALGET   CFG-ITFM-ENABLE 0, CFG-ITFM-ENABLE_AUX 0,
                 CFG-ITFM-BBTHRESHOLD 3, CFG-ITFM-CWTHRESHOLD 15,
                 CFG-ITFM-ANTSETTING 0
UBX-NAV-SIG      26 signals, 0 with authStatus = Authenticated
```

- **Jamming detection is disabled.** `jammingState = 0` alone is ambiguous —
  the interface description defines it as "unknown or feature disabled or flag
  unavailable" — but `CFG-ITFM-ENABLE = 0` read back from the RAM layer settles
  it. That is also the documented default, so nothing in the T-Watch turns it
  on.
- **Spoofing detection is running and quiet.** `spoofDetState = 1`, and 1 is
  distinct from 0 ("Unknown or deactivated"). u-blox's own caveat travels with
  it: 1 means the detector was not triggered in that epoch, *not* that the
  receiver is not being spoofed.
- **There is no signal authentication.** The `UBX-SEC` class in this firmware
  holds one message, `SEC-UNIQID`; `SEC-SIG` and `SEC-SIGLOG` are absent.
  OSNMA appears in the interface description only as a note explaining the
  `authStatus` field, with no section, no configuration key and no way to
  enable it — and live, 0 of 26 signals came back authenticated.
- **None of this can change on this unit.** The interface description's
  section 1.2 states that generation-10 receivers execute firmware from
  internal ROM, and that firmware loaded from flash or a host would be marked
  `EXT`. This module reports `ROM SPG 5.10 (7b202e)` with no `EXT`, matching
  the document's own version table row for protocol 34.10 exactly.
- **The RF environment was clean**, so none of the scatter above is
  interference: `cwSuppression` 5 of 255 and AGC at 18 % of maximum gain.
- `ttff = 320592 ms` (5 min 21 s) against `msss = 4986591 ms` (83 min) — both
  measured from the **module's** last power-up, not from the poll. BLDO1 stays
  up across ESP32-S3 resets, so 83 minutes of module uptime spans several
  firmware reboots. The 5 min 21 s is a real first fix under this sky, but it
  is not a controlled cold start.

## 5. What is still UNKNOWN

- Position accuracy against a surveyed point. §4a measures repeatability only,
  and open sky was never reached — every run so far was a balcony with roughly
  a quarter of the sky.
- How the watch performs at a *good* spot. §4a shows the reference receiver
  managing 1.84 m rms one short move from where both later sat at 11-14 m, and
  the watch was never carried to that better spot.
- Cold-start TTFF. §4b has a 5 min 21 s first fix from the module's own
  counter, but the start conditions were not controlled; a real figure needs
  `UBX-CFG-RST`.
- Whether the single-band limit costs anything here. §4a cannot answer it: the
  two receivers differ in antenna as well as in band, and when co-located their
  position scatter came within 17 %.
- Whether the `GPS_LDO` enable net on FPC pin 3
  (`docs/research/HARDWARE_MATRIX.md:103` — "enable net `GPS_LDO` on FPC pin 3")
  is doing anything the PMU rail is not. Never exercised.
- What DC4 feeds. §3.
- Whether the module retains an almanac across a power cycle on this board,
  which is a backup-supply question and was not asked.

## 6. What this changes elsewhere

- `OPEN_QUESTIONS.md` A2's GNSS half and D6 are closed by this report.
- `VERIFIED_FACTS.md`'s "The T-Watch GNSS module is also a variant" claim
  resolves to one of its two branches.
- [#429](https://github.com/hleserg/Attadipa/issues/429) — the NMEA parser and
  UART `PositionProvider` — now knows the watch's seam: 38400 baud, GPIO 41 in,
  `PROTVER=34.10`, so NMEA 4.10 with GSA `systemId` and GSV `signalId`. It also
  inherits the layout hazard found on the bench modules: field counts differ
  between sentence types and between vendors, so a parser must decide layout per
  sentence type from field count and never infer one sentence's from another's.
