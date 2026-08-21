# Open Questions

Everything the project needs to know and does not. Each entry names what would
resolve it, so answering is a task rather than a search.

Status: **UNKNOWN** (no source) · **CONFLICTING** (sources disagree) ·
**ASSUMPTION** (plausible, unconfirmed, must stay flagged in code) ·
**RESOLVED** (moved to [VERIFIED_FACTS.md](VERIFIED_FACTS.md)).

An UNKNOWN that blocks work is a blocker — record it in
[../../TASKS.md](../../TASKS.md) in the blocker format rather than coding past it.

The board survey of 2026-08-21 resolved most of the *documentary* questions.
What remains is dominated by one thing: **no physical board has been touched.**
Everything that needs a measurement is still open, and no amount of reading
will close it.

---

## Blocking everything measurable

| # | Question | Status | Resolved by |
|---|---|---|---|
| A1 | Does the developer have either board physically, and which revision? | **UNKNOWN** | ask the project owner |
| A2 | If a T-Watch is present: which of the five radio chips, and which of the two GNSS modules? | **UNKNOWN** | inspect the unit / order details |
| A3 | Is there a second radio-capable device, so mesh can be tested at all? | **UNKNOWN** | ask the project owner |
| A4 | Which regulatory region governs LoRa operation here? | **UNKNOWN — now concrete** | ask the project owner. The owner's existing MeshCore node runs 868.731 MHz at 22 dBm ([OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-2). 22 dBm is 158 mW; whether that is lawful on that frequency in the region of operation is unestablished |
| A5 | **Is an external magnetometer intended at all?** Neither board has one, so every compass feature in the plan currently has no hardware to run on | **UNKNOWN** | ask the project owner — see [../hardware/MAGNETOMETER_BACKLOG.md](../hardware/MAGNETOMETER_BACKLOG.md) |
| A6 | **Does the Firefly node carry a magnetometer?** | **UNKNOWN** | ask the project owner. Note that "yes" does *not* give the watch a compass: a node's magnetometer measures the node's orientation, and [ADR-0009](../adr/0009-heading.md) refuses to present `NodeBody` heading as `WatchBody` heading without a known, calibrated, still-valid transform. The ADR exists so that this answer does not arrive before the model does |
| A7 | **Which orange, and which olive?** The published brand art (`pics/`) and the canonical palette in final §42 disagree: the wordmark and wings sample at `#E16439`…`#EC552A` against Firefly Orange `#FF8A40`, and the head and tagline at `#595E3A`…`#666A46` against Ink Olive `#2F3A2E` | **UNKNOWN — conflict recorded** | the project owner. This is identity, not engineering. [`../../pics/README.md`](../../pics/README.md) holds the sampled values; [`../ui/DESIGN_SYSTEM.md`](../ui/DESIGN_SYSTEM.md) keeps the §42 values until this is answered. Whichever wins, the loser's values must leave the repository rather than sit beside them |
| A8 | **May the icon and favicon be re-exported with transparent corners?** Both are RGB with no alpha, so the area outside the rounded square is opaque black — visible on any non-black page or launcher background | **UNKNOWN** | the project owner. A mechanical conversion, but it alters supplied art, so it has not been done |

A1 and A2 gate all bring-up, the entire interference matrix, and every power
number. A2 in particular decides whether the radio is sub-GHz or 2.4 GHz —
which changes region rules and mesh interoperability, not just a driver.

A4 is not a preference. Which frequencies, power levels and duty cycles are
lawful is set by the region the device operates in, and the answer changes what
the radio may legally do. It has to be settled before anything transmits.

A4 stopped being theoretical on 2026-08-21. The owner's own node is already on
air at 868.731 MHz and 22 dBm. Firefly is not responsible for that node — but
the numbers it ships as *defaults* are Firefly's responsibility, and a default
cannot be chosen before A4 is answered. Note also that A4 no longer decides what
the core is built to do: per OD-2 these are settings, so the core is built to
carry a bounded, user-settable value either way. A4 decides the bounds and the
default, which is a smaller question than it was — but a legal one still.

A5 decides whether five epics in §67 are dormant or dead.

Until these are answered: simulator, architecture, host tests and protocol work
proceed; hardware work does not.

## Hardware — measurement required

| # | Question | Status | Resolved by |
|---|---|---|---|
| H1 | Real power draw of Firefly firmware per state, per board | UNKNOWN | measurement; vendor figures are a target, not evidence |
| H2 | Can the AXP2101 measure current/energy on these boards, or only voltage? | UNKNOWN | AXP2101 datasheet + schematic sense-resistor check |
| H3 | Real TTFF and fix quality for the fitted GNSS module | UNKNOWN | outdoor measurement |
| H4 | Does any of the suspected interference actually occur? | UNKNOWN | the measurement procedure in [../hardware/INTERFERENCE_MATRIX.md](../hardware/INTERFERENCE_MATRIX.md) |
| H5 | Which wake sources are usable in practice, and what does each cost? | UNKNOWN | measurement; vendor table gives the shape |
| H6 | AMOLED brightness vs power on the Waveshare board | UNKNOWN | measurement |
| H7 | Achievable LVGL frame rate and redraw cost on each panel | UNKNOWN | benchmark on hardware |
| H8 | **Is ALDO1 the `+3V3` rail?** The vendor doc says ALDO1 is unused; the schematic shows it driving `+3V3` | **CONFLICTING** | read the AXP2101 rail-enable and voltage registers on a powered board, then cut one rail at a time and watch which parts drop off the I2C scan |
| H9 | Real backlight current vs brightness, against the schematic's 45 mA at full | UNKNOWN | measurement; the 45 mA figure is a datasheet-level I_F, not a measured draw |
| H10 | **The speed gate below which GNSS course-over-ground is not trustworthy** | UNKNOWN | measurement on the fitted module. It depends on the update rate and on whether the module reports Doppler-derived velocity or differenced positions, so it is per-module and cannot be chosen. [ADR-0009](../adr/0009-heading.md) §4; final §26 forbids inventing settling intervals |

## Radio

| # | Question | Status | Resolved by |
|---|---|---|---|
| R1 | **Confirm every modulation, band and conducted-power figure in the radio matrix against the manufacturer datasheet.** The current values come from RadioLib 7.7.1's driver range checks and MeshCore's build config, not from TI, Silicon Labs or Semtech | **PARTIAL** | `ti.com` returns HTTP 403 and the Silicon Labs document host timed out under automated retrieval. Needs a manual fetch, or the PDFs obtained another way. Nothing may transmit on the strength of a number in that table until this is closed — [ADR-0003](../adr/0003-radio-not-lora.md) |
| R2 | Does the LR1121 work through MeshCore's `CustomLR1110Wrapper` plus `LR11x0Reset.h`? RadioLib's `LR1121` derives from `LR1120`, which derives from `LR11x0`, so it is plausible | **UNKNOWN** | a spike, not a reading. Decides whether `MeshCoreSupport::NeedsWork` for that chip is a week or a month |
| R3 | Which radios MeshCore supports **at the revision Firefly actually pins**, re-checked rather than assumed | tracked | the matrix is a `grep` over `RADIO_CLASS` across `variants/`; it is a task (T-013), not a hope, because upstream adds radios |

## Hardware — documentary gaps

| # | Question | Status | Resolved by |
|---|---|---|---|
| ~~D1~~ | ~~Waveshare flash and PSRAM size~~ | **RESOLVED** | schematic: `GD25Q256EYIGR` = 32 MB quad flash; SoC is `ESP32-S3R8` = 8 MB PSRAM. Type of PSRAM rolls into D12 |
| D2 | Waveshare battery capacity and charge path details | UNKNOWN | schematic + product page |
| D3 | Waveshare expansion connector pinout | PARTIAL | schematic shows header `J3` with ≥ 29 pins; the pinout needs the sheet read visually, not by text extraction |
| ~~D4~~ | ~~Does the Waveshare board have any haptic output?~~ | **RESOLVED — and the earlier answer was wrong** | **Yes.** A vibration motor on connector J1, driven from GPIO 18 through R12 (4.7 kΩ) and Q1 (MMBT3904), supplied from BLDO2. No driver IC — which is why searching for a haptic part found nothing |
| D5 | Waveshare button/wake inputs — BSP declares none; is that the board or the BSP? | **PARTIAL — it is the BSP** | schematic shows at least two tactile keys (`Key1` by `BOOT`, `Key3`) plus `PWRON`. Which GPIO each key uses is still unresolved |
| D6 | T-Watch: which PMU rail powers GNSS on the *specific* unit (BLDO1 vs DC3) | UNKNOWN | inspect the unit for rear BOOT/RST buttons |
| D7 | Exact ST7789V3 and CO5300 init sequences and their timing | UNKNOWN | vendor driver source |
| D8 | Is the T-Watch main I2C bus shared with anything timing-sensitive? | PARTIAL | schematic read: five devices confirmed on SDA 10 / SCL 11, plus a possible sixth — see D9. Timing sensitivity still needs driver review |
| D9 | **Does the GNSS daughterboard connect the `MIA-M10Q` `SDA`/`SCL` to the FPC?** If it does, the GNSS is a sixth device on the main I2C bus at 0x42 | UNKNOWN | trace the daughterboard FPC net list, or scan the bus on a board with the module fitted |
| D10 | **What is radio `DIO3` (GPIO 6) for on this board — TCXO supply or a second interrupt?** | UNKNOWN | HPD16B3 module datasheet + the vendor radio driver's `setDio3AsTcxoCtrl` usage |
| D12 | **Is the PSRAM quad or octal — on *both* boards?** Both carry an `ESP32-S3R8` marking, so this is one question with one answer. The T-Watch vendor doc says QSPI; Espressif's published part-numbering scheme is understood to use the `R8` suffix for octal PSRAM — **that last part is recollection and must itself be checked against the datasheet**. Different `sdkconfig`, ~2× bandwidth difference | **CONFLICTING** | `esptool.py flash_id` / `esp_psram` probe on hardware, or the SoC datasheet against the exact part marking. Blocks the LVGL buffer ADR — see [../architecture/RESOURCE_BUDGET.md](../architecture/RESOURCE_BUDGET.md) |
| D13 | Waveshare: which loads sit on ALDO1, ALDO2 and ALDO3 — all three are 3.3 V — and **what runs on the 1.8 V ALDO4 rail**? | UNKNOWN | read the schematic sheets visually |
| D14 | Waveshare SD card: the BSP uses SDMMC 1-bit on GPIO 1/2/3, but the schematic labels those nets `MOSI`/`SCK`/`MISO` and shows a chip-select near GPIO 17. Which mode is the board actually wired for? | UNKNOWN | schematic sheet + BSP source |
| D11 | Which AXP2101 rail is the schematic's net `LDO5`? It feeds DRV2605 `EN`, and the vendor rail map says BLDO2 — consistent but not proven | UNKNOWN | PMU register read on hardware |

## MeshCore

Answered on 2026-08-21 by reading the source at commit
**`d92964352441e53b93e8667b802e04f6e072b39e`** (branch `main`; tags
`companion-v1.17.1`, `repeater-v1.17.1`, `room-server-v1.17.1`). Every claim
below cites the file it came from. Licence: **MIT**, `license.txt`.

| # | Question | Status | Answer |
|---|---|---|---|
| ~~M1~~ | Architecture and integration points | **RESOLVED** | Arduino/PlatformIO throughout. There is no `CMakeLists.txt` and no `idf_component.yml` anywhere in the tree; `BaseSerialInterface.h` and `ContactInfo.h` include `<Arduino.h>` directly, and helpers depend on `Stream`, `File` and `HardwareSerial`. Clean dependency injection at the core: `mesh::Radio` is a pure-virtual interface in `src/Dispatcher.h:20-79` |
| ~~M2~~ | Which revision to pin | **RESOLVED — candidate** | `v1.17.1`. `origin/dev` is 29 commits ahead of `main` at that tag, and upstream asks for PRs against `dev`, so a pin to `main` at a release tag is the stable choice |
| ~~M3~~ | Crypto primitives and byte-level format | **RESOLVED — and it needs a review of its own** | Payload encryption is **AES-128 in ECB mode with zero padding**, on both the hardware (`Utils.cpp:61,92`) and the software path (`Utils.cpp:108-122`, `aes.encryptBlock` per 16-byte block, no IV, no chaining). Authentication is HMAC-SHA256 **truncated to two bytes** — `CIPHER_MAC_SIZE 2` in `MeshCore.h:17`, applied in `Utils.cpp:127-145`. Wire constants: `PUB_KEY_SIZE 32`, `CIPHER_KEY_SIZE 16`, `MAX_PACKET_PAYLOAD 184`, `MAX_PATH_SIZE 64`. On-air layout is `Packet.cpp:55-85` |
| ~~M4~~ | Threading and concurrency assumptions | **RESOLVED** | Cooperative single-loop, Arduino style. `CONTRIBUTING.md` requires no dynamic allocation outside `begin`/`setup`; fixed pools in `StaticPoolPacketManager.h`. The one FreeRTOS boundary is the BLE interface, guarded by a static queue (`src/helpers/esp32/SerialBLEInterface.h:24-35`, `FRAME_QUEUE_SIZE 4`) |
| M5 | Memory footprint on ESP32-S3 | PARTIAL | Fixed pools and `MAX_PACKET_HASHES (128+32)` in `SimpleMeshTables.h` make it computable, but no figure is claimed here without a build. `NOT MEASURED` |
| ~~M6~~ | How it abstracts the radio, and whether it covers all five T-Watch chips | **RESOLVED — and the answer was worse than expected** | Through thin wrappers over RadioLib in `src/helpers/radiolib/`. Across 87 upstream variants the `RADIO_CLASS` set is `CustomLR1110 · CustomLR2021 · CustomSTM32WLx · CustomSX1262 · CustomSX1268 · CustomSX1276` — of the five T-Watch candidates, **only the SX1262**. CC1101 is compiled out entirely (`platformio.ini:35`, `-D RADIOLIB_EXCLUDE_CC1101=1`). **Correction to an earlier version of this row**, which said RadioLib supports every chip MeshCore does not and concluded the gap is a small wrapper layer: RadioLib *drives* CC1101 and Si4432, but as **FSK/OOK** parts. Neither has a LoRa modulator, so no wrapper makes them mesh-capable. The gap is a wrapper for SX1280 and LR1121 only. [ADR-0003](../adr/0003-radio-not-lora.md) |
| ~~M7~~ | Companion protocol shape | **RESOLVED — and it largely already exists** | A framed byte protocol, identical across every transport. `>`/`<` sentinel, 16-bit little-endian length, payload; `MAX_FRAME_SIZE 176` (`BaseSerialInterface.h:5`). Payload is `[opcode][data]`, little-endian. The opcode table is `examples/companion_radio/MyMesh.cpp:6-134`. **Version negotiation already exists**: `CMD_DEVICE_QUERY` (22) carries the client's protocol version, the firmware stores it as `app_target_ver` and adapts its replies (`MyMesh.cpp:1023-1024`, and see the `app_target_ver >= 3` branches at 435 and 548) |
| M8 | Can Firefly's needs be upstreamed rather than forked? | **likely yes** | The radio-wrapper gap (M6) is the natural candidate. Requires talking to upstream, which has not happened |
| ~~M9~~ | **Does MeshCore assume it owns the radio exclusively?** | **RESOLVED — effectively yes** | `src/helpers/radiolib/RadioLibWrappers.cpp:14` is `static volatile uint8_t state = STATE_IDLE;` — a **file-static** flag set from the ISR. One radio per firmware image, structurally. It also runs its own duty-cycle governor, `Dispatcher::updateTxBudget()` (`Dispatcher.cpp:38-53`), which a Firefly coexistence coordinator would have to reconcile with rather than override. The sanctioned extension points are the virtual hooks `getCADFailMaxDuration`, `getCADFailRetryDelay`, `getAirtimeBudgetFactor` in `Dispatcher.h`, and `isReceiving()` in `RadioLibWrappers.h:44-48` |

**M9 matters less on one path and exactly as much as feared on the other.**
The concern was that a mesh stack owning the radio exclusively could not coexist
with a coordinator scheduling quiet windows around Wi-Fi and BLE. It does own it
exclusively. When the radio is in a **separate device**, the watch speaks the
companion protocol to a node and the conflict does not arise — a product
decision dissolving an engineering problem rather than solving it.

> **Corrected 2026-08-21.** What stood here went one step further and concluded
> that the watch therefore *never* runs MeshCore. That does not follow. A
> T-Watch with a supported radio is a local mesh device (final §13), and on that
> path M9 is a live constraint: whether `HardwareCoordinator` can schedule
> around MeshCore's radio ownership, or must stay out of its way, is part of the
> integration spike rather than an assumption. See
> [ADR-0008](../adr/0008-mesh-service-providers.md).

Also relevant on the local path: MeshCore runs its own duty-cycle governor,
`Dispatcher::updateTxBudget()`, which Firefly's airtime accounting must
reconcile with rather than override.

### What reading MeshCore surfaced that nobody asked

| # | Finding | Evidence | Status |
|---|---|---|---|
| M10 | **The payload cipher is AES-128-ECB.** Identical plaintext blocks under one key produce identical ciphertext blocks, so equality of messages leaks even when content does not | `src/Utils.cpp:61,92` (CC310 path) and `:108-122` (software path) | **read from source** — implications for Firefly not yet assessed |
| M11 | **The message authentication tag is 2 bytes.** One in 65 536 per forgery attempt, so the security of the tag rests on limiting attempts rather than on the tag | `MeshCore.h:17`, `Utils.cpp:127-145` | **read from source.** The owner's own node exposes a "Request Rate Limiter" — the two facts may well be related, and that is worth confirming rather than assuming |
| M12 | **`ed25519_verify` from the vendored `orlp/ed25519` is disabled upstream** with the comment *"memory corruption bug was found in this function!!"*. The active path uses `Ed25519::verify` from `rweather/Crypto` instead | `src/Identity.cpp:34-36` (`#elif 0` branch) | **read from source** |
| M13 | **There is almost no test coverage of the parts Firefly depends on.** Seven test binaries, none touching crypto or wire format; `test/mocks/AES.h` is a no-op stub and `test/mocks/SHA256.h` is self-described as *"deterministic but not cryptographic"* | `test/` | **read from source.** Consequence: there are no reference vectors to port. The only usable one in the repository is the known-good keypair embedded in `Identity.cpp:68-110` |
| M14 | **`rweather/Crypto` licence is unverified.** MeshCore resolves it through PlatformIO as `rweather/Crypto @ ^0.4.0`; it is not in this project's clones and its licence file has not been read | `platformio.ini:24` | **UNKNOWN — must be checked before anything depends on it** |

M10 and M11 are recorded as facts, not as accusations. MeshCore is solving a
different problem under tighter constraints, and a two-byte tag on a
duty-cycle-limited sub-GHz link is a defensible trade against airtime. But
Firefly's specification treats security as something that must be strengthenable
without breaking the architecture (§74 item 24), and a protocol whose
authentication rests on rate limiting is a protocol whose rate limiter is a
security control rather than a convenience. That belongs in an ADR of its own,
with someone competent reviewing it — not in a paragraph here.

## Architecture

| # | Question | Status | Resolved by |
|---|---|---|---|
| X1 | How does a capability express **variant** (which of five radios) and **degree** (accel-only vs 6-axis)? | **RESOLVED** | It does not — that is the wrong layer to ask. Variant and degree are facts about a *part* and live in the hardware inventory, below the service boundary; a product capability carries only an availability state. [ADR-0007](../adr/0007-two-capability-layers.md) |
| X2 | Who owns PMU rail sequencing — a rail service, or each driver? | UNKNOWN | ADR |
| X3 | How does an application render each of the seven availability states — and in particular tell *unsupported here*, *needs a node*, *node out of range* and *broken* apart? | **narrowed** | [ADR-0004](../adr/0004-capability-sources.md) sets one state per remedy; the screens themselves are still UX + API design together |
| X4 | Two RTC parts, two IMU parts, two audio paths — one interface each, or per-board? | UNKNOWN | driver design |
| X5 | Does the coexistence coordinator earn its complexity on boards with no measured interference? | UNKNOWN | H4 — build the measurement first, the mitigation second |

## Toolchain and dependencies

| # | Question | Status | Resolved by |
|---|---|---|---|
| T1 | Which ESP-IDF version to target | **narrowed** | Waveshare supports v5.5.5 and v6.0.2; its BSP needs ≥5.3. Decide with the LilyGO side. |
| T2 | Which LVGL major version | **narrowed** | Waveshare BSP accepts `>=8,<10`; LVGL 9 is the forward choice. Confirm simulator support. |
| T3 | Is RadioLib needed, or does MeshCore bring its own radio layer? | UNKNOWN | M6 |
| T4 | Simulator display backend | UNKNOWN | follows T2; SDL2 not currently installed |
| T5 | Host test framework | UNKNOWN | small decision, no ADR needed |
| T6 | Use the Waveshare BSP as a dependency, or take only its pin facts? | UNKNOWN | it is Apache-2.0 and incomplete — a reuse-ledger decision |
| T7 | Does the LilyGO PlatformIO pin to IDF 4.4.7 constrain Firefly? | ASSUMPTION: no | Firefly is ESP-IDF-native and does not use the Arduino layer |

## Product

| # | Question | Status | Resolved by |
|---|---|---|---|
| ~~Q1~~ | ~~What should the Waveshare board *be*, given it cannot do mesh or navigation?~~ | **RESOLVED** | [OWNER_DECISIONS.md](OWNER_DECISIONS.md) OD-1. The premise was wrong: it cannot do mesh or navigation *on its own*. With a Firefly node attached it runs the same applications as a LoRa watch; without one it is a watch, an audio device, and whatever the installed applications make it |
| Q2 | Is a magnetometer expected to be added externally, or is heading GNSS-only for good? | UNKNOWN | product decision by the owner |
| Q3 | Realistic battery-life target | UNKNOWN | measurement, after bring-up |

Q1 was a genuine product question, not an engineering one, and it was answered
on 2026-08-21 in a way that reframed it. The board is not a lesser device that
needs a purpose found for it; it is a device whose mesh and navigation arrive
over a link instead of over a bus. What was a gap in the product is now the
strongest argument for the capability model: two boards that share almost no
hardware run the same applications, because applications ask what the device can
do and never which device it is.

Q2 is the part of the compass question that OD-1 did *not* answer, and it got
sharper. The owner named "компас" among the applications the node enables. No
board has a magnetometer. Either the node carries one — which would answer both
Q2 and A5 — or "compass" means GNSS course-over-ground, which only works while
moving and shows nothing at all when the user stands still. Those are different
products and the difference is visible to the user in the first ten seconds.

---

## Recently resolved

Moved to [VERIFIED_FACTS.md](VERIFIED_FACTS.md) on 2026-08-21: both boards'
complete peripheral inventory, pin maps, I2C addresses and PMU rail map; the
absence of a sub-GHz radio and GNSS on the Waveshare board; the absence of a
magnetometer on both; the five radio and two GNSS variants of the T-Watch; the missing touch
reset line; the haptic rail gating; the incomplete vendor BSP; and the
CO5300 / SH8601 driver nuance.
