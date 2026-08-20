# Firefly OS

A wearable firmware platform for ESP32-S3 smartwatches — mesh messaging,
offline navigation, and a UI that is meant to be genuinely pleasant to use.

> **Status: pre-implementation.** The repository holds the product
> specification, the research-gate documents, and the backlog. There is no
> firmware yet. See [STATUS.md](STATUS.md) for exactly where things stand.

Firefly OS is not a Linux-like OS. It is a single embedded
firmware/application platform on top of ESP32-S3 and ESP-IDF/FreeRTOS,
designed to support several watch models from one codebase.

## What it is meant to be

- **Independent of your phone.** Mesh messaging, navigation, and time work with
  no companion app and no internet. A phone is an optional companion, never the
  brain of the watch. Where a capability needs hardware a particular watch does
  not have, it comes from a **Firefly node** — a dedicated box built for this,
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
| Firefly node | separate device — LoRa, GNSS, ESP32 | provides both, over a link |
| Desktop simulator | first-class development target | simulated, including the node |

The second board has no LoRa radio and no GNSS receiver. That is not an
oversight in the plan — it is the reason the capability layer exists. Nor does
it make that board a lesser device: a **Firefly node** is a separate box
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

**Capability-driven.** Applications ask
`device.capabilities().has(Capability::GNSS)`. They never learn which GPIO
powers the GNSS module or which SPI the radio sits on. Board differences stay
inside the BSP. Differences that are not the board's — a capability supplied by
an attached node — arrive through a provider registry instead, because no BSP
can know at build time what will be plugged in later.

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
docs/master-prompt.md          product specification (source of truth)
docs/development-addendum.md   process doctrine: reuse-first, lookahead research
docs/research/                 verified facts, owner decisions, open questions, deps, reuse ledger
docs/node/                     the Firefly node — mostly what is *not* known about it
docs/hardware/                 interference matrix, board notes
docs/architecture/             architecture, resource budget
docs/adr/                      architecture decision records
TASKS.md                       backlog: NOW / NEXT / READY / BLOCKED / WAITING / DONE
STATUS.md                      where the project is right now
```

The specification documents are written in Russian; code, comments, and the
rest of the documentation are in English.

## Building

Nothing to build yet beyond a toolchain smoke test:

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

The ESP-IDF version, the LVGL version, and the rest of the dependency set are
deliberately not chosen yet — that decision belongs to the research gate and
will be recorded in
[`docs/research/DEPENDENCIES.md`](docs/research/DEPENDENCIES.md) with an ADR.

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
