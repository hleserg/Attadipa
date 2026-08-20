# Status

Last updated: 2026-08-21

## Current

Milestone **M0 — Repository and research**.

The repository exists, the research gate is populated, and both target boards
have been surveyed down to the pin map from vendor sources. No firmware code
has been written yet, and that is deliberate: the specification requires the
research gate before large-scale implementation, and the survey changed the
architecture materially.

Both are done: every part on both boards now has an owning core service in
[`docs/architecture/ARCHITECTURE.md`](docs/architecture/ARCHITECTURE.md) —
including the parts the vendor BSPs ignore and the parts no application uses —
and the capability model is settled in
[ADR-0001](docs/adr/0001-capability-model.md).

Since the survey, all four vendor schematic PDFs have been read rather than
merely cited. That corrected two rows that had been wrong, resolved four open
questions, and produced two documented conflicts between the vendor documents
and their own drawings.

Then the product changed shape. A **separate Firefly node** — LoRa, GNSS and an
ESP32 in its own box — is part of the plan, and a watch attached to one runs the
same applications a watch with its own radio runs. Which means a capability can
now be provided by something that is not on the board, and can appear and
disappear while an application is open. Two ADRs written the same morning said
the opposite; both have been corrected rather than left contradicting, and
[ADR-0004](docs/adr/0004-capability-sources.md) carries the model that is
actually in force.

Working on: reading MeshCore upstream (T-006), and reuse reconnaissance across
the eight subsystems about to be designed. Host build and CI (T-003) are green.

## Next ready

- T-004 — pin ESP-IDF and LVGL versions
- T-013 — ADR-0003, radio abstraction across the five possible T-Watch chips

T-006 gates both. The question that matters most is whether MeshCore assumes
exclusive, uninterrupted ownership of the radio: if it does, it cannot coexist
with a coordinator that schedules around it, and that is worth knowing before
anything is built on top.

## Lookahead research

Running ahead of implementation, so the next task is never waiting on a search:

- MeshCore internals — architecture, crypto format, threading, radio
  abstraction, memory footprint
- Existing open-source firmware for these exact boards, for the reuse ledger
- LVGL 9 on QSPI AMOLED — draw-buffer strategy and realistic frame rates
- A-GNSS: what the fitted module actually supports, once the module is known

## Long-running operations

Started ahead of need, so that nothing below stalls waiting for a download.
Progress is in `/root/upstream/clone.log` and `/root/upstream/toolchain.log`.

| Operation | Purpose | Gates |
|---|---|---|
| Ten upstream clones into `/root/upstream` — MeshCore, Meshtastic, InfiniTime, RadioLib, LVGL, LilyGO T-Watch, esp-bsp, Gadgetbridge, Watchy | full history, so commit hashes and closed issues can be cited rather than paraphrased | T-006, T-007, and every reuse-ledger record |
| ESP-IDF `release/v5.5` clone with submodules, then `install.sh esp32s3` | the target toolchain; the download is large and would otherwise block the first firmware build | T-004, T-005 |
| `ninja-build`, `libsdl2-dev`, `libsdl2-image-dev`, `ccache` | the simulator is a first-class target (§35) and SDL2 was not installed | T-008 |
| Reuse reconnaissance across eight subsystems, in parallel | the addendum requires open-source recon *before* each subsystem is designed, not after | T-006, T-007, and ADR-0004 through ADR-0006 |

These were started on 2026-08-21 in response to the addendum's rule that long
operations be started before their result is needed. They were overdue: the
reuse ledger had a template and no records, which is the state the ledger exists
to prevent.

## Verified hardware

None physically. Both boards are verified **on paper only** — vendor
documentation, vendor BSP source, published schematics. No board has been
powered on by this project, and no measurement has been taken.

Nothing in this repository may be described as hardware-tested.

## Blocked

- **T-010 board bring-up** — no physical board; exact variant unknown
- **T-011 interference measurement** — same, and neither board has a
  magnetometer, so the headline haptics-vs-compass concern cannot be measured
  on current hardware at all. Four rows of the interference matrix are now
  marked NOT MEASURABLE rather than pending

## Waiting on the project owner

1. Is either board physically available, and which revision?
2. If a T-Watch: which of the five radio chips, and which of the two GNSS
   modules? This decides sub-GHz vs 2.4 GHz, which is a regulatory question as
   much as a driver one.
3. Is there a second radio device, so mesh can be tested at all?
4. Which regulatory region governs LoRa here?
5. **Is an external magnetometer intended at all?** Neither board has one, so
   every compass feature in the plan currently has no hardware to run on. The
   answer decides whether five §67 epics are dormant or dead.
6. **Does the Firefly node carry a magnetometer?** This is question 5 asked
   about the node instead of the watch, and it is the one that decides what
   "компас" can mean. If the node has one, a compass works standing still. If
   not, a compass is GNSS course-over-ground — it needs the user to be moving
   and shows nothing at all when they stop. Those are different products and
   the difference is visible in the first ten seconds of use.

**What the Waveshare board should be** was question 6 here until 2026-08-21.
It is answered: the board is not a lesser device needing a purpose found for
it, it is a device whose mesh and navigation arrive over a link instead of over
a bus. See [OWNER_DECISIONS](docs/research/OWNER_DECISIONS.md) OD-1.

Question 4 is not a preference. Which frequencies, power levels and duty cycles
are lawful follows from the region, and it must be settled before anything
transmits. It became concrete the same day: the owner's own MeshCore node runs
868.731 MHz at 22 dBm — 158 mW — and whether that is lawful on that frequency
in the region of operation is unestablished. Firefly is not responsible for
that node, but the numbers it ships as *defaults* are its own responsibility.
Note that §52 of the specification already forbids hardcoding RF settings, so
the honest default for frequency is not a number at all: it is `Unset`, and
`Unset` closes the transmit path.

## Build and test status

| Target | State |
|---|---|
| Host / native | builds; smoke test passes locally and in CI |
| Simulator | not started — SDL2 and ninja now installed; LVGL version still not chosen |
| ESP32-S3 firmware | not started — ESP-IDF `release/v5.5` installing; version not yet pinned by decision |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED` |

The development host now has cmake, gcc, ninja, SDL2 and ccache. ESP-IDF is
being installed for `esp32s3` — see "Long-running operations". Note that having
ESP-IDF v5.5 on disk is not the same as having decided on it (T-004); the clone
was started early so that the decision is not what waits for the download.

## Assumptions currently in force

- The LilyGO PlatformIO pin to IDF 4.4.7 does not constrain Firefly, which is
  ESP-IDF-native and does not use the Arduino layer. Flagged, not proven.
- Both boards' SoC is an ESP32-S3 — now also from both schematics
  (`ESP32-S3-R8` and `ESP32-S3R8`), but not yet from a chip readback.

## Known failures

None. Nothing runs yet, which is not the same as everything working.

## Open conflicts

Recorded rather than resolved by preference. Both need a powered board.

| # | Conflict |
|---|---|
| H8 | The T-Watch vendor document calls ALDO1 unused; the schematic drives the `+3V3` rail from it. If the schematic is right, `+3V3` is switchable and carries five parts |
| D12 | PSRAM documented as quad; the `R8` part marking is understood to mean octal. Affects both boards, and blocks the LVGL buffer decision |

## What changed most recently

**The schematics were read, not just cited.** The sources table listed all four
PDFs while the board data actually rested on vendor documents and BSP headers.
Reading them confirmed the important negatives from primary evidence — no
magnetometer, no GNSS PPS reaching the SoC — and corrected two rows that were
wrong:

- **The Waveshare board does have a vibration motor** (GPIO 18 through a
  transistor, no driver IC). It was recorded as "none found" on the strength of
  its absence from the vendor BSP — the same weak argument-from-absence that the
  magnetometer claim used to rest on. Two boards, two very different haptic
  degrees, one capability: the clearest live justification for ADR-0001.
- **The IR emitter's inactive level is LOW**, which the architecture had asserted
  without a source.

It also found parts that had no row at all: the radio's `DIO3` on GPIO 6, an
amplifier whose shutdown pin is strapped so firmware cannot mute it, a power
button that reaches the PMU rather than a GPIO, and three of four strapping pins
carrying live signals. Waveshare flash is 32 MB.

Before that: every peripheral on both boards got an owner in the core design, and the
capability model is decided. The argument for covering parts nothing uses
turned out not to be "we might want them later" — it is that an unowned part
still draws power, still raises interrupts nobody services, still contends for
the shared bus, and still leaves its pin floating. The Waveshare vendor BSP,
which ships with three unowned parts on the board it supports, is the evidence.

Before that, the board survey. Three findings reshaped the plan:

1. **The Waveshare board has no LoRa and no GNSS.** The two boards are not
   variants of one product; they share only the SoC and the PMU.
2. **Neither board has a magnetometer.** All magnetometer work is
   architectural — an API that can accept one later, not a driver, and not a
   measurable interference problem.
3. **The T-Watch radio and GNSS are purchase-time variants** — five radio chips
   and two GNSS modules — so the board name does not identify the hardware, and
   a capability cannot be a plain boolean.
