# Open Questions

Everything the project needs to know and does not. Each entry names what would
resolve it, so answering is a task rather than a search.

Status: **UNKNOWN** (no source) · **CONFLICTING** (sources disagree) ·
**ASSUMPTION** (plausible, unconfirmed, must stay flagged in code) ·
**RESOLVED** (moved to [VERIFIED_FACTS.md](VERIFIED_FACTS.md)).

An UNKNOWN that blocks work is a blocker — record it in
[../../TASKS.md](../../TASKS.md) in the blocker format rather than coding past it.

The board survey of 2026-08-21 resolved most of the *documentary* questions.
What remains is dominated by one thing: **no physical board has been touched.**
Everything that needs a measurement is still open, and no amount of reading
will close it.

---

## Blocking everything measurable

| # | Question | Status | Resolved by |
|---|---|---|---|
| A1 | Does the developer have either board physically, and which revision? | **UNKNOWN** | ask the project owner |
| A2 | If a T-Watch is present: which of the five radio chips, and which of the two GNSS modules? | **UNKNOWN** | inspect the unit / order details |
| A3 | Is there a second radio-capable device, so mesh can be tested at all? | **UNKNOWN** | ask the project owner |
| A4 | Which regulatory region governs LoRa operation here? | **UNKNOWN** | ask the project owner |
| A5 | **Is an external magnetometer intended at all?** Neither board has one, so every compass feature in the plan currently has no hardware to run on | **UNKNOWN** | ask the project owner — see [../hardware/MAGNETOMETER_BACKLOG.md](../hardware/MAGNETOMETER_BACKLOG.md) |

A1 and A2 gate all bring-up, the entire interference matrix, and every power
number. A2 in particular decides whether the radio is sub-GHz or 2.4 GHz —
which changes region rules and mesh interoperability, not just a driver.

A4 is not a preference. Which frequencies, power levels and duty cycles are
lawful is set by the region the device operates in, and the answer changes what
the radio may legally do. It has to be settled before anything transmits.

A5 decides whether five epics in §67 are dormant or dead.

Until these are answered: simulator, architecture, host tests and protocol work
proceed; hardware work does not.

## Hardware — measurement required

| # | Question | Status | Resolved by |
|---|---|---|---|
| H1 | Real power draw of Firefly firmware per state, per board | UNKNOWN | measurement; vendor figures are a target, not evidence |
| H2 | Can the AXP2101 measure current/energy on these boards, or only voltage? | UNKNOWN | AXP2101 datasheet + schematic sense-resistor check |
| H3 | Real TTFF and fix quality for the fitted GNSS module | UNKNOWN | outdoor measurement |
| H4 | Does any of the suspected interference actually occur? | UNKNOWN | the measurement procedure in [../hardware/INTERFERENCE_MATRIX.md](../hardware/INTERFERENCE_MATRIX.md) |
| H5 | Which wake sources are usable in practice, and what does each cost? | UNKNOWN | measurement; vendor table gives the shape |
| H6 | AMOLED brightness vs power on the Waveshare board | UNKNOWN | measurement |
| H7 | Achievable LVGL frame rate and redraw cost on each panel | UNKNOWN | benchmark on hardware |
| H8 | **Is ALDO1 the `+3V3` rail?** The vendor doc says ALDO1 is unused; the schematic shows it driving `+3V3` | **CONFLICTING** | read the AXP2101 rail-enable and voltage registers on a powered board, then cut one rail at a time and watch which parts drop off the I2C scan |
| H9 | Real backlight current vs brightness, against the schematic's 45 mA at full | UNKNOWN | measurement; the 45 mA figure is a datasheet-level I_F, not a measured draw |

## Hardware — documentary gaps

| # | Question | Status | Resolved by |
|---|---|---|---|
| D1 | Waveshare flash and PSRAM size and type | UNKNOWN | `esptool flash_id` on hardware, or the schematic BOM |
| D2 | Waveshare battery capacity and charge path details | UNKNOWN | schematic + product page |
| D3 | Waveshare expansion connector pinout and what it can carry | UNKNOWN | schematic page for the connector |
| D4 | Does the Waveshare board have any haptic output at all? | UNKNOWN | schematic; none found in BSP or README |
| D5 | Waveshare button/wake inputs — BSP declares none; is that the board or the BSP? | UNKNOWN | schematic |
| D6 | T-Watch: which PMU rail powers GNSS on the *specific* unit (BLDO1 vs DC3) | UNKNOWN | inspect the unit for rear BOOT/RST buttons |
| D7 | Exact ST7789V3 and CO5300 init sequences and their timing | UNKNOWN | vendor driver source |
| D8 | Is the T-Watch main I2C bus shared with anything timing-sensitive? | PARTIAL | schematic read: five devices confirmed on SDA 10 / SCL 11, plus a possible sixth — see D9. Timing sensitivity still needs driver review |
| D9 | **Does the GNSS daughterboard connect the `MIA-M10Q` `SDA`/`SCL` to the FPC?** If it does, the GNSS is a sixth device on the main I2C bus at 0x42 | UNKNOWN | trace the daughterboard FPC net list, or scan the bus on a board with the module fitted |
| D10 | **What is radio `DIO3` (GPIO 6) for on this board — TCXO supply or a second interrupt?** | UNKNOWN | HPD16B3 module datasheet + the vendor radio driver's `setDio3AsTcxoCtrl` usage |
| D12 | **Is the T-Watch PSRAM quad or octal?** The vendor doc says QSPI; the schematic's `ESP32-S3-R8` marking denotes octal. Different `sdkconfig`, ~2× bandwidth difference | **CONFLICTING** | `esptool.py flash_id` / `esp_psram` probe on hardware, or the SoC datasheet against the exact part marking. Blocks the LVGL buffer ADR — see [../architecture/RESOURCE_BUDGET.md](../architecture/RESOURCE_BUDGET.md) |
| D11 | Which AXP2101 rail is the schematic's net `LDO5`? It feeds DRV2605 `EN`, and the vendor rail map says BLDO2 — consistent but not proven | UNKNOWN | PMU register read on hardware |

## MeshCore

| # | Question | Status | Resolved by |
|---|---|---|---|
| M1 | Current architecture and integration points, with commit hashes | UNKNOWN | read upstream source |
| M2 | Which revision to pin | UNKNOWN | M1 + release history |
| M3 | Actual crypto primitives and byte-level format | UNKNOWN | read upstream source, not the plan document |
| M4 | Threading and concurrency assumptions | UNKNOWN | read upstream source |
| M5 | Memory footprint on ESP32-S3 | UNKNOWN | build and measure |
| M6 | How it abstracts the radio — and whether it supports all five T-Watch chips | UNKNOWN | read upstream source |
| M7 | Companion protocol shape | UNKNOWN | read upstream source |
| M8 | Can Firefly's needs be upstreamed rather than forked? | UNKNOWN | M1, then talk to upstream |
| M9 | Does MeshCore assume it owns the radio exclusively? | UNKNOWN | M4, M6 |

M9 matters more than it looks: if MeshCore assumes exclusive, uninterrupted
control of the radio, it conflicts with a coordinator that wants to schedule
quiet windows around it. That is an integration constraint, not a detail.

## Architecture

| # | Question | Status | Resolved by |
|---|---|---|---|
| X1 | How does a capability express **variant** (which of five radios) and **degree** (accel-only vs 6-axis)? | UNKNOWN | ADR — a boolean `has()` is demonstrably insufficient |
| X2 | Who owns PMU rail sequencing — a rail service, or each driver? | UNKNOWN | ADR |
| X3 | How does an application render a capability that is *absent* rather than merely idle? | UNKNOWN | UX + API design together |
| X4 | Two RTC parts, two IMU parts, two audio paths — one interface each, or per-board? | UNKNOWN | driver design |
| X5 | Does the coexistence coordinator earn its complexity on boards with no measured interference? | UNKNOWN | H4 — build the measurement first, the mitigation second |

## Toolchain and dependencies

| # | Question | Status | Resolved by |
|---|---|---|---|
| T1 | Which ESP-IDF version to target | **narrowed** | Waveshare supports v5.5.5 and v6.0.2; its BSP needs ≥5.3. Decide with the LilyGO side. |
| T2 | Which LVGL major version | **narrowed** | Waveshare BSP accepts `>=8,<10`; LVGL 9 is the forward choice. Confirm simulator support. |
| T3 | Is RadioLib needed, or does MeshCore bring its own radio layer? | UNKNOWN | M6 |
| T4 | Simulator display backend | UNKNOWN | follows T2; SDL2 not currently installed |
| T5 | Host test framework | UNKNOWN | small decision, no ADR needed |
| T6 | Use the Waveshare BSP as a dependency, or take only its pin facts? | UNKNOWN | it is Apache-2.0 and incomplete — a reuse-ledger decision |
| T7 | Does the LilyGO PlatformIO pin to IDF 4.4.7 constrain Firefly? | ASSUMPTION: no | Firefly is ESP-IDF-native and does not use the Arduino layer |

## Product

| # | Question | Status | Resolved by |
|---|---|---|---|
| Q1 | What should the Waveshare board *be*, given it cannot do mesh or navigation? | UNKNOWN | product decision by the owner |
| Q2 | Is a magnetometer expected to be added externally, or is heading GNSS-only for good? | UNKNOWN | product decision by the owner |
| Q3 | Realistic battery-life target | UNKNOWN | measurement, after bring-up |

Q1 is a genuine product question, not an engineering one. The specification
describes a mesh-and-navigation wearable; on this board neither exists. It can
still be a watch, an audio device, a development and UI platform — but somebody
has to decide, and it is not a decision to make by writing code.

---

## Recently resolved

Moved to [VERIFIED_FACTS.md](VERIFIED_FACTS.md) on 2026-08-21: both boards'
complete peripheral inventory, pin maps, I2C addresses and PMU rail map; the
absence of LoRa and GNSS on the Waveshare board; the absence of a magnetometer
on both; the five LoRa and two GNSS variants of the T-Watch; the missing touch
reset line; the haptic rail gating; the incomplete vendor BSP; and the
CO5300 / SH8601 driver nuance.
