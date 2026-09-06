<p align="center">
  <img src="pics/atta-dipa-banner.png"
       alt="Atta-dipa — Lumar, a firefly with a glowing amber abdomen, beside the wordmark and the motto Independent by design"
       width="820">
</p>

<p align="center">
  <b>English</b> · <a href="README.ru.md">Русский</a> · <a href="https://hleserg.github.io/Attadipa/">Project page</a>
</p>

<h1 align="center">Atta-dipa</h1>

<p align="center">
<b>Independent by design.</b> An open-source ESP32-S3 smartwatch platform for the<br>
places your phone gives up — LoRa mesh messaging and offline navigation, with no<br>
handset, no cloud account and no subscription anywhere in the path.
</p>

<p align="center">
  <a href="https://github.com/hleserg/Attadipa/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/hleserg/Attadipa/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/hleserg/Attadipa/actions/workflows/codeql.yml"><img alt="CodeQL" src="https://github.com/hleserg/Attadipa/actions/workflows/codeql.yml/badge.svg"></a>
  <img alt="ESP-IDF v5.5.5" src="https://img.shields.io/badge/ESP--IDF-v5.5.5-e7352c">
  <img alt="LVGL v9.5.0" src="https://img.shields.io/badge/LVGL-v9.5.0-4c9a2a">
  <a href="LICENSE"><img alt="GPL-3.0-or-later" src="https://img.shields.io/badge/license-GPL--3.0--or--later-blue"></a>
  <a href="https://github.com/hleserg/Attadipa/discussions"><img alt="Discussions" src="https://img.shields.io/badge/discussions-open-8a2be2"></a>
</p>

<p align="center">
  <a href="#run-it-on-your-desktop-in-two-commands"><b>Run the simulator</b></a> ·
  <a href="#what-works-today"><b>What works today</b></a> ·
  <a href="#help-build-it"><b>Help build it</b></a> ·
  <a href="https://github.com/hleserg/Attadipa/discussions"><b>Start a Discussion</b></a>
</p>

---

## This is a real watch, not a mockup

<table>
<tr>
<td width="50%" align="center">
  <img src="docs/hardware/CLOCK_2026-08-26.png" alt="The Atta-dipa clock face — a night meadow with fireflies, large white numerals reading 04:34 and the date WED, AUG 26" width="330">
  <br>
  <sub><b>The clock, on the physical watch.</b> Captured from the live device
  framebuffer over Atta-dipa's own debug channel, not from a design tool —
  <a href="docs/hardware/CLOCK_2026-08-26.md">bench record</a>. Original art;
  the paw and <code>7777</code> are a layout placeholder, not a step counter.</sub>
</td>
<td width="50%" align="center">
  <img src="pics/first-boot-waveshare.gif" alt="Atta-dipa completing its first physical boot on the Waveshare ESP32-S3 smartwatch" width="330">
  <br>
  <sub><b>First boot from flash</b> on the Waveshare ESP32-S3 Touch AMOLED 2.06.</sub>
</td>
</tr>
</table>

## Why Atta-dipa

**Independent.** Messaging, navigation and time work with no companion app, no
internet and no account. A phone is an optional accessory, never the brain of
the watch. Where a watch lacks the hardware for something, the capability comes
from a **companion node** over a link — and the interface tells you which of
those two situations you are in, because *"this watch has no radio"* and
*"your node is out of range"* are different problems with different answers.

**Mesh-native.** Long-range LoRa messaging built on
[MeshCore](https://github.com/meshcore-dev/MeshCore), staying compatible with
upstream instead of forking away from it.

**It tells you when it does not know.** This is the part most wearables get
wrong, and it is the reason this project exists. A position carries its source,
its age and its validity all the way to the pixel. The navigation readout has
**nine** states. Exactly one of them means *"here is your direction"*; the other
eight name the specific thing the watch cannot tell you:

```
Ready                ← the only one that is an answer

Waiting for GPS      Own position stale       Node unavailable
No fix               Own position degraded    Node position unknown
Receiver silent      Node position stale
```

Plus three caveats a good day can still carry — *"node fix unverified, heard
4 min ago"*, *"no receiver on this device"*, *"no position source is set up"* —
and a distance that admits its own ceiling: past 1000 km the screen says
`> 1000 km` rather than printing the clamp as if it were a measurement.

No `0 m`. No `(0, 0)`. No arrow pointing somewhere plausible. If the watch
cannot tell you, it says so — in English or Russian — and it says *which* thing
it does not know.

**Hardware-flexible, by construction.** Two watch boards with almost nothing in
common run the same binary. Applications ask what the device can *do* —
`availability(Capability::Position)` — never what is soldered to it. They cannot
even link against the hardware layer: asking a chip a question is a build error,
not a review comment.

**Honest about uncertainty, mechanically.** Every hardware claim in this
repository is traced to a datasheet, a schematic for the named board revision,
vendor source, or a bench run — or it is written `UNKNOWN` with the experiment
that would settle it. A test that has not run on hardware is never a `PASS`; it
is `NOT EXECUTED — HARDWARE REQUIRED`. CI enforces the citation format.

**Hackable.** ESP-IDF, C++17, LVGL, FreeRTOS. A desktop simulator that runs both
screen geometries with no board attached. A tool that screenshots and drives the
live interface — on the simulator *and* on the physical watch over USB.

## Two topologies, and both are the product

The interesting part of Atta-dipa is that "the watch" is not always one device.

```
  SELF-CONTAINED                        SPLIT
  one device                            two devices

  T-Watch S3 Plus                       Waveshare AMOLED 2.06
  ├── 240×240 display, touch            ├── 410×502 AMOLED, touch
  ├── GNSS  (u-blox MIA-M10Q) ✔         └── BLE ──┐   no radio, no receiver
  ├── sub-GHz radio  (part UNKNOWN)                │        on this board
  └── Atta-dipa                                    ▼
                                        Companion node
                                        ├── GNSS
      the wearer and the receiver       └── LoRa / MeshCore
      are on the same body
                                            the coordinate crosses a link,
                                            and the watch says so
```

That difference is not a footnote — it changes what the watch may honestly
claim. On the self-contained board the receiver is on your body, so its fix is
*your* position. On the split arrangement the node's coordinate is a coordinate
of a *node*, which may be in your pocket or on a windowsill, and the watch is not
allowed to quietly promote one to the other —
[`NODE_POSITION_FROM_MESHCORE`](docs/research/NODE_POSITION_FROM_MESHCORE.md) is
where that rule is argued, and
[ADR-0013](docs/adr/0013-node-motion.md) is why evidence measured on one body
cannot judge hardware on another.

Today's companion on the bench is a stock Heltec T114 running MeshCore. The
purpose-built **Atta-dipa node** — LoRa, GNSS and an ESP32 in one box — is
designed and not built; [`docs/node/`](docs/node/NODE_PROFILE.md) is mostly an
honest record of what is still unknown about it.

## What works today

Status words mean what they say here. `MEASURED` came off hardware or an
instrument; `IMPLEMENTED` runs and is tested, but not on a board yet;
`PLANNED` is a design with no code.

| | State |
|---|---|
| **Boots from flash on a real watch** — AMOLED, capacitive touch, AXP2101 power rails, PCF85063 RTC, and a full [sleep/wake lifecycle](docs/hardware/SLEEP_WAKE_2026-08-26.md) | ✅ `MEASURED` on the Waveshare AMOLED 2.06 |
| **Clock, provisioning and mesh screens on the device**, in English and Russian | ✅ `MEASURED` |
| **Talks to a MeshCore node over BLE** — pairs, connects, pins one node by its public key, and shows peers, SNR and the last message received over LoRa | ✅ `MEASURED` against a Heltec T114 ([bench record](docs/research/MESHCORE_T114_FIRST_CONTACT.md)) |
| **Sending a message and seeing the reply on the wrist** | 🧪 `NOT OBSERVED` — the next physical seam |
| **GNSS on the T-Watch S3 Plus** — the module named itself **u-blox MIA-M10Q**, and its NMEA reaches the position service | ✅ `MEASURED` indoors, no fix ([bench log](docs/research/TWATCH_GNSS_LOCAL_BENCH_2026-09-06.md)) |
| **An outdoor fix on that receiver** | 🧪 `NOT EXECUTED — HARDWARE REQUIRED` |
| **Navigation that refuses to invent** — bearing, great-circle distance, staleness and the eleven honest states above | ✅ `IMPLEMENTED` and rendering; distance to a *remote* node still needs [#450](https://github.com/hleserg/Attadipa/issues/450) |
| **Desktop simulator**, both geometries, radio and node presence switchable without a rebuild | ✅ `IMPLEMENTED` |
| **Screenshot and drive the live UI from another process** — tap, swipe, buttons, scripted journeys | ✅ `MEASURED` on the simulator *and* the physical watch over USB |
| **Design tokens** — twelve colour roles, day and night, with WCAG contrast arithmetic and a CI check that rejects a raw hex value in screen code | ✅ `IMPLEMENTED` |
| **Fonts that cannot silently fail** — seven Nunito Sans subsets covering exactly the 177 codepoints of the charset; an undrawable character fails the build | ✅ `IMPLEMENTED` |
| **Compass / heading** — the API exists and the needle turns with the wrist; neither board ships a magnetometer, and two modules are on the bench awaiting a retrofit | 🧪 [`MAGNETOMETER_RETROFIT`](docs/research/MAGNETOMETER_RETROFIT.md) |
| **Haptics** — typed capability descriptors, no driver code yet | 📐 `PLANNED` |
| **Child Mode** — a separate UX for a six-year-old, not the adult UI with bigger fonts | 📐 `PLANNED` |
| **Power** — one honest number: **413 mW** at the Waveshare's USB input, idle on one screen, cell disconnected, 2026-09-05. No sleep figure, no screen-off figure, no per-rail split | 🔬 measuring |
| **The T-Watch's sub-GHz radio** — five candidate parts, and only some of them do LoRa at all. Nobody has read the marking off the chip | ❓ `UNKNOWN` ([ADR-0003](docs/adr/0003-radio-not-lora.md)) |

Every one of those links to the evidence.
[`docs/research/VERIFIED_FACTS.md`](docs/research/VERIFIED_FACTS.md) is the
ledger, and [`docs/research/OPEN_QUESTIONS.md`](docs/research/OPEN_QUESTIONS.md)
is the list of things nobody has established yet.

## Right now

Live work, as of 2026-09-07 — [Issues](https://github.com/hleserg/Attadipa/issues)
and [pull requests](https://github.com/hleserg/Attadipa/pulls) are the real-time view.

- **A companion's position on your wrist as a direction** — the vertical slice
  that turns a node's coordinate into `NODE / 742 m / ↗ NE`
  ([#450](https://github.com/hleserg/Attadipa/issues/450)).
- **The T-Watch's own GNSS, outdoors** — the indoor half is logged; the fix
  needs sky ([#442](https://github.com/hleserg/Attadipa/issues/442)).
- **A magnetometer inside a watch that never shipped with one** — two modules on
  the bench, four ohmmeter readings away from a wiring decision.
- **Fetching a *remote* node's coordinate over MeshCore** — three candidate
  upstream paths, one of them behind a node-side policy gate.
- **Power characterisation** — one measured idle figure so far, and no sleep
  number at all.

## Run it on your desktop, in two commands

No board required. The simulator is the real UI, the real applications and the
real design tokens, drawing to a window instead of a panel.

```sh
sudo apt install libsdl2-dev          # or your platform's equivalent
cmake -S . -B build-sim -DATTADIPA_BUILD_SIMULATOR=ON && cmake --build build-sim -j
./build-sim/sim/attadipa_sim --board waveshare-amoled-206
```

```
--board <id>      t-watch-s3-plus (240×240) | waveshare-amoled-206 (410×502)
--radio <chip>    fit any of the five candidate T-Watch radios
--node            present a paired, reachable companion node
--no-bring-up     leave every part untouched, to see the unavailable states
--screenshot <p>  write the screen to a PNG
--frames <n>      render n frames and exit; with SDL_VIDEODRIVER=dummy, headless
```

None of those need a rebuild — that is the point of them, and it is why this
project has no per-board binaries. Try `--no-bring-up` first: it is the fastest
way to see what "the watch tells you when it does not know" actually looks like.

Then drive it from another terminal:

```sh
python3 tools/watch_control.py info
python3 tools/watch_control.py tap --x 205 --y 250 --screenshot-after
```

The same tool talks to the physical watch over USB. See
[`docs/testing/WATCH_CONTROL.md`](docs/testing/WATCH_CONTROL.md).

## Help build it

This is a young project with a lot of surface and one owner. If any of these is
your thing, there is real work here — and the research discipline means you will
not be guessing at what the hardware does.

| If you are into | There is work in |
|---|---|
| **Embedded / ESP-IDF** | board bring-up, PMU rails, sleep paths, I2C peripherals, the second board's driver gaps |
| **LoRa / MeshCore** | protocol integration, upstream compatibility, getting a message *and its reply* across |
| **GNSS / navigation** | NMEA and UBX, fix quality, staleness, dead reckoning, the honest-state ladder |
| **Sensors / heading** | magnetometer retrofit, hard-iron and soft-iron calibration, tilt compensation |
| **UI / LVGL** | watch faces, navigation UX, motion, two very different screen geometries |
| **Hardware hacking** | bench measurements, antennas, power, soldering a sensor into a watch that never had one |
| **Testing** | other ESP32-S3 boards, and field tests that are not a desk indoors |
| **Documentation / research** | tracing a claim to a datasheet, reading a schematic sheet by sheet, upstream diffs |

**Where to start.** [Discussions](https://github.com/hleserg/Attadipa/discussions)
are the front door and open to everybody — say which area interests you, or
propose a watch app. Issues are the engineering queue and are limited to
collaborators; a maintainer moves an idea across when it is ready to be built.

```
        Interested?
             ↓
     Start a Discussion  ←  say what you want to work on
             ↓
   A maintainer opens an issue with the acceptance criteria
             ↓
        CONTRIBUTING.md  ←  branch, commit and PR conventions
```

Right now the single most useful contribution is a **hardware fact with a
primary source** — a schematic, a datasheet page, a board-revision difference,
or a measurement off a real unit.
[`docs/research/OPEN_QUESTIONS.md`](docs/research/OPEN_QUESTIONS.md) is a
standing list of things worth knowing, and if you own either board you can close
one of them this weekend.

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

## Target hardware

| Board | Role | Radio / GNSS on board |
|---|---|---|
| LilyGO T-Watch S3 Plus | self-contained target | GNSS **verified** (MIA-M10Q); radio part **UNKNOWN** |
| Waveshare ESP32-S3 Touch AMOLED 2.06 | split target — the wearable half | **neither**, by design |
| Companion node | separate device — LoRa, GNSS, an ESP32 | provides both, over a link |
| Desktop simulator | first-class development target | simulated, node included |

The Waveshare board has no sub-GHz radio and no GNSS receiver. That is not an
oversight in the plan — it is the reason the capability layer exists, and it is
what keeps this from being a firmware for one PCB.

Presence is not the whole story either. Both boards have haptics, and they are
not the same haptics: one has a driver chip with a waveform library, the other
has a motor on a transistor. A capability that is merely present or absent
cannot express that, which is why it is a typed descriptor here rather than a
boolean. What each board actually carries is in
[`docs/research/HARDWARE_MATRIX.md`](docs/research/HARDWARE_MATRIX.md).

Both boards have been surveyed component by component, down to the pin map and
the power rails. The T-Watch is sourced from the vendor hardware document, the
vendor board header and **both published schematics, read sheet by sheet**. The
Waveshare board is sourced from the vendor README and its board-support package;
its schematic has also been read, by text extraction — which recovers part
numbers and nets reliably and pin adjacency only sometimes, so the rows that
still need the sheets read visually say so. Where the schematic and the vendor
document disagree — and on the T-Watch power rails they do — the disagreement is
recorded as a conflict rather than resolved by preference.

**The device build targets both boards today, and they are not at the same
stage.** The Waveshare image is the product firmware: clock, provisioning,
mesh and navigation screens, physical input, sleep and wake. The T-Watch image
is a bring-up slice — panel, touch, rails and the GNSS receiver — with no RTC,
NVS, physical input or sleep yet.

## Design ideas worth knowing about

**Capability-driven, in two layers.** Applications ask what the device can *do*
and never what is on it. They do not learn which GPIO powers the GNSS module,
which SPI the radio sits on, or whether there is a GNSS module at all — a
companion node supplies one over a link, and the answer is the same shape either
way. The hardware inventory is a separate layer below the service boundary and
is not linked into applications, so asking a chip a question is a build error
rather than a review comment. Board differences stay inside the BSP;
differences that are not the board's arrive through a provider registry, because
no BSP can know at build time what will be plugged in later.

Capabilities here are also not booleans fixed at boot: one can appear and
disappear while an application is open, and every application has to survive
that. See [ADR-0004](docs/adr/0004-capability-sources.md).

**Hardware coordination.** In a watch, subsystems interfere *physically*: the
vibration motor disturbs the magnetometer, radio transmission disturbs GNSS
acquisition. A central coordinator grants sensitive measurements quiet windows
and schedules non-critical activity around them, so no application has to know
which two subsystems must not run at once.

The specification's own example of that is a compass reading and a buzz, and it
is worth saying plainly that **this particular pair cannot occur on either
target board as shipped** — neither has a magnetometer. The coordinator's real
first job here is more mundane and more certain: five devices sharing one I2C
bus, power rails feeding two things at once, and three radios in one antenna
environment. Which combinations actually interfere is a question for
measurement, tracked in
[`docs/hardware/INTERFERENCE_MATRIX.md`](docs/hardware/INTERFERENCE_MATRIX.md).

**Reuse before writing.** Mature open-source work is preferred over new code,
with the decision and its reasoning recorded in
[`docs/research/REUSE_LEDGER.md`](docs/research/REUSE_LEDGER.md).

Atta-dipa is not a Linux-like OS. It is a single embedded firmware and
application platform on top of ESP32-S3 and ESP-IDF/FreeRTOS, designed to
support several watch models from one codebase.

## Building

The host build needs nothing but a C++17 compiler and CMake:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

LVGL is pinned at v9.5.0 and fetched by CMake at the commit; the build refuses
to continue if the version it finds is not the version that was chosen. To build
offline against a tree you already have, pass
`-DATTADIPA_LVGL_SOURCE_DIR=/path/to/lvgl`.

### The device build

Separate from the host build on purpose: the root `CMakeLists.txt` is
host-native so that the simulator and the tests keep working on a machine with
no toolchain. The ESP-IDF project lives in `firmware/` and is pinned at
**ESP-IDF v5.5.5** — see
[`docs/research/DEPENDENCIES.md`](docs/research/DEPENDENCIES.md) for why that
version and not a newer one.

```sh
. $IDF_PATH/export.sh
cd firmware
idf.py set-target esp32s3
idf.py build
```

There is a second variant that runs entirely from RAM and writes nothing to the
flash at all, which is the route to prefer for anything experimental —
[`docs/hardware/FIRMWARE_BRINGUP.md`](docs/hardware/FIRMWARE_BRINGUP.md) is the
procedure, including what a good boot looks like and what to do when it is not
one.

## Repository

```
platform/                      the hardware inventory — chips, pins, rails, board profiles
core/                          services, and the capability registry that owns the mapping
link/                          transport framing, the frame queue, the session state machine
debug/                         the development-time debug channel: screenshots out, input in
l10n/                          the string catalogue and the code generated from it
ui/                            design tokens: roles, spacing, contrast — no LVGL, no board
ui/assets/                     source art, the generated LVGL masks, and the one lookup
assets/fonts/                  the generated Nunito Sans subsets, with their provenance
apps/                          applications; links core and cannot reach platform
sim/                           the desktop simulator, and its LVGL configuration
firmware/                      the ESP-IDF project: the device build, its config and partitions
tests/                         host tests, including three where a fixture must fail to build
tests/replay/                  the deterministic navigation rig, and its recorded traces
tools/                         the font subsetter, the image pipeline, and the checks CI runs
tools/watch_control.py         drive the screen and photograph it — see docs/testing/WATCH_CONTROL.md
tools/flash/                   the partition-ceiling check, the RAM loader, the flash backup
tests/ui/scenarios/            journeys through the interface, as data rather than as code
cmake/                         the pinned LVGL dependency

docs/master-prompt-final.md    product specification (source of truth)
docs/research/                 verified facts, owner decisions, open questions, deps, reuse ledger
docs/node/                     the companion node — mostly what is *not* known about it
docs/hardware/                 interference matrix, board notes, bench records
docs/architecture/             architecture, resource budget
docs/adr/                      architecture decision records
docs/testing/                  host test plans, and the hardware-in-the-loop plans
docs/ui/                       the design system, and the owner's design references
pics/                          brand assets — banner, icon, favicon
AGENTS.md                      short repository-wide working agreement
docs/ROADMAP.md                durable product direction
```

`apps/` not being able to reach `platform/` is enforced by the link line rather
than by review, and there is a test that fails if somebody removes it —
[ADR-0007](docs/adr/0007-two-capability-layers.md) §5.

The specification documents are written in Russian; code, comments, and the rest
of the documentation are in English.

## How the project is built

Worth knowing if you are curious about the process rather than the product:
work arrives as a GitHub issue, an agent opens a branch and a draft pull
request, and a **second, independent agent reviews it** without having written
it. The workflows, their security model and their cost controls are in
[`docs/automation/`](docs/automation/CLAUDE_AUTOMATION.md).

That is why the evidence discipline above is mechanical rather than
aspirational. CI checks that a citation carries the text it cites, so a line
number that moved is a red build instead of a document that quietly lies. The
host suite runs under GCC and Clang, under `-Werror` and under ASan+UBSan, with
compile-fail tests guarding the hardware-header, translation-table and
receiver-capability boundaries.

## License

Atta-dipa is distributed under the GNU General Public License v3.0 or later
(`GPL-3.0-or-later`) — see [LICENSE](LICENSE). The project remains open source:
you may use, study, modify, and redistribute it under those terms. Distributed
modifications and derivative works must comply with the GPL, including its
source-availability requirements.

Copyright ownership and contribution provenance are documented in
[COPYRIGHT.md](COPYRIGHT.md); future contributions use the lightweight
[Developer Certificate of Origin 1.1](DCO), without copyright assignment or a
CLA.

Third-party components keep their own licenses; each one is recorded in
`docs/research/DEPENDENCIES.md` with its license before anything depends on it.

---

<p align="center">
  <sub><b>Atta-dipa</b> — from the Pali <i>attadīpa</i>, "having oneself as an
  island or refuge." Lumar, the firefly, carries his own light.</sub>
</p>
