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
| Haptic motor | Magnetometer | magnetic field distorts heading | **NOT MEASURABLE** | — | — | — | — | — |
| Haptic motor | IMU | vibration corrupts accelerometer | THEORETICAL RISK | — | — | — | — | — |
| LoRa TX | GNSS acquisition | RF desensitisation | THEORETICAL RISK | — | — | — | — | — |
| Watch↔node link TX | GNSS acquisition | RF desensitisation | THEORETICAL RISK | both | — | — | — | — |
| Watch↔node link TX | LoRa RX (T-Watch) | RF desensitisation | THEORETICAL RISK | T-Watch | — | — | — | — |
| LoRa TX | Magnetometer | supply current transient | **NOT MEASURABLE** | — | — | — | — | — |
| Display DMA | GNSS | broadband EMI | THEORETICAL RISK | — | — | — | — | — |
| High brightness | Battery / GNSS | supply droop | THEORETICAL RISK | — | — | — | — | — |
| Audio amplifier | Magnetometer | speaker magnet and coil current | **NOT MEASURABLE** | — | — | — | — | — |
| Battery charging | GNSS | switching noise | THEORETICAL RISK | — | — | — | — | — |
| Battery charging | Magnetometer | charge current field | **NOT MEASURABLE** | — | — | — | — | — |

**On the four `NOT MEASURABLE` rows.** Every magnetometer pair in this matrix is
untestable on the boards this project targets, because **neither board has a
magnetometer** — the T-Watch carries only a BMA423 accelerometer and the
Waveshare board only a QMI8658 six-axis IMU
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)). They are kept in the table
rather than deleted, because the day an external sensor is fitted they become
the first four tests to run. **A5 is no longer what gates them**: it is answered
(OD-17, 2026-08-22 — an external magnetometer is intended, and two candidate
parts are ordered for one Waveshare unit). What gates them now is two physical
facts, and both are `UNKNOWN` rather than merely pending: the part is not placed
(T-109), and the unit it is going into
[has no vibration motor fitted](../research/WAVESHARE_BOARD_RECEIVED.md) (§1.7,
`OBSERVED`; the pads are bare, T-097). Placing the sensor alone does not make
these measurable — see
[MAGNETOMETER_BACKLOG](MAGNETOMETER_BACKLOG.md). They are marked
distinctly so that "not yet measured" and "cannot be measured here" never look
like the same state.

Note that this includes the pair the master plan uses to motivate the whole
coexistence architecture. The architecture is still justified — by the shared
I2C bus, the shared ALDO3 rail and the amplifier's missing shutdown pin, all of
which are real on these boards — but not by this example. See
[COEXISTENCE_BACKLOG](COEXISTENCE_BACKLOG.md).
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
itself. They are marked THEORETICAL RISK rather than NOT MEASURABLE because they
*are* measurable once both devices exist — unlike the magnetometer rows, which
cannot be measured on any targeted hardware at all. Keeping that distinction
visible is the whole point of this file: "not yet measured" and "cannot be
measured here" must never look alike.
