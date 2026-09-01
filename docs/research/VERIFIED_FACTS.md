# Verified Facts

Facts that have been traced to a primary source. Nothing here may be recorded
from a plan document, a blog post, or a plausible-looking library — only from a
datasheet, a schematic for a named board revision, vendor documentation, or the
upstream source itself.

Every entry must carry: the claim, the primary source, the date checked, and —
for hardware — the exact board revision it applies to.

An entry that cannot name its source does not belong here. It belongs in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).

---

## Software / upstream

### MeshCore upstream is `github.com/meshcore-dev/MeshCore`

- **Claim:** the canonical MeshCore repository is `meshcore-dev/MeshCore`.
  The older path `ripplebiz/MeshCore` still resolves but redirects there.
- **License:** MIT (read from upstream's `license.txt`; the GitHub API agrees).
- **Source:** GitHub API `repos/ripplebiz/MeshCore` returns
  `full_name: meshcore-dev/MeshCore`.
- **Checked:** 2026-08-21.
- **Note:** the repository was actively pushed to on 2026-08-20, so the API
  surface should be treated as moving. A specific revision must be pinned
  before integration work starts — see
  [DEPENDENCIES.md](DEPENDENCIES.md).
- **Not yet verified:** protocol details, crypto primitives, threading model,
  memory requirements, LoRa abstraction, or the companion protocol. None of
  these have been read from source yet.

### The pinned MeshCore revision is upstream's current release, not a lagging one

- **Claim:** `d92964352441e53b93e8667b802e04f6e072b39e` is simultaneously
  Attadipa's pin, the tip of `meshcore-dev/MeshCore`'s `main` **as of 2026-08-23**,
  and the newest release (`companion-v1.17.1`, `repeater-v1.17.1`,
  `room-server-v1.17.1`, published 2026-08-14). `dev` was at
  `9d7cee66394fffd6e8c6e9f39fe03660cb314f64`, 2026-08-22.
- **Source:** GitHub API `repos/meshcore-dev/MeshCore/branches/{main,dev}`,
  `/releases` and `/commits?per_page=1`.
- **Checked:** 2026-08-23, independently the same day by two pieces of research
  that needed it for different reasons — the parser-bounds work
  ([#142](https://github.com/hleserg/Attadipa/issues/142)) and the BLE
  frame-capacity work ([#143](https://github.com/hleserg/Attadipa/issues/143)) —
  which agreed.
- **Amended 2026-08-24:** `main` has moved to
  `0679dbeffc504d562d2f09eb072fdc223f8ffc2a`, two commits ahead, and
  `compare/<pin>...main` lists exactly one file: `docs/faq.md`. So the pin is **no
  longer the tip** and is still upstream's newest **code** and newest release.
  `dev` is `12998cba8969e4004d94ed94b5e8e5bbdfa05571`. The two halves are recorded
  separately because only one of them ages.
- **Note:** `pushed_at` is more recent than either, because it counts pushes to
  any branch. It is not a claim about `main`.
- **Consequence for the BLE frame-sizing finding:** there is no superseding
  upstream fix, because vanilla has no such concept to fix.
- **Why it is here:** three open pull requests are based on `dev`, which normally
  makes "does this apply to our pin" an open question. It is not one here — see
  the next entry.

### The three MeshCore parser pull requests all apply to our pin unchanged

- **Claim:** `src/Packet.cpp`, `src/Dispatcher.cpp`, `src/Mesh.cpp`,
  `src/helpers/AdvertDataHelpers.cpp` and `src/Utils.cpp` are **byte-identical**
  between `d929643` and both pull request bases (`dev@e0031870` for #3267,
  `dev@9d7cee66` for #3269/#3270). The 29 commits `dev` leads by touch none of
  them.
- **Source:** `git diff --quiet <pin> <base> -- <file>` against a full clone.
- **Checked:** 2026-08-23.
- **Consequence:** a measurement on a pull request's head tree is a measurement
  of our pin plus that pull request's guards, and no rebasing is needed to reason
  about either.

### Nine of ten malformed-frame cases over-read on the pinned MeshCore revision

- **Claim:** at `d929643`, `Dispatcher::tryParsePacket`, `Packet::readFrom` and
  `AdvertDataParser` all read past the length they are given, on inputs of 0, 1
  and 2 bytes. Reproduced, not read: upstream's own translation units compiled
  unmodified and fed buffers of exactly their declared length behind a
  `PROT_NONE` guard page, under AddressSanitizer.
- **Source:** [MESHCORE_PARSER_BOUNDS.md](MESHCORE_PARSER_BOUNDS.md) §4, harness
  and corpus in [`meshcore-parser-bounds/`](meshcore-parser-bounds/).
- **Checked:** 2026-08-23, clang 18.1.3, Ubuntu 24.04.
- **What it is not:** at every reachable call site in the pinned tree the buffer
  behind these three parsers is a fixed array large enough that the read stays
  *inside the allocation* — 256 B in `Dispatcher::checkRecv`, 177 B in
  `MyMesh::handleCmdFrame`, 262 B and 250 B in the two bridges, the `Packet`
  object itself for adverts. The outcome in all nine is a rejected packet.
  "Reads past its length" is verified; "leaves the buffer" is verified **false**
  for these three, and the distinction is the whole blast radius.
- **Reachable from the companion link, once.** `CMD_SEND_RAW_PACKET` calls
  `tryParsePacket` on a client-supplied buffer with only a `len >= 4` guard
  (`examples/companion_radio/MyMesh.cpp:2000`), which is the one place a
  *client* — Attadipa's role on the node path — hands bytes to a MeshCore
  parser. `CMD_IMPORT_CONTACT` was checked and **cannot** reach
  `Packet::readFrom`'s over-read: it gates on `len > 98` and the parser reaches
  at most byte 70.
- **Hardware:** **NOT EXECUTED — HARDWARE REQUIRED.** Nothing here ran on a
  radio, a node or a board, and no host sanitizer result may be presented as
  radio or HIL validation.

### `Utils::decrypt` writes 192 bytes into MeshCore's 184-byte packet buffer

- **Claim:** `src/Utils.cpp:70-83` rounds its output up to whole 16-byte blocks
  and documents the precondition in `Utils.h`; `Mesh::onRecvPacket` passes it a
  wire-supplied length that does not honour it, into
  `uint8_t data[MAX_PACKET_PAYLOAD]` (184 B). For `payload_len` 181…184 it writes
  192 bytes. Reproduced against the real `src/Utils.cpp` with a stub block cipher
  — the bound belongs to the loop, not the cipher — faulting at `Utils.cpp:77`
  for `src_len` 177, 180 and 182 and clean at 176.
- **Source:** [MESHCORE_PARSER_BOUNDS.md](MESHCORE_PARSER_BOUNDS.md) §3 P4.
- **Checked:** 2026-08-23.
- **Not from upstream:** none of the three pull requests mentions it, and it is
  not filed upstream. Reporting it is the owner's call, not an agent's.
- **Not established:** what those eight bytes overwrite on an ESP32-S3, and
  whether the end-to-end path through `Mesh::onRecvPacket` runs — both need
  builds this project has not made. See
  [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) M22.

### Meshtastic shipped the same defect class and fixed it on 2026-08-23

- **Claim:** `meshtastic/firmware` PR **#11573** is **merged** — merge commit
  `ac330e6a6b9fca267fe3faab27ee50c4e91bee28`, head `6094d148`, base `develop`
  (`05f64741`), 4 files, +58 −5. It replaces
  `assert(p->encrypted.size <= sizeof(radioBuffer.payload))` in
  `src/mesh/RadioInterface.cpp::beginSending()` with a runtime check that logs,
  calls `packetPool.release(p)` and returns 0 *before* the `memcpy`; makes
  `RadioLibInterface::startSend` unwind on that zero (`completeSending()`,
  `powerMon->clearState(…Lora_TXOn)`, `startReceive()`); moves
  `reconfigureForBeaconTX(this, nullptr)` out of `completeSending()`'s `if (p)`
  arm so the radio is restored even with no packet; and releases the beacon
  packet on `ERRNO_SHOULD_RELEASE` at both `router->send(p)` sites in
  `src/modules/MeshBeaconModule.cpp`. `test/test_radio/test_main.cpp` gains
  `test_beginSending_oversizedPayloadAbortsSafely()`, which asserts the return is
  0, `sendingPacket` is null, **and the pool slot is reusable**.
- **Source:** the merged diff, `GET /repos/meshtastic/firmware/pulls/11573` with
  `Accept: application/vnd.github.v3.diff`, plus `/pulls/11573` and
  `/pulls/11573/files` for the metadata.
- **Checked:** 2026-08-24, independently of the owner's summary of the same
  change; every claim in that summary held.
- **Not in a release.** `compare/master...ac330e6a` answers `diverged`, ahead 828
  / behind 103, so `master` does not contain it, and the newest release
  `v2.7.26.54e0d8d` was published 2026-06-24. The shipping firmware still has the
  assertion.
- **Licence: GPL-3.0** (`repos/meshtastic/firmware` → `license.spdx_id`).
  **Read-only evidence. No code from it may enter this repository**, which is the
  same bar as [OD-12](OWNER_DECISIONS.md).
- **Not established, and deliberately not claimed:** whether that `assert()` was
  compiled out in shipping builds. `NDEBUG` appears in three files of that
  repository and in none of its build flags; whether the Arduino/ESP-IDF
  toolchain defines it for those environments was not traced.
- **Hardware:** **NOT EXECUTED — HARDWARE REQUIRED.** The pull request's author
  lists Heltec LoRa32 V3, LilyGo T-Deck, Seeed T-1000E and Wio-E5. None was
  checked here and this project has none of them.
- **Why it is here:** it makes the `Utils::decrypt` finding above a
  **two-instance pattern** rather than one project's defect — two unrelated mesh
  firmwares, different radios, different code, both with a wire-supplied length
  reaching a fixed destination with nothing executable in between. See
  [MESHCORE_PARSER_BOUNDS §8](MESHCORE_PARSER_BOUNDS.md).

### Attadipa's own frame decoder validates length before reading

- **Claim:** `link/src/frame_codec.cpp` rejects a declared length greater than
  `kMaxPayload` at `:139` before touching a payload byte, waits rather than reads
  when fewer bytes have arrived than the header declares (`:147`), and gates both
  behind a length-check byte (`:123`) and a CRC. The MeshCore findings do not
  transplant onto it.
- **Source:** the file, read on 2026-08-23 while answering issue #142.
- **Why it is here:** the finding that prompted the check was about a different
  protocol boundary with different invariants, and "our decoder is probably fine"
  is not a fact. This one was looked at.

### A MeshCore companion frame does not fit one BLE notification at MTU 176

- **Claim, arithmetic:** on a link whose negotiated ATT MTU is 176, one Handle
  Value Notification carries **173** bytes, because the PDU spends 3 octets on
  its opcode and handle. MeshCore's frame buffer is `MAX_FRAME_SIZE` = **176**,
  and its ESP32 BLE interface requests exactly that as the MTU. So a full frame
  is **3 bytes larger than the link it asked for**. **173 is the number for a
  vanilla node**, which has no chunked-download command and so subtracts no
  builder header; the **171** that appears throughout the upstream evidence is
  173 minus a 2-byte chunk header belonging to a derivative's `caplog` and config
  stream, and it is not a fact about this protocol.
- **Source:** the `− 3` is the Bluetooth Core specification's, not either
  project's. `MAX_FRAME_SIZE` is `src/helpers/BaseSerialInterface.h:5` and the
  request is `BLEDevice::setMTU(MAX_FRAME_SIZE)` at
  `src/helpers/esp32/SerialBLEInterface.cpp:29`, both at
  `d92964352441e53b93e8667b802e04f6e072b39e`. The arithmetic is re-derived and
  executed in [`meshcore-ble-frame-capacity/`](meshcore-ble-frame-capacity/),
  which compiles the upstream sizing header itself rather than restating it.
- **Checked:** 2026-08-23.
- **Scope, and it is the important part.** The *loss* this implies was measured
  by **`OffbandMesh/meshcore-firmware`**, a MeshCore derivative, on **their**
  Heltec V4.3 boards and on **NimBLE** — three field reports each short by a
  whole number of full frames × 3 bytes (147, 222, 249), and a bench capture of
  `83 × 171 + 1 × 53`. Vanilla MeshCore at our pin is on the Arduino core's
  **Bluedroid**, and nobody has measured it. On Attadipa hardware:
  **`NOT EXECUTED — HARDWARE REQUIRED`.**
- **Full reading:**
  [MESHCORE_BLE_FRAME_CAPACITY](MESHCORE_BLE_FRAME_CAPACITY.md).

### Vanilla MeshCore has no MTU-aware frame sizing anywhere

- **Claim:** at `d92964352441e53b93e8667b802e04f6e072b39e` the identifier
  `maxFrameSize` does not exist in the tree. `BaseSerialInterface` declares no
  such method, nothing calls one, and `onMtuChanged()` only prints
  (`src/helpers/esp32/SerialBLEInterface.cpp:100-101`). Every producer sizes
  against `MAX_FRAME_SIZE` directly.
- **Consequence, read from source:** four companion producers size against the
  buffer. `logRxRaw` → `PUSH_CODE_LOG_RX_DATA` is bounded at *exactly* 176 by
  `len + 3 <= MAX_FRAME_SIZE` (`examples/companion_radio/MyMesh.cpp:287`) and is
  called for every received raw packet (`src/Dispatcher.cpp:199`). Two others
  guard against `sizeof(out_frame)` = `MAX_FRAME_SIZE + 1` = 177 — one byte more
  than any `writeFrame()` accepts. Their `payload_len` guard admits `payload_len
  <= 173` while `writeFrame()` accepts 176, so **exactly one value drops**:
  `payload_len` of precisely 173, building a 177-byte frame. `onTraceRecv` has a
  different path-length guard; whether it can reach 177 is unresolved in #142.
  The two `payload_len` frames are **dropped**, on every transport
  and silently: `ArduinoSerialInterface.cpp:25-28` returns
  0 with no message, and `MESH_DEBUG_PRINTLN` is `{}` unless `MESH_DEBUG` is
  defined (`src/MeshCore.h:29-32`), which the root `platformio.ini` does not.
- **Source:** the files and lines above, read at the pinned revision.
- **Checked:** 2026-08-23.
- **Not verified:** that any of it behaves this way on a board.
  `NOT EXECUTED — HARDWARE REQUIRED`.

### Our MeshCore pin is upstream's `main` tip, not a lagging one

**Merged upward, 2026-08-25.** This entry and *"The pinned MeshCore revision is
upstream's current release, not a lagging one"* were the same claim, reached by
two research runs a few hours apart with different API calls. They are now one
entry near the top of this section, carrying both sources and the 2026-08-24
amendment that `main` has since moved by two documentation commits. Kept as a
signpost rather than deleted, because two entries saying the same thing is how a
reader ends up citing the one that was not updated.

---

## Toolchain / host environment

### The development host lacked an embedded toolchain on 2026-08-21

- **Claim:** on the development machine (WSL2, Ubuntu 24.04) the following are
  present: cmake 3.28.3, gcc/g++ 13.3.0, Python 3.12.3. The following are
  absent: ESP-IDF (`IDF_PATH` unset), ninja, SDL2, clang-format, ccache.
- **Source:** direct probe of the host, 2026-08-21.
- **Historical scope:** this was a direct probe on that date, not a statement
  about the current host. T-165 subsequently built both flash and PURE_RAM
  firmware with the pinned ESP-IDF v5.5.5 toolchain.

---

## Hardware

Both target boards have been surveyed from vendor documentation, vendor board
support code, and published schematics. The full result — every part, pin, I2C
address, and power rail — lives in [HARDWARE_MATRIX.md](HARDWARE_MATRIX.md).
Recorded here are only the findings that change architecture.

That promise was half true until 2026-08-22. The Waveshare peripheral table had
been written without the `I2C addr` and `Power rail` columns the T-Watch table
carries, so for that board neither existed while this sentence said they did —
which sends a reader looking for data rather than for its absence. The addresses
are there now, each cited. The rails are still D13.

The T-Watch has not been physically inspected. The Waveshare unit has now been
physically probed and has booted Attadipa firmware; the measurements below are
scoped to USB serial `28:84:85:B2:18:A4` and do not generalise to the T-Watch or
to every unit of the same model.

### Attadipa's T-165 firmware booted on the physical Waveshare unit

- **MEASURED:** the unit booted Attadipa from flash and continued emitting its
  one-second heartbeat. The boot log reported ESP32-S3 revision v0.2 and loaded
  the app from the repository's `factory` partition at `0x10000`.
- **MEASURED:** JEDEC ID `c8 40 19` identified 32 MB of physical flash while the
  bootloader and firmware deliberately declared 16 MB. This is the addressing
  ceiling chosen for the build, not a claim that the upper flash does not exist.
- **MEASURED:** the octal PSRAM driver identified AP vendor `0x0d`, 64 Mbit
  (8 MB), 3.3 V PSRAM at 80 MHz; initialisation and the ESP-IDF SRAM test both
  succeeded.
- **MEASURED:** the running firmware enumerated `nvs`, `phy_init` and `factory`;
  every reported end address was below `0x1000000`.
- **MEASURED failure boundary:** an early PURE_RAM build called
  `esp_partition_find()` without a flash driver and panicked in
  `spi_flash_mmap`. The corrected build avoids flash ID and partition APIs in
  PURE_RAM mode, reports them as unavailable, and ran for 30 heartbeats. This
  does not measure flash, PSRAM or partitions from the RAM image.
- **VERIFIED recovery asset:** before flashing, 33 554 432 bytes were captured
  to a host-local factory backup, SHA-256
  `c423dad3f0d33d56fa96f8590b3da583b05584e85bc2701a7c48c031ad747dbd`.
  `esptool verify-flash` checked all 33 554 432 bytes against the device. The
  binary is intentionally not committed.
- **MEASURED later bench state:** after the acceptance run, the complete backup
  was restored to the serial-identified unit. The write's integrated hash check
  succeeded; a separate post-restore full `verify_flash` was interrupted and
  produced no verdict. The owner observed the factory UI at high brightness,
  confirmed touch worked, and set brightness to minimum. The unit currently
  runs that factory image; this is not evidence for an Attadipa touch driver.
- **Source:** physical transcript and probe record
  [BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md), 2026-08-25.
- **Not established by T-165:** display output, touch, PMU/rail ownership, RTC, audio,
  radio, current draw, sleep/wake behaviour or long-term stability. Those were
  not exercised by T-165.

### The two boards share almost nothing but the SoC and the PMU

- **Claim:** of the two target boards, only the ESP32-S3 and the AXP2101 PMU
  are common. Display controller, touch controller, IMU, RTC, audio path,
  storage, and the presence of radio, GNSS, haptics and IR all differ.
- **Source:** S1, S5, S7 (see HARDWARE_MATRIX).
- **Impact:** a capability layer is not a nicety here, it is the only way one
  binary-compatible codebase can address both.

### Neither cited target-board schematic routes 32.768 kHz to the ESP32-S3

| Board design | VERIFIED vendor-schematic fact | Consequence |
|---|---|---|
| LilyGO T-Watch S3 Plus | PCF8563 `CLKOUT` reaches pulled-up net `RTC_CLKOUT` and test point `TP66`, and no ESP32 pin. ESP32-S3 `XTAL_32K_P` / GPIO15 is MAX98357A `LRCLK`; `XTAL_32K_N` / GPIO16 is touch `INT`. | External modes are unauthorized in this board profile; physical routing remains `UNKNOWN`. |
| Waveshare ESP32-S3-Touch-AMOLED-2.06 | PCF85063ATL `CLKOUT` has a no-connect marker and `CLKOE` has no destination. GPIO15 is shared I2C SDA; GPIO16 is `I2S_MCLK`. | External modes are unauthorized in this board profile; physical routing remains `UNKNOWN`. |

- **ESP-IDF fact:** v5.5.5 maps the driven external-oscillator option to
  `32K_XP` / GPIO15 and defaults to the internal RC source. The repository does
  not override that default. T-167 may keep `RTC_CLK_SRC_INT_RC`; the internal
  fast-clock/divider option remains unmeasured, not a 32.768 kHz board source.
- **Correction:** the earlier `R126 not fitted` statement was false. T-Watch
  schematic sheet 3 places R126 on the DRV2605L supply pin, unrelated to
  `RTC_CLKOUT`.
- **Evidence boundary:** neither physical board's revision and continuity were
  established. The T-Watch is not in hand; the received Waveshare revision is
  D20. These are schematic-design facts, not physical-board measurements.
- **Sources and exact revisions:** [RTC_SLOW_CLOCK](RTC_SLOW_CLOCK.md), with
  pinned vendor commits, schematic sheets, PDF hashes, ESP-IDF v5.5.5 source
  and the ESP32-S3 datasheet. Checked 2026-08-26.
- **Physical route and current consumption:** **NOT EXECUTED — HARDWARE
  REQUIRED.** Espressif's 3.3 mA / 230 µA values are from its NimBLE example
  using an external crystal, not measurements of either Attadipa board and not
  evidence for `EXT_OSC`.

### The Waveshare board has no LoRa and no GNSS

- **Claim:** the Waveshare ESP32-S3-Touch-AMOLED-2.06 carries neither a LoRa
  radio nor a GNSS receiver.
- **Source:** vendor README hardware table and vendor BSP v2.0.0 pin
  definitions; no radio or GNSS net appears in either (S5, S7).
- **Impact:** mesh messaging and navigation have no hardware **on this board**.
- **Amended 2026-08-21:** the claim above is sourced and stands; the inference
  originally drawn from it did not. It read "cannot exist on this board … the UI
  must not offer them". An Attadipa node supplies both to the *device*
  ([OWNER_DECISIONS](OWNER_DECISIONS.md) OD-1), so the UI offers them with the
  remedy stated, and withholds only what no configuration of the device can do.
  The lesson worth keeping is narrower than the correction: a fact about a board
  and a fact about a device are different claims, and this line turned one into
  the other without noticing.

### Neither board has a magnetometer

- **Claim:** the T-Watch carries a BMA423 (accelerometer only, no gyroscope);
  the Waveshare carries a QMI8658 (6-axis accel + gyro). Neither board has a
  magnetometer.
- **Source:** S1, S5, S6.
- **Impact:** the specification's magnetometer requirements are **architectural
  only** for now — an API that can accept one later. On real hardware today,
  magnetic heading exists nowhere. Heading from GNSS course-over-ground exists
  wherever GNSS does — which, since OD-1, is not only the T-Watch — and only
  while the user is moving. Whether the node carries a magnetometer is
  unresolved and decides what a compass application can honestly be
  ([OPEN_QUESTIONS](OPEN_QUESTIONS.md) A5/Q2,
  [NODE_PROFILE](../node/NODE_PROFILE.md) N3). The
  "haptics disturb the compass" problem the plan is concerned about cannot be
  observed on either board, because there is no compass. It stays a design
  consideration, not a mitigation to implement.

### The T-Watch radio chip is a purchase-time variant, and two of the five are not LoRa

- **Claim:** the T-Watch S3 Plus ships with one of **five** radio chips —
  SX1262 (default), SX1280, CC1101, LR1121, or SI4432 — selected as a board
  revision at build time. The SPI pin assignment is shared across them.
  **CC1101 and Si4432 have no LoRa modulator**; they are FSK/OOK-family parts.
  **SX1280 is LoRa at 2.4 GHz only.** At MeshCore `d929643` exactly one of the
  five — the SX1262 — is a supported radio.
- **Source:** vendor documentation build table (S1) and the conditional
  compilation in `src/LilyGoWatchS3.h` (S2) for the variant list. For the
  modulation and support claims: RadioLib 7.7.1 (`510e00c`) — the `setSpreadingFactor`
  API is absent from `CC1101` and `Si443x`, and the module list calls them FSK
  parts — and MeshCore `d929643`, whose `RADIO_CLASS` set across 87 variants
  contains only `SX1262` of the five, with `RADIOLIB_EXCLUDE_CC1101=1` in the
  root `platformio.ini`.
- **Evidence level: PARTIAL.** The modulation, band and power figures are read
  from driver source, not from the TI and Silicon Labs datasheets, which refused
  automated retrieval. Confirming them from primary sources is **R1**.
- **Impact:** "T-Watch S3 Plus" does not identify the radio, and "it has a
  radio" does not mean "it can join the mesh". The product capability
  `MeshMessaging` is derived from the fitted chip's modulation, band and
  upstream support — never asserted from presence
  ([ADR-0003](../adr/0003-radio-not-lora.md),
  [ADR-0007](../adr/0007-two-capability-layers.md)).

### The T-Watch GNSS module is also a variant, with different power needs

- **Claim:** either a u-blox MIA-M10Q or a Quectel LS550G. The LS550G variant
  requires the PMU to enable **DC4 at 850 mV *and* BLDO1 at 3300 mV**.
  Additionally, GNSS sits on BLDO1 only on units with rear BOOT/RST buttons;
  earlier units powered it from DC3.
- **Source:** S1.
- **Impact:** the power-up sequence for GNSS is board-revision dependent and
  cannot be inferred from the product name. Getting it wrong means GNSS
  silently never starts. Assisted-GNSS mechanisms also differ between u-blox
  and Quectel, so no assistance work can be designed until the module is known.

### The T-Watch touch panel has no reset line

- **Claim:** the FT6336U RESET pin is not connected. The vendor states that if
  the touch panel is put to sleep, touch will not work again.
- **Source:** S1 (both the pin map and an explicit warning).
- **Impact:** a direct constraint on the low-power state machine, not a driver
  detail. The Waveshare board *does* have a touch reset line, so the two boards
  cannot share a sleep strategy for touch.

### The T-Watch haptic driver is gated behind a PMU rail

- **Claim:** the DRV2605 enable is on AXP2101 rail BLDO2.
- **Source:** S1.
- **Impact:** haptic feedback has a power-sequencing dependency and a wake-up
  latency. Whatever owns hardware coordination must own this rail, not the
  application.

### The Waveshare vendor BSP does not drive the IMU, PMU, or RTC

- **Claim:** BSP v2.0.0 declares `BSP_CAPS_IMU 0` and `BSP_CAPS_BUTTONS 0`,
  and supports only display, touch, audio, and SD card. The QMI8658, AXP2101,
  and PCF85063 present on the board are handled only in standalone examples.
- **Source:** S7, `include/bsp/esp32_s3_touch_amoled_2_06.h`.
- **Impact:** "the vendor supports this board" does not mean the board's parts
  are usable. Attadipa cannot take the BSP as a complete abstraction; it must
  cover the remaining parts itself.

### The Waveshare panel is a CO5300 driven by an SH8601-family driver

- **Claim:** the product documents a CO5300 panel controller; the vendor BSP
  depends on the component `waveshare/esp_lcd_sh8601`.
- **Source:** S5 (README hardware table), S7 (`idf_component.yml`).
- **Status:** not a conflict — the vendor drives the CO5300 through the
  SH8601-family driver. Recorded so this is not later "fixed" as a mistake.

### Vendor toolchain support (evidence for choosing versions, not a decision)

- **Claim:** Waveshare states support for **ESP-IDF v5.5.5 and v6.0.2** and
  Arduino-ESP32 3.3.11; its BSP requires `idf >= 5.3` and `lvgl >=8,<10`.
  LilyGO's library targets Arduino-ESP32 >= 3.3.0-alpha1, and its PlatformIO
  path is pinned to the older 2.0.17 (IDF 4.4.7).
- **Source:** S5, S7, S1.
- **Impact:** feeds the ESP-IDF and LVGL version decisions in
  [DEPENDENCIES.md](DEPENDENCIES.md). Attadipa now pins ESP-IDF v5.5.5; this
  vendor support corroborates that choice but did not make it. The LilyGO
  PlatformIO constraint does not bind the current ESP-IDF-native build.

### Vendor-published power figures exist for the T-Watch

- **Claim:** the vendor publishes current draw per sleep mode (light sleep
  2.38 mA; deep sleep 460–530 µA depending on backup power; deep sleep with
  touch wake 1.08 mA; power off 50 µA) and a 940 mAh battery.
- **Source:** S1.
- **Impact:** these are **vendor numbers under vendor firmware**, useful as an
  order of magnitude and as a target to reproduce. They are not evidence about
  Attadipa, and must never be reported as Attadipa's measured consumption.
  Note that waking on touch costs roughly twice waking on button — a real
  design trade-off, once confirmed.

### The BMA423 counts steps, and its datasheet does not say how

- **Claim:** the BMA423 has a **32-bit hardware step counter** in registers
  `0x1E`–`0x21` (`STEP_COUNTER_0`…`_3`), and the datasheet documents those four
  registers with one line each: `DESCRIPTION: Application note – Wearable
  feature set`. The behaviour — power mode, required ODR, whether the count
  survives a soft reset, whether it accumulates while the host sleeps — is in a
  **separate document**, Bosch's *Wearable Feature Set* application note
  `BST-MAS-AN032`, which returned HTTP 403 on two attempts.
- **Source:** BMA423 Data Sheet, revision 2.0, `BST-BMA423-DS004-00`, August
  2019, p. 53 and p. 1 (*"Plug 'n' Play Step-Counter solution with watermark
  functionality"*). Confirmed against Bosch's own reference driver v1.1.4:
  `bma423_step_counter_output()` reads four bytes from
  `BMA4_STEP_CNT_OUT_0_ADDR = 0x1E` into a `uint32_t`.
- **Checked:** 2026-08-22, for the T-Watch S3 Plus.
- **Impact:** the pedometer is mandatory ([OD-6](OWNER_DECISIONS.md)).
  **Superseded 2026-08-22 by the entry below** — the behaviour is documented,
  in a revision of this same datasheet that Bosch withdrew. Full reading in
  [PEDOMETER_PARTS](PEDOMETER_PARTS.md).

### Bosch deleted the step-counter chapter from the BMA423 datasheet between revisions

- **Claim:** the behaviour revision 2.0 defers to application note
  `BST-MAS-AN032` was **printed in the datasheet itself** until three months
  earlier. Revision 1.0 (`BST-BMA423-DS000-00`, August 2017) and revision 1.1
  (`BST-BMA423-DS000-01`, May 2019) both carry a *"Step Detector / Step
  Counter"* chapter, a *"Minimum Bandwidth Settings"* section, the phone/wrist
  preset tables and the per-field configuration list, pp. 32–37 — and the text
  is byte-identical between the two. Revision 2.0 (`BST-BMA423-DS004-00`,
  August 2019) replaces all of it with a pointer, and changes the document
  number series from `DS000` to `DS004`.
- **Source:** revision 1.1 retrieved 2026-08-22 from the Watchy project's
  mirror, `watchy.sqfmi.com/assets/files/BST-BMA423-DS000-1509600-950150f51058597a6234dd3eaafbb1f0.pdf`,
  SHA-256 `98b85747bd983435b2921266401cbeb095a57e2274b1f5c49f7f04145f22de04`,
  2 363 646 bytes. Revision 1.0 from `opensourceinstruments.com`, used only to
  confirm the chapter is unchanged. Revision 2.0 from DigiKey. Bosch's own site
  returned **HTTP 403** for both the note and the datasheet.
- **Checked:** 2026-08-22.
- **Impact:** four questions this file recorded as `UNKNOWN` are answered, and
  one claim it recorded is **wrong** — see the two entries below. The general
  lesson is the one [ADR-0003](../adr/0003-radio-not-lora.md) already teaches in
  another subsystem: *"the datasheet"* is not a document, it is a document **at
  a revision**, and the newest is not always the most complete. Where a current
  datasheet defers to something unobtainable, look backwards before declaring
  the fact unknowable.

### The BMA423 step counter runs in low-power mode, and 50 Hz is the floor

- **Claim:** the feature engine takes acceleration samples *"acquired at 50Hz"*.
  In performance mode (`ACC_CONF.acc_perf_mode = 0b1`) the features work at any
  ODR; in low-power mode (`0b0`) *"the ODR must be set to minimum 50 Hz for the
  most features except Double Tap/Tap"*, and 200 Hz for tap. Violating it sets
  `INTERNAL_STATUS.odr_50hz_error` — it is detectable, not silent. Counting
  itself needs no host transaction: the sensor duty-cycles itself, and *"in all
  global power configurations both register contents and FIFO contents are
  retained."*
- **Source:** BMA423 Data Sheet revision 1.1, pp. 20–21 and 32; register `0x2A`.
- **Checked:** 2026-08-22.
- **Impact:** the power line for step counting is the **50 Hz low-power figure,
  13–14 µA `ESTIMATED`** — not 42 µA and not 150 µA. Wanting double-tap as well
  costs 3×. Whether the counter survives *the board's* sleep is now a rail
  question about the AXP2101, not a sensor question.

### CORRECTION — the BMA423 watermark field carries an implicit ×20

- **Claim:** `BMA423_STEP_CNTR_WM_MSK = 0x03FF` is 10 bits, but the field
  *"holds implicitly a 20x factor, so the range is 0 to 20460, with resolution
  of 20 steps"*. A written 10 interrupts every 200 steps, and *"as the steps are
  buffered internally, the output may be triggered between 200-210 steps."*
  Bosch's driver does **not** apply the factor —
  `bma423_step_counter_set_watermark()` writes the argument raw.
- **Source:** BMA423 Data Sheet revision 1.1, p. 36; `bma423.c` v1.1.4 l. 1049.
- **Checked:** 2026-08-22.
- **Impact:** **this corrects an earlier reading in this repository.** LilyGo's
  `setStepCounterWatermark(1)` was recorded as an interrupt *per step*; it is an
  interrupt every **20** steps. Roughly every 15 s at walking cadence, not
  ~1 Hz — an order of magnitude, and it lands in the T-061 power arithmetic.

### The BMA423's step counter lives in a 6 144-byte blob the host uploads at every boot

- **Claim:** the feature engine is not resident. `BMA4_CONFIG_STREAM_SIZE = 6144`
  bytes are streamed to `BMA4_FEATURE_CONFIG_ADDR` (`0x5E`), after which the host
  must **wait 150 ms** and then read `BMA4_INTERNAL_STAT` (`0x2A`) expecting
  `BMA4_ASIC_INITIALIZED` (`0x01`). The step-counter watermark is **10 bits**
  (`0x03FF`, so 0–1023 **as written; the sensor multiplies by 20** — see the
  correction above), and **value 0 does not mean "every step"** — it selects
  the separate *step detector* interrupt.
- **Source:** Bosch BMA423 reference driver v1.1.4 —
  `bma4_write_config_file()` in `bma4.c`, and the masks in `bma423.h` /
  `bma4_defs.h`.
- **Checked:** 2026-08-22.
- **Impact:** 150 ms of the boot budget is spent before a step can be counted,
  every time. A soft reset (`0xB6` → `0x7E`) **does** drop it — revision 1.1
  §4.2: *"Initialization has to be performed as well after every POR or soft
  reset"*, the reset being *"largely equivalent to a power cycle"* — so every
  reset is a hole in the day's total that OD-6's *no interpolation* rule
  requires be reported rather than filled. The block is also read–modify–write
  as a whole, so two callers cannot configure two features independently.

### Both Rev A datasheets carry the pedometer; only QMI8658A Rev D deleted it

- **Claim:** the pedometer is a **revision** question, not a variant one and not
  a part one. Both Rev A documents carry it; only QMI8658A Rev D deleted it.
  **QMI8658C** (`13-52-27`, Rev A, 20 June 2022) documents it fully: feature
  list p. 1, chapter 11, a **24-bit** count in `STEP_CNT_LOW/MIDL/HIGH`
  (`0x5A`–`0x5C`), `CTRL8.Pedo_EN`, and CTRL9 commands `0x0D` (configure) and
  `0x0F` (reset count).
  **QMI8658A Rev A** (`13-52-25`, 20 June 2022) documents the identical feature
  on the identical pages — chapter 11 is pp. 64–66 in both, the registers,
  the `CTRL8` bit and the two CTRL9 commands are the same. The two Rev A
  documents are near-identical twins; see the entry below for what does and
  does not differ between them.
- **The two Rev A documents are twins, and no register tells them apart.**
  `13-52-25 ∙ QMI8658A Datasheet ∙ Rev A` and
  `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A` are both 88 pages, both dated
  20 June 2022, and identical outside the part name except in these places.
  Identical: `WHO_AM_I = 0x05`, `REVISION_ID = 0x7C`, and the product id
  `0x086E00051000`. **Reading the part's own registers therefore cannot tell
  you which of the two documents describes the silicon in front of you.**
  What does differ is electrical, and it matters:

  | | `13-52-25` QMI8658**A** | `13-52-27` QMI8658**C** |
  | --- | --- | --- |
  | Gyroscope noise density | 13 mdps/√Hz | 15 mdps/√Hz |
  | Gyroscope full scale, max | ±2048 °/s | ±1024 °/s |
  | Internal I/O pull-up | 200 kΩ | 2 MΩ |
  | Min. supply slew rate, POR → 1.71 V | 40 V/s | 95 V/s |
  | Gyro temperature coefficient | ±0.05 / ±0.03 / ±0.05 dps/°C, per axis | ±0.05 dps/°C, all axes |
  | `RESV` (pin 10) left floating | permitted | *"there might be leakage current"* |

  Everything in this repository that quotes one of those six figures must name
  which document it came from. The schematic prints `QMI8658C` twice
  ([`VERIFIED_FACTS.md:1657`](VERIFIED_FACTS.md) "printed twice"), so the C
  column is the one this board is read against.
- **Both documents contradict themselves on `REVISION_ID`, in the same way.**
  The register-*map* summary table gives the default as `01101000` — **`0x68`** —
  while the register-*description* section three chapters later gives **`0x7C`**.
  This is true of `13-52-25` and `13-52-27` alike. Every `0x7C` claim in this
  tree is scoped to the register-description section for that reason, and the
  part itself answers `0x7C` — `MEASURED` on the bench 2026-08-28. A reader who
  greps either PDF and finds `0x68` has found the summary table, not a
  contradiction with this repository.
- **Source:** both PDFs read 2026-09-01, held off-tree because they are
  copyrighted and marked "Security Level: 3". `13-52-27`: md5
  `e093b1cc1d1cf85097f955abbea65c08`. `13-52-25`: md5
  `5a0fef65a358430d6499944a75d22e19`, fetched from the vendor's own published
  copy at `files.waveshare.com/upload/5/5f/QMI8658A_Datasheet_Rev_A.pdf` and
  byte-identical to the copy [`MAGNETOMETER_RETROFIT.md:138`](MAGNETOMETER_RETROFIT.md) "Admissible here as evidence"
  already recorded, which closes that document's provenance.
- **How to name these two, everywhere in this tree.** Write the vendor's own
  footer form in full — `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A` and
  `13-52-25 ∙ QMI8658A Datasheet ∙ Rev A` — and **never write "the Rev A
  datasheet"**. Both are Rev A, both are dated 20 June 2022, and that one
  ambiguous phrase is what let a correction reach three documents claiming one
  of them did not exist ([#341](https://github.com/hleserg/Attadipa/issues/341)).
  The number alone is enough where the context already names the part.
- **The one document that deleted the pedometer is `QMI8658A` Rev D**
  (`QST-PD-B002-22`, current). Its feature list reads *"Integrated Tap,
  Any-Motion, No-Motion, Significant-Motion detection"*, there is no chapter on
  the pedometer, and a search of the whole document finds **no `STEP_CNT`
  register and no `Pedo_EN` bit**. The feature is not marked deprecated or
  reserved — it is gone from the document, registers included. That is a
  **revision** boundary, not a variant one: the A part's own Rev A documents the
  pedometer as fully as the C part's does.
- **Source for Rev D:** `QMI8658A` Rev D, `QST-PD-B002-22`, read 2026-08-22.
- **Impact — smaller than this entry used to claim.** The earlier text here read
  that *"the two variants differ on precisely the feature OD-6 makes
  mandatory"*, and that is **false**: they do not differ on it at all, as the
  twin-document entry above shows. Two things remain true.
  [`HARDWARE_MATRIX.md:30`](HARDWARE_MATRIX.md) names the part without a variant
  letter at all — *"QMI8658 — 6-axis"* — and the vendor BSP does not touch the
  IMU, so no code reads the variant back. The schematic prints `QMI8658C`, and
  no register would settle it anyway. And OD-6's feasibility is still open, but for a
  reason that has nothing to do with which of the two Rev A documents applies:
  the part answers `REVISION_ID = 0x7C`, the value both of them give, and the
  step register still did not move under sixteen seconds of deliberate shaking —
  [`PEDOMETER_BENCH_2026-08-28.md:3-4`](PEDOMETER_BENCH_2026-08-28.md) "the step register never moved".
  That is a `MEASURED` negative against a part whose documentation, on both
  readings, says the engine is there. The variant question cannot explain it and
  should stop being offered as the explanation.

### The QMI8658 costs at least three times the BMA423

- **Claim:** accelerometer-only, gyroscope disabled, typical at 1.8 V and 25 °C:
  the QMI8658C draws **30 / 35 / 42 / 55 µA** at low-power ODRs of 3 / 11 / 21 /
  128 Hz, and 132–182 µA in high-resolution mode. Its idle states are 15 µA
  (power-on default), 8 µA (low power) and 6 µA (power-down, configuration and
  output registers preserved). Low-power mode is available **only with the
  gyroscope disabled**. The BMA423, for comparison, is **13 µA at 50 Hz** in
  low-power mode, 150 µA in performance mode and 3.5 µA suspended — though its
  low-power table is marked by Bosch itself as *"based on limited lab
  measurements. Only for reference."*
- **Source:** QMI8658C datasheet §3.8 tables 15 and 22, and table 31; BMA423
  datasheet electrical characteristics and the low-power current table.
- **Checked:** 2026-08-22.
- **Impact:** both sets are **vendor typicals, not measurements on our boards**.
  They are the budget to design against and the target to reproduce, never a
  figure to report as Attadipa's. A 6-axis IMU is not a cheaper accelerometer.
### The T-Watch panel is 1.54" across the active area, not the vendor's 1.3"

- **Claim:** the LilyGO T-Watch S3 Plus panel measures **27.72 mm across its
  240 × 240 active area** — a **1.544" diagonal at 220 ppi**, and not the 1.3"
  and 261 ppi that LilyGoLib's specification tables state for this product by
  name. 1.3" is **excluded**, not merely disfavoured: reaching it needs the
  image scale to be wrong by 19 %, which would have to appear as 19 %
  non-uniformity in the rule's own graduations, and does not.
- **Source:** measured on the physical unit, 2026-08-28, with a steel rule
  laid on the case coplanar with the glass. The photograph is committed and
  [`twatch-s3-plus-panel/measure_panel.py`](twatch-s3-plus-panel/measure_panel.py)
  re-derives every number above from it. Corroborated independently by the
  schematic's own LCD sheet (S3): part `QT154C2408`, symbol `LCD_1.54-TOUCH`.
- **Checked:** 2026-08-28, on the T-Watch recorded in
  [BENCH_DEVICES](BENCH_DEVICES.md).
- **Evidence level: MEASURED.** The band is **two-sided**, −0.2 to +0.7 mm.
  The rule rested on the case while the emitters sit behind the glass, which
  puts the true width up to 0.7 mm higher; the frame's own perspective
  gradient puts it about 0.23 mm lower. Neither term is tightly bounded, and
  they do not cancel by construction. Only three of the four edges are in the
  surviving frame; the panel is square because its raster is 240 × 240.
- **Impact:** every `Metrics::px` conversion at 261 dpi emitted about 19 % more
  pixels than this panel wants, and the board rendered oversized until
  [#323](https://github.com/hleserg/Attadipa/issues/323) corrected it. Supporting
  detail, including the uncertainty budget:
  [TWATCH_S3_PLUS_PANEL_2026-08-28](TWATCH_S3_PLUS_PANEL_2026-08-28.md).

### The T-Watch display has no reset line and no MISO, so it is write-only

- **Claim:** the ST7789V3 on the T-Watch S3 Plus has neither a reset line to the
  SoC nor a return path. `reset_gpio_num` must be `-1`, and **no ST7789 read
  command can ever execute on this board.**
- **Evidence — three independent sources agree.** Attadipa's own schematic read,
  [HARDWARE_MATRIX](HARDWARE_MATRIX.md):96: *"MISO and RESET not connected"*.
  LilyGO's hardware document for this exact board, `LilyGoLib@38e6f8d`
  `docs/hardware/lilygo-t-watch-s3-plus.md`: `Display RESET | Not Connected`,
  `Display MISO | Not Connected`. `espressif/arduino-esp32` `3.3.2`
  `variants/lilygo_twatch_s3/pins_arduino.h`: `#define DISP_RST (-1)`,
  `#define DISP_MISO (-1)`. Meshtastic's same-board variant sets
  `ST7789_RESET -1` and `ST7789_MISO -1` independently.
- **Impact:** `RDDPM (0Ah)` would report the `SLPOUT`, `DISON`, `BSTON`,
  `IDMON`, `PTLON` and `NORON` bits and `RDDSM (0Eh)` the `TEON`/`TEM` bits
  (ST7789V datasheet v1.6 pp. 170, 178). Neither is reachable. **The firmware
  can never confirm the controller's state, and no bring-up test on this board
  may use a register read-back as evidence** — pass and fail are a photograph.
  This is separate from, and has the same cause as, the touch controller's
  missing reset line recorded above.

### ESP-IDF's ST7789 software-reset path is 20 ms where the datasheet says 120

- **Claim:** on any board that leaves `reset_gpio_num` at `-1` — which the fact
  above makes mandatory here — ESP-IDF v5.5.5's ST7789 driver breaks an explicit
  datasheet restriction by 6×.
- **Evidence.** `components/esp_lcd/src/esp_lcd_panel_st7789.c:167-177` at tag
  `v5.5.5`: with no reset GPIO it sends `LCD_CMD_SWRESET` and then
  `vTaskDelay(pdMS_TO_TICKS(20))`, commented *"spec, wait at least 5m before
  sending new command"*. `panel_st7789_init()` (`:182-201`) then sends `SLPOUT`
  as its first action. ST7789V datasheet **v1.6, 2017/09, p. 163 §9.1.2
  `SWRESET`**, *Restriction*, verbatim: *"If software reset is sent during sleep
  in mode, it will be necessary to wait 120msec before sending sleep out
  command."* The qualifier always holds: **p. 184 §9.1.12 `SLPOUT`**, *Default*
  table, gives the state after `Power On Sequence`, `S/W Reset` **and**
  `H/W Reset` as *"Sleep in mode"*. The driver's comment quotes the unqualified
  5 ms clause, which is not the one in force.
- **Also:** `panel_st7789_sleep()` (`:319-333`) waits 100 ms after either
  `SLPIN` or `SLPOUT`, where pp. 182 and 184 both require 120 ms between
  `SLPOUT` and a following `SLPIN`.
- **Impact:** this is a defect of the *combination* — this driver with this
  board's wiring — not of the driver, and it is the reason the panel command
  table cannot be A/B tested in two arms:
  [TWATCH_S3_PLUS_BSP_REUSE](TWATCH_S3_PLUS_BSP_REUSE.md) §4 and §11.
  **Whether it is observable on this panel is NOT EXECUTED — HARDWARE
  REQUIRED**; that it is a spec violation is documentary and needs no board.

### Two mature same-board firmwares disagree by 2× on the T-Watch SPI clock

- **Claim:** there is no inherited consensus value for the ST7789V3 pixel clock
  on this board.
- **Evidence:** `LilyGoLib@38e6f8d` `src/LilyGoWatchS3.cpp:135` calls
  `LilyGoDispSPI::init(..., 80)`, which becomes `pclk_hz = 80 MHz` at
  `src/LilyGoDispInterface.cpp:430`. `meshtastic/firmware`
  `variants/esp32s3/t-watch-s3/variant.h:13` sets `SPI_FREQUENCY 40000000`.
- **Impact:** `80 MHz` is one vendor's choice, not a property of the panel, and
  quoting it as an Attadipa default would be inheriting a number rather than a
  fact. D7c stays `UNKNOWN`; the lower proven value is the defensible starting
  point and any increase is a measurement on our unit.

---

## Read from the T-Watch schematics (S3, S4)

Until this pass the T-Watch rows rested on the vendor's hardware document (S1)
and its board header (S2). Both schematics have now been read. Everything below
is sourced to the drawing itself.

### The T-Watch has no magnetometer — now from the schematic, not from a feature list

- **Claim:** the board carries exactly one motion part, the BMA423.
- **Source:** S3. An exhaustive search of all six sheets for magnetometer part
  families (`BMM*`, `QMC*`, `MMC*`, `AK[0-9]{4}`, `HMC*`, `LIS*M*`, `LSM*`,
  `IST*`) returns nothing. The full active-part inventory of the drawing is
  ESP32-S3-R8, W25Q128JW, AXP2101, PCF8563, BMA423, DRV2605L, HPD16B3,
  SPM1423HM4H-B, MAX98357A, IR12-21C.
- **Impact:** this was previously an argument from absence in a vendor feature
  table, which is weak. It is now an argument from the schematic, which is the
  right kind of evidence for a negative. All compass work stays architectural.

### The GNSS PPS signal never reaches the SoC

- **Claim:** `PPS` exists as a net on the daughterboard and appears nowhere in
  the main-board schematic.
- **Source:** S4 (net present), S3 (string absent from all six sheets).
- **Impact:** no hardware-disciplined time reference. Any design that wanted
  microsecond time alignment — mesh slotting, timestamped logging — must get it
  from the UART sentence and wear the jitter, or not claim it.

### The IR emitter is active-high and idles low

- **Claim:** GPIO 2 → R64 (0 Ω) → base of Q15, an MMBT3904 NPN low-side switch,
  with the IR12-21C anode at +3V3. Conduction requires GPIO 2 high.
- **Source:** S3 sheet 4.
- **Impact:** the inactive level is **LOW**, and the pin is safe at reset. This
  was previously written into the architecture as an unsourced assumption about
  LED polarity; it is now a fact. It is also the one easter-egg-adjacent
  peripheral that can affect other people's equipment, so its idle state being
  provably off matters more than the pin count suggests.

### The audio amplifier cannot be shut down in firmware

- **Claim:** the MAX98357A `SD_MODE` pin is set by R14 = 1 MΩ with R74 and R76
  not fitted. No GPIO is connected to it.
- **Source:** S3 sheet 6.
- **Impact:** the amplifier is enabled whenever `SPK_VDD` is up. "Mute" is a
  rail operation, not a pin operation. Any power state that wants the amplifier
  off must own the rail — which makes the rail service load-bearing rather than
  a convenience.

### The power button never reaches a GPIO

- **Claim:** SW7 wires to the AXP2101 `PWRON` pin.
- **Source:** S3 sheet 1.
- **Impact:** button presses arrive as PMU interrupts over I2C, not as GPIO
  edges. Press duration, long-press and power-off behaviour are PMU register
  policy. An input service that only knows about GPIO edges cannot see the most
  important button on the watch.

### The radio has an eighth line the vendor header omits

- **Claim:** the module fitted on the drawing is an `HPD16B3` with an
  SX1262-class pinout, and `DIO3` is wired to **GPIO 6**.
- **Source:** S3 sheet 5, pin by pin: 1 `VCC`←`GPS_VDD`, 3 `NRESET`←IO8,
  4 `BUSY`←IO7, 5 `DIO1`←IO9, 6 `DIO3`←IO6, 7 `MISO`←IO4, 8 `MOSI`←IO1,
  9 `SCK`←IO3, 10 `NSS`←IO5, 12 `ANT`.
- **Impact:** GPIO 6 was entirely absent from the pin map. On SX1262 designs
  `DIO3` is commonly the TCXO supply and sometimes a second interrupt; which one
  it is here decides whether the radio will get a clock at all. Do not write a
  radio driver before answering it — OPEN_QUESTIONS D10.

### Unplugging the GNSS module removes the BOOT and RESET buttons

- **Claim:** the 13-pin FPC carries `IO0` (pin 2) and `RST/EN` (pin 6) in
  addition to the GNSS UART, `GPS_LDO` and `IO10`.
- **Source:** S3 sheet 2, S4.
- **Impact:** bring-up instructions that say "hold BOOT" are false for a board
  running without the daughterboard. Also puts main-I2C `SDA` on a detachable
  connector.

### Three of four strapping pins carry functional signals

- **Claim:** GPIO 0 = BOOT button, GPIO 3 = LoRa `SCK`, GPIO 45 = display
  backlight, GPIO 46 = I2S `DIN`.
- **Source:** S3 sheets 2, 4, 5, 6.
- **Impact:** GPIO 45 selects `VDD_SPI` voltage at reset. It is currently safe
  because the backlight is active-high through an NPN, so it is dark at reset —
  but the safety is a consequence of the circuit, not of anything the firmware
  does. It belongs in the board file as a constraint.

### Two rails the vendor calls unused are loaded on the schematic

- **Claim:** S1 lists ALDO1 and DLDO1 as unused. S3 shows ALDO1 (pin 18) driving
  the `+3V3` net and the `DLDO1/DC1SW` pin (pin 20) driving `SPK_VDD`.
- **Source:** S1 vs S3 sheet 1.
- **Status:** **CONFLICTING.** The `DLDO1/DC1SW` half is reconcilable — one pin,
  two selectable functions. The ALDO1 half is not.
- **Impact:** if the schematic is right, `+3V3` is switchable and carries the
  accelerometer, RTC, haptic driver, microphone and IR emitter. That is the
  difference between a deep-sleep state that works and one that silently keeps
  five parts alive. Resolve on hardware — OPEN_QUESTIONS H8.

### Smaller findings

- BMA423 `INT2` is bonded out but not routed (R12, R15 not fitted). Only `INT1`
  → GPIO 14 exists, so all accelerometer events share one line.
- The PMU drives a charge-indicator LED on `CHGLED` through R182 (100 Ω). It is
  configured over I2C, not by a GPIO.
- USB `D−`/`D+` land on GPIO 19 / GPIO 20 — the S3 native USB pins — so
  USB-Serial-JTAG and USB-OTG are both physically available.
- An `MS412FE` rechargeable cell backs the RTC through D14 (1N4148) and 10 kΩ;
  the GNSS daughterboard carries a second one for hot start.
- A `MSK12C02-HB` slide switch sits in series with the battery. Firmware can
  neither sense nor override it.
- The microphone `SELECT` pin is strapped (R80, R81 not fitted).
- The backlight is one series × three parallel LEDs, I_F = 3 × 15 mA →
  **45 mA at full brightness**, V_F 3.0–3.3 V. Panel is `QT154C2408`.
- The touch `RESET` pull-up R39 is marked `4K7/NC` — not fitted. This is the
  mechanism behind the vendor's warning that a slept touch panel never wakes.

### The schematic's own title block is wrong

- **Claim:** the file published as the S3 schematic has a title block reading
  `T_WATCH-2020&GPS_V08`, Rev V1.4, Friday 8 January 2021.
- **Source:** S3, all six sheets.
- **Impact:** the contents are unambiguously S3-class, so this is a stale
  nameplate rather than the wrong document. But it means the drawing cannot be
  used to establish which board revision anything applies to. Revision still
  comes from inspecting a physical unit — OPEN_QUESTIONS **D20**, the row that
  question was carved into. A1 is struck and marked ANSWERED.

---

## Read from the Waveshare schematic (S6)

The same gap the T-Watch had: the schematic was cited but not read, while the
Waveshare part inventory rested entirely on the vendor README and BSP — the same
BSP already demonstrated to be an incomplete description of its own board.

### The Waveshare board **does** have haptics — the earlier entry was wrong

- **Claim:** a vibration motor on pads `P1`/`P2`, driven from **GPIO 18** through
  R12 (4.7 kΩ) into Q1 (MMBT3904, NPN), with the motor supplied from **BLDO2**.
- **Source:** S6, net `MOTOR`.
- **Correction:** the matrix previously recorded "Haptics — none found", because
  a search for haptic *driver parts* found none. There is no driver IC — the
  motor is switched directly by a GPIO. Searching for the wrong noun produced a
  false negative, and it was recorded with the same weak argument-from-absence
  that the magnetometer claim used to rest on.
- **Impact:** both boards have haptics and the two implementations are not
  interchangeable. The T-Watch has a DRV2605L with a waveform library, an I2C
  interface and a rail-warmup latency; the Waveshare board has on, off and PWM.
  the product capability `Haptics` is available on both and means materially
  different things at the hardware layer — the clearest live justification for
  keeping a typed descriptor below the service boundary
  ([ADR-0007](../adr/0007-two-capability-layers.md)).

### The `R8` in ESP32-S3R8 means octal PSRAM, and the datasheet says so

- **Claim:** the PSRAM in an `ESP32-S3R8` is octal, not quad.
- **Source:** ESP32-S3 Series Datasheet v2.2, §1.2 Table 1-1 "ESP32-S3 Series
  Comparison", p. 13: `ESP32-S3R8 | — | 8 MB (Octal SPI) | -40 ~ 65 °C | 3.3 V`.
  The table contains **no 8 MB quad in-package variant at all** — the only quad
  in-package parts are the 2 MB `RH2`, `R2` (EOL) and `FH4R2`. Footnote 3 names
  the octal set outright: "For chips with Octal SPI PSRAM (ESP32-S3R8,
  ESP32-S3R8V, and ESP32-S3R16V)…". `R8` and `R8V` differ by `VDD_SPI` voltage,
  3.3 V against 1.8 V, not by bus width.
- **Corroboration:** five of the six vendor examples for the Waveshare board ship
  `CONFIG_SPIRAM_MODE_OCT=y` with `CONFIG_SPIRAM_IGNORE_NOTFOUND` unset — a build
  that aborts at boot if octal PSRAM is not found. And GPIO33-37, which Datasheet
  Table 2-14 populates as DQ4-DQ7 and DQS **only** in the Octal SPI column, sit
  unrouted on the schematic with no-connect markers. That is a falsification test
  the board passed: any of those five routed to a peripheral would have refuted
  octal.
- **Status:** VERIFIED for the Waveshare (D12a). **Not transferred to the
  T-Watch** (D12b): the same marking implies the same answer, but a LilyGO
  document describing that board's PSRAM as QSPI has not been re-read against
  Table 1-1 and stands as a live conflict.
- **Why it is written down:** OPEN_QUESTIONS recorded this as recollection —
  Espressif's scheme is "*understood* to use the `R8` suffix for octal PSRAM —
  that last part is recollection and must itself be checked against the
  datasheet". It has been.

### The Waveshare main I2C bus carries six devices, not four

- **Claim:** the ES8311 audio codec and the ES7210 microphone ADC are I2C control
  slaves on the same bus as the touch, PMU, IMU and RTC.
- **Source:** the vendor BSP creates one `i2c_master_bus`
  (`esp32_s3_touch_amoled_2_06.c:93`) and hands that same handle to the ES8311
  (`:262`), the ES7210 (`:310`) and the touch IO (`:494`).
- **Why it matters:** both parts appear in HARDWARE_MATRIX as "I2S", which is
  their *data* path. Their control path is two more addresses on SDA 15 / SCL 14,
  and a board profile that omits them is wrong about the bus.
- **Status:** VERIFIED from vendor source. Each address is in HARDWARE_MATRIX
  with its own citation; `0x18` and `0x40` are both schematic-strapped.

### Waveshare memory: 32 MB flash, 8 MB PSRAM

- **Claim:** external flash is `GD25Q256EYIGR` (U3) — 256 Mbit quad SPI, i.e.
  **32 MB**. The SoC is a bare `ESP32-S3R8`, not a module.
- **Source:** S6.
- **Impact:** resolves D1. Twice the T-Watch's flash, on the board with 3.57×
  the pixels.
- **What this does NOT settle, and an earlier version of this entry said it
  did:** both boards carry the `R8` marking, and it is tempting to read that as
  one question with one answer for both. It is not. `R8` is verified as octal on
  the Waveshare — see *The R8 in ESP32-S3R8 means octal PSRAM* above — and that
  is **D12a**. **D12b**, the T-Watch, stays `CONFLICTING`: a LilyGO document
  describes that board's PSRAM as QSPI, and the marking implying otherwise is an
  inference, not a reading. This paragraph used to assert the transfer, twenty-
  five lines below the section that splits it, so the answer a reader got
  depended on which one they landed on first — which is the exact propagation
  failure this file exists to prevent, committed inside the change that fixed
  three others.

### The Waveshare board has buttons; its BSP does not

- **MEASURED / VERIFIED:** the assembled case has two pressable keys. The
  current official schematic names them PWR and BOOT: PWR pulls the AXP2101
  `PWRON` input and BOOT pulls GPIO0 low. `SYS_OUT/GPIO10` reports PMU system
  state; it is not a PWR-key mirror. The AXP2101 IRQ net terminates at `EXIO5`,
  not at an ESP32-S3 GPIO, and no I/O expander is fitted on this board. The
  vendor BSP's empty button list describes only what that BSP drives.
- **Physical result:** two BOOT presses on 2026-08-25 produced two debounced
  `physical boot down/up` pairs after routing through `core::InputQueue` with
  `InputOrigin::Physical`. PWR negative/positive edges are enabled and polled
  through AXP2101 interrupt status and were also physically observed. A
  physical PWR wake attempt on `8f098ba` did not wake through GPIO10; a later
  touch was reported as the wake cause. On the corrected path, two cycles woke
  directly from GPIO38 touch and a third woke from a 100 ms timer after the
  AXP2101 status reported the physical PWR edge; the transition was classified
  as Button and the panel restored.
- **Sources:** the current Waveshare schematic/product page and the raw bench
  transcript in [WATCH_CONTROL_2026-08-25](../hardware/WATCH_CONTROL_2026-08-25.md).

### Waveshare AXP2101 rail map, and a 1.8 V rail

- **Claim:** ALDO1 → `VL1_3.3V`, ALDO2 → `VL2_3.3V`, ALDO3 → `VCC3V`,
  ALDO4 → **`VL3_1.8V`**, BLDO2 → the vibration motor.
- **Source:** S6.
- **Impact:** the vendor BSP does not configure the PMU at all, so this map is
  the only description of the board's power topology that exists. The 1.8 V rail
  matters: something on this board is not 3.3 V, and identifying it is a
  prerequisite for any level assumption — D13.

### A conflict about the SD card interface

- **Claim:** the BSP configures SDMMC 1-bit on GPIO 1/2/3. The schematic labels
  those same nets `MOSI`, `SCK` and `MISO`, and shows a chip-select near GPIO 17.
- **Source:** S7 (BSP) vs S6 (schematic).
- **Status:** **CONFLICTING** — or, more likely, one board wiring that supports
  both modes with the BSP choosing one. Either way the chip-select on GPIO 17 is
  a pin the pin map did not have. D14.
- **Still conflicting after S13, and the reading that says otherwise is wrong.**
  The factory firmware's boot log shows the vendor's *software* selecting the
  SDMMC host driver — into an **empty** slot, where `send_op_cond` times out for
  every possible wiring alike. That moves the conflict from *"two documents
  disagree"* to *"two documents disagree and the vendor's running firmware sides
  with the BSP"*, which is worth having and is not a measurement of copper.
  Two things this second reading cannot supply either: on the ESP32-S3 the SDMMC
  slots route through the GPIO matrix, so *"any GPIO may be used for each of the
  SD card signals"* and the pin numbers constrain nothing; and the two modes
  share the card's own contacts **by specification** — pin 2 is `CMD`/`DI`,
  pin 5 `CLK`/`SCK`, pin 7 `DAT0`/`DO`, with SPI adding only a chip select on
  pin 1 — so `MOSI` and `CMD` are one net rather than two readings of one.
  (This bullet used to argue that from ESP-IDF's
  `SDSPI_DEVICE_CONFIG_DEFAULT()` instead; at v5.4 that macro fills in `gpio_cs`
  and three `GPIO_NUM_NC`s, and its struct has no clock, MOSI or MISO field, so
  the quote did not support the claim. Same conclusion, sounder reason.) **No card
  has ever enumerated on this board.** [#131](https://github.com/hleserg/Attadipa/issues/131);
  the procedure that would settle it is
  [SD_CARD_MODE_TEST](../hardware/SD_CARD_MODE_TEST.md), `NOT EXECUTED —
  HARDWARE REQUIRED`.

### What is still not resolved from this schematic

Text extraction from a schematic PDF recovers part numbers and net names
reliably and pin-to-net adjacency only sometimes. Two things need the sheets read
visually rather than greped:

- the `J3` expansion header pinout — at least 29 pins (D3);
- which loads sit on which of the three 3.3 V rails (D13).

Recorded as PARTIAL rather than left blank, so the gap is visible.

---

## Read off a physical Waveshare unit (S9)

One `ESP32-S3-Touch-AMOLED-2.06` arrived on 2026-08-22 and was opened. Everything
below is silkscreen, a printed label, or an empty footprint — the three things a
photograph is actually good for. Nothing here rests on a marking that needed
magnification the camera did not have, and the items that do need one are in
[WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §3 as bench readings
still to take.

### The Waveshare cell is *marked* 400 mAh — and probably does not hold it

- **Claim:** the battery is a `402728` pouch cell manufactured 2026-07-11,
  **labelled 3.7 V, 400 mAh**. `402728` is the geometry: 4.0 mm × 27 mm × 28 mm.
  **What is verified is the reading of the label, not the capacity behind it.**
  400 mAh at 3.7 V in 3.024 cm³ implies 132.3 mAh/cm³, against an 87–102 band
  observed across 51 datasheet cells from four manufacturers at footprints
  ≤ 32 mm — +22 % on the densest cell in that sample. Honest expectation
  **250–310 mAh**, `ESTIMATED` —
  [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1. One weighing settles it (T-106 M3):
  6.0–6.5 g is consistent with 280–330 mAh, and only 7.5–8 g with a real 400.
- **Source:** S9 — printed on the cell's own label.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `UNKNOWN` — the schematic shows the cell on `BAT1` through the AXP2101
  charge path and states no capacity, and the vendor README does not either.
- **Impact, and it is the largest single thing the unit told us.** The T-Watch
  S3 Plus carries 940 mAh (S1). This board carries 400 and drives an **emissive**
  panel, where what is drawn decides what is drawn *from*. The day theme's
  gamma-decoded emissive load is 13.9× the night theme's on the same pixels
  (`ESTIMATED`, [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §1). The expensive
  theme and the small cell are on the same board. That does not by itself yield
  a runtime — that needs a measured panel current at a known APL, which is
  `UNKNOWN` — but it makes "which theme is default here" a power decision rather
  than a taste one. T-095.

### The ten-pad expansion row, and two of its pads are the I2C bus

- **Claim:** ten plated pads along the board's bottom edge, silkscreened
  `VBUS · GND · D+/IO20 · D-/IO19 · IO15 · IO14 · RXD · TXD · GND · 3V3`.
- **Source:** S9 — each pad is individually labelled in silkscreen.
- **Board revision:** as above.
- **Impact:** `IO15` and `IO14` are printed as bare GPIO numbers and are **the
  main I2C bus** (S6: `SDA 15, SCL 14`), carrying the AXP2101, the PCF85063ATL,
  the FT3168, the QMI8658, the ES8311 and the ES7210. Driving them as
  general-purpose pins takes down power management, the clock, touch and motion
  at once. The only genuinely free channel on this row for an attached Attadipa
  node is `RXD`/`TXD`. T-096.
- **Not the same thing as `J3`** — the 29-pin header D3 is still open about. This
  row is separate and is now fully known.

### The IMU's board-frame axes are printed next to it

- **Claim:** a silkscreened axis triad beside the IMU: **X** toward the battery
  edge, **Y** toward the USB-C edge, **Z** drawn as ⊙ — out of the face the part
  is mounted on, which is the face turned away from the display.
- **Source:** S9.
- **Board revision:** as above.
- **Impact:** half of OPEN_QUESTIONS **H15**. The board frame is now known; how
  the board is rotated inside the case is not, and a wrist-raise gesture needs
  both. Cheap to finish: tilt the assembled watch through known angles and read
  raw axes.

### The vibration motor is not fitted

- **Claim:** the `MOTOR` pads (`P1`/`P2`) are bare — no solder, no wire, no part — and
  the coin-motor footprint beside them is empty, on the unit received. The drive
  circuit S6 describes (GPIO 18 → R12 → Q1 → BLDO2) is present and correct.
- **Source:** S9. **Designator corrected 2026-08-22** — these pads were recorded
  as `J1`, which is in fact the *battery* connector; see
  [BATTERY_UPGRADE](BATTERY_UPGRADE.md) §1.1. The correction also resolves an
  internal contradiction: the battery plug is visibly mated to a two-pin header on
  this unit, so `J1` cannot have been two bare pads. **The finding is unchanged.**
- **Board revision:** as above. **`OBSERVED` on one unit, not `VERIFIED` for the
  product** — whether Waveshare ships a motor loose, whether another production
  run populates it, and what the listing promises are three unanswered questions.
- **Impact:** `Capability::Haptics` resolves to `Unsupported` on this unit, and
  `Unsupported` is the terminal value in the `Availability` enum — the one that
  must be stable at runtime and must never be offered to the user as fixable.
  Nothing in firmware can tell an NPN driving an absent motor from one driving a
  present motor, so this cannot be detected and must be configured. T-097.

### The flash is a separate package, and it is GigaDevice

- **Claim:** a GigaDevice-branded SOP-8 sits beside the SoC. The brand is legible;
  the part number is not.
- **Source:** S9, corroborating S6's `GD25Q256EYIGR` at `U3`.
- **Impact:** modest but structural. Whatever is in the SoC package is **not
  flash**, which is what an `R8` suffix means. It corroborates the octal-PSRAM
  conclusion without re-deriving it. Capacity is now also measured on silicon:
  `flash-id` and Attadipa's flash boot both reported JEDEC `c8 40 19`, 32 MB —
  [BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md).

### Both microphones are populated

- **Claim:** two MEMS microphones, silkscreened `MIC1` and `MIC2`, at opposite
  ends of the board's left edge, both fitted.
- **Source:** S9, confirming S6's "dual digital microphones" on the ES7210.

### The speaker is an AAC part on wires, not a connector

- **Claim:** `AAC210602A1`, lot `15771`, a metal-can micro-speaker in the back
  cover, its red/black pair soldered to `+`/`−` pads at the board's bottom-right.
  Impedance and rated power are not published for this part number — `UNKNOWN`.
- **Source:** S9.
- **Impact:** small and practical. Both the speaker and any future motor attach
  by solder, so opening this watch twice means desoldering twice.

## Read off the silicon of that unit (S10)

`espefuse v5.3.1 summary` and `esptool v5.3.1 flash-id`, run over the board's own
USB-Serial/JTAG port on 2026-08-22. This is the first evidence in this repository
that came from neither a document nor a camera. The full reading and its
redactions are in [WAVESHARE_EFUSE_READ](WAVESHARE_EFUSE_READ.md); the three
facts that change what may be written are here.

### The die is fused as 8 MB AP Memory PSRAM at 3.3 V — so the part is `R8`, not `R8V`

- **Claim:** `PSRAM_CAP = 8M`, `PSRAM_CAP_3 = False`, `PSRAM_VENDOR = AP_3v3`,
  `PSRAM_TEMP = 85C`. `esptool` renders the same fuses as
  `Embedded PSRAM 8MB (AP_3v3)`. `PIN_POWER_SELECTION = VDD_SPI` puts GPIO33–37
  on the memory rail.
- **Source:** S10.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** D12a was `RESOLVED` by inference from the package marking against
  ESP32-S3 Series Datasheet v2.2 Table 1-1.
- **What it does and does not prove.** It proves the capacity, the vendor and the
  3.3 V rail on *this die*, which eliminates `ESP32-S3R8V` (1.8 V) and every 2 MB
  quad variant. It does **not** state bus width. The step from "8 MB in package"
  to "octal" is still Table 1-1's, and it holds because that table contains no
  8 MB quad in-package part. Both legs of D12a are now supported, one by document
  and one by silicon.
- **Impact:** GPIO33–37 are confirmed unavailable to any application — not
  argued from an unrouted schematic net, but fused. The GPIO budget loses five
  pins for good.

### The flash is outside the package, and the fuses say so

- **Claim:** JEDEC ID `0xC8 0x4019` — GigaDevice, `0x40` = GD25Q SPI family,
  `0x19` = 2^25 bytes = 32 MB. `FLASH_TYPE = 4 data lines` (quad).
  `VDD_SPI_FORCE` and `VDD_SPI_XPD` are set and `VDD_SPI_TIEH` reads
  `VDD_SPI connects to VDD3P3_RTC_IO`, i.e. 3.3 V. `FLASH_CAP`, `FLASH_TEMP` and
  `FLASH_VENDOR` in BLOCK1 are all unprogrammed.
- **Source:** S10.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `VERIFIED` from the schematic alone (`GD25Q256EYIGR` at U3).
- **Impact:** the schematic and the silicon agree, and the three unprogrammed
  BLOCK1 fields independently confirm there is no in-package flash competing for
  the bus. The combination the board actually is — **octal PSRAM in package,
  quad flash outside it, both at 3.3 V** — is now settled from two directions.

### The chip is revision v0.2, and a build must not ask for more

- **Claim:** `WAFER_VERSION_MAJOR = 0`, `WAFER_VERSION_MINOR = 2`; `esptool`
  reports `ESP32-S3 (QFN56) (revision v0.2)`. Crystal 40 MHz. ADC calibration
  (`BLK_VERSION_MAJOR = ADC calib V1`, `ADC1_INIT_CODE_*`, `ADC1_CAL_VOL_*` and
  the ADC2 counterparts) and `TEMP_CALIB` are burned.
- **Source:** S10.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `UNKNOWN` — no document states which revision a shipped board carries,
  and it is not a property of the board design.
- **Impact:** ESP-IDF's `CONFIG_ESP32S3_REV_MIN_*` gates boot. A build whose
  minimum revision exceeds 0 will be **refused by the bootloader on this unit**.
  Nothing sets it higher today; this is recorded so nobody raises it blind. The
  burned calibration fuses mean ESP-IDF's ADC calibration works rather than
  falling back to a nominal curve. **Which errata apply to v0.2 is no longer
  `UNKNOWN`**: the ESP32-S3 Errata sheet was read on 2026-08-22 —
  **v1.3, released 2025-03-31, md5 `64ffc580e78b5ab3c6c5d990e0500e38`** — and
  **all eight apply to v0.2**, seven of them with `No fix scheduled`. There is no
  revision beyond v0.2 in existence, so being on v0.2 is not being behind
  anything ([ESP32S3_ERRATA_V02](ESP32S3_ERRATA_V02.md)). The cost of the
  CACHE-126 workaround is the part that stays `UNKNOWN`, and it is `NOT
  MEASURED`.

### Nothing has been burned — every recovery path is open

- **Claim:** `WR_DIS = 0`, `RD_DIS = 0`, `SPI_BOOT_CRYPT_CNT = Disable`,
  `SECURE_BOOT_EN = False`, all three `SECURE_BOOT_KEY_REVOKE*` false, all six
  `KEY_PURPOSE_*` = `USER` with `BLOCK_KEY0..5` zero, `DIS_DOWNLOAD_MODE = False`,
  `ENABLE_SECURITY_DOWNLOAD = False`, `DIS_PAD_JTAG = False`,
  `SOFT_DIS_JTAG = 0`, `DIS_USB_SERIAL_JTAG = False`, `CUSTOM_MAC` zero,
  `SECURE_VERSION = 0`.
- **Source:** S10. `espefuse summary` reads; it burns nothing, and
  `espefuse burn_efuse` was not run.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Impact:** the unit is in the state the "never irreversible without being
  asked" rule exists to preserve. Download mode, USB-Serial/JTAG and pad JTAG are
  all available, so there is no way yet to brick this board that a reflash cannot
  undo. Recorded as a baseline: a future reading that differs from this one means
  something was burned, and this file says when it was not.

### The vendor's stored asset format, decoded from the vendor's own files

- **Claim:** the three `/image/*.bin` files in the Waveshare `storage` partition
  are each **exactly 411 652 bytes**, being a **12-byte header** followed by
  **410 × 502 RGB565, little-endian**, row-major, uncompressed, with no palette
  and no alpha. The header is `u32` magic `0x00001219` (constant across all
  three, meaning `UNKNOWN`), `u16` width `410`, `u16` height `502`, `u32` stride
  `820` = width × 2. `12 + width × height × 2` equals the file length exactly.
- **Source:** S11 — the `storage` partition of the received unit, extracted with
  `tools/flash/spiffs_extract.py`.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Was:** `LIKELY` RGB565 — an inference from "raw binary on a QSPI AMOLED".
- **What "little-endian" means here, exactly, because the scope is the whole
  fact.** Decoded as a little-endian `uint16_t` array the files are coherent
  artwork; decoded big-endian they are noise. That test ran on a host, over the
  bytes of a file. **It establishes the order the bytes sit in on disk and
  nothing beyond that** — the renderer was Python, not the panel.
- **Was, and this is the correction:** until 2026-08-23 this entry ended *"the
  panel's native pixel format and byte order are facts about the hardware"*. The
  **pixel format** half survives (below). The **byte order** half did not: a host
  render never crosses the display driver, and a driver that byte-swaps on the
  way out makes the same stored file correct for the opposite bus order. See the
  next entry, which traces a real path on this board that **does** swap.
- **Impact:** T-034's target *format* is no longer a preference — 16 bits per
  pixel, RGB565, is what the controller is put into (next entry, `COLMOD` `3Ah`
  = `0x55`). The **transfer byte order is a separate fact and is `UNKNOWN`** —
  D21. The vendor's *header* is not a hardware fact at all, and is worth noticing
  rather than copying: it carries width, height and stride but no format field,
  which is the field needed the moment a second format exists. Also: three full
  frames cost 1.18 MB, which is what an uncompressed full-screen asset costs on
  this panel.

---

## Read from upstream source at pinned revisions (S14)

Software, not silicon. Everything in this section is a fact about what a program
does, established by reading it at a named commit. It is admissible here — this
file's own preamble ends its list with *"or the upstream source itself"* — and it
is **weaker than a datasheet about the same subject**: it says what one
implementation does, not what the part requires. Where the two would answer the
same question, the datasheet wins and the entry says so.

### The panel driver does not swap pixel bytes — the layer above it does

- **Claim, and it is a claim about software rather than silicon:** on the one
  complete display path for this exact board that is readable in pinned source, a
  **software byte swap is applied to every RGB565 pixel between the LVGL
  framebuffer and the panel**, and nothing below that point swaps again. So the
  bytes that reach the CO5300 are in the **opposite** order to the host-native
  `uint16_t` the file holds. The controller is put into 16-bit mode by `COLMOD`
  (`3Ah`) `= 0x55`.
- **Source:** S14 — four upstream sources, read at pinned revisions on
  2026-08-23, in the order the pixel travels:

  | Step | Where | What it does |
  |---|---|---|
  | 1 | `78/xiaozhi-esp32` @ `bb9122ab`, `main/display/lcd_display.cc:160,166` | `SpiLcdDisplay` — the class this board's file subclasses (`main/boards/waveshare/esp32-s3-touch-amoled-2.06/esp32-s3-touch-amoled-2.06.cc:77,104`) — configures `esp_lvgl_port` with `.color_format = LV_COLOR_FORMAT_RGB565` **and** `.flags.swap_bytes = 1` |
  | 2 | `espressif/esp-bsp` @ `2f51931`, `components/esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c:739-741` | the flush callback calls `lv_draw_sw_rgb565_swap(color_map, len)` when that flag is set |
  | 3 | `lvgl/lvgl` @ `v9.5.0` (`85aa60d1`), `src/draw/sw/lv_draw_sw_utils.c:149-171` | that function is a plain in-place 16-bit byte swap: `((x & 0xff00) >> 8) \| ((x & 0x00ff) << 8)` |
  | 4 | `espressif/esp-iot-solution` @ `5d75f3f0`, `components/display/lcd/esp_lcd_sh8601/esp_lcd_sh8601.c:279-280` | `panel_sh8601_draw_bitmap` passes `color_data` **verbatim** into `esp_lcd_panel_io_tx_color(io, LCD_CMD_RAMWR, …)`. No transform. `esp_lcd_co5300_spi.c:291-292` is identical. *Verbatim* is about the **buffer**, and it is compatible with [`WAVESHARE_ARRIVAL`](WAVESHARE_ARRIVAL.md) **§3.3**'s note that the bare `tx_color()` at this same line was wrapped in an error check at `e5b9295a`, before this revision: that change is about the **return value**. Neither transforms a pixel. Spelled out because a reader should not have to infer it across two documents, which is this entry's whole subject |
  | — | same file, `:86-89` (and `esp_lcd_co5300_spi.c:88-91`) | `bits_per_pixel == 16` → `colmod_val = 0x55`, written to `LCD_CMD_COLMOD` = `0x3A` (ESP-IDF `v5.5.1 esp_lcd_panel_commands.h:40`) |

  Fetched by raw URL at those revisions and hashed, so a later reader can tell
  whether they are looking at the same bytes: `esp_lcd_sh8601.c`
  `9f2dacb388c2c3d67d791fd4f5be0d724e826a9cbf74766427639996ddbe1d51`,
  `esp_lcd_co5300_spi.c`
  `c415dadcc75d4c3c90defe6ffa61db1587eefd7575cd7fdddd6ac7df02907aa5`,
  `lcd_display.cc`
  `57cc3591789a2d42a3302b69c931ad9226abe604a75e602d70a77e16b8d5ab9c`,
  `esp32-s3-touch-amoled-2.06.cc`
  `caad7b6a48ca344f1ee0ee5f1a12d6111a4be611e009c267a3955eddb4e841f2`,
  `esp_lvgl_port_disp.c`
  `4a1bcfd9088b6216ff33509dfc15c86886426d545012568e2f21f77239c3b0f0`.
  None of them is committed here.

  **The two LVGL files, hashed the same way and by a different route.** They
  were omitted in the first two rounds, which left the only claims in this
  entry without the section's own provenance mechanism sitting on LVGL — and
  one of those is the *correction* to a finding whose whole substance was that
  an unread mechanism had been stated as fact here. Found in the third review
  round of [#152](https://github.com/hleserg/Attadipa/pull/152). At
  `lvgl/lvgl@85aa60d18b3d5e5588d7b247abf90198f07c8a63` (the commit `v9.5.0`
  points at): `src/draw/sw/lv_draw_sw_utils.c`
  `9fcad9796d421f99a88ceae4c498d9e23042c82e809a67df90370b2b44874a5b`,
  `src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.c`
  `b25dfda8103b8c5844b06d705fafc341533bcf82f7b87c069f6d53e775580c5b`.

  **Route stated, because it is not the same one.** The five above were fetched
  by raw URL. These two were hashed out of the `FetchContent` checkout this
  repository's own simulator build produces, whose `git rev-parse HEAD` is that
  same commit — so the bytes are pinned to a revision either way, but a reader
  reproducing them fetches where the other five were fetched and gets the same
  digest, or the tag has moved and that is itself the finding. Said rather than
  glossed: a provenance note that describes a route it did not take is the
  defect this whole mechanism exists to prevent.
- **Checked:** 2026-08-23.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06` — step 1 is that board's own
  upstream file, not a sibling's. Note the trap
  [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) records: the same tree carries an
  `esp32-c6-touch-amoled-2.06` directory, three characters different and a
  different SoC. This is the S3 one.
- **What this does and does not settle.** It settles that **"stored
  little-endian" does not imply "transferred little-endian"** on this hardware,
  by exhibiting a path where it is false. It does **not** settle the controller's
  own transfer order as a hardware fact: that would need the CO5300 datasheet on
  `3Ah`/`2Ch` bit packing (D7 has never been read) or a measurement. And it is
  **software**, read from a repository — not a datasheet, not a schematic, and
  not run here. `NOT EXECUTED — HARDWARE REQUIRED`.
- **And the file that carries those pixels was rendered by an app this trace does
  not cover.** `otadata` is blank, so the bootloader falls through to `factory`,
  which is `phone_s3_box_3 v0.4.2-92-g5c6be6c-dirty` — Waveshare's port of
  `espressif/esp-brookesia`, 92 commits past a tag and built from a modified
  tree, unpublished. `xiaozhi` 1.8.5 sits in `ota_0` and has never been selected
  ([WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §2.1). So the trace above
  is a **real path on this board**, and it is **not the path that drew those
  three wallpapers**. What that app's `swap_bytes` was is not readable, which is
  why D21 is `UNKNOWN` rather than resolved in either direction.
- **Impact:** nothing in this repository is mis-encoded today — T-034 emits
  `LV_COLOR_FORMAT_A8` masks (`tools/assets/generate_images.py:175` "--cf"), one byte
  per pixel, which have no byte order to get wrong. The cost lands on **the
  first line of display bring-up**, which must take the swap setting from a
  measurement or from the datasheet and must not take it from this file's
  sibling entry above.

  **D21 does not reach `ui/assets/`.** An asset's byte order is not a fact about
the panel: it is fixed by LVGL's colour-format contract and has to match the
framebuffer the software renderer writes into, and the wire order is absorbed
exactly once, at flush, by the display port's `swap_bytes` flag — which is what
the four-step trace above demonstrates. An earlier version of this entry told the
first colour asset to take its setting from a measurement or the datasheet, which
replaced one boundary-crossing inference with another, one layer up. Two
consequences, both checkable in this repository: for `RGB565A8`, the format
[`DEPENDENCIES.md`](DEPENDENCIES.md) names as *"what the mascot art needs"*, the
instruction is **not executable** — the vendored converter packs it
`uint16_t(color)` in host order and offers no swapped variant, `--cf` having
`RGB565_SWAPPED` but no `RGB565A8_SWAPPED`; and for plain `RGB565` it is
**pointless rather than wrong**, which is a correction — an earlier version of
this entry said it *"produces wrong colours either way"* because emitting
`RGB565_SWAPPED` against a port that also swaps *"mangles red and blue"*. **It
does not, and this was read rather than reasoned.** LVGL declares the format in
the descriptor's own header (`LVGLImage.py:124` "RGB565_SWAPPED = 0x1B"), and at
the pinned `lvgl@85aa60d1` (v9.5.0, [`REUSE_LEDGER`](REUSE_LEDGER.md))
`src/draw/sw/blend/lv_draw_sw_blend_to_rgb565.c:409-412` dispatches a
`LV_COLOR_FORMAT_RGB565_SWAPPED` **source** to `rgb565_swapped_image_blend()`
at `:935`, which un-swaps as it blends — `:966` mixes
`lv_color_swap_16(src_buf_u16[x])` into a native destination, and the opaque
fast path at `:955-956` copies the line and then runs
`lv_draw_sw_rgb565_swap()` over it. `sim/lv_conf_simulator.h:216` —
"LV_DRAW_SW_SUPPORT_RGB565_SWAPPED" — compiles that path in. So a pre-swapped asset renders correctly and merely pays a conversion
per blend that a native-order one does not — which is a reason not to emit one,
not a colour bug. The other half of the old claim stands and needed no
correction: turning the **port's** swap off breaks everything LVGL draws itself
— text, arcs, the watch face, and every `A8` icon — because the `ColorRole`
colour a mask is blended with lands in the same framebuffer as native-order
`lv_color16_t`. **It does not "match" a pre-swapped asset either**, which this
sentence said until the fourth review round of
[#152](https://github.com/hleserg/Attadipa/pull/152): by the time the asset is
in the framebuffer LVGL has un-swapped it into native order along with
everything else, so it breaks with everything else. There is no configuration
that leaves the asset right and only the glyphs wrong. So: **the colour asset's
byte order follows LVGL's framebuffer format. D21 governs one board-level knob
in the display port and nothing under `ui/assets/`** — and because that knob is
a board fact it belongs in `boards/`/`platform/`, not in settings and not in a
build flag. Found in review. **Which framebuffer format that is, is T-093's**:
`RESOURCE_BUDGET.md`'s Avoidability row keeps a swapped destination live as an
open input, and on that branch a pre-swapped asset is the free one. The rule
survives the decision either way; the two absolutes above do not, which is why
they are stated as consequences of today's configuration rather than as
constants.
- **And it is a per-frame cost, not only a correctness question.** On the one
  readable path every pixel goes through `lv_draw_sw_rgb565_swap()` on the LVGL
  flush path — software, in place, over the flushed region. A Waveshare full
  frame is 205 820 px / 411 640 B
  ([`RESOURCE_BUDGET.md`](../architecture/RESOURCE_BUDGET.md) §3), so on
  PSRAM-backed buffers that is a second full pass over 402 KiB against the same
  cache-coherency requirement `ESP32S3_ERRATA_V02` already flags, and per-frame
  CPU time is battery. **Whether this device needs the swap at all is `UNKNOWN`
  (that is D21), and what it costs when it is needed is `UNKNOWN` too** — neither
  is measured and neither may be assumed away. It is an input to **T-093**, the
  draw-buffer and frame-rate ADR, which is the decision a mandatory full-buffer
  software swap would change the answer to. Recorded because the trace found it
  and filed it only as a correctness question. Found in review.

### The Waveshare `storage` partition holds six files, not three, and three are music

- **Claim:** alongside the three images there is a `/music/` directory holding
  `BGM_1.mp3` (207 713 B, MPEG-1 Layer III, 112 kbps, 44.1 kHz, **mono**),
  `BGM_2.mp3` (199 664 B, 112 kbps, stereo) and `BGM_3.mp3` (380 917 B, 128 kbps,
  stereo, with a 139 756-byte ID3v2.4 tag that is mostly embedded artwork).
- **Source:** S11, same extraction.
- **Was:** a parallel reading of the same partition recorded *"only three real
  files, all raw binaries in an `/image/` dir"*. That was `strings`-derived and
  incomplete.
- **Impact, and it is not about audio formats.** The board ships 788 kB of music
  and a `MusicPlayer` app to play it. Taken with the grille slot in the case wall
  and the separate motor pads at `P1`/`P2`, this makes the reading that
  `AAC210602A1` is a *haptic actuator* very hard to sustain — see T-105, which
  now has a strong prior. It is **not** `VERIFIED` by this alone: stereo source
  material decoded to one transducer is still mono output, and only tracing the
  pads settles it.

### The factory image carries somebody else's licensed music

- **Claim:** `BGM_1.mp3`'s ID3 frames read verbatim
  `All Rights Reserved to www.Art-list.io` and `Levitate by Ryefield`.
- **Source:** S11, same extraction.
- **Impact:** the factory flash image contains **commercially licensed
  third-party audio under an all-rights-reserved grant**, on top of Waveshare's
  own proprietary binary. Keeping the dump off the repository was until now a
  convention rather than a written rule — no prior change has committed a vendor
  binary and none had needed to say why — and review on
  [#80](https://github.com/hleserg/Attadipa/pull/80) was right that "the
  existing rule" cited a document that did not exist. **It is a rule as of this
  record**, and this is the second and sharper reason for it,
  because republishing the dump would redistribute somebody else's licensed
  audio. The extracted files and the rendered PNGs are **not committed** either.
  What is committed is the extractor and the measurements.

### The factory backup of the received unit is verified against the device

- **Claim:** the 33 554 432-byte image of this unit's flash hashes to
  `2ab0fadcf8c71834fc5ac0e9197c1fcec6c71d7a25f1af382d0537f19c33dfd5`, and
  `esptool verify-flash 0x0` — which has the **device** compute the MD5 — returns
  `Verification successful` over the whole 32 MB.
- **Source:** S12 — three complete reads of the received unit's flash: the
  owner's on Windows 11 over native USB, and two here on Linux over USB/IP.
  All three agree byte for byte, per chunk. Method and chunk map in
  [WAVESHARE_FLASH_LAYOUT](WAVESHARE_FLASH_LAYOUT.md) §2.2.
- **Board revision:** `ESP32-S3-Touch-AMOLED-2.06`, unit received 2026-08-22.
- **Impact:** **the first flash of our own firmware is reversible.** That is the
  precondition every bench task on this unit was waiting for, and the reason the
  balance of risk between the two diagnostic routes has shifted.
- **Not committed, and this is the rule not a preference** — the image is
  Waveshare's proprietary binary plus third-party all-rights-reserved audio. It
  lives on the owner's machine.

### Two complete applications ship on this flash, both built with ESP-IDF v5.5.1

- **Claim:** `factory` at `0x100000` holds **`phone_s3_box_3`**
  `v0.4.2-92-g5c6be6c-dirty`, 5 175 184 B, built 4 Nov 2025; `ota_0` at
  `0xa00000` holds **`xiaozhi`** version **`1.8.5`**, 5 481 872 B, built
  31 Oct 2025. Both descriptors give `idf_ver` **`v5.5.1-dirty`**. `ota_1` is
  erased and `otadata` is blank, so `factory` is what runs.
- **Source:** S12 — the `esp_app_desc_t` in each image, parsed at its slot
  offset. Read out of the binaries, not inferred from the wake-word model as an
  earlier record did.
- **Impact:** T-104 must read xiaozhi at **tag `1.8.5`**; reading `HEAD` is
  research into a different program. And `v5.5.1` is the vendor's own answer to
  the IDF-version question T-004 asks — one version about which something is
  *known*, not a recommendation. Note `-dirty` on both: they built from modified
  trees, so it names a starting point, not a reproducible one.

### The stock firmware does not rewrite its own configuration partitions on boot

- **Claim:** `nvs`, `otadata` and `phy_init` (`0x9000`–`0x12000`) are byte-for-byte
  identical across three reads separated by hard resets and ~90 s of running,
  hashing to
  `803798ee52013c09e9dd55a72226d0195ec6a3582f85af3b43315f9247b3e26e`.
- **Source:** S12, plus a direct observation by the owner on 2026-08-22 — the
  device was cycled six times through download mode and back, and the panel
  blinked dark-then-launcher on every cycle. That is what makes the reads a test
  of a *running* firmware rather than of flash in download mode.
- **Impact:** modest but real — a bench procedure on this unit can reset it
  repeatedly without the stock firmware quietly changing the bytes underneath.
  It says nothing about what the firmware writes when a user touches it.

### Only the low 16 MB of this board's flash is bootable, and the vendor ships a partition above the line

- **Claim:** the ESP32-S3 ROM and this board's second-stage bootloader address
  flash with **24 bits**, so `0x1000000` aliases to `0x0`. The vendor's `ota_1`
  partition sits at exactly `0x1000000` and **can never boot**.
- **Source:** S13. A valid, verified app image was written into the erased
  `ota_1` and selected via `otadata`. The bootloader reported
  `segment 0: paddr=01000020 vaddr=3fce2820 size=01700h` and rejected it —
  and `vaddr=0x3fce2820, size=0x1700` is byte-identical to the segment-0 header
  of **the bootloader itself at flash `0x0`**. Independently, `esptool --no-stub`
  refuses the address in words: *"Can't access flash regions larger than 16MB"*.
  The stub flasher has 32-bit addressing, which is why the write verified and the
  boot did not.
- **Impact, and it is a design constraint rather than a curiosity.** **Every app
  partition Attadipa places on this board must live below 16 MB.** On a 32 MB part
  that leaves the upper half for data only, and even that depends on the
  *application's* flash driver having 32-bit addressing — untested here. A
  partition table is not self-validating: `ota_1` is well-formed, correctly sized,
  correctly typed and dead, and no tool in the chain warns about it.

### A PURE_RAM_APP runs on this board — but only if the serial port is never closed

- **Claim:** `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP=y` images load over
  USB-Serial/JTAG and **run**, writing nothing to flash. The four earlier runs
  that reset within milliseconds were killed by `esptool` exiting: the kernel
  changes the DTR/RTS CDC control state on the *last* close of a `ttyACM`, and
  the native USB-Serial/JTAG peripheral resets the digital core. These are USB
  control bits, not GPIO0/EN pins on this board.
- **Source:** S13. Decisive test — `esptool` used as a library in one process so
  the port is never closed (`detect_chip` → `cmds.load_ram` → read `esp._port`
  directly), run against the *same* minimal driverless image that had failed as
  attempt 4. Fifteen seconds watched: no `rst:0x`, no `ESP-ROM:` banner, ESP-IDF's
  own startup log instead. Four further images ran the same way — the bench probe
  for 30 s, the pedometer probe for **two minutes**, the touch probe for 25 s and
  the register restore for 8 s — each running to the end of its watch window.
- **The reset cause was the evidence all along.** `rst:0x15
  (USB_UART_CHIP_RESET)` is by definition a **host-driven** reset through the
  USB-Serial/JTAG peripheral; no misbehaving image produces it. Two other
  host-side causes were correctly eliminated first (`--after no-reset`; pyserial
  asserting DTR/RTS on `open()`), and neither touched esptool's own close.
- **A second, independent cause of silence** applied to attempt 4: it was built
  with `CONFIG_ESP_CONSOLE_NONE=y` and
  `CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1`, so its `esp_rom_printf` output had
  nowhere to go. In RAM images on this board, use `ESP_LOGx` or `printf`.
- **Impact:** **this retracts the earlier entry that recorded the RAM route as
  dead, and the `BLOCKED` that rested on it.** No partition holding vendor
  firmware needs overwriting; read-only bench work on this unit costs no flash
  write at all.

### The Waveshare main I2C bus is pulled up by 2.2 kΩ, not 4.7 or 10

- **Claim:** on `ESP32-S3-Touch-AMOLED-2.06` V1.0, the main I2C bus pull-ups are
  **`R49` = 2.2 kΩ on `SDA` (`GPIO15`)** and **`R23` = 2.2 kΩ on `SCL`
  (`GPIO14`)**, both to `VCC3V3`. Each line also carries **22 pF to `AGND`** —
  `C35` on `SDA`, `C34` on `SCL`. There is exactly one pull-up per line in the
  whole drawing.
- **Source:** the vendor schematic, `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf`,
  md5 `b0cdcac0afb0c8605896d995676c4468`, sha256
  `6d531fb458863c666210c92294a07204d675bcb7997a54fc219d92fadbbacf9d` —
  `HARDWARE_MATRIX` S6, re-read by **rendering** the region around `GPIO14`/`GPIO15`
  and reading the junction dots. The earlier text-only extraction had the value
  string `2.2k` and no way to attach it to a net, which is why this was missing.
- **Confirmed independently the same day, by a third method and a second
  context** that had not made the rendered reading: extracting the PDF's **vector
  paths**, where wires are line segments and junction dots are filled curves, so
  connectivity is coordinates rather than a judgement about a picture. The file
  was re-downloaded and both hashes matched byte for byte. Every value and every
  net reproduced — including the `R23`↔`SCL` / `R49`↔`SDA` assignment, which the
  rendered reading had flagged as its weakest part. Endpoints and dot centres are
  written out in [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md) §4.3.1.
- **Checked:** 2026-08-24. **Board revision:** V1.0, which is the revision the
  received unit's silkscreen matches
  ([WAVESHARE_BOARD_RECEIVED](WAVESHARE_BOARD_RECEIVED.md) §1.1).
- **Not measured on the board.** This is a schematic reading. A fitted part can
  differ from a drawing, and an ohmmeter across `IO15`↔`3V3` on the expansion pad
  row with the board unpowered would settle it in one probe.
- **Impact:** it is the input the magnetometer retrofit's parallel-pull-up
  arithmetic was blocked on. At 2.2 kΩ the bus already sinks 1.32 mA at
  `VOL` = 0.4 V, 44 % of the 3 mA that `UM10204` Rev. 7.0 §7.1 requires every
  device to sink; two 4.7 kΩ module pull-ups take it to 85 %, which passes —
  [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md) §4.3.

### The AK09911C has a reset input, `RSTN`, and it may not be left floating

- **Claim:** the `AK09911C` has a dedicated reset input `RSTN` at ball **`C2`**,
  a `VID`-domain CMOS input that *"Resets registers by setting to `L`"*, with a
  minimum effective low pulse `tRSTL` of **5 µs** and input thresholds of 30 % and
  70 % of `Vid`. It is one of four reset paths, and the datasheet's instruction
  for not using it is explicit: ***"When Reset pin is not used, connect to VID."***
  AKM's own recommended external connection drives it from a host CPU GPIO.
- **Source:** AKM `AK09911` `ShortDatasheet-E-00`, 2014/1, md5
  `1d7e1960c86b2a1fb38ecc862196c4a7` — §4.3 pin table, §5.3.1, §5.3.2, §6, §7,
  §8.2. The md5 matches the copy `MAGNETOMETER_RETROFIT` M1 already cited.
- **Checked:** 2026-08-24.
- **What this does *not* establish:** whether the CJMCU-9911 breakout routes the
  ball to its silkscreened `RST` pad, and whether it ties it to `VID`. Both are
  `UNKNOWN` and need an ohmmeter on a module nobody has yet —
  [MAGNETOMETER_RETROFIT](MAGNETOMETER_RETROFIT.md) §2.6.
- **Impact:** the retrofit is **five wires by default, not four**. It also
  supersedes the weaker source this was previously known from — a comment in
  `drivers/iio/magnetometer/ak8975.c` — which was right and is no longer what
  anything rests on.

### The main I2C bus, scanned from a RAM app — five devices, and 0x6B settles a conflict

- **Claim:** on SDA 15 / SCL 14 at 100 kHz, exactly five devices acknowledge:
  `0x18` (ES8311), `0x34` (AXP2101, `IC_TYPE = 0x4A`), `0x40` (ES7210), `0x51`
  (RTC) and `0x6B` (QMI8658). **`0x6A` does not answer**, and neither does
  `0x38`.
- **Source:** S13, the bench probe running from RAM under the vendor's own power
  configuration. Read-only: every access is an I2C write-then-read whose write
  phase carries a register address and never a value.
- **Impact, in descending order:**
  - The IMU address conflict is **RESOLVED at `0x6B`** by measurement. The
    schematic and QMI8658 revisions 0.8/0.9/A are right; the Rev 0.6 document
    Waveshare's own wiki links, which maps SA0-low to `0x6A`, does not describe
    this board.
  - **`0x0C`, `0x0D` and `0x1E` are free**, so a magnetometer retrofit (T-109)
    has an address to live at.
  - **The touch controller is not reachable** in the state a bare RAM app finds
    the board in — see the next entry.

### The QMI8658 reports REVISION_ID 0x7C, which is the datasheet with a pedometer in it

- **Claim:** at `0x6B`, `WHO_AM_I = 0x05` and **`REVISION_ID = 0x7C`**. The
  register-description sections of the two candidate documents give different
  values for that byte: **`0x7C` in `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A`**
  (© 2022 QST, 20 June 2022), whose chapter 11 documents a complete hardware
  pedometer, and `0x79` in the `QMI8658C` Rev 0.6 ADVANCE INFORMATION document,
  which marks `CTRL8` *"Reserved: Not Used"* and has no step counter.
  **This entry used to name `13-52-25` for the `0x7C`, was corrected under
  [#341](https://github.com/hleserg/Attadipa/issues/341) to name `13-52-27`
  instead, and the correction turned out not to matter**: both Rev A documents
  have since been read side by side and **both give `0x7C`** in their
  register-description sections. Either citation was right about the byte. What
  neither is is a way to tell the two documents apart — see
  [`VERIFIED_FACTS.md:583`](VERIFIED_FACTS.md) "no register tells them apart".
  Both are 88 pages, both are held off-tree because they are copyrighted and
  marked "Security Level: 3": `13-52-27` md5 `e093b1cc1d1cf85097f955abbea65c08`,
  `13-52-25` md5 `5a0fef65a358430d6499944a75d22e19`.
- **Corroborated by writing, not only by reading.** With the accelerometer
  configured per `13-52-27` Table 22 — `CTRL2 = 0x26` (±8 g, 125 Hz; that is the
  *"ODR Rate (Hz) (Accel only)"* column, which is the one that applies because
  `CTRL7 = 0x01` leaves the gyro off — the same `aODR = 0110` row reads **112.1
  Hz** in the adjacent *"(6DOF)"* column, which is what
  [PEDOMETER_BENCH_2026-08-28](PEDOMETER_BENCH_2026-08-28.md) records for its
  gyro-enabled run), `CTRL7 = 0x01`
  (`aEN`), `CTRL8 = 0x90` (`Pedo_EN` + `STATUSINT` handshake) — all three
  registers acknowledged and **read back exactly as written, `CTRL8` included**.
  The accelerometer then reported a stationary board at
  `(-0.04, 0.26, -1.00) g`, magnitude **1.03 g**: `13-52-27`'s ±8 g / 4096 LSB-per-g
  scaling produces gravity to within 3.4 %, so the full-scale encoding matches
  the silicon too. The registers read `CTRL2 = 0x24, CTRL7 = 0x03, CTRL8 = 0x00`
  beforehand — the vendor's firmware had the IMU configured and running.
- **Source:** S13, `pedoram` probe from RAM. It writes those three IMU control
  registers and nothing else on any device; the QMI8658 has no non-volatile
  configuration, and the probe restores the defaults on exit.
- **Impact:** **H14 resolves — both halves.** `13-52-27` is the register map to
  program against, and the part name on the schematic (`QMI8658C`, printed twice)
  did not predict it. This is [ADR-0003](../adr/0003-radio-not-lora.md)'s lesson
  in a second subsystem. OD-6's mandatory pedometer has a documented hardware
  engine to use.
- **NOT EXECUTED — HARDWARE REQUIRED:** that the pedometer *counts*. Step count
  stayed 0 and `STATUS1` stayed `0x00` throughout, on a board lying on a desk —
  which is the correct reading for a stationary board and no evidence either way.
  Chapter 11's engine has to be walked. T-112. **Partly answered on 2026-08-28,
  and not by a walk** — see the entry below and
  [PEDOMETER_BENCH_2026-08-28](PEDOMETER_BENCH_2026-08-28.md).

### The QMI8658 pedometer counted nothing through sixteen unbroken seconds above its threshold

- **Claim:** with the engine configured and the CTRL9 handshake confirmed, one
  159-second run recorded **16** one-second windows whose per-axis peak-to-peak
  exceeded the configured `ped_fix_peak2peak` of **78 mg**, contiguous from
  `t=34s` to `t=49s`. The smallest is **322 mg** and the largest **2242 mg**. The
  step count stayed **0 (`+0`)** throughout and `STATUS1` stayed `0x00`.
- **The milligravity scale of those figures is UNKNOWN.** They are
  `(hi - lo) * 1000 / ACCEL_LSB_PER_G` and no capture records the divisor its
  binary used. `shake.log:49`'s `±8 g` is not a candidate for it: that header is
  one of the four stale labels in `shake.log`, it names a full scale for
  a register meaning ±4 g, and it records no divisor at all. It is the reason to
  doubt the scale, not a value for it. Had the binary divided by 4096 the figures
  would read 161 mg and 1121 mg; either way they clear the 78 mg bar by more than
  2×, so the negative result stands.
- **The motion is a burst, not the whole run.** An earlier draft said the watch
  was shaken for 158 s. It was not: the other 142 windows read 0–1 mg — a board
  lying still. Sixteen consecutive seconds is what this run establishes.
- **A seventeenth window is not motion.** `t=0` also reads above the bar, and a
  second draft called it a pick-up. It is a start-up artefact: the attitude at
  `t=0` matches `t=1` to within 3 LSB on every axis and `t=33` to within 6 —
  `(350, 24, -8257)` against `(353, 27, -8254)` and `(354, 26, -8251)` — and all four of
  the session's *stationary* captures open with the same bad first sample. It is
  excluded from the count.
- **The comparison is indicative, not a like-for-like multiple.** 78 mg is the
  configured `ped_fix_peak2peak` (80 in u6.10), but **UNKNOWN**: chapter 11 does
  not state the window length or the axis combination its own peak-to-peak is
  computed over, so the engine's domain and the probe's one-second per-axis
  window are not known to be the same quantity. An earlier draft divided the two
  and published the ratio; it is withdrawn. What the run establishes is sustained
  motion far above the configured bar with no count — not a numeric multiple.
- **One profile was measured, the other only configured.** The run above used
  SensorLib's bring-up profile — 6DOF with both sensors,
  `configPedometer(50, 80, 60, 400, 8, 1, 0, 1)` with `entry_count = 1` and
  `sig_count = 1` so the register moves on the first step — from
  `examples/sensor/qmi8658_pedometer` at `lewisxhe/SensorLib`
  `2b9e591f245e447d3d00ec8798c3f49b897882d9` (`v0.4.1-123-g2b9e591`).
  **SensorLib is not among this repository's pinned upstreams**, so that is a
  lead at a recorded revision, not a source. Chapter 11's own profile was armed
  and acknowledged for the abandoned walk but never exercised under recorded
  motion. Both `0x0D` calls acknowledged with CmdDone set and cleared on every
  run, so the engine processed both commands. Whether the eighteen
  `CAL1_L..CAL4_H` bytes were in place when it did is **UNKNOWN**: the probe
  discards every `wr()` return for them and never reads them back.
- **A claim this run does NOT support.** The three desk runs found `CTRL7 = 0x03`
  on the board at start-up, and an earlier draft read that as the vendor firmware
  running 6DOF. It is not evidence of that — not because the residue is known to
  be Attadipa's, which a second draft asserted and cannot be supported, but
  because **its writer cannot be identified at all**. `0x24 / 0x03 / 0x00` is
  exactly the state the 2026-08-23 session left, and an SoC restart does not
  reset the parts on the I2C bus
  ([`WAVESHARE_RUNNING_OUR_CODE.md:646`](WAVESHARE_RUNNING_OUR_CODE.md)
  "does not reset the peripherals"), so a five-day-old vendor write surviving is
  as consistent with it as any later one. An unattributable value corroborates
  nothing. The shake run is the one case where the writer *is* known: it found
  `CTRL2 = 0x27, CTRL7 = 0x01, CTRL8 = 0x90`, the *walk probe's* armed state from
  29 minutes earlier.
- **What the vendor firmware configured is separately known.** S13 measured it on
  2026-08-23 with the factory image still present: booting it wrote `CTRL2` to
  `0x24` and `CTRL7` to `0x03` over what a probe had left, and never touched
  `CTRL8` — so the vendor runs the IMU in 6DOF and does not use the pedometer
  engine
  ([`WAVESHARE_RUNNING_OUR_CODE.md:633`](WAVESHARE_RUNNING_OUR_CODE.md)
  "Booting the vendor firmware restored"). An earlier draft called that
  **UNKNOWN**, over-correcting: it is the 2026-08-28 residue that is not evidence,
  not the vendor configuration itself.
- **Source:** **S15** — the unit itself on 2026-08-28, the `pedo` probe **RAM-booted**
  (nothing written to flash), operator the owner, watch attached and hand-moved.
  [PEDOMETER_BENCH_2026-08-28](PEDOMETER_BENCH_2026-08-28.md) carries the run, the
  raw excerpt and the caveats about the archived log headers. (An earlier draft
  cited S14, which is *four upstream repositories read at pinned revisions* and
  says of itself **"this is software, not silicon"** — the wrong kind of source
  for a measurement.)
- **Impact:** OD-6's mandatory pedometer has a documented engine that, so far,
  does not count. This is evidence to plan around, not yet a verdict.
- **NOT EXECUTED — HARDWARE REQUIRED:** the twenty-step walk T-112 asks for.
  **A shake is not a walk and the engine is entitled to reject one** — besides
  amplitude it applies a cadence window and a peak-pattern test this probe cannot
  see, and SensorLib's own example says the engine *"is for periodic gait, not
  random shaking"*. Note also that re-reading the count later depends on **not**
  reconfiguring: the `0→1` edge on `CTRL8` bit 4 clears it, per §11.6 of `13-52-27 ∙ QMI8658C Datasheet ∙ Rev A`
  (© 2022 QST, 20 June 2022), which spells out the very sequence the probe uses —
  *"Host can simply clear the CTRL8.bit4 and then set it to restart the Pedometer
  engine and reset the Step Count registers."* That document reports
  `REVISION_ID = 0x7C`, which is what this silicon reads. **The dispute this
  sentence used to defer to #341 is settled**: two Rev A datasheets exist, and
  the byte is attributed to this one, `13-52-27`, in both places. Nothing here
  ever rested on the number. **That hazard has already cost one result:** the walk attempt
  left the engine armed, and the next run configured before reading, clearing
  whatever the walk had accumulated. The walk also could not be logged: walking
  means unplugging, and the probe reports over the USB serial console, so the act
  of running the experiment destroys the channel that records it. The attempt ends
  in a `SerialException`. Three earlier runs totalling 1380 samples at zero were a
  board lying still on a desk and are **not** evidence.

### The touch controller is held in reset until GPIO 9 is pulsed low then high

- **Claim:** `0x38` does not acknowledge at all when a RAM app that initialises
  nothing else scans the bus. Driving GPIO 9 **high** and holding it changes
  nothing. **Pulsing GPIO 9 low for 10 ms and back high** makes it appear, and it
  then reads chip ID (`0xA3`) `0x64`, firmware version (`0xA6`) `0x02` and vendor
  ID (`0xA8`) `0x11`.
- **Source:** S13, a RAM probe that scans, drives, pulses and rescans in one run —
  five devices, five devices, then six.
- **Impact:** **touch is not reachable just because the I2C bus is up.** The reset
  *edge* is what brings the controller up, not the level, so a BSP that merely
  configures GPIO 9 as a high output at init sees an empty bus and no error. This
  belongs in the board layer. It also confirms the `0x38` address itself, which
  [HARDWARE_MATRIX](HARDWARE_MATRIX.md) had as *"driver source only, no datasheet
  states it"*.
- **UNKNOWN, and not claimed:** which part number `0x64` denotes. `0x11` is
  FocalTech's vendor byte and `0x64` is the chip ID the FT5x06/FT6x36-family
  drivers expect — consistent with an FT3168 behind that driver, but no FT3168
  datasheet has been obtained. T-113.

### The vendor's own boot log, and four things it settled for free

- **Claim, all read from the unit's own firmware booting unaided:** octal PSRAM
  enumerated by the `octal_psram` driver at 80 MHz with 10-cycle fixed read
  latency and 32-byte hybrid-wrap bursts (**D12a confirmed on silicon**); the SD
  slot mounted through the **SDMMC host driver** and failing at `send_op_cond`
  into an empty socket (**D14 half answered — see the correction below**);
  `sh8601: LCD panel create success, version: 1.0.2` followed
  by `Backlight on`; flash booted **QIO at 80 MHz**, `detected chip: gd`, 32 MB;
  `chip revision: v0.2`; `efuse block revision: v1.4`;
  `QMI8658 initialized successfully`.
- **Source:** S13 — the boot log captured from **62 ms** after reset, which took
  resetting over the CDC control lines rather than with esptool; the ordinary
  route reconnects at ~580 ms, by which time the bootloader has already chosen a
  partition and moved on.
- **Impact:** this is the vendor's firmware describing the vendor's board, which
  is a better witness than any inference from a datasheet. Note what it does
  **not** say: the QMI8658 line names no I2C address — the bus scan above
  settles `0x6B` by measurement instead — and the `sh8601` line is evidence about
  the driver rather than about the die.
- **And the same caution applies to the SD line, which is where it was first
  missed.** A log of the vendor's *software* is a fact about the software. The
  slot was empty, so `send_op_cond` timing out is what every wiring produces —
  the line says which host driver the vendor picked and nothing about the
  connector behind it. It was recorded here as *"D14 resolved"* for one day,
  while *"A conflict about the SD card interface"* earlier in this same file went
  on saying `CONFLICTING` — one document holding both readings at once, which is
  the shape this repository is supposed to notice.
  [#131](https://github.com/hleserg/Attadipa/issues/131), and
  the full correction is in
  [WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §4.3.

### Attadipa drives the Waveshare display and touch on the physical unit

- **Claim:** on the Waveshare ESP32-S3-Touch-AMOLED-2.06 unit identified by USB
  serial `28:84:85:B2:18:A4`, Attadipa drives the CO5300 at QSPI 40 MHz through
  the upstream Espressif component. At the commanded `5%` (`12/255`) brightness
  the owner directly observed red, green and blue swatches in the intended
  order and reported the final 28 px primary text readable. The `1%` (`2/255`)
  command was below this unit's useful visible floor.
- **Touch measurement:** six earlier presses on the visible LVGL target
  produced `physical touch 1` through `6`. T-114 then replaced direct LVGL
  registration with the shared `core::InputQueue` producer. A later physical
  tap through that producer changed the live screen to `TOUCH OK 1`; BOOT and
  AXP2101 PWR presses also produced measured `down/up` pairs through the shared
  queue.
- **PMU/RTC measurement:** the candidate preserves unrelated AXP2101 enable
  bits while owning the three rails required by this slice. After a bench-only
  diagnostic set the previously invalid PCF85063 once, the production read path
  observed later times across multiple resets. That proves retain, advance and
  read; absolute accuracy and drift remain unmeasured. An ESP-IDF master-bus
  reset also recovered a restart where a peripheral held I2C busy, while the
  preceding image failed safely with the AMOLED off.
- **Source:** the raw flash/boot/touch transcripts and owner observations in
  [BRINGUP_2026-08-25](../hardware/BRINGUP_2026-08-25.md) §7 and
  [WATCH_CONTROL_2026-08-25](../hardware/WATCH_CONTROL_2026-08-25.md), measured
  2026-08-25 on that unit. USB watch-control `info`, screenshots and a remote
  tap were executed on the physical endpoint.

### The first product Clock runs on the physical Waveshare

- **MEASURED:** the shared simulator/firmware Clock rendered on the physical
  410 × 502 AMOLED with Nunito Sans time, live seconds and date over original
  Attadipa night-meadow art. Four stationary points pulse on the foliage; a
  debug-injected tap launched one glowing point that moved for 1.6 seconds and
  faded on-device. The owner accepted the final physical presentation. The
  earlier gradient render exposed RGB565 partial-buffer bands; the current
  raster render does not.
- **Steps boundary:** the paw and `7777` are a static layout placeholder, not a
  pedometer measurement. No step source was exercised or claimed by this run.
- **Brightness boundary:** the owner-requested demonstration ran briefly at
  70%; the final image was rebuilt and reflashed at the safe repository default
  of 5%.
- **Time boundary:** the PCF85063 returned an advancing calendar, but this does
  not establish accuracy, timezone or synchronization.
- **Source:** [CLOCK_2026-08-26](../hardware/CLOCK_2026-08-26.md), including the
  final device framebuffer capture. Measured 2026-08-26 on USB serial
  `28:84:85:B2:18:A4`.

### Host-to-watch UTC synchronization runs on the physical Waveshare

- **MEASURED:** PR #279's final candidate accepted host UTC plus a `+300` minute
  presentation offset, wrote and read back the PCF85063 calendar, and removed
  the stale warning. After a software reset, the RTC advanced and the persisted
  offset restored the same local time while trust correctly returned to stale;
  a second synchronization without the large-correction override restored the
  valid state.
- **Boundary:** this establishes the device path and effective-offset
  persistence, not independent absolute accuracy, long-term RTC drift, DST
  rules, or any companion/network/GNSS/mesh provider.
- **Source:** [TIME_SYNC_2026-08-26](../hardware/TIME_SYNC_2026-08-26.md),
  measured 2026-08-26 on USB serial `28:84:85:B2:18:A4` at source commit
  `1e51897`.

### MeshCore Companion discovery and connected status run on the physical Waveshare

- **Claim:** the Waveshare watch at USB serial `28:84:85:B2:18:A4` discovered
  a connected MeshCore Companion node over BLE and rendered `CONNECTED`, node
  name, 350 reported peers and ATT MTU 247 on its AMOLED status screen.
- **Source:** [MESHCORE_COMPANION_2026-08-26](../hardware/MESHCORE_COMPANION_2026-08-26.md)
  §§1–2, measured 2026-08-26 on that unit and its separately identified node.
- **Boundary:** this does not establish an RSSI reading, an inbound Room Server
  message, or a result on any other watch or node. Those outcomes remain
  explicitly classified in the report rather than inferred from connection.

### T114 MeshCore Companion fault evidence is separated from Attadipa defects

- **Claim:** an unpaired GATT session reached T114 TX-CCCD subscription and the
  node advertised after the deliberate disconnect. Earlier node unavailability
  is not attributed to T114 firmware because it was confounded by Attadipa's
  reconnect storm and incorrect initialization order, both corrected locally.
- **Source:** [T114 BLE Companion defect evidence](T114_BLE_COMPANION_DEFECTS_2026-08-26.md),
  measured 2026-08-26 on the physical Waveshare and operator-reported T114.
- **Boundary:** T114 firmware version, revision, node serial log and a clean
  single-attempt pairing reproduction remain `UNKNOWN`; this is not an upstream
  defect verdict.
