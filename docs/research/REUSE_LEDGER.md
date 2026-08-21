# Reuse Ledger

Firefly prefers proven work over new code. This file records, for every
non-trivial problem, what already existed, what was examined, and why the
project did or did not use it.

A "we wrote our own" entry is allowed. An *undocumented* one is not — the point
of the ledger is that the next person can tell the difference between a
considered decision and an unexamined reflex.

## Decision vocabulary

| Decision | Meaning |
|---|---|
| `USE AS-IS` | taken unchanged |
| `USE AS DEPENDENCY` | pinned and consumed as an external component |
| `WRAP` | used behind a Firefly interface |
| `PORT` | moved to this platform, logic preserved |
| `ADAPT` | modified for Firefly's constraints |
| `EXTRACT ALGORITHM` | only the algorithm taken, code rewritten |
| `INSPIRE ARCHITECTURE` | only the design idea taken, nothing copied |
| `UPSTREAM PATCH` | change contributed back rather than forked |
| `REIMPLEMENT` | written fresh, with a reason recorded |
| `REJECT` | examined and not used, with a reason recorded |

## Record template

```
### <problem being solved>

Problem:
Projects investigated:
Useful implementation:
License:
Strengths:
Weaknesses:
Decision:
Reason:
Source revision:
Firefly integration:
Tests required:
```

Copy it whole. A half-filled record is worse than none — it looks like the
question was answered.

## Upstream sources examined, and at which revision

The addendum forbids writing "taken from Meshtastic" or "similar to Zephyr". It
requires repository, tag or version, **commit hash**, the relevant source files
and the licence — so that it is possible to return to the specific
implementation. These were cloned in full, with history, on 2026-08-21; the
records below cite them by hash.

Full history rather than a shallow clone is deliberate: the addendum also
requires reading issues, pull requests, changelogs and bug fixes, because closed
bugs show which obvious-looking solutions already broke for other people. We
want to inherit the experience, not only the code.

| Project | Repository | Commit at examination | Last commit | Why it is here |
|---|---|---|---|---|
| `MeshCore` | github.com/meshcore-dev/MeshCore | `d92964352441e53b93e8667b802e04f6e072b39e` | 2026-08-14 | the mesh stack Firefly builds on; T-006 |
| `meshtastic` | github.com/meshtastic/firmware | `68bfe015e6ab9ec2ab8f1657066898b7880eaf63` | 2026-08-20 | ~200 board variants, worldwide regulatory regions, nanopb phone API |
| `InfiniTime` | github.com/InfiniTimeOrg/InfiniTime | `825056574f47a8187b410b860f326050566553e2` | 2026-08-19 | mature LVGL watch firmware with a real app lifecycle, on far less RAM |
| `RadioLib` | github.com/jgromes/RadioLib | `510e00cfb05bbc3c2b7b524262785454944adb6e` | 2026-08-13 | radio abstraction across many chips; candidate for ADR-0003 |
| `lvgl` | github.com/lvgl/lvgl | `7cc13aafaa2e7acab6cf3c1977ab6ca70b6c2ed7` | 2026-08-20 | the UI toolkit; version choice is open question T2 |
| `T-Watch-S3` | github.com/Xinyuan-LilyGO/TTGO_TWatch_Library | `e5a0f825a21198f97d2bafee03ea853766483d20` | 2025-02-28 | LilyGO vendor library for one of the two target boards |
| `waveshare-bsp` | github.com/espressif/esp-bsp | `2f519317d5375f7bbb0190b29a4988c2ea2453e2` | 2026-08-13 | Espressif BSP collection, including the Waveshare board; compile-time BSP_CAPS_* |
| `Gadgetbridge` | codeberg.org/Freeyourgadget/Gadgetbridge | `40326980ca871989961ba2442e7cabd4d204b1b6` | 2026-08-21 | host side of many watch protocols; companion protocol prior art |
| `WatchyOS` | github.com/sqfmi/Watchy | `d1d233c43b36cac23bccc6abeae998aa3e27724e` | 2025-08-18 | ESP32 watch firmware |
| `esp-brookesia` | github.com/espressif/esp-brookesia | `01939b5e58fd50d18339b1c35fb74c4e808962c7` | 2026-08-10 | ESP32 UI framework with an application model |

### Licences, checked before anything was depended on

Firefly is MIT. `CLAUDE.md` says anything incompatible with MIT does not enter
this repository, and the licence is checked *before* the code is depended on,
never after. Every licence below was read from the file in the clone, not from a
badge or a recollection.

| Project | Licence | Where it was read | What Firefly may do with it |
|---|---|---|---|
| MeshCore | **MIT** | `license.txt`, and the README's licence section | anything |
| RadioLib | **MIT** | `license.txt`; `library.json` agrees | anything |
| LVGL | **MIT** | `LICENCE.txt` | anything |
| LilyGO T-Watch library | **MIT** | `LICENSE` | anything |
| esp-bsp (Espressif) | **Apache-2.0** | README, "Copyrights and License" | use and modify, with attribution |
| esp-brookesia | **Apache-2.0** | `license.txt` | use and modify, with attribution |
| **Meshtastic** | **GPL-3.0** | `LICENSE` | **read it, learn from it, copy nothing** |
| **InfiniTime** | **GPL-3.0** | `LICENSE` | **read it, learn from it, copy nothing** |
| **Gadgetbridge** | **AGPL-3.0** | `LICENSE` | **read it, learn from it, copy nothing** |

The bottom three matter more than the top six, because they are the projects
that have already solved Firefly's hardest problems. Meshtastic ships worldwide
and has therefore had to solve regulatory bounds on radio settings. InfiniTime
is a mature LVGL watch firmware with a real application lifecycle running on far
less RAM than either Firefly board has. They are the obvious places to look —
and **GPL-3.0 with no linking exception forecloses every ledger verb with copy
semantics**: not `USE AS DEPENDENCY`, not `PORT`, not `ADAPT`, and not
`EXTRACT ALGORITHM`, which is copying with extra steps. It applies to their
tests as much as their source, so "port the reference vectors" is not available
either.

What remains lawful is to read them, understand the shape of the solution, and
write MIT code. That is `INSPIRE ARCHITECTURE`, and it is the honest verb for
what several records below do.

This is written out at length because it is the kind of constraint that gets
quietly forgotten six months in, when a `DisplayApp` message loop or a region
table looks eminently copyable. It is not. Recording it here means the next
person does not have to rediscover it, and does not have to relitigate it.

Individual records still state their own licence — a licence that is convenient
to look up is a licence that gets assumed.

## Rules

- License is checked **before** the code is depended on, never after. Anything
  incompatible with MIT does not enter this repository.
- Pin a revision. "Latest" tells the next reader nothing.
- Prefer an upstream patch to a fork. If a fork is unavoidable, keep the delta
  small and record what it is and why.
- Reusing code does not mean trusting it. Every reused component needs tests
  that prove it does what Firefly needs, on Firefly's target.
- Vendor examples are a source of *knowledge*. Do not import a vendor demo's
  architecture into the project along with the one fact you needed.

---

## Records

*Empty.* No decision has been made yet.

## Candidates identified, not yet evaluated

Found during the 2026-08-21 board survey. Each needs a full record before
anything equivalent is written by hand.

| Candidate | Why it is relevant | License |
|---|---|---|
| `meshcore-dev/MeshCore` | the mesh protocol the product is specified around | MIT |
| `Xinyuan-LilyGO/LilyGoLib` | vendor library for the T-Watch family; schematics and authoritative pin map | MIT |
| `waveshare/esp32_s3_touch_amoled_2_06` | vendor BSP for the second board — display, touch, audio, SD only | Apache-2.0 |
| `waveshare/esp_lcd_sh8601` | the driver the vendor uses for the CO5300 AMOLED panel | to check |
| XPowersLib | AXP2101 driver used by **both** vendors — covers the one shared part | to check |
| `MarcoRR/S3NTRY` | an existing smartwatch firmware for the Waveshare 2.06 | to check |
| `joaquimorg/OLEDS3Watch` | another, built on ESP-Brookesia | to check |
| `infinition/waveshare-watch-rs` | a Rust `no_std` watch firmware for the same board — unusable directly, potentially instructive | to check |
| Meshtastic | mature ESP32 LoRa firmware with T-Watch support; solves overlapping problems | to check |
| ESP-Brookesia | Espressif application UI framework — overlaps the application framework requirement | to check |

Rust and Arduino candidates are still worth reading. `EXTRACT ALGORITHM` and
`INSPIRE ARCHITECTURE` are decisions in this ledger for exactly that reason —
a project does not have to be usable to be useful.

---

# Records

Six subsystems, investigated on 2026-08-21 before any of them was designed —
which is the order the addendum requires and the order this project was not
previously following. Two more (power/PMU rails, simulator) are still in
progress.

Each record cites a commit hash. Each names at least two lessons taken from
upstream **issues and pull requests** rather than from source, because the
addendum is explicit that closed bugs are the most valuable part: they show
which obvious-looking solutions already broke for other people.

---

### Mesh stack, and the watch-to-node link

**Problem:** a LoRa mesh stack for the Firefly node, and the protocol the watch
speaks to it.

**Projects investigated:** MeshCore (MIT, `d92964352441e53b93e8667b802e04f6e072b39e`,
tags `companion-v1.17.1`) · RadioLib (MIT, `510e00cfb05bbc3c2b7b524262785454944adb6e`)
· Meshtastic (GPL-3.0 — read only) · `orlp/ed25519` vendored in MeshCore (zlib)
· `rweather/Crypto` (**licence UNVERIFIED**, not cloned).

**Useful implementation:** MeshCore's `companion_radio` role, and its transport
abstraction in `src/helpers/BaseSerialInterface.h` / `MultiSerialInterface.h`.

**Decision:** `USE AS DEPENDENCY` for the node firmware image — settled. **For
the watch's local mesh path: undecided, deliberately.**

> **Corrected 2026-08-21.** This record said *"the watch links no MeshCore code
> at all"*, full stop. That is right for the node path and wrong as a statement
> about the product: a T-Watch with a supported radio is a local mesh device
> (final §13, [ADR-0008](../adr/0008-mesh-service-providers.md)). Final §14
> additionally forbids deciding the local integration mechanism — direct
> component integration, an isolated compatibility layer, upstreamable ESP-IDF
> work, a narrow Arduino island, or supporting only viable combinations —
> without a **measured spike**. So the local decision is `UNDECIDED — SPIKE
> REQUIRED` (T-013), and the one constraint that is fixed is that `Arduino.h`
> does not enter `core/`.
>
> A second correction, to the same record's radio claim: MeshCore's supported
> radio set does **not** cover the T-Watch's five variants. Across 87 upstream
> variants it is `SX1262 · SX1268 · SX1276 · LR1110 · LR2021 · STM32WLx`, with
> CC1101 compiled out. Exactly one of the five. See
> [ADR-0003](../adr/0003-radio-not-lora.md).

**Reason (node path):** MeshCore is Arduino/PlatformIO throughout — no
`CMakeLists.txt`, no `idf_component.yml`, `<Arduino.h>` in its interface
headers. Porting it to ESP-IDF means an indefinitely divergent tree, which the
addendum forbids. On the node path it is also unnecessary: the radio is in a
separate device, so the watch never originates or routes a mesh packet. `REIMPLEMENT` fails the addendum's test —
MeshCore's routing, duplicate suppression, airtime budgeting and path-hash
scheme are years of field tuning across 87 board variants, and nothing in
Firefly's requirements is unmet by them. What Firefly *does* write is a
companion-protocol client, which is a client of a published protocol rather than
a reimplementation of a stack; MeshCore's own JavaScript and Python clients are
the same thing.

**Maturity, as evidence:** 87 board variants; three firmware flavours released in
lockstep to v1.17.1; an official web flasher and first-party Android, iOS, JS and
Python clients; issue numbers past #3236 with active triage. `origin/dev` was 29
commits ahead of `main` on the day it was read.

**Weaknesses, stated plainly:** the crypto is the weak part (see M10, M11 in
[OPEN_QUESTIONS](OPEN_QUESTIONS.md)) and the test coverage is thin and lopsided —
seven test binaries, none touching crypto or wire format, with a no-op AES mock.

**Lessons from upstream issues:**

- **A packed descriptor read as a plain integer.** The companion protocol's
  `path_len` byte packs a hash count in its low six bits and a hash size in its
  high two, and shipped firmware treated it as a plain length in
  `CMD_SEND_RAW_DATA`. *Issue #3220, filed and closed 2026-08-19.* → Firefly's
  client validates every `path_len` with the semantics of
  `Packet::isValidPathLen()` (`src/Packet.cpp:13-18`) before consuming a byte of
  path.
- **A shared array across two tasks with no synchronisation.** The ESP32 BLE
  receive path had `onWrite` appending from the BLE host task while `loop()` read
  from the Arduino task. *Fixed by `3885c67c8eaf46ce66e28252338df783ca178a95`,
  2026-07-20.* → MeshCore's core has zero locking by design. Firefly must never
  touch a MeshCore object from more than one task, and all companion-frame
  reassembly on the watch belongs to one task.
- **A blocking write took down the whole cooperative loop.** The ESP32 build
  stalled outright when a USB serial host stopped draining. *Commits `fb2c61f8`,
  `39ff5b87`.* → The node link must be non-blocking on both sides; check
  writability first, never block a UI or application task on it.
- **Security is an open upstream issue, not a solved problem.** *Issue #259,
  "Security issues in encryption!", open since 2025-05.* → Firefly must never
  present the MeshCore transport to a user as private or verified. No lock icon,
  no "encrypted" label on a mesh message. Honest status text is a requirement,
  not a nicety.

**Source revision:** MeshCore `d92964352441e53b93e8667b802e04f6e072b39e`.

**Firefly integration:** [ADR-0005](../adr/0005-node-protocol.md).

**Tests to port** — all MIT, all portable as code:
`test/test_config_serializer/` · `test/test_companion_node_prefs/` ·
`test/test_mesh_tables/test_simple_mesh_tables.cpp` (duplicate-suppression ring)
· `test/test_routing_policy/`. Note there is **nothing** to port for crypto or
wire format; the only usable crypto reference vector in the repository is the
known-good keypair embedded at `src/Identity.cpp:68-110`.

---

### The application protocol encoding

**Problem:** how to frame, version and evolve the high-level watch↔node protocol
§32 mandates.

**Projects investigated:** nanopb (zlib-style, MIT-compatible) · protobuf-c ·
Meshtastic's `PhoneAPI` (GPL-3.0 — read only) · MeshCore's companion protocol
(MIT) · Apple ANCS (specification) · InfiniTime's GATT services (GPL-3.0 — read
only).

**Decision:** `INSPIRE ARCHITECTURE` — write Firefly's own versioned binary TLV,
take no schema-codec dependency.

**Reason:** measured, on `xtensa-esp32s3-elf-gcc` 14.2.0 at `-Os`.
`meshtastic_FromRadio` is a **768-byte C struct carrying a 510-byte wire
message**, because nanopb allocates every `oneof` arm at maximum. That is a
fixed-size-struct cost rather than a wire-format cost, and the distinction sets
the remedy: not "use CBOR", but *do not materialise a union*. Flash tells the
same story less sharply — nanopb runtime 7 029 B, Meshtastic's descriptor tables
13 148 B across 24 units — and matters less, because flash is free on a 16 or
32 MB part and the 512 KB of internal SRAM is not.

The licence closed the alternatives before the measurement did: the two
implementations closest to Firefly's problem are GPL-3.0 and AGPL-3.0.

**Lessons from upstream issues:**

- **A schema width is wire ABI.** nanopb halts on string overflow rather than
  truncating, so shrinking `long_name` by fifteen bytes made peers built against
  the old schema undecodable. *Reverted within the hour;* break at
  `41727ea73453233fc643395ed9467998f0891e44`, 2026-06-11. → Firefly's field
  widths are an **acceptance** property, never a **decode** property. Accept
  generously, clamp on store and on transmit.
- **Per-session state outlived the physical link.** A BLE drop mid-config-sync
  left a nonce, a state-machine cursor, a read index and a file manifest behind.
  *PR #4834, commit `9cbabb0468133474ad19a0c7be637bd8bd974289`, 2024-09-23.* →
  Every per-session value lives in **one struct**, `= {}`-assigned on disconnect,
  so that adding a field cannot forget to reset it.
- **A negotiated version that is never reset.** MeshCore writes `app_target_ver`
  in exactly two places and never clears it, so a reconnecting client inherits
  the previous session's assumption. → Negotiated state is *link* state, not
  *device* state. Firefly resets it unconditionally and returns a session epoch.
- **A length-prefixed frame with no checksum, on a link shared with log
  output.** Debug text interleaved into a declared payload and the receiver
  believed it. *Issue #10975, 2026-07-10.* → A CRC in the header, and a partial
  transport write must retain and complete the remainder rather than dropping it.

**Firefly integration:** [ADR-0005](../adr/0005-node-protocol.md).

---

### The capability model

**Problem:** applications must ask what the device can do, never which device it
is — with providers that attach and detach at runtime.

**Projects investigated:** Zephyr device model and GATT service discovery ·
Meshtastic's `variants` system (GPL-3.0 — read only) · espressif/esp-bsp
(Apache-2.0) · InfiniTime (GPL-3.0 — read only) · BLE GATT, USB device classes,
ANT+ device profiles as specifications.

**Decision:** `INSPIRE ARCHITECTURE`.

**Reason:** the licence forecloses everything else for the two closest
comparables, and independently **no candidate actually solves it**. esp-bsp's
`BSP_CAPS_*` are compile-time macros and cannot express a provider that arrives
later at all. Meshtastic's variants are a build-time selection across ~200
boards. Both answer "which board was this compiled for", which is the question
Firefly's architecture forbids asking.

**Lessons from upstream issues:**

- **Two states were not enough, and widening later was a whole-firmware change.**
  Meshtastic shipped GPS as enabled/disabled and retrofitted `NOT_PRESENT`
  across the firmware, screen text included. *PR #3157, commit
  `7f7c5cbd629e5188939926fd7c0a64280405df6f`, 2024-02-01.* → Put every state in
  on day one. This is the single strongest argument for ADR-0004's seven.
- **Adding the state did not stop code from leaving it.** Two years and one month
  later: *"fix(gps): prevent GPS re-enablement in NOT_PRESENT mode"*, commit
  `4a534f02a48626f2addf742dced2f9e8321d5e16`, 2026-03-19. → An availability enum
  needs a **centralised, tested transition table**, with `Unsupported` terminal.
  ADR-0004 §2a.
- **A remote value rendered without its age is wrong the moment it is shown.**
  InfiniTime's music progress bar showed a stale position. *Issue #127, fixed
  `34858d0a6cf750ec53bc160e75fcc29dbeed5e83`, 2022-03-28.* → Corroborates OD-2
  independently, in a shipped product. Age is not optional on a remote datum.
- **A remote provider was trusted for its length.** `MusicService` sized an array
  from a peer's GATT write length. *Issue #825, `df61907073fab7d4c2f9595c7771e894a3841b65`.*
  → A detached provider is a trust boundary. Bound every length at the link edge.

**Firefly integration:** [ADR-0004](../adr/0004-capability-sources.md).

---

### The application framework

**Problem:** §33's lifecycle, plus surviving a capability that vanishes while an
application is open.

**Projects investigated:** InfiniTime (GPL-3.0 — read only; the closest mature
comparable that exists) · esp-brookesia (Apache-2.0) · Watchy (read) ·
Android/Wear OS lifecycle contracts (specification).

**Decision:** `INSPIRE ARCHITECTURE`.

**Reason:** InfiniTime is the only project solving Firefly's exact shape — an
LVGL watch firmware with a real app model on far less RAM — and it is GPL-3.0.
Recorded explicitly so nobody relitigates it later when `DisplayApp`'s message
loop looks copyable. It is not.

**Lessons from upstream issues:**

- **A resource an open app depended on was powered down underneath it, and the
  app silently rendered a missing element.** *Issue #2451, closed 2026-07-19.*
  Upstream's fix was to stop letting the resource vanish — **Firefly cannot take
  that escape hatch**, because a node walking out of range is not something we
  can decide to keep powered. The app must be told.
- **Availability as a `bool` at one call site, silently disabled by a refactor,
  broken for nineteen months.** *Fixed by `9afc23cba9bcf938d8c49d6e15e7662ee8e6385d`,
  2025-05-24.* → A capability requirement is a compulsory member of a manifest the
  framework reads, so omitting it is a compile error.
- **A fixed-depth back-stack whose own comment reads "Returns random data when
  popping from empty array."** `src/utility/StaticStack.h`, with push and pop
  scattered across call sites. → Navigation history belongs to the framework, is
  bounds-checked, has one push site and one pop site, and popping empty is a
  defined operation returning *nothing*.
- **An app that owned its own LVGL timer crashed on exit, twice.** *Commit
  `f780ac999a069b3539f5419b9e07a624ae018030`, 2021-09-28.* → Applications never
  get a timer handle. The manifest declares a tick period; the framework owns the
  timer's lifetime.

**Tests to port:** none exist. `InfiniTime/tests/` contains two files. That is
itself the finding, and it means Firefly writes these from scratch — all
host-native, no hardware, no LVGL.

---

### Settings and regulatory bounds

**Problem:** typed, persisted, validated settings, including two values bounded
by law.

**Projects investigated:** ESP-IDF NVS (Apache-2.0, at
`c197d718bcc240e82d31536f5c671a3503ac9c78`) · Meshtastic config/moduleConfig
(GPL-3.0 — read only) · MeshCore `ConfigSerializer` (MIT) · InfiniTime
`Settings` (GPL-3.0 — read only) · Zephyr settings subsystem.

**Decision:** `INSPIRE ARCHITECTURE`, with ESP-IDF NVS as the storage dependency
and MeshCore's `ConfigSerializer` tests portable as code.

**Reason:** licence, and one hard constraint — **NVS has no float accessor**
(`espressif/esp-idf#11182`, open since 2023-04-12), which combined with the float
precision measurements in [ADR-0006](../adr/0006-settings-and-bounded-values.md)
§2 forces integer storage regardless of what anyone prefers.

**Lessons from upstream issues:**

- **Persisting a clamped value destroys the user's intent, and compounds every
  reboot.** Meshtastic's `limitPower()` wrote the clamped value back into the
  persisted config — documented in a comment as a thing not to do, and shipped
  anyway, twice. *Fixed by `f95c77b8bd8babd071e7cc2b36f0e3952bf4ed92`, PR #7255,
  2025-07-07.* → Firefly separates stored intent from effective value and makes
  the write-back **structurally impossible**, not merely documented.
- **A wrong number in a region table makes the device transmit illegally.**
  Meshtastic's `EU_433` band edge is wrong and *issue #3371 has been open since
  2024-03-11.* → Do not transcribe anyone's region table. Beyond the GPL-3.0 bar,
  it contains a known-illegal value. Re-derive from primary sources.
- **A transmit gate that is visibly present in the source can be silently dead.**
  Meshtastic gated transmission on `region == UNSET`; the check was there to
  read, and the device transmitted anyway. *Issue #2205, 2023-01-25.* → This is
  Firefly's single most safety-critical line, it is exactly the state the project
  ships in while A4 is open, and it needs a test that actually observes silence
  rather than reads the source.
- **A firmware update reset a setting and the device exceeded legal power.** On a
  board with an external amplifier, `lora.tx_power` returned to a default that
  was lawful without the amplifier. *Issue #1830.* → What a reset restores is a
  safety question, not a convenience one. Factory reset never restores a transmit
  parameter to a higher value.

**Tests to port:** MeshCore's `test_config_serializer` and
`test_companion_node_prefs` are MIT and portable as code. Meshtastic's and
InfiniTime's are GPL-3.0 — their *case lists* may inform, their code may not be
copied.

**Firefly integration:** [ADR-0006](../adr/0006-settings-and-bounded-values.md).

---

### GNSS parsing and heading without a magnetometer

**Problem:** parse NMEA safely, and present a heading on hardware that has no
compass.

**Projects investigated:** minmea (**MIT** via `LICENSE.grants`) · TinyGPS++ and
Meshtastic's fork (LGPL-2.1 — a relink obligation this project should not take)
· MicroNMEA (LGPL-2.1) · GeographicLib (MIT) · Meshtastic's GPS handling
(GPL-3.0 — read only).

**Decision:** `WRAP` — take `minmea.c` / `minmea.h` unmodified at
`2dd2cd11a359de5583e68053182d5bbf29725934`, behind a Firefly wrapper that owns
line assembly, length enforcement, strict checksum verification and value
validation.

**Reason:** minmea is the only licence-clean candidate that is *structurally*
correct on the property this subsystem depends on. `struct minmea_float {value,
scale}` makes **"field absent" a distinct representable state** (`scale == 0`)
rather than a magic value — so an empty course-over-ground field can never be
read as 0°. Verified by compiling and running it against a real sentence with an
empty course field.

Keeping `minmea.c` byte-identical to upstream means the known bug below can
arrive as a version bump rather than a merge.

**Lessons from upstream issues:**

- **A parser that could not represent "absent" reported north.** TinyGPS++
  committed empty NMEA fields as zero, and course-over-ground is *exactly* the
  field a receiver leaves empty when it is not trustworthy — so an untrustworthy
  heading was displayed as 0°, due north. *Fixed 2024-01-21,
  `2044b2c51e91ab4cd8cc93b15e40658cd808dd06`.* → This single issue is why minmea
  was chosen over the more popular library.
- **minmea's own overflow guard can be defeated, producing a negative scale.**
  *kosma/minmea issue #104, open, filed 2026-07-19.* → Validity is
  `field.scale > 0`. **Never** `scale != 0`.
- **`isnan()` is not a safe validity test.** Compiled twice here against the same
  source: with `-ffast-math`, `isnan()` returns 0 on an actual NaN, and so does
  `x != x`. → `-ffast-math` is a correctness hazard in this subsystem, not a
  performance knob. Do not enable it, and do not rely on NaN as a sentinel.
- **A checksum computed from a fixed offset rather than from the located `$`.**
  Meshtastic's output began with CRLF and folded the newline into the checksum.
  *PR #11293, `63671329199e1b6721f837043965a9d891afb092`, 2026-07-31.* → minmea
  has no line assembler, so the assembler is code **Firefly** writes — this is
  our risk surface, not the library's.
- **Geodesic code crashed on hostile coordinates arriving over the air.** *PR
  #10862, `b4dd76a4db78292c9d181d9cc181104b662add13`, 2026-07-02.* → Coordinates
  from a mesh or a node are untrusted input: clamp before every narrowing, bound
  every table index, and make the antimeridian an explicit test case.

**Tests to port:** minmea's own `test_minmea_parse_vtg3` (pins the empty-field
regression), `test_minmea_parse_rmc1/rmc2`, `test_minmea_checksum`,
`test_minmea_check` — MIT, portable wholesale. GeographicLib's `GeodTest.dat` is
data from an MIT library and freely usable as reference vectors.

**Open:** whether the node carries a magnetometer decides whether this is the
fallback plan or the only plan ([NODE_PROFILE](../node/NODE_PROFILE.md) N3). The
speed gate below which course-over-ground is not trustworthy is unresolved and
every documented figure found so far was designed for a vehicle, not a wrist.
