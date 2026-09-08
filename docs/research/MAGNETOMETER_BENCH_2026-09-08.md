# Received magnetometer: unpowered tracing and watch supply check

**Date:** 2026-09-08. **Task:** [#450](https://github.com/hleserg/Attadipa/issues/450).
**Result:** the owner measured **3.313 V DC** at the Waveshare's labelled
`3V3` pad relative to adjacent `GND`, with the magnetometer disconnected.
The purple module has not been powered in this procedure. Its silicon identity,
internal supply voltage, reset routing and actual I2C address remain **UNKNOWN**.

## Evidence and classification

`MEASURED` below means owner-reported physical readings, not independently
witnessed probe placement. `VERIFIED` identifies legible photographic markings
or a cited manufacturer's statement. `ASSUMED` identifies a working hypothesis;
`UNKNOWN` is not filled from a module name. The owner explicitly noted that a
probe could land on the wrong point; the agent's visual interpretation can also
be wrong. Inconsistent readings are retained.

Original photos are local under `/home/hleserg/temp/magneto/`, now organised as
`AK09911C/`, `GY-271/`, `BMI160/`, `GY-58/`, `tester/` and `Waveshare watch/`.
Folder names and seller labels do not identify silicon. The selected originals
are identified by SHA-256 in the table below; they were not altered or uploaded.
The watch photo comparison lives in [WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md)
§1.10. The raw exchange is recorded in
[the bench comment](https://github.com/hleserg/Attadipa/issues/450#issuecomment-5590285432).

| Original relative path | SHA-256 |
| --- | --- |
| `AK09911C/IMG_20260908_214331.jpg` | `b67ae8b7f9560cae36cddd572aa53a124d29e9f47fcfd4fe657883f303c4a878` |
| `AK09911C/IMG_20260908_214548.jpg` | `08e45b055c4291ab5ce79c16c740ff0d66bab1631c6866fd13bdea9c6d33fadf` |
| `tester/IMG_20260908_223841.jpg` | `7590cb75a1699f9720cdaa4e78109899d13d5feea7fe1a30cbb01e641879ce15` |
| `tester/IMG_20260908_223911_1.jpg` | `6bb00edd6135f0e7cb846f6758f49965b7cf1e822c23660f7b0b1d969ef27be2` |
| `Waveshare watch/IMG_20260908_223437.jpg` | `6bdcb3c3aef1863ab44f9aafe4934466f2434500a80f32c4b4512077fee27484` |
| `Waveshare watch/IMG_20260908_223653.jpg` | `c70725a888b4bb80a218f25a775967c42336a6104f1badbfb20e68e291dc2e23` |
| `Waveshare watch/IMG_20260908_223714.jpg` | `30b13289e9d92b901b9409aa2c07c661faa1caae9effbd171466d926a55320b1` |
| `Waveshare watch/IMG_20260908_223729.jpg` | `51d7937ab4cd1d9648261ad16c775c960d7fb21a4a9fe8351398f3e7638730f1` |
| `Waveshare watch/IMG_20260908_223741.jpg` | `a8159275570c6f9e093131d9441988b32c359768e3bc8b13949155082719f53a` |

## Purple module: physical points and readings

`VERIFIED` photo markings: PCB labelled `AK09911C`, three resistors marked
`103`, and a three-lead package marked `662K`. The sensor's own marking is
not resolved sufficiently to confirm AK09911C. The blue GY-271 is not selected
or electrically identified by these measurements.

All resistance checks were requested with the module isolated and unpowered.
Point **P** means the **left metal end of the uppermost `103` immediately below
`662K`**, with the component side facing the viewer and header holes on the left.
The `662K` package has one upper lead and two lower leads in that orientation.
**C-top** means the beige capacitor above the square sensor, to the right of
`662K`. Left/right below preserve the descriptions given to the owner.

| Red probe | Black probe | MEASURED display / qualification |
| --- | --- | --- |
| VCC | GND | 1.56; owner tentatively identified unit as MΩ |
| RST | VCC | OL |
| RST | GND | OL |
| SDA | VCC | 0.593 MΩ |
| SCL | VCC | 0.594 MΩ |
| CAD | VCC | 0.595 MΩ |
| CAD | GND | 1.57 MΩ |
| SDA | SCL | 19.92 kΩ |
| SDA | CAD | 19.9 kΩ |
| SCL | CAD | 19.91 kΩ |
| RST | SDA | 0.468 MΩ |
| SDA | P | 9.96 kΩ |
| Lower-right 662K lead | P | 62.4 kΩ initially; 0.2 Ω on repeat |
| RST | P | OL, repeated OL |
| Probe tip | Other probe tip | 0.4 Ω contact control |
| Lower-left 662K lead | GND | 0.2 Ω |
| Single upper 662K lead | VCC | 0.2 Ω |
| CAD | P | 8.55 kΩ; not yet reconciled with pairwise readings |
| C-top left end | P | 62.2 kΩ |
| Left end of capacitor below sensor | P | 62.1 kΩ |
| Unassigned | Unassigned | Trailing unnumbered 62.1 kΩ; fourth measurement versus repeat UNKNOWN |
| P | Lower-right 662K lead | 0.2 Ω repeated contact/reference check |
| C-top left end | Lower-right 662K lead | 61.9 kΩ |
| C-top right end | Lower-right 662K lead | 0.2 Ω |
| C-top left end | GND | 0.2 Ω |

The near-zero results support continuity at ordinary meter resolution, not an
accurate 0.2 Ω trace resistance: the shorted probes themselves read 0.4 Ω.
The initial 62.4 kΩ to the lower-right lead did not reproduce; its cause is
**UNKNOWN**. The capacitor results put its described **left** end on GND and
its **right** end on the lower-right lead, contrary to the initial photo-based
interpretation. That discrepancy is explicit; neither the owner nor the photo
is silently relabelled. No sensor VDD/VID pin has yet been traced directly.

**VERIFIED source, conditional application:** Torex
[XC6206 datasheet ETR03005-009](https://product.torexsemi.com/system/files/series/xc6206.pdf),
pages 3 and 14, gives SOT-23 pins 1 = GND, 2 = VOUT, 3 = VIN and marking rules
consistent with `662K` for a nominal 3.3 V device. **ASSUMED:** this module uses
a compatible regulator. Actual manufacturer and output remain **UNKNOWN**.
Continuity ties VCC to its single upper lead and P to its lower-right lead;
consequently VCC must not be assumed to be the internal pull-up/sensor rail.
The 9.96 kΩ SDA-to-P path is evidence for that path, not three proven equal
pull-ups. OL at RST establishes neither a broken trace nor a valid reset-high
voltage. Do not bridge RST to VCC on that basis.

## Watch supply and meter correction

**VERIFIED photographs:** TESMEN `TMM-569A`; red lead in `INPUT`, black in
`COM`. The photographed dial and LCD show **DC mV**, not DC V. The LCD's
`000.0 mV` is not a photographed watch-rail measurement. After the initial
owner-reported OL, the instruction was to select the combined V position and
DC using `FUNC`. The owner then reported **3.313 V** for red on labelled `3V3`,
black on adjacent `GND`. This supports a range-selection explanation for OL;
it is not evidence of an open rail or a defective watch.

**MEASURED, owner-reported:** 3.313 V DC, magnetometer disconnected.
**UNKNOWN:** meter calibration/accuracy, exact watch firmware and USB/battery
state during this reading, loaded voltage, ripple, current headroom and whether
the expansion rail remains live in sleep. The reading does not resolve the
rail-ownership question in [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md) §5.9.

## Boundary for the next bench step

The selected source is Waveshare first, per owner instruction. Before changing
wiring, remove watch power, including the battery, and confirm the work points
are unpowered. First module power must use confirmed VCC/GND connections;
measure its internal rail before connecting SDA/SCL or deciding the reset-high
connection. A nominal 3.3 V regulator fed from 3.313 V has no guaranteed full
regulation margin; measure, do not substitute a predicted output. Do not bypass
the regulator or substitute VBUS/5 V while the sensor routing is unknown.

Powered module identification, raw XYZ, calibration, tilt compensation,
case-placement comparisons and vibration OFF/ON/recovery:
**NOT EXECUTED — HARDWARE REQUIRED**. No port was opened or firmware changed
by this procedure. Fit no motor before a working, calibrated compass baseline.
