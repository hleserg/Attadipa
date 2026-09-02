# 0018 — What counts as owner consent when a product image is provisioned

Status: **accepted** — the owner chose A on 2026-09-02, recorded as
[OD-26](../research/OWNER_DECISIONS.md#od-26--owner-consent-for-provisioning-is-a-finger-on-the-watchs-own-screen). This ADR was written while the choice was still open, and its
Alternatives section is what was put in front of him.
Date: 2026-09-02

## Context

[#346](https://github.com/hleserg/Attadipa/issues/346) took the unauthenticated
USB control plane out of the product image, and established the rule this
decision has to live under: **a cable is not consent.** A product image may not
carry an endpoint that does what it is told because something was plugged in.

What that left is [#356](https://github.com/hleserg/Attadipa/issues/356): a
product image can no longer set its wall clock or receive a MeshCore passkey,
and for the passkey there was not even a flash-and-flash-back workaround, because
what `configure_meshcore_ble()` set was per-boot RAM (#356's first change made
it survive a boot; the workaround exists now, and fact 2 says what it costs).
The issue states that state and does not pick the mechanism. This ADR prices
the three candidates.

The question is not which wire. It is **what act, performed by a person, can a
product image observe and treat as consent** — an act nothing on the other end
of a cable or a radio can perform for itself.

### What is already true, and is therefore not a discriminator

Four facts are the same under every option. They are shared cost, and they are
recorded here so that no option is credited with paying them.

1. **The RTC write path exists and is compiled out, not missing.** When this
   was decided, one `#if CONFIG_ATTADIPA_WATCH_CONTROL` gated `BoardTimeSink`
   and a second one gated `save_time_metadata()` — two blocks rather than one,
   which is the difference between ungating this and thinking it is ungated.
   #356's first change removed the second: the sequence is
   `firmware/main/provision_time.h:121` — "ProvisionTimeResult provision_time(Ops &ops,"
   in every image, and `firmware/main/waveshare_board.cpp:385` — "#if CONFIG_ATTADIPA_WATCH_CONTROL"
   still gates its one instantiation,
   `firmware/main/waveshare_board.cpp:386` — "class BoardTimeSink final : public attadipa::debug::TimeSink {".
   The restore side is already unconditional:
   `firmware/main/waveshare_board.cpp:231` — "esp_err_t restore_time_metadata() {". Every option therefore costs *re-gating
   existing code and reaching it*, never *writing an RTC driver*.

2. **The passkey was RAM-only when this was decided, and the storage it
   needed is one key in a namespace that already existed.** Nothing persisted
   the passkey:
   `firmware/main/meshcore_ble.cpp:1839` — "bool configure_meshcore_ble(std::uint32_t passkey)"
   reaches `firmware/main/meshcore_ble.cpp:1441` — "secure_pairing.store(event.passkey != 0);"
   and nothing else, and the two flags a scan waits on are plain atomics:
   `firmware/main/meshcore_ble.cpp:160` — "std::atomic_bool configured{false};"
   and `firmware/main/meshcore_ble.cpp:162` — "std::atomic_bool reconnect_allowed{false};".
   But the seam that would hold it is already in this translation unit, put
   there by #304: `firmware/main/meshcore_ble.cpp:224` — "constexpr const char* kMeshNvsNamespace = ",
   read at `firmware/main/meshcore_ble.cpp:313` — "const esp_err_t err = nvs_get_blob(handle, kNodeKeyNvsKey,"
   and written at `firmware/main/meshcore_ble.cpp:383` — "esp_err_t err = nvs_set_blob(handle, kNodeKeyNvsKey, id.public_key.data(),",
   behind an `nvs_flash_init()` at `firmware/main/meshcore_ble.cpp:1786` —
   "const esp_err_t nvs_err = nvs_flash_init();" whose failure path is already
   handled. So this is one key added to a live namespace, not a
   storage layer to design — and it is the same key under every option, because
   persisting what was provisioned is orthogonal to the channel that delivered
   it. #356's first change added that key: an accepted pairing passkey is
   stored once the stack has taken it
   (`firmware/main/meshcore_ble.cpp:1451` — "if (event.persist_passkey && !store_passkey(event.passkey)) {"),
   replayed at boot through the same event, and erased again by `Deconfigure`
   (`firmware/main/meshcore_ble.cpp:1490` — "if (!erase_passkey()) {"), which
   is the whole of revocation. `Deconfigure` is reached only from the HIL
   image's `mesh-disconnect`, so a product image cannot revoke on its own: it
   can be given another passkey, or be flashed over with the HIL image and
   told to stop, or have its flash erased. This ADR does not add a revocation
   gesture, for the reason it adds no mode — nothing on a product image accepts
   input except the panel, and forgetting a node is a screen that does not
   exist yet.

3. **Touch already works, as an LVGL pointer device.**
   `firmware/main/physical_input.cpp:86` — "lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);".
   No screen in this tree uses an `lv_keyboard`, `lv_textarea` or
   `lv_buttonmatrix` — the names appear only in `tools/ui/lvgl_inventory.py`,
   which catalogues LVGL's API, and in `sim/lv_conf_simulator.h`, which
   configures it — and `apps/src/` holds only `clock.cpp` and
   `app_manifest.cpp`. On-screen
   entry is therefore **new UI on a working input path**, not a new input path.

4. **Both sink interfaces live in the same header as the forbidden symbol.**
   `debug/include/attadipa/debug/bridge.h:171` — "class TimeSink {" and `:191` —
   "class MeshSink {", while `tools/flash/firmware_elf_check.py:47` — "# Bridge::handle is the single function every privileged opcode is dispatched"
   names what a product image may not contain. The interfaces are pure virtual
   and a header is not a symbol, so reuse is probably fine — but any option that
   reuses them **shows** the elf check still passes rather than assuming it.

### What a gesture would cost, and the one thing about it that is not established

Options B and C both need a **gesture** to open a bounded window, because a
window that opens by itself is the endpoint #346 removed wearing a different
transport. There are **two case keys**, not one, and they are not
interchangeable: `docs/research/HARDWARE_MATRIX.md:399` — "| Buttons | **two case keys: PWR and BOOT.**".

**BOOT is available and measured.** It is a plain GPIO, its edges were observed
through this project's own input queue, and it is in the product image today
with nothing gating it: `firmware/main/physical_input.cpp:52` —
"    buttons.pin_bit_mask = 1ULL << GPIO_NUM_0;",
`firmware/main/physical_input.cpp:461` —
"  PhysicalButton physical_buttons_[1] = {{GPIO_NUM_0, false, 1}};", and
`firmware/main/physical_input.cpp:501` — "  ESP_LOGI(kTag, ".
So a gesture is not something B or C would have to invent. It costs two things
instead: BOOT is a reset strap, so it cannot be injected remotely and a
provisioning gesture built on it is not testable the way touch is; and on the
T-Watch it leaves with the GNSS module —
`docs/research/HARDWARE_MATRIX.md:266` — "1. **Unplugging the GNSS module also unplugs BOOT and RESET.** A board running".

**PWR is the one that is not established**, and it does not reach the SoC:

`docs/research/VERIFIED_FACTS.md:853` — "button presses arrive as PMU interrupts"
— over I2C rather than as GPIO edges, so press duration, long-press and
power-off behaviour are PMU register policy —

with the consequence already written down in the testing guide:
`docs/testing/WATCH_CONTROL.md:101` — "so on a device a held power key may be a shutdown rather than an event".

That entry is read from the **T-Watch** schematic —
`docs/research/VERIFIED_FACTS.md:852` — "- **Source:** S3 sheet 1." — and its
claim names SW7, a T-Watch designator, so by itself it is a fact about the other
board. What carries it here is the Waveshare row cited above,
`docs/research/HARDWARE_MATRIX.md:399`, which records the same wiring for the
product board and whose evidence is a measurement taken on one — "physical BOOT
and PWR edge pairs measured through `core::InputQueue`" — rather than a reading
of another board's sheet.

**UNKNOWN:** whether the AXP2101 can be configured to report a long press to
firmware as an event instead of acting on it. That is traceable — a
register-policy question in the AXP2101 datasheet — and it is not traced. Until
it is, **no option may rest its consent gesture on a held power key.** That is a
constraint on which key a gesture uses, not on whether one exists.

## The three mechanisms

### A — the holder enters it on the watch

Consent is that a person is holding this watch and touching its screen. Nothing
on a cable or a radio can do that.

The decisive fact is one the firmware already asserts to its peer:
`firmware/main/meshcore_ble.cpp:1685` — "ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;".
The watch tells the node it has a keyboard. Today that claim is satisfied by a
USB cable and a laptop. **Option A makes it true.** The node displays, the watch
types — which is BLE passkey pairing exactly as specified, and the passkey is
six digits, not a key: `firmware/main/meshcore_ble.cpp:1839` —
"bool configure_meshcore_ble(std::uint32_t passkey)".

The clock half is likewise already anticipated by the ADR that owns time.
`docs/adr/0014-time-source-and-synchronization.md` ranks sources "GNSS, network,
companion, mesh, manual, RTC, simulated" — `manual` is in that list, above
`RTC`, and it is built: `firmware/main/provision_time.h:143` —
"core::TimeSource::Manual, core::TimeQuality::Trusted," — is what the clock
sequence tags its observation with. What no product image has is a caller for it.
A hand-typed UTC is minutes-accurate at best,
which is precisely the quality `manual` denotes.

Cost: a numeric entry screen, in two languages
([ADR-0010](0010-localization.md)), on a 2.06-inch panel. That is the whole cost.

What it does **not** cost: no new BLE role, no bond decision, no GATT service,
no second USB dispatcher, no window, no timeout, no rate limit, and nothing to
authenticate — because nothing is listening.

### B — BLE peripheral, with a code shown on the watch

Consent is that the code on the watch's own screen is entered on the phone; only
someone looking at the watch has it.

Priced against the current build, B is **A plus a radio**:

- The role is not compiled in. `firmware/sdkconfig.defaults:102` —
  "CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=n". Turning it on costs flash and RAM in
  every product image, including the images that never provision.
- There is one bond slot, and it is spoken for.
  `firmware/sdkconfig.defaults:116` — "CONFIG_BT_NIMBLE_MAX_BONDS=1", and the
  watch's one bond is the MeshCore node's. A provisioning phone that bonds
  evicts it — NimBLE drops the oldest peer to make room:
  `docs/research/VERIFIED_FACTS.md:238` — "### A wrong MeshCore node's bond evicts the pinned node's",
  which traces it to the upstream source and marks the boundary honestly as
  source-traced rather than measured. So B either raises `MAX_BONDS` (more NVS
  and RAM in every image) or pairs without bonding, which means re-entering the
  code every single time and never recognising the phone again.

  One qualifier, because the cost lands later than it looks: the watch only
  reaches the SMP path once a passkey has been armed —
  `firmware/main/meshcore_ble.cpp:161` — "std::atomic_bool secure_pairing{false};",
  set at `firmware/main/meshcore_ble.cpp:1441` — "secure_pairing.store(event.passkey != 0);"
  and read at `firmware/main/meshcore_ble.cpp:829` — "if (secure_pairing.load()) {". An image nobody has provisioned
  writes no bond at all, so the eviction is a cost of the *second* provisioning
  and of bench images, not of every build. It is still B's cost, because B's
  whole purpose is to provision a second peer.
- The I/O capability is one host-wide global, not a per-connection property:
  ESP-IDF v5.5.5's vendored NimBLE declares it once, as
  `extern struct ble_hs_cfg ble_hs_cfg;` in
  `components/bt/host/nimble/nimble/nimble/host/include/host/ble_hs.h` — a path
  outside this repository, so nothing here checks it and it is quoted for a
  reader to confirm. Showing a code needs a
  DISPLAY capability, so a provisioning window has to flip a global that the
  MeshCore path also reads, and flip it back.
- And there is one connection slot —
  `firmware/sdkconfig.defaults:115` — "CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1" — so
  provisioning and mesh can never be up together.
- It still needs the gesture, and it still needs the on-screen UI, because it
  has to *display* the code.

B loses on price because it pays A's UI cost and then adds a radio role, a bond
decision, a global mode flip and a mutually exclusive connection slot on top.

### C — USB, restricted to provisioning opcodes inside a window

Consent is the gesture that opens the window; the cable is only the wire.

C is the smallest diff and the largest reversal:

- It is not "keep `watch_control.py` and add a window". #356's Definition of Done
  keeps `attadipa::debug::Bridge::handle` out of a product image, so C means a
  **second, provisioning-only dispatcher** that
  `tools/flash/firmware_elf_check.py` can tell apart from the debug bridge. Two
  dispatchers on one wire, and the check has to distinguish them for the life of
  the product.
- It still needs the gesture — so all the consent is in the gesture, and the
  cable contributes only convenience.
- And what it buys with that convenience is exactly the property #346 removed: a
  product image with an endpoint a host can talk to. Bounded, but present.

C loses on price because the consent it establishes is the gesture's, and it pays
for a second endpoint to deliver what the gesture already authorised.

## Decision

**A.** Written as a recommendation, chosen by the owner
([OD-26](../research/OWNER_DECISIONS.md#od-26--owner-consent-for-provisioning-is-a-finger-on-the-watchs-own-screen)):
a production image is provisioned by the holder entering the value on the watch
itself, and carries no provisioning listener of any kind.

It is the only candidate that adds no listener to the product image, the
one ADR-0014 already named, and the one that makes the firmware's existing
`KEYBOARD_ONLY` claim honest. B and C both need A's screen anyway, so choosing A
first makes either of them cheaper later rather than foreclosing them.

The weakness of A is repetition: six digits is nothing, but typing a full UTC is
tedious. **How often it has to be typed is UNKNOWN, and that is a hardware fact
this ADR is not entitled to assume.** What is MEASURED is retention across an
esptool-driven reset — `docs/hardware/TIME_SYNC_2026-08-26.md:33` — "Hard resetting via RTS pin..." —
where the RTC never lost its rail, which is the case that proves the least. Retention across power-off and battery
removal is listed as unknown in the same report: `docs/hardware/TIME_SYNC_2026-08-26.md:68` —
"- **UNKNOWN:**" and `docs/hardware/TIME_SYNC_2026-08-26.md:69` —
"  PCF85063 long-term drift, behavior across battery removal, DST/zone-rule".
The Waveshare RTC's own rail is not resolved either —
`docs/research/HARDWARE_MATRIX.md:409` — "The `Power rail` column reads `D13` where the load is known to be on a PMU rail" —
and the documented backup cell belongs to the other board.

The persisted UTC offset does survive, because it is in NVS rather than in the
chip: `firmware/main/waveshare_board.cpp:231` — "esp_err_t restore_time_metadata() {".

If the RTC does not retain, hand entry is recurring rather than one-time, on the
path the owner meets first, because GNSS has not landed. That makes GNSS more
urgent; it does not make B or C cheaper, because neither is any less
hand-entered — both display or accept a code the person still types, and both
pay for a channel on top. The decision does not move. The claim that A's
weakness is bounded does, and it is withdrawn until somebody measures it.

## Alternatives considered

Beyond B and C:

- **Keep the flash-HIL-provision-reflash round trip.** When this was
  decided, #356 had established it never existed: what
  `configure_meshcore_ble()` set was RAM, so provisioning did not survive a
  power cycle of the HIL image either. #356's first change made the passkey
  persist, so the round trip now exists — and it is still rejected as the
  *rule*, because it makes a laptop, a cable and a second image the consent
  factor for a watch that will be worn by somebody who has none of them. It
  stays as the bench path.
- **A third, provisioning-only firmware variant.** Rejected: three images to
  build and check instead of two, for the same consent factor as the round
  trip above, and the thing that had to persist was the state, not the image
  — once the state persists, the extra image buys nothing.

## Consequences

- Commits to the **second LVGL face this project has**. `ui/lvgl/` holds exactly
  one today — `clock_face.cpp` — with the application half beside it in `apps/`
  (`clock.cpp`, and an `AppManifest` declaring what it requires). Both halves
  are new work: in English and Russian ([ADR-0010](0010-localization.md)), built
  from theme tokens rather than literal colours because
  `tools/ui/check_raw_values.py` refuses the latter, and checked against a
  rendered image rather than by compiling, which is what this repository's UI
  rule asks for.
- Commits to an NVS seam for what was provisioned — shared with every future
  mechanism, so it is not a cost of this choice alone.
- **Fixes the authentication factor at possession, and this ADR should be the
  place that says so.** There is no lock and no confirmation: whoever is holding
  the watch can set its clock and arm a passkey. That is the same factor B and C
  establish — a gesture and a screen are both possession — so it does not
  separate them, but it is a boundary the record should carry rather than leave
  for somebody to discover. Arming a passkey is also what puts the watch on the
  SMP path where a wrong node's bond evicts the pinned node's. Child Mode on the
  roadmap is where a second factor would belong if one is ever wanted.
- **"Possession" is of the watch or of a cable, and writing "possession alone"
  hides the second half.** What is provisioned is stored in plain NVS, this
  project builds with no flash or NVS encryption and will not — `AGENTS.md`
  forbids burning eFuses — and a full flash read over that same port is
  documented on this unit: `docs/research/WAVESHARE_BOARD_RECEIVED.md:314` —
  "esptool.py --port <port> --baud 921600 read_flash 0 0x2000000 waveshare-2.06-factory.bin".
  esptool writes as well as reads, and the bond store
  is in the same unencrypted NVS: `firmware/sdkconfig.defaults:108` —
  "CONFIG_BT_NIMBLE_NVS_PERSIST=y". So what a cable reaches is not only the
  passkey it can read but the bond records it can overwrite. Whether a
  hand-written bond record is one NimBLE would accept is **UNKNOWN** and this
  ADR does not need it to be: the passkey alone reaches the SMP path with nobody
  touching the panel, which is the scenario #346 exists for. Every option here
  stores the same secret in the same place, so this separates none of them; it
  bounds all of them, and the register records it under
  [Q4 in OPEN_QUESTIONS.md](../research/OPEN_QUESTIONS.md#q4--a-production-watch-cannot-be-provisioned-at-all-and-the-missing-piece-is-a-consent-rule).
- **Both interfaces exist and are the right shape, and a product image
  compiles neither. That is the largest unpriced item in this decision.**
  Fact 4 above named them; this is what they cost. The clock's is
  `debug/include/attadipa/debug/bridge.h:171` — "class TimeSink {", implemented
  by `firmware/main/waveshare_board.cpp:386` — "class BoardTimeSink final : public attadipa::debug::TimeSink {"
  — which hands the request to the sequence that validates it, tags it
  `firmware/main/provision_time.h:143` — "core::TimeSource::Manual, core::TimeQuality::Trusted,"
  — writes the PCF85063 and persists the offset. The passkey's is
  `debug/include/attadipa/debug/bridge.h:191` — "class MeshSink {" — whose
  `configure` takes a passkey and may refuse it: a request the application makes
  and the firmware answers, which is the shape this needs. Neither is missing
  and neither is merely uncalled. `firmware/main/CMakeLists.txt:31` —
  "if(CONFIG_ATTADIPA_WATCH_CONTROL)" — is what adds the `debug` layer, and
  `debug/CMakeLists.txt:14` — "target_include_directories(attadipa_debug PUBLIC include)"
  — is the only route its headers take into `firmware/main/`. So under the
  symbol that keeps the bridge out, a product image cannot name `TimeSink`,
  `MeshSink` or `MeshSinkResult` in any signature at all. That is a stronger
  constraint than a missing caller and it points somewhere else: what #356 adds
  is a seam a product image can compile.
  `core::` is not one yet: `core/include/attadipa/core/mesh_service.h:83` —
  "class MeshProvider {" — is four methods — status, peer count, peer, send —
  and not one of them arms a passkey, while
  `core/include/attadipa/core/time_service.h:60` —
  "bool observe(const TimeObservation& observation);" — is one step inside the
  clock sequence and the step that does not persist; nothing in `core/` reaches
  the PCF85063 or NVS. So what an option here buys is that seam and the firmware
  behind it, not an NVS key: the shortest path from `apps/` to
  `firmware/main/meshcore_ble.h:12` — "bool configure_meshcore_ble(std::uint32_t passkey);"
  — is the application-layer hardware access `AGENTS.md` forbids, and an
  implementer reading a cost list that stops at `firmware/main/` will take it.
- Leaves the AXP2101 long-press question UNKNOWN and untouched, because A needs
  no gesture at all. B or C would not have to close it either — they would use
  BOOT, and pay for a reset strap that cannot be injected remotely and that on
  the T-Watch is fitted to the GNSS daughterboard.
- Does not decide the timezone-offset UI, only that the offset is entered on the
  device like everything else.
- Does **not** change what a product image pays for the BLE stack:
  `firmware/main/attadipa_main.cpp:310` — "const esp_err_t mesh_err = start_meshcore_ble();"
  is unconditional, so a product image still brings the controller up. #356
  records that; it stays open here.
- ADR-0014's "first real input is the existing physical USB debug connection"
  bullet stopped being true of a product image at #346, not here, and this
  branch adds the note saying so and pointing at this ADR. The bullet itself is
  rewritten by the implementation, so the sentence and the code change together.
- Puts the face in `ui/lvgl/`, which is what subjects it to the theme-token
  rule: `tools/ui/check_raw_values.py` scans `sim`, `apps` and `ui` and not
  `firmware`, which is why `build_mesh_screen()` in
  `firmware/main/waveshare_board.cpp:546` — "void build_mesh_screen() {" — is
  full of literal colours. Building the entry screen where the mesh screen was
  built would silently opt it out of the check.
