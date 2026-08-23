<p align="center">
  <img src="pics/AttadipaBanner.png"
       alt="Attadipa — Lumar, a firefly with a glowing amber abdomen, beside the wordmark and the motto Independent by design"
       width="820">
</p>

**English** · [Русский](README.ru.md) · [Project page](https://hleserg.github.io/Attadipa/)

# Attadipa

**Open-source firmware and application platform for ESP32-S3 smartwatches** —
LoRa mesh messaging over MeshCore, offline GNSS navigation that needs no phone
and no network, and an LVGL interface on FreeRTOS that is meant to be genuinely
pleasant to use. MIT licensed, built with ESP-IDF.

It targets two very different wearables from one codebase — the **LilyGO T-Watch
S3 Plus** and the **Waveshare ESP32-S3-Touch-AMOLED-2.06** — which share almost
nothing but the SoC and the PMU. The T-Watch ships with **one of five** radio
chips and one of two GNSS modules, and the product name tells you which of
neither — and **two of those five are not LoRa transceivers at all**, so "it has
a radio" and "it can join the mesh" are different sentences
([ADR-0003](docs/adr/0003-radio-not-lora.md)). The Waveshare board has no LoRa
and no GNSS whatsoever. So an application here asks *what the device can do*,
never *which device it is*: board differences live behind a capability registry,
and a capability that arrives over the air from an attached node is answered by
the same registry as one soldered to the board.

> **Status: early implementation.** The host-testable layers exist and are
> tested — the hardware inventory, the capability registry, the transport, the
> GNSS trust evaluator, the design-token system, the desktop simulator, and 24
> host tests. One target board is now on the owner's desk and has been read over
> its own USB port, which is the first evidence in this repository that came
> from silicon rather than from a document. **No Attadipa firmware has run on
> it**, no power, timing or GNSS number has been measured, and nothing here is
> written as if any of that had happened. See [STATUS.md](STATUS.md) for exactly
> where things stand.

### What exists today

| | |
|---|---|
| **Builds and is tested on a host** | six libraries, the desktop simulator and **24 tests**, under GCC and Clang, under `-Werror` and under ASan+UBSan. Two of them compile one fixture against two different libraries to prove an application still cannot include a hardware header |
| **Runs two geometries from one binary** | 240 × 240 and 410 × 502, selected at run time, fitting any of the five candidate T-Watch radios and a present-or-absent node without a rebuild |
| **Draws through design tokens** | twelve colour roles across day and night themes with WCAG contrast arithmetic, plus a CI check that refuses a raw hex value or a pixel count back into screen code |
| **Renders every character it claims to** | four generated Montserrat subsets covering all 181 codepoints of the charset; an undrawable codepoint **fails the run** rather than printing a warning |
| **Has one board on the desk** | a Waveshare ESP32-S3 Touch AMOLED 2.06, read over its own USB-Serial/JTAG port: every eFuse still at its factory default, 8 MB PSRAM on the `VDD_SPI` rail, external quad flash `0xC8 0x4019`, chip revision v0.2 — which decides `CONFIG_ESP32S3_REV_MIN` and brings all eight errata of sheet v1.3 into scope |
| **Has not been measured** | there is no Attadipa firmware to flash yet, and no power, timing, GNSS or interference number exists. Ten hardware-in-the-loop plans are written with equipment, procedure and pass/fail criteria, so each unproven claim is visibly unproven rather than merely absent |
| **Runs its own engineering queue** | work arrives as a GitHub issue, an agent opens a branch and a draft pull request, and a second agent reviews it independently. The workflows, their security model and their cost controls are in [`docs/automation/`](docs/automation/CLAUDE_AUTOMATION.md) |

Attadipa is not a Linux-like OS. It is a single embedded
firmware/application platform on top of ESP32-S3 and ESP-IDF/FreeRTOS,
designed to support several watch models from one codebase.

> **Have an idea for a watch app?**
> [Start a Discussion](https://github.com/hleserg/Attadipa/discussions) — that
> is the front door, and it is open to everybody. Issues are the engineering
> queue and are limited to collaborators; a maintainer moves an idea across when
> it is ready to be built. See [CONTRIBUTING.md](CONTRIBUTING.md).

## What it is meant to be

- **Independent of your phone.** Mesh messaging, navigation, and time work with
  no companion app and no internet. A phone is an optional companion, never the
  brain of the watch. Where a capability needs hardware a particular watch does
  not have, it comes from a **Attadipa node** — a dedicated box built for this,
  not a handset — and the interface says which situation it is in rather than
  pretending.
- **Mesh-native.** Long-range LoRa messaging built on
  [MeshCore](https://github.com/meshcore-dev/MeshCore), staying compatible with
  upstream rather than forking away from it.
- **Navigation that admits what it knows.** Position and heading are reported
  with their source and confidence. The watch does not draw a confident arrow
  when it is not confident.
- **Beautiful, not "engineering demo".** Design is part of Definition of Done —
  a design-token system, day/night themes, considered motion, meaningful
  haptics.
- **A real Child Mode.** A separate UX designed for a six-year-old — not the
  adult UI with bigger fonts. Large targets, recognisable icons, little
  reading, SOS, direction to a parent.
- **Long battery life as a feature**, tracked with real telemetry rather than
  claimed.
- **Desktop simulator as a first-class target**, so UI work does not wait on
  hardware.

## Target hardware

| Board | Role | Radio / GNSS on board |
|---|---|---|
| LilyGO T-Watch S3 Plus | first target — the full product | LoRa + GNSS |
| Waveshare ESP32-S3 Touch AMOLED 2.06 | second target | **neither** |
| Attadipa node | separate device — LoRa, GNSS, ESP32 | provides both, over a link |
| Desktop simulator | first-class development target | simulated, including the node |

The second board has no sub-GHz radio and no GNSS receiver. That is not an
oversight in the plan — it is the reason the capability layer exists. Nor does
it make that board a lesser device: a **Attadipa node** is a separate box
carrying LoRa, GNSS and an ESP32, and a watch attached to one runs the same
applications a watch with its own radio runs. Mesh and navigation are
unavailable there *without a node*, and the interface says which of those two
situations it is in — "this watch has no radio" and "your node is out of range"
are different sentences with different things the user can do about them.

That is the point where the capability layer stops being decorative. An
application asks for a position; whether it came from a receiver on the board,
from a node over a link, or from nowhere at all is the location service's
business and never the application's. It is also why capabilities here are not
booleans fixed at boot: one can appear and disappear while an application is
open, and every application has to survive that. See
[ADR-0004](docs/adr/0004-capability-sources.md).

Presence is not the whole story either. Both boards have haptics, and they are
not the same haptics: one has a driver chip with a waveform library, the other
has a motor on a transistor. A capability that is merely present or absent
cannot express that, which is why it is a typed descriptor here rather than a
boolean. What each board actually carries
is in [`docs/research/HARDWARE_MATRIX.md`](docs/research/HARDWARE_MATRIX.md).

Both boards have been surveyed component by component, down to the pin map and
the power rails. The T-Watch is sourced from the vendor hardware document, the
vendor board header and **both published schematics, read sheet by sheet**. The
Waveshare board is sourced from the vendor README and its board-support package;
its schematic has also been read, by text extraction — which recovers part
numbers and nets reliably and pin adjacency only sometimes, so the rows that
still need the sheets read visually say so. Where the schematic and the vendor document disagree — and on the
T-Watch power rails they do — the disagreement is recorded as a conflict rather
than resolved by preference. Those findings are in
[`docs/research/HARDWARE_MATRIX.md`](docs/research/HARDWARE_MATRIX.md), each
with its source, and in
[`docs/research/VERIFIED_FACTS.md`](docs/research/VERIFIED_FACTS.md).

What is *not* established is anything that requires the physical board:
measured power draw, real GNSS performance, and whether the interference the
architecture guards against actually occurs. Those stay in
[`docs/research/OPEN_QUESTIONS.md`](docs/research/OPEN_QUESTIONS.md) until
somebody measures them. A datasheet number is not a measurement.

## Design ideas worth knowing about

**Capability-driven, in two layers.** Applications ask what the device can
*do* — `availability(Capability::Position)` — and never what is on it. They do
not learn which GPIO powers the GNSS module, which SPI the radio sits on, or
whether there is a GNSS module at all: an Attadipa node supplies one over a link,
and the answer is the same shape either way. The hardware inventory is a
separate layer that lives below the service boundary and is not linked into
applications, so asking a chip a question is a build error rather than a review
comment. Board differences stay inside the BSP; differences that are not the
board's arrive through a provider registry, because no BSP can know at build
time what will be plugged in later.

**Hardware coordination.** In a watch, subsystems interfere *physically*: the
vibration motor disturbs the magnetometer, radio transmission disturbs GNSS
acquisition. A central coordinator grants sensitive measurements quiet windows
and schedules non-critical activity around them, so that no application has to
know which two subsystems must not run at once.

The specification's own example of that is a compass reading and a buzz, and it
is worth saying plainly that **this particular pair cannot occur on either
target board** — neither has a magnetometer. The coordinator's real first job
here is more mundane and more certain: five devices sharing one I2C bus, power
rails feeding two things at once, and three radios in one antenna environment.
Which combinations actually interfere is a question for measurement, tracked in
[`docs/hardware/INTERFERENCE_MATRIX.md`](docs/hardware/INTERFERENCE_MATRIX.md).

**Reuse before writing.** Mature open-source work is preferred over new code,
with the decision and its reasoning recorded in
[`docs/research/REUSE_LEDGER.md`](docs/research/REUSE_LEDGER.md).

## Repository

```
platform/                      the hardware inventory — chips, pins, rails, board profiles
core/                          services, and the capability registry that owns the mapping
link/                          transport framing, the frame queue, the session state machine
l10n/                          the string catalogue and the code generated from it
ui/                            design tokens: roles, spacing, contrast — no LVGL, no board
ui/assets/                     source art, the generated LVGL masks, and the one lookup
assets/fonts/                  the generated Montserrat subsets, with their provenance
apps/                          applications; links core and cannot reach platform
sim/                           the desktop simulator, and its LVGL configuration
tests/                         host tests, including the two that check the layer boundary
tests/replay/                  the deterministic navigation rig, and its recorded traces
tools/                         the font subsetter, the image pipeline, and the checks CI runs
cmake/                         the pinned LVGL dependency

docs/master-prompt-final.md    product specification (source of truth)
docs/research/                 verified facts, owner decisions, open questions, deps, reuse ledger
docs/node/                     the Attadipa node — mostly what is *not* known about it
docs/hardware/                 interference matrix, board notes
docs/architecture/             architecture, resource budget
docs/adr/                      architecture decision records
docs/testing/                  host test plans, and the hardware-in-the-loop plans
docs/ui/                       the design system, and the owner's design references
pics/                          brand assets — banner, icon, favicon
TASKS.md                       backlog: NOW / NEXT / READY / BLOCKED / WAITING / DONE
STATUS.md                      where the project is right now
```

`apps/` not being able to reach `platform/` is enforced by the link line rather
than by review, and there is a test that fails if somebody removes it —
[ADR-0007](docs/adr/0007-two-capability-layers.md) §5.

The specification documents are written in Russian; code, comments, and the
rest of the documentation are in English.

## Building

The host build needs nothing but a C++17 compiler and CMake:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The simulator is opt-in, because it is the only part that needs SDL2 and a
network fetch of LVGL:

```sh
sudo apt install libsdl2-dev          # or your platform's equivalent
cmake -S . -B build-sim -DATTADIPA_BUILD_SIMULATOR=ON
cmake --build build-sim
./build-sim/sim/attadipa_sim --board waveshare-amoled-206
```

```
--board <id>      t-watch-s3-plus (240x240) | waveshare-amoled-206 (410x502)
--radio <chip>    fit any of the five candidate T-Watch radios
--node            present a paired, reachable Attadipa node
--no-bring-up     leave every part untouched, to see the unavailable states
--screenshot <p>  write the screen to a PNG
--frames <n>      render n frames and exit; with SDL_VIDEODRIVER=dummy, headless
```

None of those need a rebuild. That is the point of them: the simulator has to
be able to present a configuration it was not compiled for, which is also why
this project has no per-board binaries.

LVGL is pinned at v9.5.0 and fetched by CMake at the commit; the build refuses
to continue if the version it finds is not the version that was chosen. To
build offline against a tree you already have, pass
`-DATTADIPA_LVGL_SOURCE_DIR=/path/to/lvgl`. The ESP-IDF version is still not
chosen, and it blocks the device build rather than this one — see
[`docs/research/DEPENDENCIES.md`](docs/research/DEPENDENCIES.md).

## Contributing

Early days — the architecture is still being established, so the most useful
contributions right now are hardware facts with primary sources: schematics,
datasheets, and board-revision differences. If you own either target board,
[`docs/research/OPEN_QUESTIONS.md`](docs/research/OPEN_QUESTIONS.md) is a list
of things worth knowing.

## License

MIT — see [LICENSE](LICENSE).

Third-party components keep their own licenses; each one is recorded in
`docs/research/DEPENDENCIES.md` with its license before anything depends on it.
