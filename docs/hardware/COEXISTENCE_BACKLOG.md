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
| **Amplifier has no shutdown pin** | `SD_MODE` strapped, R14 = 1 MΩ | The only way to silence it is to drop `DLDO1`. Audio state *is* a rail state |
| **`+3V3` may be switchable** | ALDO1 conflict, OPEN_QUESTIONS H8 | If it is, cutting it takes the accelerometer, RTC, haptic, mic and IR with it |
| **Single accelerometer interrupt** | BMA423 `INT2` not routed (R12, R15 not fitted) | Every motion event shares one line — tilt, tap and step cannot be separated in hardware |
| **Backlight is the largest controllable load** | 45 mA at full brightness, 1S3P at I_F 3 × 15 mA | Any concurrency decision competes with the screen for current |

## The example from the plan that cannot be tested

The master plan motivates coexistence with a vibration motor disturbing a
magnetometer. **Neither board has a magnetometer**, so that specific pair is not
measurable here in any configuration — see
[MAGNETOMETER_BACKLOG](MAGNETOMETER_BACKLOG.md).

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
| C-08 | Haptic / magnetometer tests | **NOT POSSIBLE** | no magnetometer on either board |
| C-09 | Audio / magnetometer tests | **NOT POSSIBLE** | same |
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

[INTERFERENCE_MATRIX.md](INTERFERENCE_MATRIX.md) lists 14 candidate pairs. Every
one is marked `THEORETICAL RISK` and the results table is empty, because no
measurement has been made. Two of those pairs (haptic/magnetometer,
audio/magnetometer) should be reclassified as **NOT MEASURABLE ON THESE BOARDS**
rather than left looking like pending work.
