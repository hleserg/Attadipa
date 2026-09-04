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

**No accuracy, no time-to-first-fix and no satellite count for this module are
claimed.** None was measured. Getting them needs sky, and this run had none.

Talkers seen: `GP`, `GA`, `GB`, `GQ` — GPS, Galileo, BeiDou, QZSS. **`GL` is
absent**, so GLONASS is disabled in the configuration the module is running,
even though §1 shows the firmware supports it. That is an observation about the
current configuration, not about the part, and nothing was written to change it.

## 5. What is still UNKNOWN

- Position accuracy, cold- and warm-start times, and satellite count under open
  sky. Requires taking the watch outdoors.
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
