# Integrating the upstream research — audit, verdicts, and what actually changed

Owner instruction, 2026-08-21: take the findings of the ESP32-S3 / MeshCore /
GNSS research and apply *only* what is genuinely needed and genuinely ready.
The instruction is explicit about the epistemics and this document follows it:

> Не делай изменения только ради соответствия research-отчёту. Его findings —
> входные гипотезы. Источник истины — актуальный Attadipa, фактический upstream
> и проверенное железо.

So every finding below is a hypothesis until it is checked against three things:
**what this repository actually contains today**, **what the actual upstream
actually says at a named revision**, and **what the hardware research has
verified**. A finding that survives all three becomes work. One that does not
becomes a recorded rejection, which is worth as much.

Audited at `53f8cea`, 2026-08-21.

---

## 1. The audit: what Attadipa actually is today

Before deciding anything, the state of the tree — because half the research
report's recommendations name subsystems that do not exist here, and acting on
a path like `src/hal/esp32` or `src/navigation` would be inventing an
architecture rather than integrating a finding. **Attadipa has none of those
directories, and it is not an oversight.**

```
core/       Capability, Availability, the capability registry
platform/   HardwareFeature, HardwareState, RadioInfo, two board profiles
apps/       AppManifest and the launcher gating rule
l10n/       StringId, catalogues, tr()
sim/        the desktop simulator and its composition root
tests/      10 host tests
tools/      the font and l10n pipelines
```

What is **absent**, and therefore what "implement this finding" would have to
create from nothing:

| Not present | Consequence for this task |
|---|---|
| any ESP-IDF build target | T-004 is open. There is no `idf.py build`, no `sdkconfig`, no component. Nothing that calls an ESP-IDF API can be compiled, let alone run |
| any HAL, driver or service | `PowerService`, `LocationService`, `RadioService` exist in [ARCHITECTURE](../architecture/ARCHITECTURE.md) §4 and §6 as ownership decisions, not as code |
| any BLE, USB or UART code | the transport is a decision ([ADR-0005](../adr/0005-node-protocol.md)), not an implementation |
| any GNSS code | no driver, no parser, no observation type |
| any crypto or RNG | none, anywhere |
| `docs/testing/` | the directory exists and is empty |

This shapes every verdict below. The owner's own §3 anticipated it: *"Если
реализация power manager ещё не находится в текущем этапе проекта, зафиксируй
конкретные решения в архитектуре/backlog вместо преждевременного кода."* That
sentence generalises to most of the report, and this document applies it
uniformly rather than only where it was written.

**What that leaves genuinely buildable today** is everything that is
device-independent, host-testable and does not depend on a decision that is
still open: value types, state machines, framing, and the test rig that drives
them. That is what got written, and §3 of this document says exactly which.

---

## 2. Verdicts

`IMPLEMENT` — code landed in this pass · `DOCUMENT` — the decision is recorded,
the code waits for a subsystem that does not exist · `MONITOR` — upstream may
change the answer · `REJECT` — checked, and it does not apply here.

| # | Finding | Applies? | Verdict | Where |
|---|---|---|---|---|
| §3 | tickless idle, automatic light sleep, PM locks | yes, later | `DOCUMENT` | [§4](#4-esp32-s3-power) · T-045 |
| §3 | power state model ACTIVE/IDLE/LIGHT_SLEEP/DEEP_SLEEP | yes | **`IMPLEMENT`** | `core/power_state.h` |
| §3 | apps must not touch rails; power manager must not know apps | already true | `DOCUMENT` | ARCHITECTURE §6, enforced by ADR-0007 |
| §3 | future measurable metrics | yes | **`IMPLEMENT`** (the slots) + HIL plan | `core/power_state.h`, [HIL-1](../testing/HIL_PLANS.md) |
| §4 | crypto/RNG provider seam | yes, later | `DOCUMENT` | [§5](#5-crypto-and-rng) · T-048 |
| §4 | do not write our own crypto | yes | **`REJECT`** writing any | [§5](#5-crypto-and-rng) |
| §5 | BLE connect/disconnect/reconnect state machine | yes | **`IMPLEMENT`** | `link/link_state.h` |
| §5 | do not port workarounds for bugs already fixed upstream | yes | `DOCUMENT` | [§6](#6-ble) |
| §6 | USB: enumerated ≠ session ready; backpressure; bounded queues | yes | **`IMPLEMENT`** | `link/` |
| §6 | seven named regression tests | yes | **`IMPLEMENT`** | `tests/test_link.cpp` |
| §7 | which GNSS chip is on which board | already established | `DOCUMENT` | [§8](#8-gnss) |
| §7 | provider hides bus, vendor protocol and local-vs-node | already decided | `DOCUMENT` | ADR-0004, ADR-0011 |
| §7 | prefer vendor binary protocol over full NMEA | yes, blocked | `DOCUMENT` | T-051, T-052 |
| §8 | GNSS power state machine | yes | **`IMPLEMENT`** (the model) | `core/gnss_power.h` |
| §8 | AGPS must never be a dependency | yes | **`IMPLEMENT`** as a rule | `core/gnss_power.h`, ADR-0011 |
| §9 | position quality carried with the coordinate | yes | **`IMPLEMENT`** | `core/position.h` |
| §9 | VALID / DEGRADED / STALE / NO_FIX | yes | **`IMPLEMENT`** | `core/position.h` |
| §10 | Madgwick/Mahony fusion for heading | **no** | **`REJECT`** | [§9](#9-heading-and-sensor-fusion) |
| §10 | GNSS course only above a speed and quality gate | already decided | `DOCUMENT` | ADR-0009 §4 |
| §10 | a heading provider can be added without touching apps | already true | `DOCUMENT` | ADR-0004, ADR-0009 §3 |
| §11 | hard/soft-iron calibration, declination, tilt | not yet applicable | `DOCUMENT` | [MAGNETOMETER_BACKLOG](../hardware/MAGNETOMETER_BACKLOG.md) |
| §12 | breadcrumb trail with bounded storage | yes, design only | `DOCUMENT` | [§11](#11-breadcrumb-and-navigation) · T-056 |
| §12 | A* on a large offline map | **no** | **`REJECT`** for now | [§11](#11-breadcrumb-and-navigation) |
| §13 | replayable, deterministic navigation test rig | yes | **`IMPLEMENT`** | `tests/replay/` |
| §14 | structured diagnostics snapshot, no JSON in core | yes | **`IMPLEMENT`** | `core/diagnostics.h` |
| §15 | GNSS / LoRa / power field metrics | yes | `DOCUMENT` + HIL plans | [HIL_PLANS](../testing/HIL_PLANS.md) |
| §16 | reuse ledger discipline | already in force | `DOCUMENT` | [REUSE_LEDGER](../research/REUSE_LEDGER.md) |

---

## 3. What was implemented, and why each one was safe to write now

Four rules governed the choice, and they are the reason this list is shorter
than the finding list:

1. **No ESP-IDF API is called**, because there is no target that could compile
   it. Everything below builds host-native and runs in `ctest`.
2. **No decision that is still open is pre-empted.** In particular the node
   protocol's *encoding* is provisional pending T-016, so what landed is the
   framing beneath it — sync, length, CRC, resynchronisation, fragmentation
   bookkeeping — which is the same regardless of how the body is encoded.
3. **No hardware fact is assumed.** Where a number would have to be measured, a
   slot exists and is empty, and the HIL plan says how to fill it.
4. **Nothing is written to satisfy a bullet point.** Where the honest answer was
   "this cannot exist yet", it is a `DOCUMENT` row above and not a stub.

| Landed | Lines | What it does | Tests |
|---|---|---|---|
| `core/include/attadipa/core/position.h` | value types | a fix that carries its own quality, and four validity states | `test_position` |
| `core/include/attadipa/core/trust.h` | the trust state | evidence, reasons, hysteresis, last-trusted position | `test_trust` |
| `core/include/attadipa/core/gnss_power.h` | receiver states | OFF / BACKUP / ACQUIRING / TRACKING / POWER_SAVE / DEGRADED and their legal transitions | `test_power` |
| `core/include/attadipa/core/power_state.h` | device states | the six states, their wake sources, and the rule a board cannot break | `test_power` |
| `core/include/attadipa/core/diagnostics.h` | the snapshot | every field optional, no serializer, no JSON | `test_diagnostics` |
| `link/` | framing + link state | sync, CRC, resync, bounded queues, backpressure, the connection state machine | `test_link` |
| `tests/replay/` | the rig | deterministic replay of timestamped fixtures into the trust engine | twelve scenarios, plus `test_replay_rig` |

The two power state machines share one test file rather than two, because the
check that matters most spans both: `next_state()` and `transition_is_legal()`
are written separately, and a cross-check over every capability set, power state
and flag combination is the only thing keeping them in agreement. It caught them
disagreeing on the first run.

### What the tests found

Writing them was not a formality, and the results belong here rather than in a
commit message nobody re-reads:

| Where | What the test found |
|---|---|
| `core/src/gnss_power.cpp` | `next_state()` proposed `Off → Backup` when the device slept with the receiver already off — spending current to hold a domain with nothing in it, and a transition the legality table correctly refused |
| `core/src/gnss_power.cpp` | `start_kind()` read *having* a backup domain as evidence the domain had been *powered*, reporting a warm start where the truth was cold: a caller told to expect a fix in thirty seconds that arrives in several minutes |
| `core/src/trust.cpp` | the interval between epochs was read after the previous timestamp had been overwritten, so every `dt` was zero and both rate detectors silently did nothing. Found before this branch, and the reason `tests/test_trust.cpp` walks sequences rather than single calls |
| `tests/replay/replay.cpp` | `-Werror` caught a comma operator where two statements were meant |

Four of the author's own expectations were wrong and the code was right, which
is recorded in the commits rather than quietly corrected: three were coordinate
arithmetic an order of magnitude out — one unit of 1e-7 degrees of latitude is
11 mm — and the fourth was a design not internalised, that at boot validity is
`NoFix` and trust is `Degraded`, two separate answers about the same moment
rather than one restated.

---

## 4. ESP32-S3 power

**Verified against the ESP-IDF this project actually has**, which is
`release/v5.5` at `c197d718bcc` (`v5.5.5-496-gc197d718bcc`) in `/root/esp/esp-idf`
— not against the version the research report assumed. Every name below was
grepped out of that tree; the ones that are *absent* matter more than the ones
that are present.

| Claim | Verified | Evidence |
|---|---|---|
| `esp_pm_configure()` / `esp_pm_config_t` with `light_sleep_enable` | **yes** | `components/esp_pm/include/esp_pm.h:22-25,70` |
| PM locks: `ESP_PM_CPU_FREQ_MAX`, `ESP_PM_APB_FREQ_MAX`, `ESP_PM_NO_LIGHT_SLEEP` | **yes** | same header, :47-58 |
| tickless idle is `CONFIG_FREERTOS_USE_TICKLESS_IDLE` | **yes** | `components/freertos/Kconfig:319` |
| light sleep and deep sleep on the S3 | **yes** | `SOC_LIGHT_SLEEP_SUPPORTED`, `SOC_DEEP_SLEEP_SUPPORTED`, `SOC_PM_SUPPORTED` |
| EXT0 and EXT1 wake on the S3 | **yes** | `SOC_PM_SUPPORT_EXT0_WAKEUP`, `SOC_PM_SUPPORT_EXT1_WAKEUP` |
| CPU retention across light sleep | **yes** | `SOC_PM_SUPPORT_CPU_PD`, `SOC_PM_CPU_RETENTION_BY_RTCCNTL` |
| power domains: RTC_PERIPH, RTC_SLOW/FAST_MEM, VDDSDIO, MODEM, XTAL | **yes** | `esp_sleep.h:60-95`, `SOC_PM_SUPPORT_*_PD` |
| **top-domain power-down** | **NO — not on the ESP32-S3** | `SOC_PM_SUPPORT_TOP_PD` is not defined for `esp32s3`. It is a C6/H2-class feature. Any "retention" story copied from a C6 example does not apply |
| **`esp_sleep_enable_usb_wakeup()` applies to us** | **NO** | its own doc comment says *"Enable wakeup by High-Speed USB-OTG"*. The S3's OTG is full-speed. Do not wire USB into a wake plan on this evidence |
| **a USB-Serial-JTAG light-sleep wake API** | **NO — none exists in v5.5** | grepped `esp_hw_support` and `esp_driver_usb_serial_jtag`: nothing. This matters because §6's transport is exactly that peripheral |

One quantified consequence, and it is the first honest number this project has
about sleep current: `CONFIG_ESP_SLEEP_PSRAM_LEAKAGE_WORKAROUND` defaults to `y`,
must not be deselected on a module rather than a bare chip, and *"will increase
the sleep current about 10 µA"*
(`components/esp_hw_support/Kconfig:114-128`). Both Attadipa boards are
ESP32-S3**R8** modules with PSRAM, so **~10 µA of the light-sleep floor is not
ours to optimise away.** Labelled `VENDOR-STATED`, not `MEASURED` — Espressif's
figure, not one taken here.

### The state model

Implemented as `core/power_state.h`. It is the owner's four states plus the two
the MeshCore review proved are distinct, and the distinction is the whole point:

| State | CPU | Radio | Legal wake sources |
|---|---|---|---|
| `Active` | running | as configured | — |
| `Idle` | running, screen off | as configured | — |
| `LightSleep` | retained | as configured | timer, button, radio IRQ, touch, UART |
| `MeshListenSleep` | retained or off | **held in RX across the sleep** | **radio IRQ**, timer, button |
| `DeepSleep` | off, RTC memory retained | **off** | timer, button, EXT0/EXT1 |
| `PowerOff` | off | off | external only — PMU or reset |

`MeshListenSleep` exists because upstream's `HeltecV4R8Board::powerOff()` was
`enterDeepSleep(0)` with the FEM held in RX and EXT1 armed on the radio's DIO1
([meshcore-1.17-review §5](meshcore-1.17-review.md), issue #3165). Two behaviours
that differ only in their wake sources shared one function name, and "off" ended
at the next received packet. Attadipa makes that a compile-and-test-time error:
`legal_wake_sources()` returns nothing radio-shaped for `DeepSleep` or
`PowerOff`, and `wake_plan_is_legal()` rejects the combination.

The metrics §3 asks for exist as declared, currently-empty slots with an
explicit `MEASURED / ESTIMATED / UNKNOWN` label on each — average current, sleep
current, wake latency, wake reason, energy per GNSS fix, energy per LoRa
TX/RX cycle. **Every one of them is `UNKNOWN` today** and the type will not let a
caller pretend otherwise. Filling them is [HIL-1 and HIL-2](../testing/HIL_PLANS.md).

Not implemented, and deliberately: any call into `esp_pm`, any sleep entry, any
rail manipulation. There is no target to run it on, and ARCHITECTURE §6 already
assigns rail ownership to `PowerService` — writing that service before there is
a driver to power would be inventing its clients.

---

## 5. Crypto and RNG

**Attadipa has no crypto and no RNG today**, so there is nothing to fix; the
question is only what shape the seam should be when there is.

Three facts from the upstream read
([meshcore-1.17-review §8](meshcore-1.17-review.md)), which stand:

- MeshCore's ESP32 LoRa path draws entropy from `radio->randomByte() ^
  ::random(0,256)` — the Arduino PRNG, whose own header calls itself *"VERY
  SLOW"*.
- `esp_fill_random` appears **only** in the two ESP-NOW variants. There is no
  `mbedtls`, no `esp_aes`, no `esp_sha` anywhere in the tree.
- nRF52 got CC310 hardware crypto (#2824, merged, released); the ESP32
  equivalent (#2280) is **open**.

So for Attadipa this is a *gap to fill*, not a port. The decisions, recorded now
and implemented when there is a MeshCore adapter to need them (T-048):

1. **One interface, three named backends** — `software`, `ESP32-S3 hardware`,
   `nRF52 CC310`. No `#ifdef` for a backend above the seam. The simulator gets
   its own provider, and it is **deterministic and seeded**, because a test that
   cannot reproduce its own random numbers is not a test.
2. **Entropy comes from the platform's hardware RNG** — `esp_fill_random()` on
   ESP32 — never from a PRNG seeded at boot.
3. **We write no cryptography.** Not AES, not SHA, not a PRNG construction. The
   owner's instruction and ordinary sense agree. Whatever we use arrives as a
   licence-checked dependency with its own test vectors, recorded in the
   [REUSE_LEDGER](../research/REUSE_LEDGER.md).
4. **MeshCore's algorithms and wire format are not ours to change.** Any
   divergence would be a compatibility break dressed up as an improvement.
5. **Known-answer vectors are a build gate** for whatever primitives we end up
   using, per backend, so "the hardware path is enabled" can never be assumed
   from a Kconfig symbol.
6. **No claim that hardware acceleration is faster** may be written until it is
   `MEASURED` against the software path at Attadipa's real payload sizes.
   MeshCore's frames are ≤ 176 bytes, and a hardware AES block can lose to
   software at that length once driver setup is counted.

---

## 6. BLE

The research finding is a class of defects around reconnect, bonding and state
recovery. Checked against upstream at `d929643`:

- **#3005** (ESP32 BLE bonded reconnects) and **#3007** (BLE receive queue
  synchronization) are **merged and released** in v1.17.0. Per the owner's §5 —
  *"Не добавляй workaround для старого upstream bug, если текущая используемая
  версия уже содержит исправление"* — Attadipa does **not** carry a workaround for
  either, and this line exists so nobody adds one later.
- **#2333** (BLE ghost connection) is **open**, and its root-cause list is worth
  more than its patch: `onDisconnect()` not resetting state unconditionally;
  `onAuthenticationComplete()` setting connected while the interface was
  disabled; three state variables not reset in `begin()`. All three are the same
  mistake — **connection state that no single place owns and that no single
  point resets**.

That is a state-machine problem, and a state machine is host-testable without
NimBLE, without ESP-IDF and without a radio. So it is the one BLE thing that got
written: `link/link_state.h` is transport-agnostic and models exactly the nine
events §5 lists, including the two that break naive implementations —
**a callback that arrives after the state has already moved on**, and
**a subsystem restart that must reset everything, not merely disconnect**.

What is **not** written: any NimBLE integration, any GATT profile, any bonding
store. Those need a target. Comparing NimBLE's own reconnect semantics against
this state machine is filed as part of T-043 and is `MONITOR` until there is a
build that can link it.

---

## 7. USB and the byte-stream transport

This is where the upstream read paid for itself, and the details are in
[meshcore-1.17-review §2](meshcore-1.17-review.md). Confirmed at source, not
inferred: `isConnected()` returns `true` unconditionally with the comment *"no
way of knowing, so assume yes"*; `isWriteBusy()` returns `false` unconditionally;
framing is a start byte plus a 16-bit length with **no checksum, no escaping and
no resync marker**; and an over-long frame is silently truncated to
`MAX_FRAME_SIZE` and delivered as if complete.

Attadipa's `link/` answers each of those directly:

| Requirement | How |
|---|---|
| enumerated ≠ session ready | `LinkState` separates `Attached` (the peripheral exists) from `Ready` (a peer has been heard from within the liveness window). There is no way to spell "assume yes" |
| survive disconnect/reconnect | an explicit `Reset` that clears every field, and a session epoch that invalidates in-flight work — the ADR-0005 §5 rule applied to the transport |
| never lose part of a frame | a frame is enqueued whole or refused whole. There is no partial write path |
| backpressure | `writable()` reflects the queue, and a full queue refuses rather than blocks |
| fragment reassembly | length-prefixed, bounded, and an over-long or inconsistent fragment is an **error with a reason**, never a truncation |
| bounded queues | fixed capacity chosen from the protocol, not copied from the report — see below |
| not blocking realtime work | no allocation, no blocking call, no syscall anywhere in `link/` |

**Queue sizing, derived rather than copied.** ADR-0005 §4's envelope is 12 bytes
and MeshCore's companion frames cap at 176 bytes, so a frame slot is 192 bytes.
[RESOURCE_BUDGET](../architecture/RESOURCE_BUDGET.md) §4 requires *"fixed-size
pools sized to the maximum payload"* and *"a declared maximum and a defined
behaviour on reaching it"*. Four slots per direction is 1536 bytes per
interface, which is the same depth MeshCore's BLE interface chose and survives
in the field. The number is a **starting point with a stated derivation**, it is
a compile-time constant in one place, and the drop counter exists precisely so
the right value can be *measured* rather than argued about.

---

## 8. GNSS

**Which chip is on which board — settled, and not by assuming u-blox.** From
[HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md) and
[VERIFIED_FACTS](../research/VERIFIED_FACTS.md), both traced to schematics:

| Board | GNSS |
|---|---|
| LilyGO T-Watch S3 Plus | **u-blox MIA-M10Q *or* Quectel LS550G** — a purchase-time variant on a 13-pin FPC daughterboard. UART TX 42 / RX 41. **PPS is not connected.** Rails: BLDO1, plus DC4 at 850 mV for the LS550G only |
| Waveshare ESP32-S3-Touch-AMOLED-2.06 | **none.** Position arrives from an Attadipa node or not at all |

So "assume u-blox because u-blox is well documented" is precisely the error this
project's first rule exists to prevent, and it would have cost a GNSS that never
starts: the LS550G variant needs a second rail the MIA-M10Q does not.

**The vendor binary protocol question** — whether to drive the receiver in UBX
or Quectel's binary rather than parsing the full NMEA stream — is real and is
**blocked on T-051 and T-052**, because the answer depends on what each part
actually supports and this project has read neither interface specification yet.
What is *not* blocked, and is already decided in the
[REUSE_LEDGER](../research/REUSE_LEDGER.md): NMEA parsing itself is `WRAP` around
**minmea** at `2dd2cd1`, MIT, chosen because `struct minmea_float{value,scale}`
makes *"field absent"* a representable state — the property that stopped
TinyGPS++ from reporting an empty course as due north.

**The quality model** (§9) is implemented, because it needs no receiver:
`core/position.h` carries fix type, satellites used and in view, C/N0, HDOP and
PDOP, fix age, timestamp, the receiver's own accuracy estimate where it gives
one, and the source — and every one of them is an *optional* field with an
explicit "the receiver did not say" state, not a zero. Validity is the four
states the owner named:

```
Valid       usable
Degraded    usable with a caveat the UI must show
Stale       too old to act on; the position is a circle, not a point
NoFix       there is no position at all
```

Which is not the same axis as `Availability` (is a provider bound and
reachable), nor the same as trust (is it lying) — ADR-0011 §2's ten axes, of
which these are three.

**The receiver power state machine** (§8) is implemented as a model:
`Off → Backup → Acquiring → Tracking → PowerSave → Degraded`, with `Backup`
gated on a capability flag because it is only real if the hardware retains the
RTC and ephemeris — on the T-Watch that is the `MS412FE` cell on the
daughterboard, and **whether it actually backs the receiver's RAM is `UNKNOWN`
until T-051**. Transitions carry the inputs §8 lists: time since last fix,
whether ephemeris is retained, motion, an application's request for a fresh
position, and the device power state.

**AGPS is not a dependency.** `assistance_available()` is an input to *how fast*
a start is expected to be, never to *whether* a start is attempted. Attadipa must
work with no network, ever, and an assistance path that becomes load-bearing is
a bug even when it works.

---

## 9. Heading and sensor fusion

The research report recommends Madgwick and switching between GNSS course and
magnetic heading. Checked against the hardware matrix, as the owner instructed,
and the answer is **no** — on both boards, for a reason stronger than "not yet":

| Board | IMU | Magnetometer | Yaw observable? |
|---|---|---|---|
| T-Watch S3 Plus | **BMA423 — accelerometer only.** No gyroscope | **none** (schematic-verified) | **no** |
| Waveshare AMOLED 2.06 | QMI8658, 6-axis (accel + gyro) | **none** | **no** |

Madgwick and Mahony fuse gyro with accel and magnetometer. Without a
magnetometer, **yaw is unobservable**: accelerometer gives the gravity vector,
which fixes roll and pitch and says nothing about rotation about it, and a gyro
integrates to a heading that drifts without bound because nothing corrects it.
On the T-Watch there is not even a gyro to drift. So a fusion filter here would
not be an approximation of a compass — it would be a confidently drawn arrow
with no information in it, which is the exact failure
[ADR-0009](../adr/0009-heading.md) exists to forbid and the exact failure
TinyGPS++ shipped for years.

**Verdict: `REJECT`**, and not deferred — the finding is inapplicable to this
hardware rather than premature. `xioTechnologies/Fusion` and its relatives are
recorded in the [REUSE_LEDGER](../research/REUSE_LEDGER.md) as *evaluated and not
needed*, with the licence noted, so the same library is not re-evaluated next
quarter.

Everything the finding was reaching for is already decided and remains in force:
course over ground only above a speed and quality gate, standing still as a
designed state rather than an error, and a node's compass presented in the
node's frame or not at all (ADR-0009 §3, §4). And the architecture already
admits a magnetometer arriving later — via an Attadipa node or a future board —
without an application changing, because an application asks for
`Capability::Heading` and never learns the source (ADR-0004 §2). §11's
calibration work stays in [MAGNETOMETER_BACKLOG](../hardware/MAGNETOMETER_BACKLOG.md),
where it already was, and the warning there is the right one: six-orientation
tumbling is a hard-iron procedure and calling it soft-iron calibration without
checking the algorithm is how a compass ends up confidently wrong.

---

## 10. Diagnostics

`core/diagnostics.h`: one structured snapshot, every field optional and
explicitly "not known" rather than zero, and **no serializer**. Core does not
know what JSON is; a snapshot is rendered by whoever is displaying or shipping
it, which today is the simulator and tomorrow may be a companion app or a
support bundle. That is the same boundary [ADR-0010](../adr/0010-localization.md)
§4 draws for language, applied to encoding: the layer that produces a fact does
not decide how it will be written down.

It works in the simulator and in tests by construction, because it is a plain
aggregate with no I/O in it.

---

## 11. Breadcrumb and navigation

`DOCUMENT`, filed as T-056, not implemented — because the storage decisions it
depends on are open and writing a track logger before them would fix them by
accident.

What is decided now, so the task starts from constraints rather than from an
algorithm:

- **A track has a declared maximum and a defined behaviour at the bound**, per
  RESOURCE_BUDGET §4. Oldest-out, refuse, or degrade — chosen deliberately,
  never "runs until it stops".
- **Sampling is by minimum distance and minimum heading change**, not by a
  fixed interval, because a stationary wrist writes thousands of identical
  points and flash has a write endurance.
- **Simplification is offline.** Ramer–Douglas–Peucker is a fine algorithm for
  the export path and a poor one for a live buffer.
- **A\* on an arbitrary offline map is `REJECT` for now.** Not because it is
  wrong, but because the map format and the ESP32-S3 memory budget for it do not
  exist, and choosing a routing algorithm before a data format is choosing
  nothing at all.

---

## 12. What remains hardware-dependent

Nothing in this pass was validated on hardware, because this project has none of
the relevant hardware: no Heltec board, no fitted GNSS daughterboard of either
variant, no current-measurement setup. Every such item is written up as a
concrete HIL test with equipment, procedure, measured quantity and pass/fail
criteria in [docs/testing/HIL_PLANS.md](../testing/HIL_PLANS.md), rather than
left as a wish.

The honest summary: **everything in §3 of this document is `UNIT-TESTED` on a
host, and nothing in it is `HARDWARE-VERIFIED`.**

The ten plans, and the claim each one settles:

| Plan | Settles |
|---|---|
| H-1 | which radio and which GNSS module are actually on the board — everything else depends on it |
| H-2 | sleep current in each of the five power states |
| H-3 | that deep sleep is deep and the radio really is off — the defect the MeshCore review found upstream |
| H-4 | the front-end regression, as a measured noise floor rather than as somebody else's issue report |
| H-5 | time to first fix, cold against warm against hot — the whole duty-cycling argument |
| H-6 | which interference indications each receiver actually emits. The one that answers OD-5 for the LS550G |
| H-7 | energy per fix, and therefore whether duty cycling pays at all |
| H-8 | that USB survives a cable pulled mid-frame |
| H-9 | that a bonded peer reconnects after a reboot |
| H-10 | how far the battery reading sags during a transmission |

Two of them are shaped by upstream's failures rather than by our expectations.
H-3 **fails** if deep-sleep current is within 20 % of mesh-listen sleep, because
that would mean the front end is still powered and the two states differ only in
name. H-8 **fails** if a frame ever arrives with a valid checksum and wrong
content.

H-6 carries a safety line rather than a procedure alone: radiating on a GNSS
band in open air is illegal in most jurisdictions, so it happens in a shielded
enclosure or it does not happen, and spoofing detection stays `UNKNOWN` rather
than being guessed at.
