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
| `lvgl` | github.com/lvgl/lvgl | `85aa60d1` (**v9.5.0**) | 2026-08-23 | the UI toolkit. **T2 is settled**: [`DEPENDENCIES.md`](DEPENDENCIES.md) pins v9.5.0 = `85aa60d1…`, verified by `git ls-remote` and observed in CI, and source **S14** in [`VERIFIED_FACTS`](VERIFIED_FACTS.md) reads that revision. This row carried `7cc13aaf…` with *"version choice is open question T2"* until 2026-08-24, so the ledger and the dependency record named two different revisions of the same dependency — the ledger being the file `CLAUDE.md` sends an agent to before implementing anything. Found in review |
| `T-Watch-S3` | github.com/Xinyuan-LilyGO/TTGO_TWatch_Library | `e5a0f825a21198f97d2bafee03ea853766483d20` | 2025-02-28 | LilyGO vendor library for one of the two target boards |
| `esp-bsp` | github.com/espressif/esp-bsp | `2f519317d5375f7bbb0190b29a4988c2ea2453e2` | 2026-08-13 | Espressif's BSP collection and the source of the `esp_lcd_touch_ft5x06` dependency. **It does not contain the Waveshare board** — `esp-bsp/bsp` holds 26 board entries and none is a Waveshare AMOLED. Recorded here as `waveshare-bsp` until 2026-08-22, which sent readers to the wrong repository |
| `waveshare-components` | github.com/waveshareteam/Waveshare-ESP32-components | — | 2026-08-22 | where the Waveshare BSP actually lives. Drives display, touch, audio and SD only: `BSP_CAPS_BUTTONS 0` and `BSP_CAPS_IMU 0`, and it never touches the QMI8658, AXP2101 or PCF85063 on the board. Its `esp_lcd_sh8601` is a fork of Espressif's in which `tx_color()` goes unchecked, so a failed frame reports success — **inherited from upstream rather than introduced by the fork**, and fixed upstream in `v2.0.1` (2025-12-10); the "two-line" count is withdrawn pending a re-derivation against the right base, see [WAVESHARE_ARRIVAL.md](WAVESHARE_ARRIVAL.md) §3.3. Espressif ships both an unforked `esp_lcd_sh8601` and a purpose-named `esp_lcd_co5300`, same Apache-2.0, in `esp-iot-solution` |
| `Gadgetbridge` | codeberg.org/Freeyourgadget/Gadgetbridge | `40326980ca871989961ba2442e7cabd4d204b1b6` | 2026-08-21 | host side of many watch protocols; companion protocol prior art |
| `WatchyOS` | github.com/sqfmi/Watchy | `d1d233c43b36cac23bccc6abeae998aa3e27724e` | 2025-08-18 | ESP32 watch firmware |
| `lv_i18n` | github.com/lvgl/lv_i18n | `08944ec6dc2faed83121c53e9cf9ba05013a6686` | 2026-03-30 | LVGL's own localization generator — the closest existing answer to T-033 |
| `esp-brookesia` | github.com/espressif/esp-brookesia | `01939b5e58fd50d18339b1c35fb74c4e808962c7` | 2026-08-10 | ESP32 UI framework with an application model. **Also the ancestor of the app the bench unit actually runs** — `phone_s3_box_3 v0.4.2-92-g5c6be6c-dirty`, Waveshare's port, 92 commits past a tag and unpublished, so the running binary cannot be read from here |
| `esp-iot-solution` | github.com/espressif/esp-iot-solution | `5d75f3f0dc499d9ed4b69284a3741187c2b75a70` | 2026-08-23 | **where `esp_lcd_sh8601` and `esp_lcd_co5300` upstream actually live** — not `esp-bsp`, whose `components/lcd` holds neither. Read 2026-08-23 for the byte-order trace (D21): both drivers pass the framebuffer to `esp_lcd_panel_io_tx_color()` **verbatim**, so any swap is above them. Apache-2.0. **Read, not depended on** |
| `xiaozhi-esp32` | github.com/78/xiaozhi-esp32 | `bb9122ab08c3083eeb4f67b3974b7afe771723b8` | 2026-08-22 | MIT; carries `main/boards/waveshare/esp32-s3-touch-amoled-2.06/` — this exact board. Evaluated for the audio path below; **re-read 2026-08-23 for the display path**, where `SpiLcdDisplay` sets `.swap_bytes = 1`. The commit is the one the licence check was run against. Note the version on the unit is **1.8.5**, in `ota_0`, never selected |

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
| `waveshare/esp_lcd_sh8601` | the driver the vendor uses for the CO5300 AMOLED panel | **Apache-2.0** at the pinned `==1.0.2`, checked 2026-08-22 (§ the xiaozhi record). Upstream is `espressif/esp-iot-solution`, **not** `esp-bsp`, whose `components/lcd` contains neither this driver nor `esp_lcd_co5300`. Read 2026-08-23 for D21. **The unchecked `tx_color()` was upstream's own code, not a fork divergence** — see the correction in [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §3.3 — and upstream fixed it in `v2.0.1`, 2025-12-10 |
| XPowersLib | AXP2101 driver used by **both** vendors — covers the one shared part | to check |
| `MarcoRR/S3NTRY` | an existing smartwatch firmware for the Waveshare 2.06 | to check |
| ~~`78/xiaozhi-esp32`~~ | **evaluated 2026-08-22 — see the record below.** MIT, and it carries a board directory for this exact board. Its *audio-path dependencies* are the finding: `esp-sr`, `esp_audio_codec` and `esp_audio_effects` are **not** MIT | MIT; deps vary |
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
  Attadipa's single most safety-critical line, it is exactly the state the
  project ships in with the region profile `Unknown` — permanently, now that
  [OD-14](OWNER_DECISIONS.md#od-14--which-region-is-the-owners-problem-not-the-firmwares)
  has closed A4 without naming one — and it needs a test that actually observes
  silence rather than reads the source.
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

**The distance function stayed ours — `REIMPLEMENT`, re-examined 2026-08-23.**
Issue #28 found that `distance_mm()` (`core/src/geo.cpp`) discarded the
fractional latitude before its cosine lookup and so overstated a polar longitude
difference by up to a thousand times, which put the choice back on the table:
fix it locally, or take **GeographicLib** (MIT), which is already named in this
entry and would be correct at every latitude by construction.

Kept local, and the fix was fifteen lines of integer interpolation. Reasons, in
the order they mattered: the consumer is the jump detector comparing two fixes
taken seconds apart, and at that baseline the equirectangular approximation is
already inside the error of the fixes being compared, so a geodesic would be
*more* precise about a quantity nobody needs precisely; GeographicLib is C++ with
`<cmath>` and `double` throughout, and this runs on every fix on a battery, where
the whole point of the 91-entry table is not linking libm into that path; and the
defect was quantization, not method — the fix is arithmetic the existing design
already implied rather than a different design.

What the decision costs, so the next person can weigh it rather than inherit it:
the residual error is **0.9% to 89.999°**, measured on every test run against an
independent haversine reference, and it is dominated by the rounding of
`kCosTable1024`'s own entries. Anything that needs better than a percent — a
route distance, a bearing, a track length — must not reach for `distance_mm()`,
and this is the entry that says GeographicLib is where to go instead. That
boundary is written into `core/include/attadipa/core/geo.h` as well, because a
ledger nobody opens does not stop anybody.

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

### Turning source art into LVGL assets

**Problem:** convert committed source art into flash-resident LVGL image
descriptors, reproducibly, with a stale generated tree visible as a failing test
rather than as a wrong picture on a panel.

**Projects investigated:** LVGL's own `scripts/LVGLImage.py` (MIT, in the pinned
v9.5.0 tree) · LVGL's online image converter, which is the same conversion
behind a web form · writing the `lv_image_dsc_t` emitter by hand from
`src/draw/lv_image_decoder.h` · `imagemagick` plus a hand-written array writer.

**Useful implementation:** `LVGLImage.py` itself. It is the tool LVGL uses to
produce its own assets, it supports every colour format the header defines
including `A8` and `RGB565A8`, its output is byte-identical across runs, and it
takes its input as files and its parameters as flags — which is what makes it
scriptable rather than a web page somebody has to remember to visit.

**Licence:** **MIT**, read from `LICENCE.txt` in the same clone and copied to
`tools/assets/vendor/LVGL-LICENCE.txt` beside the file.

**Strengths:** authoritative — it emits the struct the version of LVGL we pinned
actually reads, so a format mismatch is impossible by construction. Deterministic
with `--compress NONE`: two runs produce identical bytes, verified rather than
assumed.

**Weaknesses:** it hardcodes an include block that falls through to
`lvgl/lvgl.h` with no flag to change it (worked around with
`LV_LVGL_H_INCLUDE_SIMPLE`); it imports `lz4` at module scope even when
compression is off; and it has no notion of a manifest, a digest or a refusal —
point it at a 1440-pixel concept sheet and it will happily convert it. Those
last three are why there is a pipeline around it rather than a call to it.

**Decision:** `USE AS-IS`, **vendored unmodified** at
`tools/assets/vendor/LVGLImage.py`, wrapped by
`tools/assets/generate_images.py`.

**Reason.** Rewriting an encoder for a struct somebody else defines is how a
format drifts. Vendoring rather than referencing the clone is the part that
needed a decision: regeneration has to work for the next agent and in CI, and
neither has `/root/upstream`. LVGL is not a submodule here, and a download
inside a generation step is a network dependency in a build. One MIT file,
pinned by hash, unmodified, is cheaper than a submodule and more honest than a
path that only exists on one machine. **The hash is part of the pipeline's
inputs digest**, so a converter bump that changes any output byte fails
`ui_images_are_current` until the tree is regenerated — an encoder that changes
its output *is* the asset changing.

**Source revision:** LVGL v9.5.0, commit `85aa60d18`. File SHA-256
`c4b59a99104a7592d38b84747296c5e94e86263ca973137b897d295e39b1bff3`, recorded in
`tools/assets/vendor/README.md` and re-checkable with `sha256sum`.

**Attadipa integration:** `attadipa_images`, a target that links LVGL and
`attadipa_ui` — the same separation `attadipa_fonts` has, and for the same
reason: an `lv_image_dsc_t` knows about LVGL and `attadipa_ui` must not.

**Tests required, and present:** determinism (two runs, byte-compared);
`--check` catching a stale tree with no converter installed; the refusals
actually refusing — a source over the dimension cap, a source under `docs/` or
`pics/`, a size with no drawing, a filename that disagrees with its pixels; and,
in C++, that every linked descriptor is `A8` with `stride == width` and carries
a drawing rather than a blank rectangle.
---

### Binding a committed generated tree to the bytes in it

**Problem:** two asset trees are generated and committed —
`assets/fonts/generated/` and `ui/assets/generated/` — so that a build needs
neither Node nor Pillow. Something then has to fail when the committed bytes
stop being what the sources produce, on a machine with nothing installed, and it
has to distinguish "the inputs moved", "an output was edited" and "the record
itself is damaged", because those need three different repairs.

**Projects investigated:** `sha256sum` and its `-c` verify mode (coreutils, the
obvious answer) · `git hash-object` and `git diff --exit-code` after a
regeneration · `pre-commit` with `check-added-large-files`-style hooks ·
content-addressed build systems, Bazel and DVC, which solve exactly this and
several other problems nobody here has · the existing `INPUTS.sha256` mechanism
in both pipelines, which is what had to be extended.

**Useful implementation:** `sha256sum -c` is a real candidate and was close.
It reads a `SHA256SUMS` file, verifies every listed file, exits non-zero on any
mismatch, and is on every machine that already has coreutils.

**License:** n/a — nothing was taken. `hashlib` is the Python standard library.

**Strengths of the candidate:** universally available, universally understood, a
format anyone can read and re-verify by hand.

**Weaknesses:** three, and together they decide it. It verifies only the files
it lists, so deleting an asset and leaving a stale line passes for every file
still there while a *newly generated* file nobody stamped is invisible —
the check has to compare both directions, and `-c` compares one. It has nowhere
to put the inputs digest, so a tree would need two files that must agree and can
be updated separately, which is the same class of defect one file down. And its
failure output is `FAILED` per line, with nothing about which of the three
faults occurred or what command repairs it; both pipelines already compute an
inputs digest in Python, so wrapping a second process to get a worse message
costs more code than not doing it.

**Decision:** `REIMPLEMENT` — `tools/integrity/stamp.py`, about 200 lines of
standard library, shared by both pipelines rather than copied into each.

**Reason.** The thing being written is not "hash some files": it is a small
format with a strict parser and three distinguishable verdicts, whose whole
value is that it refuses ambiguity. A `SHA256SUMS` beside an `INPUTS.sha256`
would be two records that must agree, maintained by two code paths, which is the
shape the finding in issue #69 was already about. One file, one writer, one
parse — and a deliberate absence of any "re-stamp what is on disk" command,
because a tool that blesses whatever bytes it finds reopens the hole while
looking like maintenance.

**Source revision:** n/a. The two things it is pinned against are recorded
elsewhere and stay there: `lv_font_conv` **1.5.3** and LVGL **v9.5.0** at
`85aa60d18b3d5e5588d7b247abf90198f07c8a63`, both in
[DEPENDENCIES](DEPENDENCIES.md), and the vendored `LVGLImage.py` hash in
`tools/assets/vendor/README.md`. The font generator now refuses a converter
whose `--version` is not the pinned one, so the pin is enforced rather than
documented.

**Attadipa integration:** `tools/integrity/stamp.py`, used by
`tools/font/generate_ui_fonts.py` and `tools/assets/generate_images.py`.

**Tests required, and present:** `tools/integrity/selftest.py` — forty-five
cases, registered as `ui_generated_outputs_reject_mutations`. Each of the
fourteen committed outputs is mutated in turn in a copy of the tree and the real
check must reject it and name it; so are a deleted output, a changed input, a
doctored hash, a dropped line, a stamp with no inputs line, a stamp naming a
file nobody generates, and a deleted stamp. A control case at each end asserts
an untouched tree passes, which is what catches a harness that has broken the
sandbox and is therefore rejecting everything for free. Reproducibility across
checkout paths is `tools/integrity/reproducibility.py` rather than this file,
because it needs the pinned converter; it has been run and its CI job is written
and waiting on a permission (T-128).

---

### Speaking the vanilla MeshCore companion protocol

**Problem:** a watch must talk to a MeshCore node that is running **stock**
firmware — one the owner did not build and cannot be asked to reflash
([OD-7](OWNER_DECISIONS.md#od-7--the-companion-is-any-node-not-only-ours)).
Send and receive mesh messages, request telemetry, and take positions from it.

**Projects investigated:** MeshCore `companion_radio` at
`d92964352441e53b93e8667b802e04f6e072b39e` (MIT) — the protocol itself · its
first-party JavaScript and Python client libraries, **identified and not read**
· Gadgetbridge as prior art for a host-side companion protocol (AGPL-3.0, read
only).

**Useful implementation:** the protocol, not the code. The dispatch is a flat
`if/else if` chain over `#define`s in one 2 000-line `.cpp`, inseparable from
the firmware's own state; there is no client library in the firmware repository
to depend on. The transport interfaces are Arduino-typed throughout.

**Licence:** MIT. Anything is permitted; nothing is *useful* to take.

**Strengths:** small, flat, legible in an afternoon; 58 commands with no
schema compiler and no code generation; framing that fits on a page.

**Weaknesses**, all of them ours to absorb rather than fix: `MAX_FRAME_SIZE 176`
is a bare `#define` with no `#ifndef` guard, so a peer cannot raise it; no
checksum on any transport; the byte-stream receiver **truncates** an over-long
frame and delivers it as complete (the same defect already recorded under
*Transport framing*); a defined command with a malformed argument returns
`ERR_CODE_UNSUPPORTED_CMD`, indistinguishable from an unknown opcode; the BLE
path requests a 176-byte MTU and never checks the negotiated one; and a stock
build answers `CMD_EXPORT_PRIVATE_KEY`.

**Decision:** `REIMPLEMENT` — an Attadipa-side **client**, written fresh against
the documented protocol, behind [ADR-0008](../adr/0008-mesh-service-providers.md)'s
provider interface. Explicitly **not** `PORT`, **not** `USE AS DEPENDENCY`, and
**not** a second code path in `core/`.

**Reason.** There is nothing to port: the firmware's client-facing side *is* the
firmware. Depending on the first-party JS or Python clients is not available to
an ESP-IDF image. The protocol is small enough that reimplementation costs less
than adapting anything, and reimplementing is what lets our own framing rules —
refuse an over-long frame, never deliver a truncated one — apply to a link whose
peer has neither.

**Source revision:** `d92964352441e53b93e8667b802e04f6e072b39e`
(`companion-v1.17.1`). Read on 2026-08-22. **The full reading is
[MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md)**, which states
which of its claims were independently verified against the clone and which
rest on a single reading.

**Attadipa integration:** a provider behind ADR-0008, over any of the transports
the node exposes. LAN/TCP first — it needs no BLE stack and no pairing, and it
is testable from a host long before an ESP32 is involved.

**Tests required:** the framing pair against a recorded stock-node exchange;
`CMD_DEVICE_QUERY` re-sent on every connection (the `app_target_ver` hazard);
an over-long frame refused rather than truncated; a malformed argument **not**
reported to the user as an unsupported node; a telemetry response whose
`LPP_GPS` record is absent because permission was denied, which is a normal
outcome and not an error; and a position from a companion carrying no better
provenance than "arrived at time T from key K".

---

### Meshtastic's protocol definitions — the licence gate

**Problem:** OD-7 asks for Meshtastic as a companion alternative to, or
alongside, MeshCore. A client needs the wire format.

**Projects investigated:** `meshtastic/protobufs` at submodule commit `aca181b`,
under firmware `68bfe015e`.

**Useful implementation:** the `.proto` definitions, which are the whole
protocol.

**Licence:** **GPL-3.0.** The definitions live in their own repository with their
own `LICENSE` file — and that file is the same licence as the firmware.
`packages/ts/package.json:10` declares `"license": "GPLV3"`;
`packages/rust/Cargo.toml:7` points `license-file` at the same `LICENSE`. No
exception paragraph, no SPDX identifier in any `.proto`, no dual licensing.

**Decision:** `REJECT` — and since 2026-08-22 the rejection is the owner's, not
a holding position. [OD-12](OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence).

**Reason.** Generating code from those definitions and linking it into an MIT
firmware image is the thing this ledger's rule about GPL-3.0 exists to prevent.
The separate repository was the hypothesis worth testing and it did not survive
contact with the file. The alternatives — a clean-room from published
documentation, separate distribution, or asking upstream for an exception — are
put to the owner as four options, and the owner chose the last of them:
Meshtastic is not supported. The licence closed the cheap path; the *decision*
is that the expensive one is not worth taking.

**Source revision:** `protobufs` `aca181b`; firmware
`68bfe015e6ab9ec2ab8f1657066898b7880eaf63`. Read on 2026-08-22.

**Attadipa integration:** none. MeshCore alone answers what OD-7 asked for.

**Tests required:** none — there is nothing to test and, per OD-12, there will
not be. If this is ever revisited, only the product decision needs to change:
the licence question is answered and stays answered.
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
against five real regressions, plus the replay traces — sixteen fixtures, of
which fifteen are replayed and one is deliberately broken for the rig's own
test.

**Nothing was taken for the 2026-08-23 recovery fix either, and the decision
above is why.** The defect — silence completing a recovery hold, issue #151 —
is a defect in *policy*, in a state machine whose whole reason for existing is
that the policy is ours and explicit. There is no library to reach for and
reaching for one would have been the mistake this entry already rejected.
`REIMPLEMENT` stands, unchanged.

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

### BLE tracker detection — the reverse of tag emulation

**Problem:** T-070 — scan for an unknown BLE identifier that has stayed near
the wearer for an implausibly long time, and say so, without implying the
detector catches everything.

**Projects investigated:** `seemoo-lab/AirGuard` (Apache-2.0) — the only
actively-maintained, open-source implementation of exactly this feature.
`seemoo-lab/AirGuard-iOS` was identified but not read; its detection
capability is materially constrained by iOS's restriction on third-party BLE
MAC-address access, which this record cannot quantify.

**Useful implementation:** the detection policy in
`app/src/main/java/de/seemoo/at_tracking_detection/`: ten ecosystem-specific
BLE scan filters (`device/types/*.kt`), the risk-evaluation thresholds
(`util/risk/RiskLevelEvaluator.kt` — sighting count, distinct-location count,
time-span floor, altitude gates), a scoped identity-rotation stitching
mechanism for Samsung tags only (`device/DeviceManager.kt`,
`device/BaseDevice.kt`), and its own in-product admission of what it cannot
catch (`ui/dashboard/articles/en/limitations_of_the_app.md`).

**License:** **Apache-2.0**, read from `LICENSE` at the repository root, not
a badge. Compatible with Attadipa's MIT: permissive, no copyleft, requires
retaining the Apache notice for anything actually taken. Copyright per
`CITATION.cff`: Niklas Bittner, Alexander Matern, Dennis Arndt, Matthias
Hollick (SEEMOO, TU Darmstadt).

**Strengths:** the only reference implementation of this exact feature that
is both readable and actively pushed (last push 2026-08-20); its
false-positive avoidance — a 150 m distinct-location requirement, owner-
proximity filtering where the ecosystem exposes it, altitude gates for the
aeroplane case — is read from source rather than assumed; it states its own
honest limit in its own shipped strings, which is the same discipline this
project's `CLAUDE.md` asks for, arrived at independently.

**Weaknesses:** Android/Kotlin — nothing is firmware-reusable as code, only
as policy; its rotation-evasion countermeasure covers Samsung's aging-counter
scheme only, and two 2025/2026 papers (one peer-reviewed, one preprint —
[`docs/research/TRACKER_DETECTION.md`](TRACKER_DETECTION.md) §3) report that
an identifier rotated faster than its correlation window, on any other
ecosystem, still evades it; its scan cadence (15-minute period, 20–30 s
bursts) is tuned for an Android phone's battery, not a device meant to scan
all day on a 940 mAh cell.

**Decision:** `EXTRACT ALGORITHM` — the detection policy (thresholds, the
distinct-location false-positive guard, the honest-limit wording discipline),
not the code, which does not run on this target at all.

**Reason:** there is no embedded/firmware equivalent to port, and porting
Kotlin/Android APIs to ESP32-S3 firmware is not meaningful. What is worth
taking is the policy AirGuard arrived at through a WiSec best-paper-award
process and four years of field use: which thresholds actually distinguish a
following tracker from a stationary beacon, and — as important — the
project's own discipline about what it admits it cannot do. `REJECT` was
considered and set aside because the thresholds themselves (150 m, 3
sightings, 14-day window) are a genuinely useful starting point rather than
something Attadipa should re-derive from nothing.

**Lesson from its own commit history, the addendum's rule applied here too:**
a 2025-03-17 release note claims improved evasion resistance, and reading the
source behind the claim (§2.7 of `TRACKER_DETECTION.md`) shows the fix is
scoped to Samsung's aging counter alone — the release note, read in
isolation, would have overstated what changed. → Attadipa's own detector, if
built, must not claim a fix's scope more broadly than the code that
implements it.

**Source revision:** `seemoo-lab/AirGuard`
`7f71a37d0776acc5f0e8d3046d3daaf8b71ad58d` ("AirGuard 3.1.1", 2026-07-20).

**Attadipa integration:** none yet — T-070 is not implemented. When it is,
the thresholds above are a starting point to validate against this project's
own duty-cycle and power constraints
([`TRACKER_DETECTION.md`](TRACKER_DETECTION.md) §4), not values to copy
unchanged onto different hardware and a different battery.

**Tests required:** none yet — no code exists. When T-070 is implemented:
host tests over synthetic scan traces, per `TRACKER_DETECTION.md`'s own
research and `TASKS.md` T-070's acceptance criteria — a co-travelling
identifier flagged, a shop full of stationary beacons not.

---

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
can answer. How much work a MeshCore companion client actually is was open when
that decision was taken and is not any more: T-072 answered §1 of
[COMPANION_AND_POSITION_SOURCES](COMPANION_AND_POSITION_SOURCES.md) on every row
later the same day, and the detail is in
[MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md) — 58 commands, a
176-byte frame budget no build flag can raise, and a TCP transport that makes a
host-side client the cheapest bring-up available. **The rejection above never
depended on that number and does not change now that it exists.**

Recording both matters. If Meshtastic's protocol licensing ever changes, the
licence half of this is answered and only the product decision needs revisiting.

**Source revision:** `meshtastic/protobufs` submodule `aca181b`, under
`meshtastic/firmware` `68bfe015e`, read 2026-08-21.

**Attadipa integration:** none. MeshCore remains the one companion protocol a
client may lawfully be written for, under ADR-0008's provider list. No such
client exists yet.

**Tests required:** none — nothing is taken.


---

### Crowd-sourced tag emulation — the ecosystems, and why none of them was reached

**Problem:** A7 asked whether the watch should be findable through a
crowd-sourced finding network the way a smart tag is, so that a lost watch is
located by strangers' phones rather than only by its owner's.

**Projects investigated:**

| | Licence | Reachable from an MIT ESP32 firmware? |
|---|---|---|
| `seemoo-lab/openhaystack` | **AGPL-3.0** | no — copying into an MIT repository is not available, and AGPL is the strongest copyleft here |
| `dchristl/macless-haystack` | **AGPL-3.0** | no, same |
| Google's Find My Device reference | proprietary, and licensed *"only … with a Nordic Semiconductor ASA integrated circuit"* | no — the silicon clause alone ends it, before the approval form, the email allowlist and the third-party certification |
| Samsung SmartThings Find SDK | proprietary | no — ships for no Espressif part, and an unregistered advertisement is inert by construction |

**Useful implementation:** the *specifications* rather than the code. DULT's
rotation requirements — identifier and BLE address rotating together, 15 minutes
near-owner and 24 hours separated — are a published contract and are readable
without touching an AGPL implementation. Those stay relevant to T-069 and T-070
whatever happens to this feature.

**Decision:** `REJECT`.

**Reason:** two, in this order, and the second is the decisive one.

*The licences and the platform gates* made it expensive. Both open
implementations are AGPL-3.0; both proprietary SDKs are closed to this hardware.
Apple's network is the one that is technically reachable, and reaching it costs
an Apple ID bootstrapped on physical Apple hardware, a self-hosted endpoint and
SMS-only 2FA — and the watch would still never appear in Apple's own Find My
app, because that is the MFi pairing flow and MFi excludes individuals. Median
latency is 26 minutes: recovery, not live tracking.

*The owner* then rejected the feature itself
([OD-13](OWNER_DECISIONS.md#od-13--no-tag-emulation-a-track-is-a-way-back-on-foot-and-saving-one-whole-is-a-separate-feature),
2026-08-22, on [#33](https://github.com/hleserg/Attadipa/issues/33)): *"Не
делаем. Ни Apple, ни какую-либо ещё."* That is a product decision and it does
not rest on any of the above.

Recording both matters, and the order matters more here than usual. If a
crowd-sourced network ever became reachable under a permissive licence, the
first half of this record would be obsolete and the second half would still
stand. This is not a blocked feature waiting for the ecosystems to change.

**Source revision:** licences read 2026-08-21;
`docs/research/TAGS_TRACKS_RECKONING.md` §1 carries the detail and the citations.

**Attadipa integration:** none, and T-064 is closed. The need A7 was aimed at is
answered by **T-063** instead — the companion phone remembers where it last saw
the watch over BLE. No account, no membership, no other company's identifier, no
server, and it works with the companion ADR-0002 already specifies.

**Tests required:** none — nothing is taken.

### Reading a SPIFFS image without an ESP-IDF build

**Problem:** the Waveshare factory flash dump holds a SPIFFS partition, and
T-103 needed the files out of it — the UI assets whose format and count were the
whole question. `strings` recovers file *names* and no file *bodies*: SPIFFS
scatters a file's pages non-contiguously and out of order, so reading it means
parsing the object lookup table, the 5-byte page headers and the object index
header. That is a non-trivial reimplementation of a standard on-disk format,
which is exactly what this ledger exists for. Review on
[#80](https://github.com/hleserg/Attadipa/pull/80) caught that it had been done
without a record; this is the record, written after the fact and saying so.

**Projects investigated:**

| Candidate | What it is | Why it was not used |
|---|---|---|
| `espressif/esp-idf` `components/spiffs/spiffsgen.py` | Espressif's own pure-Python SPIFFS tool, Apache-2.0 | **Generates only.** Its header reads *"spiffsgen is a tool used to generate a spiffs image from a directory"* and its CLI describes itself as *"SPIFFS Image Generator"*; the file contains no parsing path at all. The obvious first candidate, and it cannot do this. |
| `igrr/mkspiffs` | The reference builder/unpacker, MIT | `-u <dest_dir>` does exactly this job. It is **C++**, and building it needs gcc ≥ 4.8 or clang ≥ 600.0.57, `make`, and `git submodule update --init` — a toolchain, for a one-off read of one partition. |
| `octopus-framework/spiffs-dumper` | Dumps SPIFFS off a live ESP32, C/C++ plus Python | Wrong shape twice over: it reads from **a running board over serial**, not from an image already on disk, and it needs ESP-IDF and CMake to build the firmware it uploads. **No licence is stated in the repository**, which on its own would have ruled it out. |

**Useful implementation:** the SPIFFS on-disk layout itself, taken from the
upstream sources rather than from any of the above — the object lookup table,
the `spiffs_page_header` (`obj_id` u16, `span_ix` u16, `flags` u8), the
`SPIFFS_OBJ_ID_IX_FLAG` high bit distinguishing index pages from data pages, and
the object index header at `span_ix == 0` carrying size and name.

**License:** `spiffsgen.py` Apache-2.0, `mkspiffs` MIT, `spiffs-dumper`
**unstated**. Nothing is copied from any of them, so no licence is inherited.

**Strengths (of the rejected options):** `mkspiffs -u` is the reference
implementation and would be right for anyone who already has the toolchain.

**Weaknesses:** all three need something this task did not have — a generator
that cannot read, a C++ build, or a board on the end of a cable.

**Decision:** `REIMPLEMENT` — `tools/flash/spiffs_extract.py`, 115 lines, host
Python with no dependencies.

**Reason:** the only pure-Python option in the ecosystem cannot read images, and
the two that can each require a build environment to recover six files from one
partition once. The cost of the reimplementation is bounded and visible: it is a
read-only offline parser over a file that is already on disk, it writes nothing
back, and being wrong about the format shows up immediately as garbage instead
of as a corrupted image.

**What it deliberately does not hard-code:** the offsets of size and name inside
the object index header, which move between SPIFFS versions and with
`SPIFFS_OBJ_META_LEN`. It finds the name as the first NUL-terminated printable
run beginning with `/` and reads the size from the `u32` immediately before it,
then **checks that against the number of data-page bytes actually recovered** —
a file whose declared size exceeds its recovered bytes is reported and not
written, rather than written short. Review on #80 walked that assumption against
the real `spiffs_page_object_ix_header` layout and found it matches.

**Source revision:** `spiffsgen.py` read from `espressif/esp-idf` `master`,
2026-08-22; `mkspiffs` and `spiffs-dumper` read from their repository pages the
same day. `docs.espressif.com` is unreachable from this environment, so the
ESP-IDF claim rests on the **source file itself** rather than on the
documentation page — which is the better source anyway.

**Attadipa integration:** none, and there must not be any. This is a `tools/`
script for reading a vendor image on a workstation. Nothing in `core/`,
`platform/` or `boards/` links against it, and Attadipa does not use SPIFFS —
if it ever needs an on-device filesystem that is a separate decision with its
own record.

**Tests required:** none automated, and that is a real gap rather than a
judgement. It has been run against exactly one image — the Waveshare factory
dump — which cannot be committed (Waveshare's own copyright, plus
all-rights-reserved third-party audio found inside it), so there is no fixture
to test against. A synthetic image built by `spiffsgen.py` would be one, and
that is worth doing if this script is ever needed twice.

### This board's audio path — the I2S wiring, the ES8311 bring-up, and what the two microphones are for

**Problem:** the Waveshare `ESP32-S3-Touch-AMOLED-2.06` carries an ES8311 codec,
an ES7210 microphone ADC with **both** microphones fitted, and one I2S bus shared
between them. [HARDWARE_MATRIX](HARDWARE_MATRIX.md) already has the pins and the
I2C addresses from the schematic and the physical unit. What it does not have is
the *sequence*: which part is configured first, how the codec is clocked against
those pins, what sample rates the board actually runs, and what the second
microphone is for — one microphone is a microphone, two are a decision. The
received unit shipped with a firmware in which all of that already works.

**Projects investigated:** `78/xiaozhi-esp32` — identified as the stock firmware
because the `model` partition of the received unit holds WakeNet9
`wn9_nihaoxiaozhi_tts` ([WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §3,
source **S11**). Its audio-path dependencies were licence-checked in the same
pass, because the audio path is the thing we came for: `espressif/esp-sr`,
`espressif/esp_audio_codec`, `espressif/esp_audio_effects`,
`espressif/esp_codec_dev`. Display dependencies `waveshare/esp_lcd_sh8601` and
`espressif/esp_lcd_co5300` were checked incidentally. Already in this ledger and
adjacent to the same problem: `waveshare-components`, the vendor BSP, which does
drive audio per its declared capabilities but not the IMU, PMU or RTC.

**Useful implementation:** `main/boards/waveshare/esp32-s3-touch-amoled-2.06/` —
**the exact board, not a sibling**. Four files: `README.md`, `config.h`,
`config.json`, `esp32-s3-touch-amoled-2.06.cc` (12 917 bytes). Its `README.md`
links `waveshare.com/esp32-s3-touch-amoled-2.06.htm` and names the **ESP32-S3R8**,
which matches the `espefuse` readback of the owner's unit (source **S10**,
[WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md)): 8 MB in-package OCTAL PSRAM.
`config.json` declares manufacturer `waveshare`, target `esp32s3`, with
`CONFIG_USE_DEVICE_AEC=y` and `CONFIG_USE_WECHAT_MESSAGE_STYLE=n`. The board
instantiates `BoxAudioCodec` with `AUDIO_CODEC_ES8311_ADDR`
(`esp32-s3-touch-amoled-2.06.cc:5`, `:321–335`), and `box_audio_codec.cc` reaches
the codec through `esp_codec_dev.h` / `esp_codec_dev_defaults.h` — so this
board's codec bring-up runs over the one dependency that is cleanly licensed.
`main/audio/` (35 files) holds the engine above it.

Two traps in the same tree. `main/boards/waveshare/esp32-c6-touch-amoled-2.06/`
is the **same 2.06″ panel on an ESP32-C6** — three characters different in the
directory name, a different SoC, no PSRAM of this kind; usable for panel and
touch, actively misleading for anything I2S, CPU or memory. And there are 42
Waveshare directories under `main/boards/waveshare/` (46 vendor directories in
all), including `-1.8`, `-1.8-v2`, `-1.75`, `-1.43c`, `-1.32` and `-2.16` — near
relatives that are not this board.

**License:** **MIT** for `78/xiaozhi-esp32` itself. Read from the file in the
tree, not from a badge: `LICENSE`, verbatim MIT, sha256
`0a5a839033bfe18fe75d32b50d9d028912cf876f69ef59c2791aeb2971335d05`, identical in
the clone and at
[the commit-pinned raw URL](https://raw.githubusercontent.com/78/xiaozhi-esp32/bb9122ab08c3083eeb4f67b3974b7afe771723b8/LICENSE).
Copyright holders, to be preserved in any substantial copy:
`Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.` and
`Copyright (c) 2025 Project Contributors`. `README.md:159` agrees in prose.

**One verdict for the project would be wrong. Per component, which is which:**

| Component | Licence, read from the file | What Attadipa may do |
|---|---|---|
| `78/xiaozhi-esp32` tree — incl. the 2.06 board directory and `main/audio/` | **MIT** (no per-file SPDX or copyright headers anywhere in the board directory; the four files inherit the repository `LICENSE`) | read, copy, modify, ship — with the notice preserved |
| 23 vendored Espressif LCD / IO-expander driver files + 2 build scripts (`scripts/build_default_assets.py`, `scripts/spiffs_assets_gen.py`) | **Apache-2.0**, SPDX-tagged, 25 files; all in *other* vendors' board directories, **none** in the Waveshare 2.06 one | use and modify, with attribution |
| vendored `gifdec` (`main/display/lvgl_display/gif/`, own `LICENSE.txt`) | **public domain** — "released into the public domain and provided without warranty of any kind" | anything |
| `espressif/esp-sr` `~2.4.7` — WakeNet/MultiNet/AFE **and the model blobs** | **ESPRESSIF MIT License**: "Permission is hereby granted **for use on all ESPRESSIF SYSTEMS products**" (registry `license.txt` at 2.4.7, sha256 `7d916fb00bc0742c47cafb0d0144b67f826d76779730b1cb8796045ea6ba1b9a`, byte-identical to the master `LICENSE`). The registry's own version labels put the change at **1.3.2** — "Custom" from there onward, while 1.3.1 and earlier are labelled `MIT` or `ESPRESSIF MIT` — but labels are labels: what was read here is the 2.4.7 file | **not MIT.** A field-of-use restriction. Does not enter this repository |
| `espressif/esp_audio_codec` `~2.5.0` — Opus | **`LicenseRef-Espressif-Modified-MIT`**, clause 3: "Redistribution … for use with **non-Espressif products** is strictly prohibited". Registry metadata gives its repository as `espressif/esp-adf-libs` — **this is esp-adf**, arriving through the component manager rather than a submodule | **not MIT.** Does not enter this repository |
| `espressif/esp_audio_effects` `~1.3.0` — rate conversion | same Espressif Modified MIT, licence text byte-identical to `esp_audio_codec`'s; also `esp-adf-libs` | **not MIT.** Does not enter this repository |
| `espressif/esp_codec_dev` **1.x line** (`~1.5.6`) — the ES8311/ES7210 drivers | genuine **Apache-2.0**, full text, no extra clauses (sha256 `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` at 1.5.6, 1.5.10, 1.5.11, 1.6.1, 1.6.2 alike) | use and modify, with attribution — **the clean path.** See the 2.0 warning below |
| `waveshare/esp_lcd_sh8601` `==1.0.2`, `espressif/esp_lcd_co5300` `^2.0.3` — display, not audio | **Apache-2.0**, verified at the *pinned* versions, same sha256 as above | use and modify, with attribution |

No GPL, LGPL, AGPL or MPL anywhere in the tree — grepped for all of them, zero
hits. No git submodules: `.gitmodules` is absent and `git submodule status` is
empty. Only two licence files exist in the whole tree, `./LICENSE` and the
`gifdec` one.

**Strengths:** it targets **this** board, and it is the firmware the received
unit **shipped with** — a stronger claim than any vendor example in this ledger
can make, and it is why the record exists. MIT, so the licence closes nothing. No
submodules and no copyleft, so the tree can be read and copied without tracing a
graph first. The whole dependency footprint is legible from
`main/idf_component.yml` without opening a source file. The board's codec
bring-up goes through `esp_codec_dev`, which is the one audio dependency that is
cleanly licensed — the knowledge we want is on the clean side of the line.

**Weaknesses:** the audio *engine* is welded to components Attadipa may not
depend on — `main/audio/` includes `esp_afe_sr_models.h`, `esp_wn_iface.h`,
`esp_wn_models.h`, `esp_mn_iface.h`, `esp_mn_models.h`,
`esp_mn_speech_commands.h`, `esp_vadn_models.h`, `model_path.h` from esp-sr and
`esp_audio_enc.h`, `esp_opus_enc.h`, `esp_opus_dec.h`, `esp_ae_rate_cvt.h`,
`esp_audio_types.h` from esp-adf-libs. It is a whole application built around a
cloud voice assistant, not a component — its architecture answers a question
Attadipa is not asking. It is a **third-party application, not a vendor source
and not a schematic**, so nothing in it is a hardware fact on its own authority.
The wake-word model it ships is separately encumbered (below). And the licence
review covered **6 of roughly 60 manifest dependencies** — the audio path, plus
the two display drivers — which is the right scope for the decision taken here
and is **not** a full-manifest audit; anything that later proposes consuming
xiaozhi as a whole would have to start that audit from the beginning.

**Decision:** **two decisions, scoped separately.**

- `ADAPT` — `78/xiaozhi-esp32` itself. This is the **ceiling the licence
  permits**, recorded so nobody has to re-derive it. The verb actually exercised
  is settled by T-104 step 2 once the code has been read, and may honestly
  become the weaker `EXTRACT ALGORITHM` or `INSPIRE ARCHITECTURE` if nothing is
  literally taken. It is not `USE AS DEPENDENCY`: xiaozhi is an application, not
  a component.
- `REJECT` — `espressif/esp-sr`, `espressif/esp_audio_codec` and
  `espressif/esp_audio_effects`, **as dependencies of Attadipa**. Not on
  quality; on licence.
- `espressif/esp_codec_dev` **1.x** is licence-cleared for `USE AS DEPENDENCY`
  and nothing more is decided here — whether Attadipa takes it is a step-2 and
  implementation call, not a licence one.

**Reason.** The gate is passed for the repository and failed for three of its
four audio dependencies, and the two facts are independent: an MIT project that
calls a restricted component does not become restricted, and Attadipa reading
that MIT source is unaffected. The restriction is also wider than "avoid
esp-adf" — it arrives through the component manager under Espressif names, and
`esp_audio_codec`'s own registry metadata is what reveals it as esp-adf-libs. A
field-of-use restriction is not MIT; this ledger's rule is that anything
incompatible with MIT does not enter the repository, so those three do not.

**The precise constraint on `main/audio/` is functional, not legal, and the
distinction matters.** Those files are MIT and lawful to copy verbatim. They
will not *build* without the rejected components. So the useful thing to take is
the wiring and the codec sequence; the engine has to be left where it is. Take a
fact, not an architecture — this ledger's own rule about vendor examples,
applying with unusual force here because the shape of xiaozhi's `AudioService`
carries its dependencies with it.

**Three constraints that survive the licence being clean:**

1. **xiaozhi is corroboration, not the pin source.** The matrix already holds
   this board's I2S wiring as **VERIFIED** from the schematic and the physical
   unit — MCLK 16, SCLK 41, LCLK/WS 45, DOUT 40, DSIN 42; ES8311 at `0x18`,
   ES7210 at `0x40`. Where xiaozhi agrees, it corroborates a fact we already
   own. Where xiaozhi asserts something the matrix does not have — sample rates,
   MCLK ratios, register sequences, which microphone is which — that is a
   **lead**, `LIKELY` at best, and it must be cross-checked against the
   schematic or the Waveshare BSP before it is written down as `VERIFIED`.
   `CLAUDE.md` wants a datasheet, a schematic for this revision, or vendor
   source; a third-party application is none of the three.
2. **`esp_codec_dev`'s clean licence is a property of the 1.x line, not of the
   component.** The 2.0 line abandons Apache-2.0: `2.0.0-beta1/beta2/beta3` ship
   the Espressif Modified MIT text (sha256
   `c2e554675571a5370ea38c89131529d235db368fac12673cd5c569473f118d81`,
   "Copyright (c) 2023-2026") — the same field-of-use restriction as
   `esp_audio_codec`. The `~1.5.6` pin cannot reach it, so nothing is wrong
   today; **any bump past 1.x re-opens this question.** A related trap for a
   future auditor: registry *metadata* labels 1.5.11, 1.6.1 and 1.6.2 "Custom"
   while their `license.txt` is byte-identical Apache-2.0. The label is wrong;
   the file is the licence, and the file is what this ledger reads.
3. **T-104 step 2 is research.** Its deliverable is a `docs/research/`
   document citing file and line, not a pull request full of new subsystems.

**Wake words are not in scope, and identifying the vendor's firmware is not a
decision to ship one.** This repository has no such requirement and adding one
would be a product change — T-104 and
[WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §3 both say so. The licence
position is recorded anyway, so that nobody re-derives it in six months.
`wn9_nihaoxiaozhi_tts` is **not** vendored by xiaozhi: `main/CMakeLists.txt`
(1136–1138) locates the esp-sr component at build time and points
`ESP_SR_MODEL_PATH` at `${ESP_SR_COMPONENT_PATH}/model`, so the blobs are
packaged out of the dependency. Upstream the directory holds `_MODEL_INFO_`
(44 B), `wn9_data` (289 638 B) and `wn9_index` (1 200 B), which **corroborates**
— it does not exactly match — the owner's `model` partition, an esp-sr
`srmodels` container carrying two 32-byte name records, `wn9_nihaoxiaozhi_tts`
and `wn9_data`, with no sizes and no `wn9_index`: a packed container is not the
source directory. Redistribution is a **qualified no** for an MIT repository:
there is no `LICENSE` or `NOTICE` anywhere under `esp-sr/model/`, so the blobs
inherit the ESPRESSIF MIT field-of-use restriction and cannot be shipped under
Attadipa's MIT licence. A second, independent encumbrance sits on top of the
first: esp-sr's own README warns that wake-word names and brands belong to their
rights holders and that commercial use requires being, or being authorised by,
that holder. 你好小智 is the xiaozhi project's wake word and Attadipa is not its
rights holder.

**One question is flagged, not decided — `needs-owner`.** Both Attadipa targets
are Espressif silicon, so the Espressif-only field of use would in fact be
satisfied *in operation*. Whether Attadipa may ever consume esp-sr or
esp-adf-libs as pinned components on that basis — accepting that Attadipa's
source stays MIT while its **build** stops being redistributable to non-Espressif
targets — is a licence-policy call above a research task. It blocks nothing:
T-104 step 2 does not need it.

**Source revision:** `78/xiaozhi-esp32`
`bb9122ab08c3083eeb4f67b3974b7afe771723b8` ("Add streamed notify playback
(#2191)", 2026-08-21), read 2026-08-22. Dependency versions are those pinned by
`main/idf_component.yml` at that commit; their licences were read from the ESP
Component Registry's `license.txt` **at the pinned versions**
(`components-file.espressif.com/components/<ns>/<name>/<version>/license.txt`).
One provenance caveat, because "pin a revision" is this ledger's rule: the
esp-sr model-directory listing is **master provenance — esp-sr 2.5.1**, not the
pinned `~2.4.7`. GitHub has no `v2.4.7` tag (its tags stop at `v2.0.0`), so
2.4.7 is reachable only through the registry archive; the licence conclusion is
unaffected because the master `LICENSE` and the registry's 2.4.7 `license.txt`
are byte-identical.

**Attadipa integration:** none yet, and none of xiaozhi's code is in the tree.
What follows is the T-104 step 2 write-up: this board's audio path as a
`docs/research/` document citing file and line, feeding the `AUDIO_OUT` and
`AUDIO_IN` capability rows in [HARDWARE_MATRIX](HARDWARE_MATRIX.md) and whatever
resolves them behind the capability registry. The ES8311/ES7210 bring-up, if it
is taken as code rather than as knowledge, comes via Apache-2.0 `esp_codec_dev`
1.x, not via xiaozhi's engine. One entry in this ledger moves as a
result: `78/xiaozhi-esp32` leaves the "candidates identified, not yet evaluated"
table, where the licence column above is the answer to its "to check — **and the
check comes first**".

**Tests required:** the licence question needs no test and is answered. For
anything taken from it: I2S loopback and a codec-register readback on a
**physical** board — the ES8311 and ES7210 are I2C control slaves on the main
bus, so a bring-up that reports success without a register read has proved
nothing. Any pin, rate or register sequence sourced from xiaozhi is `LIKELY`
until an instrument or a schematic says otherwise, and none of it may be written
`PASS` until it has run on the unit. `NOT EXECUTED — HARDWARE REQUIRED` is the
honest status of every one of these today.

### Reading a C++ call expression well enough to police it

**Problem:** T-009's acceptance criterion is a property of the *source tree* —
no raw colour, pixel count or duration anywhere under `sim/`, `apps/` or `ui/`.
The first implementation read one physical line at a time, which quietly made it
a property of the *formatting* instead: `clang-format` at a narrow column splits
a call across four lines and the same raw pixel count stops being visible
([#68](https://github.com/hleserg/Attadipa/issues/68)). Fixing that means the
checker has to see a complete call expression, which is a parsing question.

**Projects investigated:**

| Candidate | Licence, at revision | Why it was not taken |
|---|---|---|
| `clang.cindex` — Clang's own Python bindings | Apache-2.0 **WITH LLVM-exception**, stated in the file header of `clang/bindings/python/clang/cindex.py` | The licence is fine and the parse would be authoritative. It needs a matching `libclang.so` **and the translation unit's include path** — which here means LVGL's headers. LVGL is behind `ATTADIPA_BUILD_SIMULATOR`, **OFF by default**, so on four of the five CI jobs the headers are simply not on disk. A checker that needs a dependency the primary gate does not fetch is a checker that degrades to a skip, and a skipped check reads as a passing one in a summary. |
| `pycparser` | BSD-3-Clause (`LICENSE`, "Copyright (c) 2008-2022, Eli Bendersky"); latest tag `release_v3.00` `77de509f0268f44ee587b5a4d9f0d680e269fcae` | Parses **C99 only, and only after a real preprocessor**. Everything it would be pointed at is C++ with templates, namespaces and `constexpr`. Not a close call. |
| `tree-sitter` + `tree-sitter-cpp` | MIT both (`tree-sitter/py-tree-sitter`, `tree-sitter/tree-sitter-cpp`); latest grammar tag `v0.23.4` `f41e1a044c8a84ea9fa8577fdd2eab92ec96de02` | The genuinely strong candidate: error-tolerant, needs no include path, ships wheels. Rejected on where it would sit rather than on quality — see below. |
| This repository's own bounded-scanner shape | — | `tools/l10n/`, `tools/assets/` and `tests/boundary/` are all "one focused checker plus a self-test that proves it still refuses things". Taken. |

**Decision:** `INSPIRE ARCHITECTURE` from the checkers already in `tools/`;
`REIMPLEMENT` the reading of a call as a bounded tokenizer in
`tools/ui/check_raw_values.py`. `REJECT` all three parsers, with tree-sitter
recorded as the one to reach for if the requirement ever grows.

**Reason:** `ui_no_raw_values` runs in *every* CI job and is the enforcement
half of a design invariant, so what it costs to run is part of what it is worth.
Adding a wheel to it would put the invariant behind an installed package, and
this repository has already decided that question in the other direction and
written down why: `tests/CMakeLists.txt` registers a **deliberately failing**
test when Pillow is missing rather than skipping the asset checks, and
[DEPENDENCIES](DEPENDENCIES.md) keeps the image *digest* check free of Pillow so
"the primary staleness gate never depends on a package being installed". The
same argument applies here and points the same way.

The second half of the reason is scope. The question the checker asks is not
"what does this program mean" but "did an integer literal reach an LVGL length",
and that is answerable without types, overloads or the preprocessor: blank the
comment and string bodies, find a known entry point, balance parentheses to the
end of its argument list, split on top-level commas, and look at the argument
the signature says is the value. Three passes, no grammar. A full C++ parse
would answer a much harder question than the one being asked, and the extra
capability is not free — it is a dependency, a version to pin, and a second
thing that can be wrong.

**Where the knowledge came from instead of the code:** LVGL v9.5.0 itself, at
the commit `cmake/AttadipaLvgl.cmake` pins and verifies —
`85aa60d18b3d5e5588d7b247abf90198f07c8a63`, checked out and confirmed by the
configure step's own `LVGL commit verified:` line. The inventory of which
entry points take a length, a duration or a colour, and at which argument
position, was read out of `src/core/lv_obj_style_gen.h`,
`src/misc/lv_style_gen.h`, `src/core/lv_obj_style.h`, `src/core/lv_obj_pos.h`,
`src/core/lv_obj_scroll.h`, `src/misc/lv_anim.h` and `src/lv_api_map_v9_1.h`
rather than recalled. That last file is why `lv_anim_set_time` is still checked:
in v9 it is a compatibility macro for `lv_anim_set_duration`, so both spellings
compile — and the old hand-written list knew only the compatibility one, which
meant the name a v9 screen would actually reach for was the one nobody checked.

**Weakness, stated rather than discovered:** the inventory is a list in a Python
file, so LVGL can grow a setter and the checker will not know. That is the price
of not depending on the headers at check time, and it is paid where it can be
seen — [DEPENDENCIES](DEPENDENCIES.md) carries re-deriving the inventory as a
step of an LVGL bump, beside retesting both geometries.

**Source revision:** no third-party code taken, so nothing to pin. The revisions
above are recorded so the rejections can be re-examined rather than re-argued.

**Attadipa integration:** `tools/ui/check_raw_values.py`, run by the
`ui_no_raw_values` test; `tools/ui/selftest.py`, run by
`ui_check_rejects_mistakes`.

**Tests required:** every negative fixture in both formattings — one line and
wrapped — because the whole defect was that those two disagreed. Plus the
positives that keep the rule from becoming "no numbers in UI code": a repeat
count, a rotation in tenths of a degree, a gradient stop, a flex weight and an
array index all pass, and are tests rather than comments. The self-test was
mutation-checked three ways: disabling the comment/string blanking, loosening
the colour rule back to where it would read `Rgb make_colour()\n{...}` as a
colour literal, and dropping the zero exemption each turn it red. No hardware —
this is a source-tree checker and touches no board.

---

### Remote UI testing: screenshot a running interface and inject input into it

**Problem:** an agent, or a person over ssh, has to be able to see what is on the
watch's screen and drive it the way a finger does — screenshot, look, tap or
swipe or press, wait, screenshot again. Owner request, 2026-08-23, filed as
[#117](https://github.com/hleserg/Attadipa/issues/117).

**Projects investigated:**

- **LVGL 9.5 itself**, at the revision this build pins
  (`cmake/AttadipaLvgl.cmake`). Two facilities are directly relevant and both
  are used rather than reimplemented:
  - `lv_snapshot_take(obj, LV_COLOR_FORMAT_RGB888)` (`src/others/snapshot/`) —
    re-renders an object tree into a fresh buffer. This is the "штатный
    screenshot API графической библиотеки" the request asks to prefer, and it
    is preferred.
  - `lv_indev_create` + `LV_INDEV_TYPE_POINTER` with a read callback, and
    `lv_indev_data_t::continue_reading` to hand LVGL a queue of transitions in
    one pass (`src/indev/lv_indev.c`: the read timer is created at `:132` with
    `LV_DEF_REFR_PERIOD`, and `:253-287` is the
    `do { read; process } while (continue_reading)` loop).
    Several indevs may be registered at once, **each with its own read timer
    and its own press state**, and each dispatches independently to whatever
    lies under its own point — which is what lets a person's SDL mouse and an
    injected touch coexist without either knowing about the other. LVGL does
    **not** arbitrate between them: `LV_STATE_PRESSED` is per widget, so a
    release from one clears the state the other is holding. An earlier version
    of this entry said "LVGL arbitrates" and cited the header; the word was
    wrong and the citation was to the API rather than to the behaviour.
    Read 2026-08-23.
- **LVGL's own `lv_test_indev_*` harness** (`tests/src/`) — rejected, and worth
  saying why. It drives LVGL's simulator inside LVGL's own unit-test build with
  a fake tick, which is the opposite of what is wanted: the point here is a
  *live* interface at real speed, in this project's binary, reachable from
  another process.
- **`esp_lcd`'s panel read-back path** — rejected on hardware grounds, not on
  preference. The Waveshare's AMOLED sits behind QSPI and the CO5300's read path
  is not established; [D7](OPEN_QUESTIONS.md) has not even settled its
  initialisation sequence. The request says explicitly not to read pixels back
  from an SPI display when the controller and wiring do not properly support it.
- **`espressif/esp-idf`'s `esp_console`** — considered as the command channel
  and rejected: it is a line-oriented text REPL over the same UART as the log,
  and a 617 kB binary image does not belong in one. The framing this project
  already has solves the interleaving problem properly.
- **Android's `adb shell input` / `screencap`** — studied as a *shape*, not as
  code (Apache-2.0, but it is an Android system service). Two things were taken
  as ideas: separating low-level `down`/`move`/`up` from convenience verbs like
  `tap` and `swipe`, and expressing a swipe as a duration rather than as a
  single event. Its worst property was avoided deliberately: `adb shell input
  swipe` synthesises the intermediate points *on the device*, which makes the
  host unable to control the gesture's speed profile.
- **`pytest-embedded`, `Appium`, `Squish`** — rejected as frameworks. The
  request says not to build a large test framework where the project already has
  one, and this project's runner is CTest. A scenario here is a data file CTest
  can point at.

**Useful implementation:** LVGL's snapshot and input-device APIs, used as APIs.
Nothing was copied.

**License:** LVGL is MIT (`LICENCE.txt` at the pinned revision), already a
dependency of the simulator target, and no new dependency was added. PyYAML is
optional and only for `.yaml` scenarios; JSON works without it. `pyserial` is
optional and only for a serial device, of which there is none yet.

**Strengths of reusing LVGL's own facilities:** `lv_snapshot_take` gives one
internally consistent frame, which reading a driver's partial draw buffers
cannot; and a second registered pointer device is the supported way to have two
sources of touch, so physical input keeps working while remote input is
connected.

**Weaknesses, recorded rather than discovered later:** `lv_indev_data_t` carries
**one** point, so the graphics stack is single-touch regardless of what any
panel can do. That is asserted about LVGL and about nothing else — whether the
Waveshare's controller can report two fingers is still open ([T-113], no FT3168
datasheet obtained). The wire format keeps a `touch_id` field and refuses a
second point rather than silently merging it.

**Decision: reuse LVGL's snapshot and input-device APIs; reuse this project's
own `link::frame_codec` framing and [ADR-0005](../adr/0005-node-protocol.md) §4
envelope; write our own message bodies.**

**Reason:** the framing question was already answered in this repository, and
answering it twice is how a project acquires two incompatible debug channels —
which the request forbids by name. `link::frame_codec` already provides exactly
what a debug stream sharing a link with a text log needs: resynchronisation on a
sync pattern that ASCII does not contain, a length checked before it is trusted,
a CRC over length and payload, and an over-long frame treated as an error rather
than truncated. The envelope's `class` field is the extension point ADR-0005
provided for precisely this.

The message **bodies** are ours and are fixed little-endian rather than TLV,
which is a deliberate narrowing rather than a departure: ADR-0005's TLV body is
recorded as provisional pending the encoding benchmark (T-016), and the debug
class must not pre-empt that decision. Its messages are fixed-shape and one of
them is high-volume — a screenshot is thousands of chunks, and a tag and a
length on every field of every chunk is overhead paid ten thousand times to
describe a layout that never varies. `class` and `ver` are what let the two
version independently.

`kMaxPayload` was **not** raised for screenshots. It is 192 bytes and
RESOURCE_BUDGET §4 requires the bound be declared; images are chunked to fit, so
every buffer in the system stays the size it was reviewed at.

**Source revision:** LVGL v9.5.0, the tag pinned in `cmake/AttadipaLvgl.cmake`
and reported by the build (`LVGL 9.5.0 at build/_deps/lvgl-src`). Read
2026-08-23: `src/others/snapshot/lv_snapshot.h`, `src/indev/lv_indev.h`,
`src/misc/lv_color.h` (which is where `LV_COLOR_FORMAT_RGB888` being **B, G, R**
in memory comes from — the fact the wire format names as `Bgr888` rather than
swapping silently).

**Attadipa integration:** `core/input.{h,cpp}` (the input layer, which did not
exist), `debug/` (protocol and bridge), `sim/remote_input.cpp`,
`sim/screen_source.cpp`, `sim/debug_server.cpp`, `sim/diagnostic_screen.cpp`,
`tools/watch/` and `tools/watch_control.py`.

**Tests required, and their status:**

- Protocol round trips, corruption, length disagreement, chunk reassembly,
  rate limiting, hold expiry, disconnect cleanup — `tests/test_debug.cpp`,
  **PASSING**, no device needed.
- The input queue and state machine, including every refusal —
  `tests/test_input.cpp`, **PASSING**.
- The host format, RGB565 and BGR888 conversion, orientation, PNG structure,
  non-zero exit codes — `tools/watch/selftest.py`, **PASSING**. Pinned to the
  same fixed byte literals as the C++ suite — the framing, a whole `HelloOk`, a
  whole `ScreenInfo`, a whole `InputEvent`, and both numbering tables written
  out — so the two independent implementations cannot drift into agreeing on a
  mistake. Until 2026-08-23 that covered the framing alone and the sentence said
  otherwise.
- The whole loop against the simulator — `tools/watch/e2e_test.py`,
  **PASSING**.
- **On a physical watch: `NOT EXECUTED — HARDWARE REQUIRED`, and it cannot be
  executed.** There is no Attadipa firmware, so nothing on the far end of a USB
  cable speaks this protocol. `SerialTransport` is written and has never spoken
  to a device.

---

### Proving a GraphQL connection was read in full

**Problem:** the unattended merge sweep decides whether a robot may write to
`main`, out of one GraphQL round trip — and every fact in it arrives in a
*connection*, which is a page. `reviewThreads(first:100)` over a pull request
with 101 threads returns a hundred, so `[ .nodes[] | select(.isResolved | not) ]
| length` answers **zero** when the unresolved one is the hundred-and-first, and
zero is the value that merges. Past the fiftieth label the same shape hid
`ai-review:blocking`; past the hundredth context, a failing check
([#170](https://github.com/hleserg/Attadipa/issues/170)). The question is
therefore not "how do we paginate" but "how does a caller *prove* it read the
whole set, in a way a test can execute".

**Projects investigated:**

| Candidate | Licence, at revision | Why it was not taken |
|---|---|---|
| GitHub's own `pageInfo { hasNextPage }` — the Relay Cursor Connections contract | the schema, no code taken | **Taken.** The schema already answers exactly the question being asked, per connection, and answers it about the *filtered* set |
| `gh api --paginate` | MIT (`cli/cli`) | Already used in this workflow for REST, and it does not apply: `--paginate` follows REST `Link` headers and GraphQL `pageInfo` **only for a query written to accept `$endCursor`** — one connection per query. Five connections in one document is exactly the shape it cannot walk |
| `octokit/plugin-paginate-graphql.js` | MIT; `octokit/plugin-paginate-graphql.js` | Real, maintained and the right tool for a Node action. This job is `bash` + `gh` in a sparse checkout with no `node_modules` and no `npm install` step, and it must stay that way: adding a package manager to a workflow holding `contents: write` on `main` is a bigger change than the defect it would fix |
| Hand-rolled pagination loops, one per connection | — | `REJECT` for now, with the reason recorded below rather than left as taste |
| `nodes \| length == first` as the truncation test | — | `REJECT`. It cannot tell an exactly-full page from a truncated one, so it either lets truncation through or holds every pull request landing on the boundary — guessing in both directions while the schema answers exactly. The workflow already had one instance of this (`FILE_COUNT >= 100`) and it is removed |
| `totalCount` as the truncation test | — | `REJECT`, and this one is not a matter of taste: on a **filtered** connection GitHub does not count the filtered set. Measured against this repository's #173 on 2026-08-24, `timelineItems(last:100, itemTypes:[LABELED_EVENT])` answered `totalCount: 15` beside a single node, while `pageInfo` on the same response respected the filter. A `length < totalCount` rule would have held every pull request in the repository, forever |
| This repository's own "the rule is a file, the workflow calls it" shape | — | Taken. `merge-candidate.sh`, `intake-decision.sh`, `queue-scan.jq`, `failure-count.jq` |

**Decision:** `USE AS-IS` the schema's `pageInfo`; `REIMPLEMENT` the completeness
check as `.github/scripts/merge-facts.jq` behind `merge-facts.sh`, in the shape
this repository already uses for every other decision an unattended workflow
makes; `REJECT` full pagination, for now and with a condition on when to revisit.

**Reason:** for an *unattended* gate the bounded fail-closed answer is the one
worth having. Paginating five connections adds request loops, partial-failure
states and a second way to be wrong, in order to raise a ceiling that the
three-per-run cap and a documentation-only path allowlist make almost
unreachable — a hundred labels or 101 review threads on a `docs/` pull request is
not a case to optimise, it is a case for an orchestrator session, which is where
everything off the allowlist already goes. The condition for revisiting is
written into `merge-facts.sh`: if refusals on truncation stop being rare, the
change is to paginate **there**, not to widen what counts as complete.

The second half of the reason is testability, which is why the filter is a file.
A query and a `jq` program inside a YAML block cannot be executed, so nothing can
assert what they ask for — and what this query asks for *is* the security
property. Both files are now driven by `.github/tests/merge-candidate-test.sh`
over documents shaped like GitHub's own replies, and the shapes were taken from
live responses rather than imagined.

**Where the knowledge came from instead of the code:** the responses themselves.
The query was run read-only against `hleserg/Attadipa` pull requests #173 and
#176 on 2026-08-24 before anything depended on it, which is what established
three things that were otherwise assumptions: that `totalCount` ignores
`itemTypes` while `pageInfo` respects it; that `statusCheckRollup` is **null**,
not empty, on a head commit with no checks at all; and that a `last:`-only
connection answers `hasNextPage: false`.

**And the mechanism the caller depends on, which is `gh`'s and not GitHub's.**
The parked workflow half submits the query as `gh api graphql -F
query=@.github/scripts/merge-facts.graphql`, and nothing on `main` executes that
form: the filter takes a document on stdin, so the suite never reaches `gh`, and
the only place the flag appears is inside the patch. It was therefore run
directly, read-only, against `hleserg/Attadipa` #176 on 2026-08-24 — the exact
invocation from the patch, with the three variables **bound as literals**.
The caller's own `${REPO%%/*}` and `${REPO##*/}` expansion is **NOT EXECUTED** —
it is the one link in this chain that nothing exercises, and this run did not
exercise it either. Exit 0, one complete document, every connection present.
So `-F query=@FILE` does read a file and does bind alongside the other `-F`
variables, which was previously asserted only in a pull request body.

The same reply re-established two of the three facts above on a **different**
pull request than the one they were found on: `statusCheckRollup` came back
`null` on #176's head commit, which has never had a check run at all, and
`timelineItems` answered `totalCount: 6` beside `nodes: []` under
`itemTypes: [LABELED_EVENT]` — the count ignoring the filter that the nodes
respect, in one document.

What this does **not** establish is the filter's verdict over that document.
`jq` is absent on the host that ran it, so `merge-facts.sh` answered *HOLD the
pull request's facts could not be parsed* — its designed fail-closed answer to a
`jq` that will not run, and a statement about the host rather than about the
reply. The verdict over a live document is `NOT EXECUTED` until CI or the sweep
itself runs one.

That third one was first written down as evidence that the flag is the right one
to assert on the timeline. It is not, and the observation could not have shown
it either way: #173's filtered connection held a **single** node, nowhere near
the limit, so `false` there cannot be told apart from `false` by construction.
Under the Relay contract `hasNextPage` is true only when paginating forward with
`first:` or when `before:` is set, so on a `last:`-only page it is a constant and
the completeness refusal on that connection can never fire. What the timeline is
actually safe on is downstream: an event outside the window is older than
everything in it and cannot raise the maximum, so a truncated window yields no
date and the caller holds. Recorded as a correction rather than edited away,
because this file is what the next agent is told to trust.

**Weakness, stated rather than discovered:** a pull request that genuinely
exceeds a page is now unmergeable by the sweep rather than merged wrongly, and it
will say so every half hour until a person looks. That is the intended direction
and it is still a cost. No hardware — this is repository automation and touches
no board.

---

### Checking a partition table against the 16 MB addressing ceiling

**Problem:** on the Waveshare's 32 MB part, `0x1000000` aliases to `0x0` for the
bootloader — measured, on the unit. Any partition table this project writes has
to be held against that line, and
[WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §1.4 already said the
uncomfortable part out loud: *"A partition table is not self-validating. `ota_1`
is well-formed, correctly sized and correctly typed. It is also dead."* So the
question is not whether to write a partition-table validator — there is a large,
Apache-2.0, battle-tested one in ESP-IDF — but whether the check we actually need
is inside it.

**Projects investigated:**

| Candidate | What it is | What it does about the ceiling |
|---|---|---|
| `espressif/esp-idf` `components/partition_table/gen_esp32part.py`, Apache-2.0 | **The** partition-table tool: parses the CSV, validates types, subtypes, alignment, overlap, uniqueness, size against `CONFIG_ESPTOOLPY_FLASHSIZE`, and emits the binary | **Nothing.** It has no notion of an addressing ceiling below the flash size, because on every part Espressif supports the whole part is nominally addressable. It would pass the vendor's own table, `ota_1` and all — and the vendor's table is the proof, since it was generated by this tool |
| `espressif/esp-idf` `components/esptool_py/esptool/esptool.py` | flashes and verifies | Refuses > 16 MB **only** on the ROM loader path; the stub addresses 32 bits and verified a write to `0x1000000` on this unit. So it is not a check either — it is the thing that made the defect invisible |
| ESP-IDF's `check_sizes.py` / build-time size checks | app image vs partition size | Orthogonal. Answers "does the image fit", not "is the partition reachable" |

**Useful implementation:** the CSV dialect, from `gen_esp32part.py` — five or
six comma-separated fields, `#` comments, `K`/`M` suffixes, hex offsets — and
the two alignment rules ESP-IDF enforces (4 KiB sectors everywhere, 64 KiB for
app partitions). Read from the upstream source, not guessed.

**License:** Apache-2.0. Nothing is copied, so nothing is inherited.

**Strengths (of the rejected option):** `gen_esp32part.py` is correct, exercised
by every ESP-IDF project in existence, and does far more than we do.

**Weaknesses:** it does not do the one thing this repository needs, and it
cannot be run here at all — there is no ESP-IDF project in this tree yet
([#127](https://github.com/hleserg/Attadipa/issues/127) opens by saying so), so
there is no build step for it to be part of. A check that only exists inside a
build we have not started is not a check.

**Decision:** `WRITE OUR OWN`, deliberately small —
`tools/flash/partition_check.py`, and it says in its own docstring that it is
**not** a replacement for `gen_esp32part.py`. When the firmware project exists,
both run: ours first, because it is the one that knows about this board.

**Reason:** the check we need is absent from the only upstream candidate, and
the ceiling is a property of *this board* that no general tool will ever grow.
The overlap with `gen_esp32part.py` is kept as narrow as the job allows — sector
alignment, overlap and flash-size overflow are duplicated only because nothing
else will run until there is a firmware build, and the docstring says so rather
than letting a future reader discover two sources of truth. One deliberate
divergence: **blank offsets are refused rather than computed**, because an
implicit offset is exactly how a table drifts across the line with nothing in
the diff to show for it.

**Source revision:** ESP-IDF `v5.5.5`, read at that tag for the CSV dialect, the
alignment rules and the whole of
[FLASH_ADDRESSING_LIMITS](FLASH_ADDRESSING_LIMITS.md)'s path trace.

**Attadipa integration:** `tools/flash/partition_check.py`, run by the
`flash_partitions_below_ceiling` test; `tools/flash/selftest.py`, run by
`flash_partition_check_rejects_mistakes`.

**Tests required:** the boundary as four separate cases — a partition that ends
exactly on the line and must pass, one that starts on it, one wholly above it,
one that crosses it — plus 32-bit overflow, flash-size overflow, both alignment
rules, overlap, and five malformed rows that must be errors rather than skips.
And the vendor's own shipped table as a fixture, which must be refused naming
both `ota_1` and `storage` and neither of the six partitions below the line. The
self-test was mutation-checked four ways: `>=` loosened to `>` at the ceiling,
the crossing check removed, the crossing check tightened so that ending exactly
on the line fails, and unparseable rows silently skipped — each turned it red.
No hardware; this reads CSV files and touches no board.
