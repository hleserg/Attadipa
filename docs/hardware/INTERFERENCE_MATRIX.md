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
| Haptic motor | Magnetometer | magnetic field distorts heading | THEORETICAL RISK | — | — | — | — | — |
| Haptic motor | IMU | vibration corrupts accelerometer | THEORETICAL RISK | — | — | — | — | — |
| LoRa TX | GNSS acquisition | RF desensitisation | THEORETICAL RISK | — | — | — | — | — |
| LoRa TX | Magnetometer | supply current transient | THEORETICAL RISK | — | — | — | — | — |
| Display DMA | GNSS | broadband EMI | THEORETICAL RISK | — | — | — | — | — |
| High brightness | Battery / GNSS | supply droop | THEORETICAL RISK | — | — | — | — | — |
| Audio amplifier | Magnetometer | speaker magnet and coil current | THEORETICAL RISK | — | — | — | — | — |
| Battery charging | GNSS | switching noise | THEORETICAL RISK | — | — | — | — | — |
| Battery charging | Magnetometer | charge current field | THEORETICAL RISK | — | — | — | — | — |
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
