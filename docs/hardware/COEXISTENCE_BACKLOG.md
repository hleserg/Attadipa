# Coexistence backlog

The mandatory epics from master plan §68.

Coexistence is the part of the architecture that stops two subsystems from
quietly ruining each other. On these boards it is not hypothetical — the
schematics show the contention directly.

## What actually contends on this hardware

Established from the vendor documents and both schematics
([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md)), not assumed:

| Contention | Evidence | Why it bites |
|---|---|---|
| **Shared I2C bus** | five devices on T-Watch SDA 10 / SCL 11 — PMU, RTC, accelerometer, haptic driver, and possibly the GNSS at 0x42 (D9) | A haptic pulse and a battery read are on the same wire. Bus ownership must be decided once, centrally |
| **Shared rail: display + touch** | both on ALDO3 | The touch controller has **no reset line** (R39 not fitted), so recovering it means cycling a rail the display is on |
| **Radio vs Wi-Fi/BLE** | LoRa on ALDO4; Wi-Fi/BLE in the same SoC | Two radios, one antenna environment, one power budget |
| **The node link** | Wi-Fi or BLE, standing, on **both** boards | A continuous rather than occasional radio and power load — including on the board this table otherwise describes as having one radio. On a T-Watch it is a third transmitter in the same antenna environment; on a Waveshare it is how a second LoRa's traffic enters the power budget at all |
| **Amplifier has no shutdown pin** | `SD_MODE` strapped, R14 = 1 MΩ | The only way to silence it is to drop `DLDO1`. Audio state *is* a rail state |
| **`+3V3` may be switchable** | ALDO1 conflict, OPEN_QUESTIONS H8 | If it is, cutting it takes the accelerometer, RTC, haptic, mic and IR with it |
| **Single accelerometer interrupt** | BMA423 `INT2` not routed (R12, R15 not fitted) | Every motion event shares one line — tilt, tap and step cannot be separated in hardware |
| **Backlight is the largest controllable load** | 45 mA at full brightness, 1S3P at I_F 3 × 15 mA | Any concurrency decision competes with the screen for current |

## The example from the plan that cannot be tested

The master plan motivates coexistence with a vibration motor disturbing a
magnetometer. **Neither board has a magnetometer as shipped**, and until
2026-08-22 that made the pair unmeasurable here in any configuration.

**A5 is answered and this section is narrower than it was.** The owner has
ordered a CJMCU-9911 and a GY-271 and is soldering one into the Waveshare unit
([OD-17](../research/OWNER_DECISIONS.md)). So the magnetometer arrives — but the
*haptic* pair still cannot be measured on it, because that unit has no vibration
motor fitted (`OBSERVED`, T-097). The audio pair can be, once the sensor is
placed, a rail is chosen and the seventh-device bus hazard **T-130** is settled — T-096 is the *node* link and does not ask this. Two
epics, two different blockers; see
[MAGNETOMETER_BACKLOG](MAGNETOMETER_BACKLOG.md), which is the authority.

The architecture survives the loss of its example because the contention above is
real and independently sufficient. But the epics that name the magnetometer are
blocked, and saying so is more useful than quietly substituting a different test.

## Backlog

| # | Epic | Kind | Can start now? |
|---|---|---|---|
| C-01 | Hardware operation arbiter | DESIGN | **Yes** — the central mechanism; ARCHITECTURE §9 |
| C-02 | Bus ownership | DESIGN + BUILD | **Yes, and first.** Five devices on one bus is today's problem, not a future one |
| C-03 | Power rail arbitration | DESIGN + BUILD | **Yes** — shared-rail reference counting; ALDO3 and `DLDO1` need it now |
| C-04 | Quiet windows | DESIGN | **Yes** — the mechanism is not sensor-specific |
| C-05 | Priority model | DESIGN | **Yes** — must state plainly that SOS and CRITICAL outrank everything |
| C-06 | GNSS coexistence tests | BLOCKED | hardware (A1) |
| C-07 | LoRa coexistence tests | BLOCKED | hardware (A1) |
| C-08 | Haptic / magnetometer tests | **BLOCKED** | placement (T-109) **and a vibration motor**: the unit the sensor is going into has none fitted, `OBSERVED` — T-097 |
| C-09 | Audio / magnetometer tests | **BLOCKED** | placement (T-109), a rail (G-14), the module pull-ups, and the bus hazard **T-130**. The speaker is on the unit and `VERIFIED` |
| C-10 | Display / GNSS tests | BLOCKED | hardware (A1) |
| C-11 | Charging / GNSS tests | BLOCKED | hardware (A1) |
| C-12 | Diagnostic trace | DESIGN + BUILD | **Yes** — and it must come *before* the tests that need it |
| C-13 | Board-specific interference profile | DESIGN | **Yes** — the shape can be defined; it stays empty until measured |

## Order, and the reason for it

**C-12 before C-06, C-07, C-10 and C-11.** ARCHITECTURE §8 is explicit: build the
measurement before the mitigation. A coexistence test with no trace produces an
anecdote. The trace is the deliverable that makes every blocked test meaningful
the day hardware arrives — so it should be finished *while* waiting, not started
afterwards.

**C-02 and C-03 are not future work.** The shared bus and the shared rails exist
on the boards as drawn. Any second driver written before those two exist will
have to be rewritten.

**C-05 has one non-negotiable clause.** Priority is not only about power and
buses. Anything that could delay an SOS or a critical alert loses, unconditionally
— including every easter egg, which is already written into their invariants.

## Current state of the interference matrix

[INTERFERENCE_MATRIX.md](INTERFERENCE_MATRIX.md) lists **16** candidate pairs
across its two tables — eleven in the first, five in the second — and the
results table is empty, because no measurement has been made. **Twelve** are
marked `THEORETICAL RISK`; the other **four** name the magnetometer and are
marked **`BLOCKED`**, rather than left looking like pending work, with the
blocker named per row because they do not share one.

Two corrections have landed in this paragraph. An earlier version said two rows
and `NOT MEASURABLE ON THESE BOARDS`; both were overtaken by A5 being answered.
A later one kept *"every one is marked `THEORETICAL RISK`"* while changing the
next sentence from a recommendation into the present tense, so the paragraph
contradicted itself in three sentences — review caught it. The count of 14 was
wrong throughout and predates the second table.
