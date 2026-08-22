# Reuse Ledger

Attadipa prefers proven work over new code. This file records, for every
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
| `WRAP` | used behind an Attadipa interface |
| `PORT` | moved to this platform, logic preserved |
| `ADAPT` | modified for Attadipa's constraints |
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
Attadipa integration:
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
| `MeshCore` | github.com/meshcore-dev/MeshCore | `d92964352441e53b93e8667b802e04f6e072b39e` | 2026-08-14 | the mesh stack Attadipa builds on; T-006 |
| `meshtastic` | github.com/meshtastic/firmware | `68bfe015e6ab9ec2ab8f1657066898b7880eaf63` | 2026-08-20 | ~200 board variants, worldwide regulatory regions, nanopb phone API |
| `InfiniTime` | github.com/InfiniTimeOrg/InfiniTime | `825056574f47a8187b410b860f326050566553e2` | 2026-08-19 | mature LVGL watch firmware with a real app lifecycle, on far less RAM |
| `RadioLib` | github.com/jgromes/RadioLib | `510e00cfb05bbc3c2b7b524262785454944adb6e` | 2026-08-13 | radio abstraction across many chips; candidate for ADR-0003 |
| `lvgl` | github.com/lvgl/lvgl | `7cc13aafaa2e7acab6cf3c1977ab6ca70b6c2ed7` | 2026-08-20 | the UI toolkit; version choice is open question T2 |
| `T-Watch-S3` | github.com/Xinyuan-LilyGO/TTGO_TWatch_Library | `e5a0f825a21198f97d2bafee03ea853766483d20` | 2025-02-28 | LilyGO vendor library for one of the two target boards |
| `waveshare-bsp` | github.com/espressif/esp-bsp | `2f519317d5375f7bbb0190b29a4988c2ea2453e2` | 2026-08-13 | Espressif BSP collection, including the Waveshare board; compile-time BSP_CAPS_* |
| `Gadgetbridge` | codeberg.org/Freeyourgadget/Gadgetbridge | `40326980ca871989961ba2442e7cabd4d204b1b6` | 2026-08-21 | host side of many watch protocols; companion protocol prior art |
| `WatchyOS` | github.com/sqfmi/Watchy | `d1d233c43b36cac23bccc6abeae998aa3e27724e` | 2025-08-18 | ESP32 watch firmware |
| `lv_i18n` | github.com/lvgl/lv_i18n | `08944ec6dc2faed83121c53e9cf9ba05013a6686` | 2026-03-30 | LVGL's own localization generator — the closest existing answer to T-033 |
| `esp-brookesia` | github.com/espressif/esp-brookesia | `01939b5e58fd50d18339b1c35fb74c4e808962c7` | 2026-08-10 | ESP32 UI framework with an application model |

### Licences, checked before anything was depended on

Attadipa is MIT. `CLAUDE.md` says anything incompatible with MIT does not enter
this repository, and the licence is checked *before* the code is depended on,
never after. Every licence below was read from the file in the clone, not from a
badge or a recollection.

| Project | Licence | Where it was read | What Attadipa may do with it |
|---|---|---|---|
| MeshCore | **MIT** | `license.txt`, and the README's licence section | anything |
| RadioLib | **MIT** | `license.txt`; `library.json` agrees | anything |
| LVGL | **MIT** | `LICENCE.txt` | anything |
| LilyGO T-Watch library | **MIT** | `LICENSE` | anything |
| esp-bsp (Espressif) | **Apache-2.0** | README, "Copyrights and License" | use and modify, with attribution |
| esp-brookesia | **Apache-2.0** | `license.txt` | use and modify, with attribution |
| **Meshtastic** | **GPL-3.0** | `LICENSE` | **read it, learn from it, copy nothing** |
| **InfiniTime** | **GPL-3.0** | `LICENSE` | **read it, learn from it, copy nothing** |
| `lv_i18n` | **MIT** | `LICENSE` in the clone | anything |
| `cldr-core` (plural rules data) | **Unicode-DFS-2016** | npm registry metadata — **not** the file, because it is not vendored | permissive and MIT-compatible; read the file itself before vendoring any of it |
| **Gadgetbridge** | **AGPL-3.0** | `LICENSE` | **read it, learn from it, copy nothing** |

The bottom three matter more than the top six, because they are the projects
that have already solved Attadipa's hardest problems. Meshtastic ships worldwide
and has therefore had to solve regulatory bounds on radio settings. InfiniTime
is a mature LVGL watch firmware with a real application lifecycle running on far
less RAM than either Attadipa board has. They are the obvious places to look —
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
  that prove it does what Attadipa needs, on Attadipa's target.
- Vendor examples are a source of *knowledge*. Do not import a vendor demo's
  architecture into the project along with the one fact you needed.

---

## Records

Below, under the second `Records` heading. This one is kept because the file's
own structure is quoted elsewhere; the records themselves are further down.

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
| ESP-Brookesia | Espressif application UI framework — overlaps the application framework requirement | to check |

Rust and Arduino candidates are still worth reading. `EXTRACT ALGORITHM` and
`INSPIRE ARCHITECTURE` are decisions in this ledger for exactly that reason —
a project does not have to be usable to be useful.

---

# Records

Seven subsystems, investigated before any of them was designed — which is the
order the addendum requires and the order this project was not previously
following. Six on 2026-08-21; localization was added the same day, before the
generator it decides was written.

Each record cites a commit hash. Each names at least two lessons taken from
upstream **issues and pull requests** rather than from source, because the
addendum is explicit that closed bugs are the most valuable part: they show
which obvious-looking solutions already broke for other people.

---

### Mesh stack, and the watch-to-node link

**Problem:** a LoRa mesh stack for the Attadipa node, and the protocol the watch
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
Attadipa's requirements is unmet by them. What Attadipa *does* write is a
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
  `CMD_SEND_RAW_DATA`. *Issue #3220, filed and closed 2026-08-19.* → Attadipa's
  client validates every `path_len` with the semantics of
  `Packet::isValidPathLen()` (`src/Packet.cpp:13-18`) before consuming a byte of
  path.
- **A shared array across two tasks with no synchronisation.** The ESP32 BLE
  receive path had `onWrite` appending from the BLE host task while `loop()` read
  from the Arduino task. *Fixed by `3885c67c8eaf46ce66e28252338df783ca178a95`,
  2026-07-20.* → MeshCore's core has zero locking by design. Attadipa must never
  touch a MeshCore object from more than one task, and all companion-frame
  reassembly on the watch belongs to one task.
- **A blocking write took down the whole cooperative loop.** The ESP32 build
  stalled outright when a USB serial host stopped draining. *Commits `fb2c61f8`,
  `39ff5b87`.* → The node link must be non-blocking on both sides; check
  writability first, never block a UI or application task on it.
- **Security is an open upstream issue, not a solved problem.** *Issue #259,
  "Security issues in encryption!", open since 2025-05.* → Attadipa must never
  present the MeshCore transport to a user as private or verified. No lock icon,
  no "encrypted" label on a mesh message. Honest status text is a requirement,
  not a nicety.

**Source revision:** MeshCore `d92964352441e53b93e8667b802e04f6e072b39e`.

**Attadipa integration:** [ADR-0005](../adr/0005-node-protocol.md).

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

**Decision:** `INSPIRE ARCHITECTURE` — write Attadipa's own versioned binary TLV,
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
implementations closest to Attadipa's problem are GPL-3.0 and AGPL-3.0.

**Lessons from upstream issues:**

- **A schema width is wire ABI.** nanopb halts on string overflow rather than
  truncating, so shrinking `long_name` by fifteen bytes made peers built against
  the old schema undecodable. *Reverted within the hour;* break at
  `41727ea73453233fc643395ed9467998f0891e44`, 2026-06-11. → Attadipa's field
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
  *device* state. Attadipa resets it unconditionally and returns a session epoch.
- **A length-prefixed frame with no checksum, on a link shared with log
  output.** Debug text interleaved into a declared payload and the receiver
  believed it. *Issue #10975, 2026-07-10.* → A CRC in the header, and a partial
  transport write must retain and complete the remainder rather than dropping it.

**Attadipa integration:** [ADR-0005](../adr/0005-node-protocol.md).

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
Attadipa's architecture forbids asking.

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

**Attadipa integration:** [ADR-0004](../adr/0004-capability-sources.md).

---

### The application framework

**Problem:** §33's lifecycle, plus surviving a capability that vanishes while an
application is open.

**Projects investigated:** InfiniTime (GPL-3.0 — read only; the closest mature
comparable that exists) · esp-brookesia (Apache-2.0) · Watchy (read) ·
Android/Wear OS lifecycle contracts (specification).

**Decision:** `INSPIRE ARCHITECTURE`.

**Reason:** InfiniTime is the only project solving Attadipa's exact shape — an
LVGL watch firmware with a real app model on far less RAM — and it is GPL-3.0.
Recorded explicitly so nobody relitigates it later when `DisplayApp`'s message
loop looks copyable. It is not.

**Lessons from upstream issues:**

- **A resource an open app depended on was powered down underneath it, and the
  app silently rendered a missing element.** *Issue #2451, closed 2026-07-19.*
  Upstream's fix was to stop letting the resource vanish — **Attadipa cannot take
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
itself the finding, and it means Attadipa writes these from scratch — all
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
  2025-07-07.* → Attadipa separates stored intent from effective value and makes
  the write-back **structurally impossible**, not merely documented.
- **A wrong number in a region table makes the device transmit illegally.**
  Meshtastic's `EU_433` band edge is wrong and *issue #3371 has been open since
  2024-03-11.* → Do not transcribe anyone's region table. Beyond the GPL-3.0 bar,
  it contains a known-illegal value. Re-derive from primary sources.
- **A transmit gate that is visibly present in the source can be silently dead.**
  Meshtastic gated transmission on `region == UNSET`; the check was there to
  read, and the device transmitted anyway. *Issue #2205, 2023-01-25.* → This is
  Attadipa's single most safety-critical line, it is exactly the state the project
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

**Attadipa integration:** [ADR-0006](../adr/0006-settings-and-bounded-values.md).

---

### GNSS parsing and heading without a magnetometer

**Problem:** parse NMEA safely, and present a heading on hardware that has no
compass.

**Projects investigated:** minmea (**MIT** via `LICENSE.grants`) · TinyGPS++ and
Meshtastic's fork (LGPL-2.1 — a relink obligation this project should not take)
· MicroNMEA (LGPL-2.1) · GeographicLib (MIT) · Meshtastic's GPS handling
(GPL-3.0 — read only).

**Decision:** `WRAP` — take `minmea.c` / `minmea.h` unmodified at
`2dd2cd11a359de5583e68053182d5bbf29725934`, behind an Attadipa wrapper that owns
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
  has no line assembler, so the assembler is code **Attadipa** writes — this is
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

---

### Localization: the string catalogue, the generator and the plural rules

**Problem:** ADR-0010 requires that no user-facing literal exists above the
platform boundary, that both catalogues ship from the first screen, and that
three checks — coverage, uniqueness, and *a catalogue glyph the font cannot
draw* — fail the build rather than the wrist. Russian needs three plural forms
selected by a rule on the last one and two digits.

**Projects investigated:** `lvgl/lv_i18n` — LVGL's own localization tool, and
therefore the first place to look; the CLDR cardinal plural data it is built on;
`gettext`, considered and already rejected by name in ADR-0010.

**Useful implementation:** `lv_i18n` at `08944ec6dc2faed83121c53e9cf9ba05013a6686`
(v0.2.1, 2026-03-30). YAML source per locale, a Node CLI that extracts keys from
source, and a C generator producing `src/lv_i18n.template.c` — per-locale
`singulars[]`, `plurals[form][]` and a `locale_plural_fn`, with a `lv_i18n_lang_t`
per locale and a NULL-terminated language pack.

**License:** **MIT**, read from `LICENSE` in the clone. Its plural rules are
compiled from `cldr-core`, **Unicode-DFS-2016** — permissive, MIT-compatible,
and read from registry metadata rather than the file because nothing here
vendors it.

**Strengths:**

- The **plural machinery is correct and generated from CLDR**, not hand-written.
  The Russian function it emits is the CLDR cardinal rule verbatim:

  ```c
  if (v == 0 && i10 == 1 && i100 != 11)                      return ONE;
  if (v == 0 && 2 <= i10 <= 4 && !(12 <= i100 <= 14))         return FEW;
  if ((v == 0 && i10 == 0) || (v == 0 && 5 <= i10 <= 9)
      || (v == 0 && 11 <= i100 <= 14))                        return MANY;
  return OTHER;
  ```

  Reading it settles something a hand-written rule would have got wrong: for
  **integers `OTHER` is unreachable in Russian**, so a Russian catalogue entry
  needs exactly `one`, `few` and `many` — and a generator that lets someone
  write `ru.other` is inviting a form that will never be shown.
- The **table shape is right for an embedded target**: parallel `const char *`
  arrays indexed by an integer id, one set per locale, no map and no allocation.
- Separating the plural *category function* from the catalogue is the design
  that makes a third language a data change.

**Weaknesses:**

- **The key is a string literal in the code** — `_("s_translated")` — resolved
  at runtime by `strcmp`. This is precisely the `gettext` shape ADR-0010
  rejects, and for the reason the ADR gives: a key that is a string literal
  cannot be enumerated at build time, so *the coverage check cannot be static*.
  `lv_i18n`'s own answer is `LV_I18N_OPTIMIZE`, a generated macro that is a
  nested ternary of `strcmp` over **every key in the project** — O(n) string
  compares per lookup, expanded inline at every call site.
- It generates **C with a fixed API** (`_()`, `_p()`, `lv_i18n_set_locale`)
  around a global current-locale. Attadipa's `tr()` has to return something the
  UI layer can hold, and that signature is T-033's to choose.
- The toolchain is **Node plus a CLI that scrapes source files** for keys.
  Attadipa's source of truth is the catalogue, not the source scrape — the
  direction is reversed, and reversing it is not a small edit.
- **No font check.** The one check that is invisible without machine
  enforcement — a Russian string the embedded font cannot draw — is not
  something `lv_i18n` does or could do, because it does not know about the font
  subset. That check only exists if it is written here.

**Decision:** `EXTRACT ALGORITHM` for the plural rules · `INSPIRE ARCHITECTURE`
for the table layout · `REJECT` as a dependency.

**Reason:** the part that is hard to get right — the CLDR plural categories —
is taken, as a rule re-expressed and tested against a vector, not as copied
code. The part that does not fit is structural rather than cosmetic: ADR-0010
freezes "the identifier is what the code holds", and `lv_i18n`'s identifier is a
string. Adopting it would either break the frozen decision or require rewriting
its generator, its runtime and its lookup — at which point the only thing left
of it is the plural function, which is what is being taken anyway. Adding Node
to the firmware build for that is a poor trade, and it would not deliver the
font check regardless.

**Source revision:** `lvgl/lv_i18n` `08944ec6dc2faed83121c53e9cf9ba05013a6686`;
files read: `src/lv_i18n.template.h`, `src/lv_i18n.template.c`,
`lib/plurals.js`, `package.json`, `LICENSE`.

**Attadipa integration:** a TOML catalogue as the single source of truth, a
Python generator beside `tools/font/` emitting a `StringId` enum and parallel
per-locale tables, and three checks wired as `ctest` entries so that a local run
and CI enforce the same thing. The plural category function is Attadipa's own
code, in C++, tested against the vector ADR-0010 names.

**Tests required:** the plural vector 0, 1, 2, 5, 11, 21, 101, 111, 1001
asserting *categories* rather than rendered strings — `0→many, 1→one, 2→few,
5→many, 11→many, 21→one, 101→one, 111→many, 1001→one` — plus English `1→one`
and everything else `→other`; generator failure on a missing locale entry, on a
duplicate identifier, and on `ru.other`; and the font check failing on a
catalogue entry carrying a codepoint outside `charset.py`.

---

### Attitude and heading filters — examined before writing nothing

**Problem:** the research report recommends Madgwick fusion for heading. §10 of
the owner's brief requires studying `xioTechnologies/Fusion` before writing our
own rather than reaching for the first paper.

**Projects investigated:** `xioTechnologies/Fusion` (**MIT**, the reference C
implementation of the algorithm, with the magnetic-rejection and acceleration-
rejection logic the original paper leaves out) · Madgwick's own release
(GPL/other, ambiguous) · Mahony's complementary filter (widely reimplemented) ·
Bosch's BSX fusion for the BMA/BMI family (proprietary, licensed per part) ·
`RTIMULib2` (MIT, unmaintained since 2016).

**Decision:** `REJECT` — and not "later", **not on this hardware**.

**Reason:** the decision is settled by the parts list, not by the algorithms.
Madgwick and Mahony fuse a gyroscope with an accelerometer and a *magnetometer*.
The accelerometer gives roll and pitch because gravity is a fixed reference; the
magnetometer gives yaw because the field is another one. Remove it and yaw has
no observable reference at all — the filter still runs, still converges, and
converges to an arbitrary heading that then drifts.

The parts:

| Board | IMU | Magnetometer |
|---|---|---|
| T-Watch S3 | BMA423 — **accelerometer only**, no gyroscope | **none** |
| Waveshare 2.06 | QMI8658 — accelerometer + gyroscope | **none** |

So on one board there is not even a gyroscope to fuse, and on neither is there a
magnetometer. Running Fusion on the Waveshare would produce a confidently drawn
arrow carrying no information about which way the wearer is facing, which is
worse than no arrow: a compass that is wrong is used, and a compass that is
absent is not.

What Fusion *is* worth reading for, and what was taken as
`INSPIRE ARCHITECTURE` rather than as code: its separation of the filter from
the rejection logic, and its explicit `FusionAhrsFlags` reporting when the
filter does not trust its own output. That shape — a filter that says when it is
unsure rather than always producing a number — is the same shape ADR-0011 uses
for GNSS trust, and it is why `HeadingSource` in
[ADR-0009](../adr/0009-heading.md) has a value for `Unknown` that is not a
synonym for north.

**Revisit when:** an external magnetometer is decided
([OPEN_QUESTIONS](OPEN_QUESTIONS.md) A5), or a node with one is specified and
[ADR-0009](../adr/0009-heading.md)'s `NodeBody`→`WatchBody` transform is
resolved. Until then this is not a backlog item, it is a part that does not
exist.

**Tests that would be required if it were ever taken:** Fusion's own repository
carries no test suite. The vectors would have to come from a recorded IMU
capture with a known ground truth, which is a hardware plan
([HIL_PLANS](../testing/HIL_PLANS.md)) and not a host test.

---

### Transport framing over USB, BLE and the node link

**Problem:** frame a byte stream that arrives in arbitrary fragments, over a
transport that can disconnect mid-frame, with bounded memory and no allocation.

**Projects investigated:** MeshCore's `ArduinoSerialInterface` framing (MIT,
readable, and the source of the defects below) · SLIP, RFC 1055 · COBS, and
`bakercp/PacketSerial` (MIT) · HDLC-style byte stuffing as used by Nanopb
examples · ESP-IDF's `esp_serial_slave_link` (Apache-2.0) · TinyFrame (MIT).

**Decision:** `REIMPLEMENT`, with the framing shape taken from COBS-adjacent
prior art and the *failure* requirements taken from MeshCore's bugs.

**Reason.** The requirement that decided it is in
[the MeshCore review](../upstream/meshcore-1.17-review.md), verified at source:
upstream truncates an over-long frame to `MAX_FRAME_SIZE` and **delivers it as
though it were complete**. That is the one failure a protocol cannot recover
from, because the receiver believes the corrupted message. Its framing also
carries no checksum, no escape and no resynchronisation, and `isConnected()`
returns `true` unconditionally with the comment *"no way of knowing, so assume
yes"*.

None of that is fixable by wrapping it, because the defects are in what the
format *is*. Attadipa's frame is therefore a sync pattern, a length with a check
byte so a corrupted length is caught before the decoder waits for bytes that
will never arrive, and a CRC-16/CCITT over length and payload. An over-long
frame is refused, and `tests/test_link.cpp` asserts that specifically.

COBS was the strongest alternative and was not taken because its cost is a
variable-length encoding — a payload's on-wire size depends on its content,
which makes a fixed-size queue slot either wasteful or occasionally too small.
A length-prefixed frame with a checksum has the same recovery properties for
this traffic and a constant overhead of seven bytes.

**What was *not* invented:** CRC-16/CCITT-FALSE is a standard, and
`tests/test_link.cpp` pins it against published vectors (`123456789` → `0x29B1`)
computed independently rather than read out of our own implementation. A
checksum that quietly disagrees with the peer's is a link that never carries
anything.

**Numbers, and where they come from.** `kMaxPayload = 192` is not copied from
anywhere: it is the largest MeshCore packet plus the node-link header, rounded
up to a multiple of 32 so a queue slot aligns. `kDefaultQueueDepth = 4` is one
frame in flight, one being built, and two of slack. Both are stated in
`link/include/attadipa/link/frame_codec.h` beside the constants, because §6 of
the brief forbids magic numbers copied from another project.

**Tests required, and present:** the owner's §6 list one function per item —
fragmented input, several frames in one read, partial writes, a full queue, a
disconnect mid-frame, a reconnect, a large payload, a malformed frame — plus the
upstream defects held against our own code.

---

### GNSS integrity and trust

**Problem:** decide whether a position is worth navigating by, and keep the
reasons.

**Projects investigated:** u-blox's own `UBX-SEC-SIG` and `UBX-NAV-STATUS`
jamming and spoofing indicators (a receiver feature, not code we can take) ·
RTKLIB (**BSD-2-Clause**, and a full RTK/PPP engine — orders of magnitude beyond
this problem) · GNSS-SDR (GPL-3.0, read-only, and a software-defined receiver) ·
Meshtastic's position handling (GPL-3.0, read-only) · Android's
`GnssMeasurement` API model (not code, but a data model worth studying).

**Decision:** `REIMPLEMENT` for the trust engine; `INSPIRE ARCHITECTURE` from
Android's separation of raw measurement from derived location.

**Reason:** there is no existing embedded library for this, and the reason is
that the problem is mostly *policy*. The detectors are arithmetic; what makes
the subsystem worth having is that the policy — weights, hysteresis, what counts
as evidence — is explicit and separable, and that the receiver's own verdict is
the strongest single input without being the only one. Taking a general library
would mean taking somebody else's policy for a wrist in a forest.

RTKLIB was examined and rejected on scale rather than on licence: it is a
correct and permissively-licensed geodesy engine, and §15 of the owner's GNSS
amendment explicitly rules out building the navigation stack now — no Kalman, no
RTS, no PDR, no RTK, no DGNSS. Reaching for RTKLIB would be building it by
accident.

**What was taken from the receiver rather than reimplemented:** everything the
receiver can see and we cannot — per-signal carrier-to-noise, the correlator,
the RF front end. `ReceiverIndication` and `ProtectionLevel` exist so that the
receiver's own verdict enters the model as the heaviest single input, and OD-5
is why `Unknown` and `Unsupported` are distinct from `None`.

**Tests required, and present:** `tests/test_trust.cpp`, mutation-checked
against four real regressions, plus the twelve replay traces.

---

### The replayable navigation rig

**Problem:** verify detectors for events that cannot be staged.

**Projects investigated:** `gpsd`'s regression framework (BSD-2-Clause; replays
recorded sentences against expected output) · RTKLIB's RINEX-driven tests
(BSD-2-Clause) · u-blox u-center's log playback (proprietary, and a desktop GUI)
· Google's `gnss_analysis` tooling (Apache-2.0, Python, for raw measurements).

**Decision:** `INSPIRE ARCHITECTURE` from gpsd's regression framework;
`REIMPLEMENT` the rig itself.

**Reason:** gpsd's design is the right one and its shape is what was taken — a
directory of recorded inputs, each with its expected output, replayed by a
runner that fails loudly on a mismatch and treats an unreadable fixture as a
failure rather than a skip. What could not be taken is the level: gpsd replays
*sentences* to test a *parser*, and Attadipa has no parser yet (minmea is a `WRAP`
decision above and is not vendored). The rig therefore replays normalized
observations to test the trust and validity model, and the fixture format is
shaped so that an NMEA, GPX or vendor-binary front end later adds a reader
rather than a second rig.

**The one thing worth copying deliberately:** gpsd's rule that a regression
fixture nobody can read is a build failure. `tests/CMakeLists.txt` refuses to
configure if the scenario glob matches fewer than ten files, because a glob that
silently matched nothing would produce a test that passes by running no
scenarios at all.

### The agent queue's driver: `anthropics/claude-code-action`

**Problem:** `claude-agent.yml`, `claude-pr-review.yml` and `claude-ci-repair.yml`
— the whole agent queue `docs/automation/AI_TASK_PROTOCOL.md` describes — all
invoke the same third-party GitHub Action to run Claude against an issue or
pull request: it authenticates, invokes the Claude Code SDK headlessly, applies
`--allowedTools`/`--permission-mode`, and (via `display_report`) posts the
summary a human reads. Writing this by hand means reimplementing GitHub App
authentication and the headless SDK invocation, and getting the same things
wrong the workflow's own comments already record having gotten wrong once —
agent mode setting no default `--allowedTools`, and `display_report` defaulting
off, which together produced a 28-turn run in smoke test A that left no branch,
no pull request and no comment.

**Projects investigated:** `anthropics/claude-code-action` only. It is
Anthropic's own action for running Claude Code inside a GitHub Actions job; no
alternative was examined because none offers the same first-party GitHub-native
integration (issue/PR events, labels, branches) for this specific product. That
absence of a search is recorded here rather than implied.

**Useful implementation:** the whole action — GitHub event parsing, agent-mode
invocation of the Claude Code SDK, the `--allowedTools`/`--permission-mode`
plumbing, and the `display_report`/`show_full_output` output controls that
`claude-agent.yml`'s inline comments already describe verifying by reading the
action's own source.

**License:** **MIT**, read from `LICENSE` in the action's own repository
(`github.com/anthropics/claude-code-action`, raw content confirmed via the
GitHub API on 2026-08-21: "Copyright (c) 2025 Anthropic, PBC") — not from a
badge, a marketplace listing or a recollection.

**Strengths:** first-party, so it matches the product it drives; already
load-bearing across all three workflows and observed working — `claude-agent.yml`
records specific behavior (no default `--allowedTools` in agent mode, no
label-setting feature at all) verified by reading `src/` at a pinned commit,
not assumed from documentation.

**Weaknesses:** consumed as a black box with nothing vendored, so a breaking
upstream change reaches all three workflows at once with no local copy to fall
back on; the pin is a floating major-version tag, not a commit (see *Source
revision*), so "the same version" is not guaranteed between two runs unless
something else fixes the commit.

**Decision:** `USE AS DEPENDENCY`.

**Reason:** it is the vendor's own action for running their own product inside
GitHub Actions. `WRAP`, `PORT` or `ADAPT` would mean reimplementing and then
hand-tracking Anthropic's own SDK invocation, for no benefit the action does
not already provide. Alternatives were not examined beyond confirming none
exist for this specific integration — an honest `REJECT`-free `Reason`, per the
scope this record was opened to fill, rather than a claim that a comparison
happened.

**Source revision:** pinned in all three workflows as `@v1` — a **floating
major-version tag, not a commit**. Resolved against the GitHub API on
2026-08-21: `refs/tags/v1` is an annotated tag that peels to commit
`3f854a8fb5146b39d5cbf8b57f70d80810e1366f`, currently identical to the
`v1.0.198` release tag. This matches what `claude-agent.yml`'s own inline
comments already record having verified source behavior against — but that
comment answers "what was checked", and this record answers "why the
dependency is trusted at all"; neither substitutes for the other. Floating
means "verified against `@v1`" is a snapshot: the tag can move to a later patch
between one workflow run and the next with no change in this repository,
silently outdating both that comment and this record until someone re-checks.
Whether to re-pin to a fixed commit SHA for reproducibility, trading it against
picking up upstream fixes automatically, is not decided anywhere in
`docs/automation/CLAUDE_AUTOMATION.md` — that trade is the owner's to make, not
this record's, and is not a blocker on the record existing.

**Firefly integration:** invoked as a `uses:` step in three workflow files,
configured entirely through action inputs (`github_token`, `branch_prefix`,
`allowed_bots`, `allowed_non_write_users`, `show_full_output`,
`display_report`, `additional_permissions`, `--allowedTools`); no source is
vendored or modified.

**Tests required:** none host-testable — the action only runs inside GitHub
Actions, and its behavior can only be observed by running an actual job, which
is how `claude-agent.yml`'s existing comments arrived at their own findings
(e.g. the `display_report` fix after smoke test A). Any future version bump
past the resolved commit above should be re-verified the same way — read the
source at the new commit — rather than assumed compatible from a changelog.

### A Meshtastic companion client

**Problem:** [OD-7](OWNER_DECISIONS.md#od-7--the-companion-is-any-node-not-only-ours)
asked for Meshtastic as a companion alongside or instead of MeshCore, so that
somebody who will not build an Attadipa node still has a mesh. That means
speaking Meshtastic's protocol from the watch.

**Projects investigated:** `meshtastic/protobufs` — the protocol definitions,
which is the whole question, since a client is generated from them — and
`meshtastic/firmware` as the reference for how they are used.

**Useful implementation:** the `.proto` definitions themselves. There is no
partial way to use them: a client that speaks the protocol is generated from
those files or is a clean-room reimplementation of them.

**License:** **GPL-3.0**, read from `protobufs/LICENSE` in the protocol
repository itself (submodule `aca181b` under firmware `68bfe015e`) — the full
text, with **no linking exception**. Corroborated by `packages/ts/package.json`
(`"license": "GPLV3"`) and `packages/rust/Cargo.toml` (`license-file =
"LICENSE"`). No `.proto` file carries an SPDX identifier of its own.

The hypothesis worth checking was that protocol definitions might be licensed
separately from the firmware, as some projects do. They are in a separate
repository — and under the same licence.

**Strengths:** a large installed base, and the only other ESP32 LoRa mesh with
comparable reach. Answering OD-7 with it would have cost nothing in hardware.

**Weaknesses:** generating from those `.proto` files and linking the result into
Attadipa would make an MIT firmware a derivative work under GPL-3.0. The rule in
this file — read it, learn from it, copy nothing — applies to protocol
definitions exactly as to C++.

**Decision:** `REJECT`.

**Reason:** two, and they are not the same one.

*The licence* closed the cheap path. That is a fact about the sources and it is
not a judgement.

*The owner* then closed the expensive one
([OD-12](OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence),
2026-08-22): a genuine clean-room from published documentation, by somebody who
has not read the `.proto` files, is months of work and is done honestly or not
at all — and MeshCore is MIT, which is the half of OD-7's need that a licence
can answer. How much work a MeshCore companion client actually is stays open:
T-072 is unfinished and §1 of
[COMPANION_AND_POSITION_SOURCES](COMPANION_AND_POSITION_SOURCES.md) is `UNKNOWN`
on every row. The rejection above does not depend on that number.

Recording both matters. If Meshtastic's protocol licensing ever changes, the
licence half of this is answered and only the product decision needs revisiting.

**Source revision:** `meshtastic/protobufs` submodule `aca181b`, under
`meshtastic/firmware` `68bfe015e`, read 2026-08-21.

**Attadipa integration:** none. MeshCore remains the one companion protocol a
client may lawfully be written for, under ADR-0008's provider list. No such
client exists yet.

**Tests required:** none — nothing is taken.

