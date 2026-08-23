# Interference Matrix

In a watch-sized enclosure, subsystems interfere physically — magnetically,
electrically, over shared buses, and through the power supply. This file is the
empirical record of what actually happens on real hardware.

**Nothing is recorded here as fact until it has been measured.** The column
that matters is Evidence:

| Evidence | Meaning |
|---|---|
| `THEORETICAL RISK` | there is a physical reason to suspect it; not observed |
| `OBSERVED` | someone saw it happen; not quantified |
| `MEASURED` | quantified, with a repeatable method |
| `CONFIRMED NEGLIGIBLE` | measured, and the effect does not matter |
| `BLOCKED` | there is a physical reason to suspect it, and it cannot be measured here yet. The blocker is named per row and is a fact, not an absence — a part not fitted, a rail not chosen, a board not owned |

`BLOCKED` is not a fifth degree of certainty. It is the same state as
`THEORETICAL RISK` about the *effect*, plus a named obstacle to finding out. It
replaced `NOT MEASURABLE`, which read as a property of physics rather than of
this project's current hardware, and which said "never" about rows that a
soldering iron and a decision about a rail would open. Where an older document
says `NOT MEASURABLE`, it means this.

A `THEORETICAL RISK` row is not permission to add a mitigation. Mitigating a
problem that does not exist costs power and latency for nothing.

Every measured row must name the board revision and firmware commit. An effect
on one revision is not an effect on another.

---

## Candidate pairs

Listed because there is a physical reason to look, **not** because the effect
is known to exist.

| Subsystem A | Subsystem B | Suspected effect | Evidence | Severity | Method | Mitigation | Board | Firmware |
|---|---|---|---|---|---|---|---|---|
| Haptic motor | Magnetometer | magnetic field distorts heading | **BLOCKED** — no motor is fitted to the retrofit board | — | — | — | Waveshare (no motor); T-Watch (not owned) | — |
| Haptic motor | IMU | vibration corrupts accelerometer | THEORETICAL RISK | — | — | — | — | — |
| LoRa TX | GNSS acquisition | RF desensitisation | THEORETICAL RISK | — | — | — | — | — |
| Watch↔node link TX | GNSS acquisition | RF desensitisation | THEORETICAL RISK | both | — | — | — | — |
| Watch↔node link TX | LoRa RX (T-Watch) | RF desensitisation | THEORETICAL RISK | T-Watch | — | — | — | — |
| LoRa TX | Magnetometer | supply current transient | **BLOCKED** — the retrofit board has no LoRa at all, so no placement or rail decision opens this one | — | — | — | T-Watch only, and not owned | — |
| Display DMA | GNSS | broadband EMI | THEORETICAL RISK | — | — | — | — | — |
| High brightness | Battery / GNSS | supply droop | THEORETICAL RISK | — | — | — | — | — |
| Audio amplifier | Magnetometer | speaker magnet and coil current | **BLOCKED** — sensor not placed (T-109), rail open (G-14), module pull-ups unknown, bus hazard T-130 | — | — | — | Waveshare | — |
| Battery charging | GNSS | switching noise | THEORETICAL RISK | — | — | — | — | — |
| Battery charging | Magnetometer | charge current field | **BLOCKED** — sensor not placed (T-109), rail open (G-14), module pull-ups unknown, bus hazard T-130 | — | — | — | Waveshare | — |

**On the four `BLOCKED` rows.** No magnetometer pair in this matrix can be
measured on either board **as shipped** — and three of the four have a route
that does not need a different board, described below, which is why they are
`BLOCKED` and not "never". Neither board has a
magnetometer — the T-Watch carries only a BMA423 accelerometer and the
Waveshare board only a QMI8658 six-axis IMU
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)). They are kept in the table
rather than deleted, because the day an external sensor is fitted they become
the first tests to run. **A5 is no longer what gates them**: it is answered
(OD-17, 2026-08-22 — an external magnetometer is intended, and two candidate
parts are ordered for one Waveshare unit). What gates them now is **three
different things, not one**, and an earlier version of this paragraph asserted a
closed list of two that was true of one row:

- **Audio amplifier × Magnetometer** and **Battery charging × Magnetometer** are
  the two whose *disturbing source* is already on the unit: the speaker is
  `VERIFIED` (`AAC210602A1`, back cover,
  [WAVESHARE_BOARD_RECEIVED](../research/WAVESHARE_BOARD_RECEIVED.md) §1.8), and
  the AXP2101 charge path exists — §1.2 records where the battery connects.

  **They are not gated on placement alone, and an earlier version of this
  paragraph said they were.** Three things are still open, and each would corrupt
  the measurement rather than merely delay it:

  1. **The rail.** `MAGNETOMETER_BACKLOG` G-14 says *"rail is still open"* in the
     same table that called these unblocked. An unpowered sensor measures
     nothing, and a sensor **sharing a rail with** the disturbing subsystem
     measures a confounded circuit — which is worse than not measuring, because
     the number comes out `MEASURED` and nobody can retract it. Deliberately not
     naming a rail: on the Waveshare board
     [HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md) lists ALDO1, ALDO2 and
     ALDO3 as three bare 3.3 V rails and says which load sits on which is *"not
     resolved from the text extraction"*, and both disturbing sources here carry
     `D13` for exactly that. An earlier version named ALDO3 — which is the
     **T-Watch's** display-and-touch rail, and neither the audio path nor the
     charge path on either board. An engineer who read "don't share ALDO3",
     mounted on ALDO1 and believed the confound cleared would have a one-in-three
     chance of a confounded circuit and a certainty of a clean-looking number, in
     the one paragraph whose subject is an irretractable `MEASURED`.
  2. **Pull-ups.** [MAGNETOMETER_RETROFIT](../research/MAGNETOMETER_RETROFIT.md)
     records *"external pull-ups required"* for the retrofit.
  3. **The bus.** The sensor is the **seventh** device on the Waveshare main I2C
     bus — [WAVESHARE_BOARD_RECEIVED](../research/WAVESHARE_BOARD_RECEIVED.md)
     §1.5 lists six (AXP2101 PMU, RTC, touch, IMU, two codecs) and warns that a
     seventh holding `SDA` low *"takes the watch's power management with it"*,
     filed as **T-130** — T-096 scopes a *detachable node* and can close without
     this being asked. `MAGNETOMETER_RETROFIT` §4.3 clears the *address*
     conflict only, and its plan to fit both candidates at once would make eight.

  So: placement plus a rail decision plus the module pull-ups plus T-130 makes
  these two measurable.
  Placement alone makes them *attemptable*, which is not the same sentence and is
  the one that produces a bad number.

  **The charge path is not characterised.** An earlier version of this paragraph
  cited *"§1.3"* for it; §1.3 is the **flash** section. The only charge-path
  mention is in §1.2, quoting the superseded `HARDWARE_MATRIX` row about where
  the battery connects, and that section explicitly declines to give a current
  figure. So the charging test has to establish its own disturbing current as
  part of its method; it cannot cite one.
- **Haptic × Magnetometer** is gated on placement **and** on a motor: the unit
  the sensor is going into
  [has none fitted](../research/WAVESHARE_BOARD_RECEIVED.md) (§1.7, `OBSERVED`;
  the pads are bare, T-097).
- **LoRa TX × Magnetometer** has a gate of its own that nothing else on this
  list shares, and the previous wording dropped it while claiming completeness.
  The retrofit goes into the **Waveshare** unit, which has no LoRa radio at all
  ([VERIFIED_FACTS](../research/VERIFIED_FACTS.md); `CLAUDE.md` states it in one
  line — *"the Waveshare board has no LoRa and no GNSS at all"*). **Not
  [ADR-0003](../adr/0003-radio-not-lora.md)**, which an earlier version cited and
  which does not mention the Waveshare anywhere: it is entirely about which of
  the T-Watch's five radios is fitted. Neither placement nor a motor
  will ever make this row measurable *here*; it needs a magnetometer in a
  **T-Watch**, which is a different unit and is not ordered. The older text
  ("neither board has a magnetometer") covered this by accident; the more
  specific text lost it.

All four are **`BLOCKED`**, not `UNKNOWN` — the word matters, because `CLAUDE.md`
makes this vocabulary load-bearing and the motor's absence is `OBSERVED`, which
is the opposite of unknown. Placing the sensor alone does not make all of them
measurable — see [MAGNETOMETER_BACKLOG](MAGNETOMETER_BACKLOG.md). They are
marked distinctly so that "not yet measured" and "cannot be measured here" never
look like the same state, which is this file's whole job and which the closed
list of two quietly broke.

Note that this includes the pair the master plan uses to motivate the whole
coexistence architecture. The architecture is still justified — by the shared
I2C bus, a 3.3 V rail shared between a sensor and a disturbing subsystem, and
the amplifier's missing shutdown pin, all of which are real on these boards (the
shared rail is documented as ALDO3 on the T-Watch and is unresolved among
ALDO1/2/3 on Waveshare, which is why it is named by role here) — but not by this example. See
[COEXISTENCE_BACKLOG](COEXISTENCE_BACKLOG.md).

These five rows continue the table above. They followed two paragraphs of prose
with no header of their own, so every renderer showed them as literal
pipe-delimited text rather than as a table — pre-existing, and the insertion
above moved them further from their header. The header is repeated rather than
the prose moved, because the prose is about the rows before it.

| Subsystem A | Subsystem B | Suspected effect | Evidence | Severity | Method | Mitigation | Board | Firmware |
|---|---|---|---|---|---|---|---|---|
| Wi-Fi | GNSS | in-band harmonics | THEORETICAL RISK | — | — | — | — | — |
| BLE | GNSS | duty-cycled RF | THEORETICAL RISK | — | — | — | — | — |
| SD transfer | GNSS | bus EMI | THEORETICAL RISK | — | — | — | — | — |
| Flash write | Timing-critical ops | cache stall / CPU block | THEORETICAL RISK | — | — | — | — | — |
| CPU burst | Battery / RF | supply and thermal | THEORETICAL RISK | — | — | — | — | — |

## Measurement method

Every row is produced by the same three-way comparison, not by a single
observation:

```
baseline            (A off, B off)
A only              (A on,  B off)
A + B               (A on,  B on)
```

Run each long enough for the metric to be stable, and repeat — a single run
measures the weather, not the board.

### Metrics to collect

| Subsystem | Metrics |
|---|---|
| GNSS | TTFF, fix type, satellite count, C/N0, HDOP/PDOP, fix stability, lost fixes |
| LoRa | RSSI, SNR, packet loss, RX/TX errors |
| Magnetometer | raw XYZ, variance, mean offset, drift, saturation |
| Power | subsystem active time, battery voltage, PMU measurements where available |

Record conditions alongside the numbers: indoors or outdoors, sky view,
temperature, battery state, whether charging. A GNSS measurement without its
conditions is not a measurement.

## Settling intervals

Where a mitigation needs a delay — for example, waiting after a vibration
before reading the compass — that delay is a **measured parameter with a
recorded derivation**, not a number someone liked. Record how it was obtained
so it can be re-derived when the hardware changes.

---

## Results

*Empty.* No hardware has been measured. No board is present.

## The pairs this matrix cannot reach

Every row above assumes both subsystems sit inside a device this project's
firmware controls. Two situations break that assumption and need saying, because
a matrix that silently omits them reads as coverage:

**Interference inside the node.** On a node-provided setup, the LoRa transmitter
and the GNSS receiver are both inside a third box whose firmware this project may
not control ([NODE_PROFILE](../node/NODE_PROFILE.md) N8). The classic
LoRa-TX-desenses-GNSS pair is then neither measurable nor mitigable *from the
watch*. It does not stop being real; it stops being ours. What the watch can do
is notice the symptom — a fix that degrades whenever the node transmits — and
report it, which is a diagnostics requirement rather than a mitigation.

**The link as an interferer.** Two rows have been added for the watch↔node link
itself. They are marked THEORETICAL RISK rather than `BLOCKED` because they *are*
measurable once both devices exist, and nothing else stands in the way.

The magnetometer rows are `BLOCKED`, and **not all for the same reason** — an
earlier version of this paragraph said they *"cannot be measured on any targeted
hardware at all"*, which was true before A5 was answered and is not true now.
Two need a placed sensor, a rail, the module pull-ups and T-130; one needs a
motor that is not fitted;
one needs a T-Watch that is not owned. Keeping that distinction visible is the
whole point of this file: "not yet measured", "blocked on a thing we can do" and
"cannot be measured here at all" must never look alike, and collapsing the last
two into one word is how the previous version of this file got it wrong.

That distinction now lives **in the rows**, which is where a reader meets it.
Review pointed out that it was true of this paragraph and false of the table
above: four identical `BLOCKED` cells with every other column `—`, a hundred and
sixty-five lines from the sentence explaining that they are not the same state.
Each row now names its own blocker and its own board — including the LoRa row,
which is the *"cannot be measured here at all"* case and had been indistinguishable
from the two that a placement and a rail decision will open.
