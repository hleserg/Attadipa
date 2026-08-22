# Hardware-in-the-loop plans

Every claim in this repository that a host cannot settle, written as a test
somebody can run.

**None of these has been executed.** There is no board on a bench, there is no
HIL runner in CI, and every plan below is marked `NOT EXECUTED — HARDWARE
REQUIRED` until a person writes a result into it with a date and an instrument.
That marker is the point of the document: a claim with a plan attached and no
result is visibly unproven, where the same claim in a comment reads as settled.

**Which nodes the plans below mean** by "a second Attadipa node" is in
[`TEST_FLEET`](../research/TEST_FLEET.md), along with the measured fact that a
cloud agent session can reach none of them — no serial device, no USB
enumeration, no flashing tool. That is why the marker above is not a formality.

## How to record a result

Append to the plan, never edit the plan to match what happened:

```
RESULT 2026-09-14 — board: T-Watch S3 rev 2.1, serial 0x…
  instrument: Nordic PPK2, 100 kHz, 10 s window
  measured:   sleep current 214 µA
  verdict:    FAIL (expected ≤ 150 µA)
  raw:        docs/testing/data/2026-09-14-ttw-sleep.csv
```

A number without an instrument named beside it is `ESTIMATED`, whatever it looks
like. A number from a datasheet is `VENDOR-STATED` and is not a measurement of
*this* board.

---

## H-1 · Which parts are actually on this board

**Claim under test.** The hardware matrix says the T-Watch ships with one of
five radio chips and one of two GNSS modules, and that the product name does not
tell you which ([ADR-0003](../adr/0003-radio-not-lora.md)). Everything else in
this document depends on knowing which board is in front of you, so this runs
first and nothing else runs before it.

**Equipment.** The board, a USB cable, `esptool.py`, a magnifier or a phone
camera with macro.

**Procedure.**
1. `esptool.py --port <p> chip_id` — record the chip revision and MAC.
2. Read the markings on the radio module and the GNSS module physically. Do not
   infer them from the product page.
3. Boot the bring-up firmware and read the SPI ID register of the transceiver
   and the UBX-MON-VER or equivalent version string of the receiver.
4. Photograph both, and file the photographs beside the result.

**Expected.** One of the documented combinations in
[`docs/research/HARDWARE_MATRIX.md`](../research/HARDWARE_MATRIX.md).

**Pass/fail.** PASS when the silicon ID read over the bus agrees with the
physical marking *and* with the matrix. Any disagreement is a FAIL and blocks
every other plan here, because a plan run against an unknown part measures
nothing.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-2 · Sleep current, per power state

**Claim under test.** `PowerMetrics::sleep_current_ua` is `Unknown` in every
build (`core/include/attadipa/core/power_state.h`), and
`docs/upstream/research-integration.md` §4 records
`CONFIG_ESP_SLEEP_PSRAM_LEAKAGE_WORKAROUND` as costing about 10 µA — labelled
`VENDOR-STATED`, which is not a measurement of this board.

**Equipment.** Nordic PPK2 or a Joulescope, in series with the battery. A bench
supply is not a substitute: it hides the PMU's own quiescent draw.

**Procedure.** For each of `Idle`, `LightSleep`, `MeshListenSleep`, `DeepSleep`
and `PowerOff`:
1. enter the state with a known wake plan (`legal_wake_sources()` says which
   sources are permitted; arm exactly those);
2. record 60 s at ≥ 10 kHz;
3. report the mean, the 95th percentile and the peak.

Then repeat `DeepSleep` twice more: once with PSRAM leakage workaround off, once
with the display rail explicitly cut, to attribute the difference.

**Expected.** No expectation is stated, because stating one would be inventing a
number. The purpose is to establish the first real values, after which a
regression threshold can be set at the measured figure plus a margin.

**Pass/fail.** PASS when five states have five numbers and the ordering is
monotonic — `PowerOff ≤ DeepSleep ≤ MeshListenSleep ≤ LightSleep ≤ Idle`. A
non-monotonic result is a FAIL and means a rail is not being cut where the model
says it is.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-3 · Deep sleep really is deep, and the radio really is off

**Claim under test.** The MeshCore review
([`docs/upstream/meshcore-1.17-review.md`](../upstream/meshcore-1.17-review.md))
records upstream's V4-R8 `powerOff()` calling `enterDeepSleep(0)` with the front
end left in receive and EXT1 armed on `P_LORA_DIO_1`.
`legal_wake_sources(PowerState::DeepSleep)` in this repository excludes
`RadioIrq` and `NodeLink`, and `tests/test_power.cpp` asserts it — on a host.
This is the plan that makes the assertion mean something.

**Equipment.** PPK2 in series; a second Attadipa node or any transmitter on the
same frequency; a spectrum analyser or an RTL-SDR is helpful but not required.

**Procedure.**
1. Enter `DeepSleep` with the timer and button armed and nothing else.
2. Measure the current for 60 s. Record it.
3. Transmit a packet the device would normally receive, from a metre away.
4. Confirm the device does **not** wake: current trace shows no excursion, and
   the uptime after the next timer wake is consistent with an uninterrupted
   sleep.
5. Repeat for `MeshListenSleep`, where the device **must** wake.

**Expected.** Deep sleep current unchanged by the transmission. Mesh-listen
sleep shows a wake within the radio's own latency.

**Pass/fail.** FAIL if a packet wakes the device from `DeepSleep`, or if deep
sleep current is within 20 % of mesh-listen sleep — the latter would mean the
front end is still powered and the two states differ only in name, which is the
upstream defect.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-4 · The front-end regression, measured as noise floor

**Claim under test.** The review confirms upstream commit `e2aa7b98` set
`radio_fem_rxgain = 1`, that #3203 compiled the preference out with `#if 0`, and
that issues #3010 and #3232 report the noise floor moving from −115 dB to −95 dB
and from −108 dB to −86 dB. Those are *other people's* boards.

**Equipment.** The board; a signal generator or a second node with a calibrated
output; an anechoic-ish environment, or at minimum the same physical setup for
every run.

**Procedure.**
1. With no transmitter active, read the transceiver's RSSI in receive over
   1000 samples. Record the mean as the noise floor.
2. Repeat with the FEM LNA explicitly enabled and explicitly disabled.
3. Measure packet error rate against a transmitter at a fixed attenuation, at
   both settings.

**Expected.** A difference of the order the issues describe, in the direction
they describe. If there is no difference, the FEM on this board is not the part
the issues are about, and that is itself the finding.

**Pass/fail.** This plan cannot fail; it establishes a fact. It PASSES when
`RadioStatus::noise_floor_dbm` and `front_end_lna_enabled` have real values in a
diagnostics snapshot taken during the run.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-5 · Time to first fix, cold, warm and hot

**Claim under test.** `start_kind()` in `core/src/gnss_power.cpp` distinguishes
three starts, and the whole duty-cycling argument rests on the difference being
large. The four-hour ephemeris window in `kHotStartWindow` is a textbook figure,
not a measurement of this receiver.

**Equipment.** The board, outdoors with an open sky, a stopwatch or a serial log
with timestamps, and the ability to cut the receiver's backup rail.

**Procedure.** Three runs each, from a cold device:
- **Cold**: backup rail cut for ≥ 10 minutes beforehand. Confirm
  `backup_retained == false`.
- **Warm**: backup rail powered, last fix ≥ 6 hours ago.
- **Hot**: backup rail powered, last fix ≤ 5 minutes ago.

Record the time from power-on to the first `PositionValidity::Valid`, and
separately to the first `FixType::ThreeD` of any quality.

**Expected.** Cold in minutes, hot in seconds, warm between. Any ordering other
than `hot < warm < cold` is a FAIL and means the state model is retaining
something it thinks it is retaining and is not.

**Pass/fail.** PASS when nine runs produce that ordering with no overlap between
the hot and cold groups.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-6 · Does this receiver detect jamming at all?

**Claim under test.** [OWNER_DECISIONS](../research/OWNER_DECISIONS.md) OD-5:
anti-spoofing on the LS550G is `UNKNOWN`, not `SUPPORTED`. `ReceiverIndication`
has a value for exactly this and `tests/test_trust.cpp` asserts that `Unknown`
never reads as an all-clear. What no host test can establish is which
indications the part in front of you actually emits.

**Equipment.** The board; a shielded enclosure; a GNSS repeater or simulator if
available. **A transmitter radiating on a GNSS band in the open air is illegal
in most jurisdictions — do this in a shielded enclosure or not at all.**

**Procedure.**
1. Log every field the receiver publishes for 10 minutes with a clean sky.
   Record which of `jamming`, `spoofing`, `protection_level` and per-signal C/N₀
   ever carry a value other than absent.
2. Inside the enclosure, raise a broadband noise floor in the L1 band in steps
   and record at which step the receiver's jamming indication changes, if it
   ever does.
3. Do **not** attempt a spoofing test without a simulator and a legal setup.
   Record spoofing detection as `UNKNOWN` rather than guessing.

**Expected.** For a u-blox M10, a jamming indicator that responds. For the
LS550G, unknown — that is the point of the test.

**Pass/fail.** PASS when
[`docs/research/VERIFIED_FACTS.md`](../research/VERIFIED_FACTS.md) gains a row
per receiver saying which indications are produced, each traced to an observed
log rather than to a datasheet. An indication the part does not produce must be
recorded as `Unsupported`, never left as `Unknown`, and never as `None`.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-7 · Energy per fix, and whether duty cycling pays

**Claim under test.** `PowerMetrics::energy_per_gnss_fix_uj` is `Unknown`. The
GNSS power model (`core/src/gnss_power.cpp`) assumes that duty cycling saves
energy, which is only true if the cost of re-acquiring is smaller than the cost
of tracking continuously.

**Equipment.** PPK2 with energy integration; H-5's results.

**Procedure.** Integrate current over:
1. a cold acquisition to first valid fix;
2. a hot re-acquisition to first valid fix;
3. sixty seconds of continuous tracking;
4. sixty seconds in the receiver's own power-save mode, where it has one;
5. sixty seconds in backup.

**Expected.** No expectation. The output is the break-even interval: the sleep
duration beyond which turning the receiver off is cheaper than leaving it on.

**Pass/fail.** PASS when a break-even figure exists with an instrument named
beside it. Until then, `next_state()`'s duty-cycling decision is a reasonable
assumption and is labelled as one.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-8 · USB CDC survives being unplugged mid-frame

**Claim under test.** `tests/test_link.cpp` covers fragmented input, a
disconnect mid-frame and a reconnect — against the host implementation of
`Decoder` and `LinkState`. What it cannot cover is what the ESP32-S3's USB stack
actually delivers when a cable is pulled, which is the case the review found
upstream handling by assuming the connection was fine
(`isConnected()` returning `true` unconditionally).

**Equipment.** A host with a serial terminal, a cable, and patience. A powered
USB hub with a switchable port makes this repeatable.

**Procedure.** Twenty times each:
1. pull the cable mid-frame while a large payload is streaming;
2. reconnect and confirm the first frame after reconnection decodes intact and
   the epoch advanced;
3. pull the cable while the device is writing and confirm the write fails rather
   than blocking a real-time task;
4. suspend the host and resume it;
5. reboot the device with the cable attached and confirm the session re-forms.

Read `TransportStatus` after each: `resyncs`, `frames_malformed` and
`frames_dropped` must account for everything lost.

**Expected.** No frame is ever delivered corrupt. Frames may be lost; the
counters must say how many.

**Pass/fail.** FAIL if any frame arrives with a valid CRC and wrong content, if
a counter under-reports, or if a disconnect blocks a task for longer than the
watchdog period.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-9 · Bonded reconnect after a reboot

**Claim under test.** §5 of the brief requires a BLE state machine that survives
connect, disconnect, failure, bonded reconnection, the peer disappearing, a
reboot, a stack restart and an unexpected callback. `LinkState` models all of
those and `tests/test_link.cpp` exercises them as events. Whether NimBLE
actually delivers those events in that order is not a host question.

**Equipment.** The board; a phone; `nRF Connect` or equivalent.

**Procedure.**
1. Bond. Reboot the device. Confirm the phone reconnects without re-pairing and
   that `sessions` increments while `epoch` advances.
2. Walk out of range and back. Confirm a `LivenessTimeout` disconnect, then a
   new session.
3. Kill the BLE stack and re-initialise it while connected. Confirm
   `SubsystemRestart` and that nothing above the link keeps a stale handle.
4. Turn Bluetooth off on the phone mid-transfer.

**Expected.** Every case ends in a phase the model has a name for, and
`ignored_events` is zero or explained.

**Pass/fail.** FAIL if any sequence leaves the link in `Ready` with no peer, or
in a phase from which no event can escape.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## H-10 · The battery reading during a transmission

**Claim under test.** `BatteryStatus::sampled_during_tx` exists because a
reading taken while the radio is transmitting is a different reading. The size
of the difference is unknown.

**Equipment.** PPK2 or a scope on the battery rail; the board transmitting at
maximum power.

**Procedure.** Sample the battery ADC 100 times during a transmission and 100
times between transmissions, at three states of charge.

**Expected.** A measurable sag. Its size sets whether the flag is enough or
whether readings during transmission must be discarded outright.

**Pass/fail.** PASS when a figure exists. If the sag exceeds the width of one
percentage point of the charge curve, this plan produces a task to suppress
sampling during transmission rather than flagging it.

**Status.** `NOT EXECUTED — HARDWARE REQUIRED`

---

## What CI says about all of this

Every CI run prints an evidence table ending with:

> | Anything on a physical board | **NOT EXECUTED — HARDWARE REQUIRED** |

That line is generated by `.github/workflows/ci.yml` and is not conditional. A
green badge on this repository means host builds, host tests and a simulator
render. It is not evidence about firmware and it is not evidence about hardware,
and the only reliable way to stop that being forgotten is to write it down on
every single run.
