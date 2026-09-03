# What a watch can do after its MeshCore node is reset, and what it cannot

Readiness research for [#409](https://github.com/hleserg/Attadipa/issues/409),
against `main@6b88158`. **RESEARCH ONLY — no production code changed.**

The question was whether a product image can recover on its own after the node
it is bonded to is factory-reset or reflashed. The answer is no, and the reason
is not the one the issue names.

> **The stale bond is not the blocking piece of state. The pinned node public
> key is.** A stale bond can be deleted, and one image can delete it. The pin
> can be deleted by nothing — not by the product image, not by the HIL image,
> not by `mesh-forget-bond`, not by `mesh-disconnect`. After a factory reset of
> the node, the only recovery this repository has is `idf.py erase-flash` on the
> watch, which takes the bonds, the pin and the time metadata together.

That makes the documented HIL recovery in
[`MESHCORE_T114_FIRST_CONTACT.md:674`](MESHCORE_T114_FIRST_CONTACT.md) —
"Recovery is an operator action:" — **incomplete for the case its own section is
about**, and it means the gap #409 reports is wider than #409 states: it is not
"the product image lacks a surface the HIL image has", it is "no image has the
operation at all".

Everything about physical devices below is `NOT EXECUTED — HARDWARE REQUIRED`.
Section 10 is the field-test plan that would change that; section 9 is the one
question in it that needs the owner, because one of its steps destroys a key.

---

## 1. What was read, and at which revision

| Source | Revision | How it was read |
| --- | --- | --- |
| Attadipa firmware | `main@6b88158` | this tree |
| ESP-IDF | `v5.5.5` | tag on `espressif/esp-idf`, for the submodule pointer only |
| NimBLE, as ESP-IDF vendors it | `espressif/esp-nimble` at `685675c0128deafdd201c9eb82e61d227364646c` | that is the submodule SHA `esp-idf@v5.5.5` records for `components/bt/host/nimble/nimble`; `nimble/host/src/ble_gap.c` and `ble_sm.c` fetched at that SHA and read |
| MeshCore node firmware | `meshcore-dev/MeshCore` at `d929643` | the revision the bench node runs, `v1.17.1-d929643` — [`TEST_FLEET.md:54`](TEST_FLEET.md) "It stays on " |
| Apache NimBLE issue 2206 | re-read 2026-09-02 | GitHub API |

No hardware was touched. No mock, simulator run or host test in this repository
is quoted below as evidence of a device behaviour.

---

## 2. The state a stale bond leaves behind

Issue question 1 asks for the exact persistent and volatile state after
`BLE_GAP_EVENT_ENC_CHANGE` with `PIN or Key Missing` (HCI `0x06`). This is that
list, traced through `firmware/main/meshcore_ble.cpp`.

The trigger is one branch, and only one status reaches it:
[`firmware/main/meshcore_ble.cpp:952`](../../firmware/main/meshcore_ble.cpp) —
"BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {", which calls
[`:851`](../../firmware/main/meshcore_ble.cpp) —
"(void)record_stale_bond(event->enc_change.conn_handle," and then falls into
[`:854`](../../firmware/main/meshcore_ble.cpp) —
"disconnect_fault(generation, " like every other pairing failure.

| State | Where it lives | What the fault does to it | Survives a reboot |
| --- | --- | --- | --- |
| The bond (LTK, IRK, CCCD) | NimBLE's own NVS, `CONFIG_BT_NIMBLE_NVS_PERSIST=y` — [`firmware/sdkconfig.defaults:108`](../../firmware/sdkconfig.defaults) "CONFIG_BT_NIMBLE_NVS_PERSIST=y", one slot, [`:116`](../../firmware/sdkconfig.defaults) "CONFIG_BT_NIMBLE_MAX_BONDS=1" | **untouched.** The whole of #325 is that the firmware does not delete it | **yes** |
| The conflict record | RAM, `BondRecovery::conflicted_`, written at [`firmware/main/meshcore_bond_recovery.h:85`](../../firmware/main/meshcore_bond_recovery.h) — "if (!conflicted_.valid && peer.valid) conflicted_ = peer;" | set, once, to the **first** conflicting peer | **no** — and this is the load-bearing asymmetry, see below |
| The pinned node public key | NVS, `attadipa_mesh` / [`firmware/main/meshcore_ble.cpp:253`](../../firmware/main/meshcore_ble.cpp) — "constexpr const char* kNodeKeyNvsKey" | **untouched, and untouchable** — section 4 | **yes** |
| The stored passkey | NVS, [`:228`](../../firmware/main/meshcore_ble.cpp) — "constexpr const char* kPasskeyNvsKey" | untouched | **yes**, replayed at boot: [`:1825`](../../firmware/main/meshcore_ble.cpp) — "restore_passkey();" |
| `reconnect_allowed` | RAM atomic | cleared: [`:552`](../../firmware/main/meshcore_ble.cpp) — "reconnect_allowed.store(false);" inside `disconnect_fault()`. This is the fail-closed stop | **no** |
| `configured` | RAM atomic | untouched — still true, so the transport still believes it is provisioned | **no**, but the boot replay re-sets it |
| `secure_pairing` | RAM atomic | untouched | **no**, same replay |
| Session generation / `SessionOwner` | RAM | faulted, connection terminated, a new generation allocated for the next attempt | **no** |
| Capability / `MeshStatus` | RAM snapshot | availability stays provisioned (`configured` is still true), so the mesh screen reports a provisioned watch that is not connected | **no** |
| Refused-address cooldown | RAM, `RefusalState` | **not armed.** The cooldown belongs to the node-pin refusal, not to a stale bond | **no** |

**There is no reconnect storm, and the reason is worth stating precisely.**
`disconnect_fault()` clears `reconnect_allowed`, and `start_scan()` refuses to
run without it, so the transport stops after exactly one attempt. What re-arms
it is a `Configure` event, and the boot replay posts one — so the cost is **one
failed connection attempt per boot and per passkey entry**, not a loop. That is
the intended #325 behaviour and it holds.

**The conflict record is RAM and the bond is flash, and they do not have the
same lifetime.** Power-cycle the watch between the failure and the recovery and
the bond is still there while the record that names it is gone. On the next boot
the replayed passkey drives one connection, that connection fails the same way,
and the record is written again — so the state converges rather than jamming.
That convergence belongs to this state alone: it needs the failure to come back
as `PIN or Key Missing`, and §6.1 names two states in which it does not and
nothing is recorded.
But it converges only because the failure repeats; a recovery surface that
assumed the record had survived the reboot would be refused with "nothing to
forget". `ForgetBondOperation` already distinguishes that from a store failure
([`firmware/main/meshcore_forget_outcome.h:51`](../../firmware/main/meshcore_forget_outcome.h) —
"Nothing,"), which is the behaviour a product surface would inherit.

---

## 3. Re-entering a passkey cannot recover the link · source-traced

Issue question 2, and the issue is right to insist it be answered from source
rather than from an API name. It cannot, and the reason is one branch in NimBLE.

On the product image after PR [#406](https://github.com/hleserg/Attadipa/pull/406)
the **only** thing an owner can do is post a `Configure` with a six-digit
passkey. That sets
[`firmware/main/meshcore_ble.cpp:1705`](../../firmware/main/meshcore_ble.cpp) —
"configured.store(true);" and
[`:1457`](../../firmware/main/meshcore_ble.cpp) —
"reconnect_allowed.store(true);", and scanning resumes. The watch connects,
sees `secure_pairing` at
[`:829`](../../firmware/main/meshcore_ble.cpp) — "if (secure_pairing.load()) {",
and calls `ble_gap_security_initiate()`.

That call does **not** pair when a bond exists. At
`685675c0128deafdd201c9eb82e61d227364646c`, `nimble/host/src/ble_gap.c:9216-9235`:

```c
    if (conn_flags & BLE_HS_CONN_F_MASTER) {
        /* Search the security database for an LTK for this peer.  If one
         * is found, perform the encryption procedure rather than the pairing
         * procedure.
         */
        rc = ble_store_read_peer_sec(&key_sec, &value_sec);
        if (rc == 0 && value_sec.ltk_present) {
            rc = ble_sm_enc_initiate(conn_handle, value_sec.key_size,
                                     value_sec.ltk, value_sec.ediv,
                                     value_sec.rand_num,
                                     value_sec.authenticated);
```

The watch is the central (`BLE_HS_CONN_F_MASTER`), the store still holds the
LTK, so the branch taken is `ble_sm_enc_initiate()` — link-layer encryption with
the key the node no longer has. **The passkey is not consulted at all**: a static
passkey is an input to the pairing procedure, and the pairing procedure is not
entered. The node answers `PIN or Key Missing`, and the watch is back at section
2 with one more log line.

**So on a product image, entering a passkey — the same one, a different one, the
node's new one — buys exactly one more failed attempt.** Not a recovery, and not
a loop either. This is the fact that turns #409 from a UX gap into a
correctness one: there is no sequence of product-image actions that reaches a
working mesh.

**One branch of this is address-dependent and is `UNKNOWN`.** The lookup is keyed
on `key_sec.peer_addr = addrs.peer_id_addr`. If the node's identity address
changed with the reset, no LTK is found, `ble_sm_pair_initiate()` runs, and the
watch pairs and bonds afresh — a different symptom with the same ending, because
section 4 then refuses the node anyway. Which of the two happens on this fleet is
**UNKNOWN** and section 10 measures it:
[`MESHCORE_T114_FIRST_CONTACT.md:47`](MESHCORE_T114_FIRST_CONTACT.md) —
"Heltec T114, BLE address" records the address as `MEASURED` once, and nothing
has read it across a reset. The MeshCore source reads it from the SoftDevice
(`sd_ble_gap_addr_get`, `src/helpers/nrf52/SerialBLEInterface.cpp:140` at
`d929643`) and never sets one, and the node's factory reset formats a
filesystem rather than writing FICR — so the address is *expected* to survive,
which is a source-traced expectation and not a measurement.

---

## 4. Deleting the bond is not enough either · the pin

This is the part #409 does not have, and it is the reason the issue's
recommended framing has to change.

Suppose the bond *is* deleted — today that means the HIL image and
`mesh-forget-bond`, and it works: the request reaches
[`firmware/main/meshcore_ble.cpp:1802`](../../firmware/main/meshcore_ble.cpp) —
"const int deleted = ble_store_util_delete_peer(&address);" and re-arms one
attempt at [`:1580`](../../firmware/main/meshcore_ble.cpp) —
"reconnect_allowed.store(true);".

The watch then reconnects and pairs afresh (no LTK now, so section 3's other
branch) — **provided a `Configure` has carried the node's current digits.**
With the stored passkey alone it cannot: the node shows new digits at every
boot after its reset (§5.3), the watch is keyboard-only with a static passkey —
[`firmware/main/meshcore_ble.cpp:2001`](../../firmware/main/meshcore_ble.cpp) —
"ble_hs_cfg.sm_io_cap = BLE_HS_IO_KEYBOARD_ONLY;" — and `mesh-forget-bond`
re-arms the attempt without touching it, so the fresh pairing fails on the
confirm value, [`firmware/main/meshcore_ble.cpp:654`](../../firmware/main/meshcore_ble.cpp) —
"reconnect_allowed.store(false);" runs, and the transport stops after **one**
attempt with no handshake, no `RESP_CODE_SELF_INFO` and no refusal — §5.3's
consequence 2. Given the current digits, the pairing completes, the Companion
handshake runs, and the watch reads the node's public key out of
`RESP_CODE_SELF_INFO`. A factory-reset node's key is a **new** key —
[`MESHCORE_T114_FIRST_CONTACT.md:50`](MESHCORE_T114_FIRST_CONTACT.md) —
"a factory reset regenerates it" is `MEASURED` on this bench. So:

- [`link/src/meshcore_companion.cpp:525`](../../link/src/meshcore_companion.cpp) —
  "if (pinned_set_ && !(status_.node_id == pinned_)) {" latches `wrong_node_`;
- [`firmware/main/meshcore_node_pin.h:194`](../../firmware/main/meshcore_node_pin.h) —
  "if (!ops.wrong_node()) return PinOutcome::Pinned;" therefore falls through to
  [`:200`](../../firmware/main/meshcore_node_pin.h) —
  "return PinOutcome::Refused;";
- the address is cooled down for 60 s and the connection is terminated.

The scan then walks back to the same node a minute later and refuses it again,
indefinitely. Mesh does not come back.

**And nothing erases the pin.** The only writer of `attadipa_mesh/node` is
`store_node_pin()` — [`firmware/main/meshcore_ble.cpp:457`](../../firmware/main/meshcore_ble.cpp) —
"nvs_set_blob(handle, kNodeKeyNvsKey" — reached only from the *adopt* path of
`settle_node_pin()`, which runs only when the watch has no pin. The single
`nvs_erase_key` in the file names the passkey and nothing else:
[`:372`](../../firmware/main/meshcore_ble.cpp) —
"esp_err_t err = nvs_erase_key(handle, kPasskeyNvsKey);". `Deconfigure` calls
that one — [`:1490`](../../firmware/main/meshcore_ble.cpp) —
"if (!erase_passkey()) {" — and touches neither the bond nor the pin. There is
no opcode for it: the debug protocol's mesh block is `MeshConfigure`,
`MeshSend`, `MeshRoomSend`, `MeshDisconnect`, `MeshForgetBond` and stops there
([`debug/include/attadipa/debug/protocol.h:84`](../../debug/include/attadipa/debug/protocol.h) —
"MeshForgetBond= 0x0054").

**The tree already knew.** Two comments said it, and both pointed at #356 to
close it — "no in-image way to re-pin", "the recovery is `idf.py erase-flash`".
#411 rewrote both to point at what closed it instead, the entry screen's node
field:

- [`firmware/main/meshcore_ble.cpp:245`](../../firmware/main/meshcore_ble.cpp) —
  "What the image has since #411 is the reverse";
- [`core/include/attadipa/core/mesh_service.h:61`](../../core/include/attadipa/core/mesh_service.h) —
  "the way out, the entry screen's node field (#411)".

**#356 did not close it.** PR #406 — the second and last change for #356 —
lists under *Not in this PR*: "a screen to forget a node or clear the passkey (a
product image still cannot revoke on its own — recorded in ADR-0018)". So the
two comments' expectation expires with #356 and nothing replaces it. That is
the finding, and it is a documentation defect as well as a product one, because
those two comments will read as "closed by #356" to the next agent.

### 4.1 The consequence for `mesh-forget-bond` as it stands

`mesh-forget-bond` is not wrong and it is not wasted. It is **sufficient only
when the node kept its MeshCore public key** — a bond lost without an identity
change. Whether any real node operation produces that combination is `UNKNOWN`:
the node's identity and its bond store are both in the nRF52 InternalFS that
`DataStore::formatFileSystem()` formats (`examples/companion_radio/DataStore.cpp:166-172`
at `d929643`, `_fs->format()`), so a factory reset takes both together, and a
DFU that preserves the filesystem takes neither. A partial loss would need some
third operation nobody here has named.

For the case §8.1 of `MESHCORE_T114_FIRST_CONTACT.md` is explicitly about — a
factory reset — `mesh-forget-bond` completes step one of a two-step recovery
whose step two does not exist in any image. That document is corrected on this
branch.

---

## 5. What the node does to itself across a factory reset

Three node-side facts follow from `meshcore-dev/MeshCore@d929643`, and one of
them changes what any recovery surface has to ask the owner for.

**5.1 The identity key is regenerated.** `MEASURED` on this bench already
([`MESHCORE_T114_FIRST_CONTACT.md:50`](MESHCORE_T114_FIRST_CONTACT.md) —
"a factory reset regenerates it"). Source-consistent: the reset formats the
filesystem the identity is stored in.

**5.2 The bond goes with it.** `DataStore::formatFileSystem()` on
`NRF52_PLATFORM` calls `_fs->format()` on the InternalFS the node's Bluefruit
stack also keeps its bond files in — `examples/companion_radio/main.cpp` at
`d929643` opens it with `InternalFS.begin()` on line 142 and hands it to the
`DataStore`. Source-traced, **not measured** — this repository has never read the
node's bond directory. It is consistent with the symptom #325 was written for,
which is the strongest thing that can be said without a bench run.

**5.3 The node's BLE passkey becomes a new random six digits at every boot.**
This is the fact with teeth. `MyMesh::begin()` at `d929643`:

```c++
#ifdef BLE_PIN_CODE // 123456 by default
  if (_prefs.ble_pin == 0) {
#ifdef DISPLAY_CLASS
    if (has_display && BLE_PIN_CODE == 123456) {
      StdRNG rng;
      _active_ble_pin = rng.nextInt(100000, 999999); // random pin each session
```

- `_prefs.ble_pin == 0` is exactly what a formatted filesystem leaves: the
  field's initialiser is `uint32_t ble_pin = 0;` in
  `examples/companion_radio/NodePrefs.h`, and the reset removes the file the
  loader would have overwritten it from.
- `has_display` is `disp != NULL` — the display was detected at boot; that is
  the argument `main()` passes to `MyMesh::begin()`, lines 156-162 of
  `examples/companion_radio/main.cpp`.
- The bench node is a T114 **with** a display
  ([`TEST_FLEET.md:72`](TEST_FLEET.md) — "the free bench one has both"), and the
  variant it runs defines both flags: `variants/heltec_t114/platformio.ini`,
  `[env:Heltec_t114_companion_radio_ble]` extends `Heltec_t114_with_display`
  (`-D DISPLAY_CLASS=ST7789Display`) and sets `-D BLE_PIN_CODE=123456`.

So after a factory reset the node shows a **different passkey every time it
powers on**, until somebody sets `_prefs.ble_pin` back to a fixed value. The
watch, by contrast, stores one passkey and replays it at every boot.

**Consequences, and they are design constraints rather than observations:**

1. A recovery that *replays* the stored passkey is wrong: it is stale the
   moment the node is reset, and stale again after the node's next power
   cycle. It does not follow that a recovery must erase it. `store_passkey()`
   replaces the value —
   [`firmware/main/meshcore_ble.cpp:430`](../../firmware/main/meshcore_ble.cpp) —
   "esp_err_t err = nvs_set_u32(handle, kPasskeyNvsKey, passkey);" — and a
   recovery necessarily carries the node's current digits, so the stale value
   is overwritten by the same entry that recovers the link. Erasing it is
   worse than keeping it: a passkey erased with `configured` left as it is
   comes back at the next boot as
   [`firmware/main/meshcore_ble.cpp:2070`](../../firmware/main/meshcore_ble.cpp) —
   "no MeshCore passkey stored; BLE stays unconfigured", and
   [`firmware/main/meshcore_ble.cpp:1276`](../../firmware/main/meshcore_ble.cpp) —
   "if (configured.load()) start_scan();" stays false until somebody enters
   one again. Keeping it costs one failed attempt per boot, the #325 cost §2
   already accepts.
2. A watch that has recovered once is not recovered permanently: the node
   rebooting invalidates the passkey the watch holds, and the watch is back at
   a pairing failure — a *different* failure from `PIN or Key Missing`, which
   the firmware does not record a conflict for, and which therefore leaves the
   transport faulted with nothing offered.
3. Any field test that resets the node **must** re-read the passkey off the
   node's screen after every node power cycle, and must record which one was
   used for each attempt. A run that types the old passkey proves nothing.
4. The honest owner-facing statement is not "re-enter your passkey"; it is
   "read the six digits your node is showing now".

This is source-traced at a pinned revision, not measured. Section 10 measures it.

---

## 6. The minimum atomic scope of a revocation

Issue question 4. Given sections 2–5, the smallest operation that can actually
return a product watch to service is:

**Must be cleared, together or not at all:**

| Item | Where | Why it cannot be left |
| --- | --- | --- |
| The conflicting bond | NimBLE NVS, one slot | §3 — while it exists, every attempt encrypts instead of pairing |
| The pinned node public key | `attadipa_mesh/node` | §4 — while it exists, the reset node is refused after pairing |
| The RAM copy of that pin — `pinned_set_`, `pinned_`, `status_.pinned_id`, `has_pinned`; **`pinned_set_` is the one that decides**: [`link/src/meshcore_companion.cpp:831`](../../link/src/meshcore_companion.cpp) — "if (!pinned_set_) return false;" is the whole of `pinned()`, and a clear that zeroes the key but leaves it true refuses *every* node, since [`link/src/meshcore_companion.cpp:525`](../../link/src/meshcore_companion.cpp) — "if (pinned_set_ && !(status_.node_id == pinned_)) {" is then true for any key and the adopt path behind [`firmware/main/meshcore_node_pin.h:188`](../../firmware/main/meshcore_node_pin.h) — "if (!ops.pinned(expected)) {" is unreachable | the worker-owned `MeshCoreCompanion` singleton, [`firmware/main/meshcore_ble.cpp:128`](../../firmware/main/meshcore_ble.cpp) — "attadipa::link::MeshCoreCompanion provider;" | it is what `settle_node_pin()` actually asks — [`firmware/main/meshcore_ble.cpp:1527`](../../firmware/main/meshcore_ble.cpp) — "return provider.pinned(out);" — never NVS. It has two writers — boot, [`:2135`](../../firmware/main/meshcore_ble.cpp) — "provider.pin(pinned);", and the adopt path, [`:1531`](../../firmware/main/meshcore_ble.cpp) — "void adopt(const attadipa::core::MeshPeerId& id) { provider.pin(id); }" from [`firmware/main/meshcore_node_pin.h:190`](../../firmware/main/meshcore_node_pin.h) — "ops.adopt(seen);" — and the second is what completes a recovery: the next handshake with no pin stores the new key to NVS ([`:189`](../../firmware/main/meshcore_node_pin.h) — "if (!ops.store(seen)) return PinOutcome::AdoptFailed;" → `nvs_set_blob`, a set, not an erase-then-set) and pins it in RAM. `begin()` keeps it across sessions on purpose ([`link/src/meshcore_companion.cpp:109`](../../link/src/meshcore_companion.cpp) — "// `status_.pinned_id` and `status_.refused_id` are deliberately NOT cleared"), and there is no unpin: [`link/include/attadipa/link/meshcore_companion.h:83`](../../link/include/attadipa/link/meshcore_companion.h) — "void pin(const core::MeshPeerId& node);" is the whole write side. Clear NVS alone and the next handshake refuses the node exactly as before, until the watch reboots — the reboot this whole report exists to remove. The mesh screen reads the same copy: [`firmware/main/waveshare_board.cpp:862`](../../firmware/main/waveshare_board.cpp) — "if (status.has_refused && status.has_pinned)" |
| The live session and `reconnect_allowed` | RAM | the deletion cannot happen under a live encrypted session — the existing worker already terminates first, [`firmware/main/meshcore_ble.cpp:1802`](../../firmware/main/meshcore_ble.cpp) "const int deleted = ble_store_util_delete_peer(&address);" is preceded by a terminate |
| The refusal cooldown and `has_refused` | RAM / `MeshStatus` | otherwise the screen keeps reporting a refusal that has been revoked, and the scan skips the node for up to a minute after the owner acted |

**Must be replaced, not erased:**

- the stored passkey. It is stale by construction (§5.3), but the entry that
  recovers the link overwrites it through `store_passkey()`, and erasing it
  instead leaves the watch unconfigured at its next boot (§5.3, consequence 1).
  An earlier draft of this table listed it under the clears; the round-1
  review of this branch showed the erase to be both unnecessary and harmful.

**Must be preserved:**

- the wall clock and the persisted UTC offset — a different NVS namespace, and
  ADR-0014's `restore_time_metadata()` path;
- every other NVS namespace;
- any other bonded peer. There is only one bond slot today
  ([`firmware/sdkconfig.defaults:116`](../../firmware/sdkconfig.defaults) —
  "CONFIG_BT_NIMBLE_MAX_BONDS=1"), so "another peer" is not reachable now, but
  the operation must be addressed at one recorded bond rather than at the store,
  or raising `MAX_BONDS` later silently turns it into a wipe.

**Atomicity is the requirement, and partial completion is the hazard.** Clearing
the pin without the bond leaves a watch that pairs with the stale LTK and fails;
clearing the bond without the pin is exactly today's dead end. A surface that
reports success after the first of five clears repeats #378 with more state.
The existing `ForgetBondOperation` slot is the right shape for this and is
discussed next.

**What re-pins the watch, and the window the clear opens.** Nothing in the
operation re-pins; the adopt path does, at the next handshake that finds no pin
(`settle_node_pin()` → `store` → `adopt`, both copies). Two consequences:

- *The NVS eraser is a crash-recovery guard, not what makes the clear take
  hold.* Clearing the RAM copy alone is enough for the next adoption to
  overwrite `attadipa_mesh/node`, because the store is a set. What the eraser
  covers is a reboot **between** the clear and that adoption, which would
  otherwise re-pin the old key out of flash at
  [`firmware/main/meshcore_ble.cpp:2135`](../../firmware/main/meshcore_ble.cpp) —
  "provider.pin(pinned);" and put the watch back where it started.
- *Between the clear and the adoption the watch is unpinned*, in the state
  [`firmware/main/meshcore_ble.cpp:2152`](../../firmware/main/meshcore_ble.cpp) —
  "will attach to whichever node answers first" describes — and which node
  answers first is advertisement order, measured on this bench as five to four
  ([`MESHCORE_T114_FIRST_CONTACT.md:72`](MESHCORE_T114_FIRST_CONTACT.md) —
  "watch reached node A five times and node B four"). The only gate on that
  adoption is the armed passkey, and **it binds nothing** — `MEASURED`: both
  bench nodes take the same operator passkey,
  [`MESHCORE_T114_FIRST_CONTACT.md:68`](MESHCORE_T114_FIRST_CONTACT.md) —
  "Both advertise the Companion service and both pair with the same operator" —
  so the digits the owner entered open the intended node and its neighbour
  alike, and which one is adopted is advertisement order. The one attempt the
  passkey entry arms therefore has two silent endings beside the good one:
  the *other* node is adopted and pinned, and the watch talks to it as if that
  were the plan; or pairing fails — wrong digits, or the node rebooted and
  rolled its passkey (§5.3) — which faults the transport and records nothing.
  Nothing in this repository binds the adoption to the intended node, and
  #411 does not pretend to. What its surface does about the first ending is
  show the adopted key and let the owner forget again: the entry screen's node
  field carries the pinned prefix for exactly that comparison against the
  node's own screen
  ([`../../apps/src/provisioning.cpp:437`](../../apps/src/provisioning.cpp) —
  "// The first eight hex digits of the node's key, the way the mesh screen").
  §10.5's second-node run measures how often the wrong one wins, not whether
  it can.

**What re-arms the radio: nothing in the operation, ever.** `reconnect_allowed`
is dropped before the first clear and left down —
[`../../firmware/main/meshcore_node_forget.h:105`](../../firmware/main/meshcore_node_forget.h) —
"    ops.disarm();" — because in state (b), and on a healthy pinned watch, the
flag is *up*: a refusal only cools one address down for a minute and keeps
scanning, so an unpin that left the flag alone would be followed within
seconds by a reconnect that adopts the first node to answer, before the owner
had reached the passkey field. The passkey entry that follows is the one arm —
[`../../firmware/main/meshcore_ble.cpp:1706`](../../firmware/main/meshcore_ble.cpp) —
"                reconnect_allowed.store(true);" in `Configure` — exactly as it
is for a first provisioning, and the owner may repeat it. That is also §7's
answer to "already re-arms exactly one attempt": the *bond* operation does; the
node operation must not.

The RAM disarm is mirrored by a crash-safe `attadipa_mesh/reprovision` marker,
committed before either trust copy is deleted
([`../../firmware/main/meshcore_node_forget.h:111`](../../firmware/main/meshcore_node_forget.h) —
"    if (!ops.mark_reprovision()) return ForgetNodeOutcome::BondKept;").
Boot treats that marker, and an unreadable marker, as a fail-closed replay gate
([`../../firmware/main/meshcore_ble.cpp:400`](../../firmware/main/meshcore_ble.cpp) —
"    return PasskeyReplay::Inhibited;"). The old digits remain stored but are
not armed after a power loss; an owner-entered replacement commits first and
only then removes the marker
([`../../firmware/main/meshcore_ble.cpp:1699`](../../firmware/main/meshcore_ble.cpp) —
"store_passkey(event.passkey) && clear_reprovision_pending()").
If a non-destructive failure needs to undo the marker and NVS refuses that
rollback, the operation reports `ReplayInhibited` instead of claiming that
nothing changed; the node remains on the screen for a retry.

**What must *not* be in scope, and this is not a UI opinion:** peer-triggered
deletion. Nothing a radio peer does may cause any of the five clears. §325's
whole argument — [`firmware/main/meshcore_bond_recovery.h:85`](../../firmware/main/meshcore_bond_recovery.h) —
"if (!conflicted_.valid && peer.valid) conflicted_ = peer;" keeps the **first**
conflict rather than the newest, so a peer cannot aim the owner's next action.
A revocation that clears the pin as well as the bond makes that property *more*
important, not less: the pin is what stops a stranger's node being adopted, so
an attacker who can provoke a revocation gets adoption, not just a dropped link.

### 6.1 Which bond the operation acts on, in each of the three states

The scope above is written for the state §2 describes, and that is one of
three. The round-1 review of this branch showed that the record §7 endorses
does not survive the other two, so each is named here with what the operation
must act on.

| State | How the watch got there | What the bond store holds | What `BondRecovery` holds | What the operation must act on |
| --- | --- | --- | --- | --- |
| **(a)** stale bond, address unchanged | §2: the node was reset, the watch reconnects, encryption fails with `PIN or Key Missing`, and the worker records the peer — [`firmware/main/meshcore_ble.cpp:953`](../../firmware/main/meshcore_ble.cpp) — "(void)record_stale_bond(event->enc_change.conn_handle," | one bond, stale, for the peer the record names | that peer | the recorded bond **and** the pin — both copies of it, §6 — together — the scope above |
| **(b)** bond re-made, pin refused | §4: the bond was deleted (today `mesh-forget-bond`) or evicted, the owner entered the node's current digits, the watch paired and bonded afresh — which empties the record: [`firmware/main/meshcore_ble.cpp:961`](../../firmware/main/meshcore_ble.cpp) — "recovery.pairing_succeeded();" → [`firmware/main/meshcore_bond_recovery.h:107`](../../firmware/main/meshcore_bond_recovery.h) — "void pairing_succeeded() { conflicted_ = BondIdentity{}; }" — and then the pin refused the new key ([`firmware/main/meshcore_node_pin.h:200`](../../firmware/main/meshcore_node_pin.h) — "return PinOutcome::Refused;") | one bond, **good**, with the reset node. Under `CONFIG_BT_NIMBLE_MAX_BONDS=1` NimBLE evicts the oldest bond on overflow rather than refusing, so nothing stale is left beside it — the worker's own comment traces this: [`firmware/main/meshcore_ble.cpp:1630`](../../firmware/main/meshcore_ble.cpp) — "and on overflow NimBLE evicts rather" | nothing | the pin — both copies — and the refusal cooldown **only**; the bond must be kept. A revocation that reaches for `take_forget()` here gets `false` — [`firmware/main/meshcore_bond_recovery.h:99`](../../firmware/main/meshcore_bond_recovery.h) — "if (!conflicted_.valid) return false;" — and the worker answers [`firmware/main/meshcore_ble.cpp:1787`](../../firmware/main/meshcore_ble.cpp) — "forget_op.complete(attadipa::firmware::ForgetOutcome::Nothing);" — right about the bond, useless for the recovery. The peer whose key was refused is known only to the pin path — [`firmware/main/meshcore_ble.cpp:1613`](../../firmware/main/meshcore_ble.cpp) — "case attadipa::firmware::PinOutcome::Refused:" — and is recorded nowhere. #411 needs no such record: in this state the operation clears the pin and keeps the bond, and neither needs the refused peer's identity — [`../../firmware/main/meshcore_node_forget.h:114`](../../firmware/main/meshcore_node_forget.h) — "    const bool taken = ops.take_forget(peer);" is false here and the sequence goes on to the pin |
| **(c)** identity address changed | §3's open branch: the store misses, the watch pairs afresh, `PIN or Key Missing` never happens and `record_stale_bond()` never runs | **UNKNOWN** | nothing | **UNKNOWN until §10.3 measures whether the address survives the reset.** If it does, this state never occurs. If it does not, the fresh pairing needs the node's current digits like (b); with them the state is (b) after the eviction above, and without them it is §5.3's second consequence — a pairing failure that records nothing and offers nothing |

So "together or not at all" is (a)'s rule. In (b) it is the pin alone, and an
operation addressed at the bond store from either state — rather than at one
named record — is the wipe the previous paragraph forbids. The implementation
issue that follows this report must be scoped from this table, not from the
first one.

---

## 7. Reuse: what already exists and what it costs

Issue question 5. Full entry added to [`REUSE_LEDGER.md`](REUSE_LEDGER.md); the
short form:

| Candidate | Location | Reusable for a recovery operation? |
| --- | --- | --- |
| `BondRecovery` | [`firmware/main/meshcore_bond_recovery.h:97`](../../firmware/main/meshcore_bond_recovery.h) — "bool take_forget(BondIdentity& out)" | **Yes, unchanged — for state (a) of §6.1 only.** It answers "which bond, and only that one". It says nothing about the pin, and it should not — a second record would be a second thing to keep in sync with a single conflict. In state (b) it is empty by design (`pairing_succeeded()` cleared it), and what the operation needs then is the peer the pin refused: a second record with a second writer, the `Refused` arm at [`firmware/main/meshcore_ble.cpp:1613`](../../firmware/main/meshcore_ble.cpp) — "case attadipa::firmware::PinOutcome::Refused:" — not an extension of this one |
| `ForgetBondOperation` | [`firmware/main/meshcore_forget_outcome.h:59`](../../firmware/main/meshcore_forget_outcome.h) — "class ForgetBondOperation {" | **Not reused, in the end (#411).** The slot crosses the right two tasks, but the request comes from the same cancellable screen as the passkey and needs the passkey's *ticket*, so the passkey slot became a template — [`firmware/main/meshcore_passkey_outcome.h:76`](../../firmware/main/meshcore_passkey_outcome.h) — "class TicketedOperation {" — and the node operation is its second instance. This one stays the HIL bridge's, bond-shaped names intact |
| The worker `ForgetBond` event | [`firmware/main/meshcore_ble.cpp:1776`](../../firmware/main/meshcore_ble.cpp) — "taken = recovery.take_forget(peer);" | **Yes as the seam, not as the code.** #411's `ForgetNode` runs beside it on the same worker, the only task that may touch the bond store, and terminates the live session first as it does. What it does *not* inherit is the re-arm: this event arms one attempt because the bond is the only thing it clears; the node event clears the pin, and an armed reconnect over no pin adopts whichever node answers (§6). The node event arms nothing, and the passkey entry does |
| `erase_passkey()` | [`firmware/main/meshcore_ble.cpp:446`](../../firmware/main/meshcore_ble.cpp) — "esp_err_t err = nvs_erase_key(handle, kPasskeyNvsKey);" | **Not needed.** A recovery replaces the passkey through `store_passkey()` and never erases it (§5.3, §6); erasing leaves the watch unconfigured at the next boot. The function stays what it is, `Deconfigure`'s |
| An erase for the pin | [`link/src/meshcore_companion.cpp:805`](../../link/src/meshcore_companion.cpp) — "bool MeshCoreCompanion::unpin()" and [`firmware/main/meshcore_ble.cpp:469`](../../firmware/main/meshcore_ble.cpp) — "bool erase_node_pin()" | **Written for #411, and it is two things, not one line.** The RAM half is the one that makes the clear take hold: the reverse of `pin()` on a host-tested `link/` class, whose test asserts `pinned(out)` is **false** afterwards, not that the key reads back zero — a clear that zeroed the key and left `pinned_set_` true would refuse every node. The NVS half is `erase_passkey()`'s mirror on `kNodeKeyNvsKey`, and its role is narrower than it looks: the next adoption overwrites the key anyway, so the eraser only covers a restart in the window before that adoption (§6). Flash is erased first and memory second, so a restart between the two finds no pin rather than the old one |
| `core::Provisioner` | [`core/include/attadipa/core/provisioning.h:100`](../../core/include/attadipa/core/provisioning.h) — "virtual ProvisionOutcome forget_mesh_node() = 0;" | **The right seam, three methods wide since #411**: which node, forget it, how that ended — each with the passkey's shape, `Pending` and a consumed-once answer. ADR-0018 already argues why `apps/` must not reach `configure_meshcore_ble()` directly |

Licence and maintenance risk: all of the above is this repository's own code
under its own licence. No new dependency is implied. The only upstream
constraint is the one in section 8 below.

---

## 8. Upstream: NimBLE's automatic path is still not safe

Issue question 6, re-checked rather than inherited.

[Apache NimBLE issue #2206](https://github.com/apache/mynewt-nimble/issues/2206),
"BLE_GAP_EVENT_REPEAT_PAIRING forces bond deletion before authentication
completes": **still open**, created 2026-04-17, last updated 2026-04-17, zero
comments. Read through the GitHub API on 2026-09-02.

Nothing about the position changes, and one thing sharpens it. The reason this
firmware never returns `BLE_GAP_REPEAT_PAIRING_RETRY` is that the deletion
happens inside the callback, before Phase 2, so any peer in range could evict a
bond with one Pairing Request
([`firmware/main/meshcore_bond_recovery.h:34`](../../firmware/main/meshcore_bond_recovery.h) —
"inline constexpr int kRepeatPairingRetry = 1;"). That argument was about a
*bond*. Under section 6 the same automatic path would now also be the trigger
for clearing the **pin**, which is the thing that decides which node this watch
talks to at all. An automatic `RETRY` was rejected; an automatic anything is
worse now than it was in #325.

The event is in any case not the one this device sees — the watch is the central
and `ble_sm_chk_repeat_pairing()` is reached only from the inbound Pairing
Request handler — so this is a constraint on design, not a live code path.

---

## 9. The owner-consent contract, and the one open question

Issue question 3. Most of this is already decided and is not reopened here.

**Already decided, and this research does not touch it:** OD-26 and ADR-0018 fix
the consent factor at possession of the watch, expressed as a finger on its own
screen, with no listener of any kind in the product image. A revocation surface
inherits that rule — it is the same factor and the same panel. **No new owner
decision is needed about the mechanism.** This report does not choose a gesture,
a screen or a menu, and #409 says not to.

**What ADR-0018 got wrong, and is corrected on this branch:** its fact 2 called
`Deconfigure` erasing the passkey "the whole of revocation". It is not; by
section 6 it is no part of a recovery at all — the passkey is replaced by the
next entry, never erased — and the two pieces the erase leaves behind are the
two that actually block a reconnect. The correction now sits in the ADR beside the claim:
[`docs/adr/0018-owner-consent-for-provisioning.md:78`](../adr/0018-owner-consent-for-provisioning.md) —
"Correction, 2026-09-02". The ADR's *decision* is unaffected — it declines to
add a revocation gesture under either reading — so this is a factual correction
to a consequence, not a reopened decision.

**The one question that does need the owner** is not about the contract. It is
about the experiment:

> **The field test in section 10 requires factory-resetting the free bench T114,
> which destroys that node's MeshCore identity key.**

`AGENTS.md` forbids destroying keys without an explicit decision, and nothing
covers this one. OD-16 authorises *flashing* two T114s with the latest official
MeshCore ([`OWNER_DECISIONS.md:1127`](OWNER_DECISIONS.md) —
"to be flashed with the latest official"), which is a different act with a
different consequence; and the free bench node is pinned to `v1.17.1-d929643` by
a separate owner decision because it is this repository's protocol reference.
A factory reset keeps the firmware revision and destroys the identity, the
contacts and the channels — including the key `5c62d9bc82e530fc…` that
`TEST_FLEET.md` and `MESHCORE_T114_FIRST_CONTACT.md` both record as this node's.

Options, with a recommendation:

| | Option | Cost | Risk |
| --- | --- | --- | --- |
| **A** | **Reset the free bench T114** (recommended) | its identity, contacts and channels are lost; two research documents need their key updated; the node must be re-added to any room it is in | lowest — it is the node the fleet table names as free, and the only node whose firmware this repository has read |
| B | Reset the V4.3 companion instead | it runs a third-party fork (`dt267`), so a failure would not distinguish "Attadipa cannot recover" from "this fork differs" | the run would not answer the question for the fleet's actual T114s |
| C | Buy or borrow a sixth node for the reset | no fleet node is harmed; the pinned bench baseline is preserved intact | schedule; and the new node is not the one the protocol facts were read from |
| D | Do not run the experiment; ship the recovery surface on host tests alone | none now | the whole failure is a hardware interaction. Every claim stays `NOT EXECUTED — HARDWARE REQUIRED`, which is what #409 exists to stop |

**Recommendation: A.** The free bench node is the one the fleet table sets aside
for exactly this, the reset is reversible in every respect the repository cares
about *except* the identity key, and the two documents that record that key can
be updated in the same run that reads the new one. B answers a different
question and C costs a purchase to protect a key whose only value is that two
documents quote it.

This is the only item on this issue that needs a human, and it does not block
the research output.

---

## 10. Field-test plan · `NOT EXECUTED — HARDWARE REQUIRED`

Reproducible, and written so that a person with the hardware can run it without
this document's author. Every physical number it asks for is a number nobody
here has.

### 10.1 Exact setup, recorded before anything is touched

| Field | How to record it |
| --- | --- |
| Attadipa firmware SHA | the full 40-character SHA actually flashed. **Not `6b88158`** — that is what this research read, not what the run will flash |
| Watch | model, revision, USB serial, BLE MAC. Bench unit is the Waveshare ESP32-S3 Touch AMOLED 2.06" |
| Watch config | `sdkconfig` for the **product** image and, separately, for the HIL image; both archived. Confirm `CONFIG_ATTADIPA_WATCH_CONTROL` is `n` in the product one |
| Partition table, build manifest, `idf.py --version`, esptool version | archived alongside |
| Node | the free bench T114 only. Record hardware revision, BLE address, self name, public key, radio band, and firmware SHA from `RESP_CODE_DEVICE_INFO` — **before** and **after** the reset |
| Node passkey | the six digits used for each pairing attempt, and which boot of the node they came from (§5.3). Record the digits themselves: they are a bench passkey on a reset node, not a credential |

Do not reset, reflash or otherwise write to the Home Assistant node, the Room
Server or the repeater. Section 9's owner decision covers the free bench node
and nothing else.

### 10.2 Baseline, on the product image

1. Provision the watch from its own screen (PR #406's entry screen): clock,
   offset, and the node's current passkey.
2. Confirm pairing, the Companion handshake, and at least one send with a reply.
3. Snapshot, machine-readable: selected node public key, session/connection
   generation, negotiated ATT MTU, both firmware IDs, reset reason, battery
   voltage.
4. Archive redacted serial logs. **Do not log the passkey through the watch's
   own console** — record it in the run sheet instead.

### 10.3 Fault injection

0. **Identity gate, before the destructive step.** Power down every other
   MeshCore node — the Home Assistant node, the Room Server, the repeater and
   the V4.3 companion — so that exactly one node advertises. Read
   `RESP_CODE_DEVICE_INFO` and `RESP_CODE_SELF_INFO` from the node in hand and
   confirm they match the free bench T114 as §10.1 recorded it and as
   [`MESHCORE_T114_FIRST_CONTACT.md:47`](MESHCORE_T114_FIRST_CONTACT.md) —
   "Heltec T114, BLE address `f7:f3:33:6b:9b:61`" — and
   [`:50`](MESHCORE_T114_FIRST_CONTACT.md) — "`5c62d9bc82e530fc…` after the
   reset" — record it: address `f7:f3:33:6b:9b:61`, public key
   `5c62d9bc82e530fc…`, firmware `v1.17.1-d929643`. **Any mismatch aborts the
   run before the reset.** Nothing else distinguishes the fleet's four T114s —
   [`TEST_FLEET.md:49`](TEST_FLEET.md) — "roles — the model, the BLE address (which"
   — and node selection is by advertisement order (#304), so this gate
   is the only thing that keeps the reset off a node in service.
1. Factory-reset **only** the free bench node. Record its new self name, new
   public key, and — the point of §3's open branch — **whether its BLE address
   changed**.
2. On the watch, record: the detection latency from the node coming back to the
   first `PIN or Key Missing`; the exact ENC_CHANGE status; whether a conflict
   was recorded or the peer was unidentifiable; and that no reconnect storm
   follows (expected: exactly one attempt, then silence).
3. If the address did change, record that instead: the watch will have paired
   and bonded afresh and then refused the node on its key (§4). Both are
   informative; only one can happen.

### 10.4 The recovery attempt, product image only

**USB, the HIL image and `erase-flash` are forbidden on this path.** They may be
used afterwards, as a separately labelled rescue, and the label must survive
into the report.

1. Read the node's current passkey off its screen. Enter it on the watch.
2. Record what happens. The prediction from §3 is: one connection, one
   `PIN or Key Missing`, transport faulted again. **A recovery here would
   falsify section 3 and is the single most valuable negative result this run
   can produce.**
3. Repeat with a deliberately wrong passkey, and record that the failure is
   distinguishable from (2) in the log.
4. Power-cycle the watch and record whether the boot replay reproduces the same
   single attempt.
5. Power-cycle the *node* and record that its passkey changed (§5.3).

### 10.5 Negative paths

- A second, unselected MeshCore node in range during the failure and during any
  recovery — brought back after §10.3 step 0, which had it powered down for
  the reset only: it must not become the recorded conflict. Whether it is
  adopted is what this run **records**, not what it forbids: both nodes take
  the same passkey (§6), so adoption is advertisement order. Record which node
  was adopted; that the entry screen's node field showed *that* node's prefix;
  and, if it was the wrong one, that forgetting it again from the screen was
  possible and the second attempt landed on the other.
- Reordered reconnect events: node powered down and back up mid-attempt.
- A recovery action repeated with nothing recorded: must refuse, and must say
  "nothing to forget" rather than report a store failure.
- Bond-store deletion refusal, **only if it can be provoked safely**. Do not
  simulate it and call the result a hardware `PASS`.
- After every step: the clock and the UTC offset still correct, no unrelated NVS
  namespace disturbed.

### 10.6 Pass

- The owner returns the watch to a working mesh **from the product image alone** —
  no HIL firmware, no USB control plane, no `erase-flash`.
- Exactly the confirmed stale bond and the pin it belongs with are cleared; a
  nearby unselected node can cause neither.
- The forget arms nothing: after it the watch is silent — no scan, no
  connection — until the passkey is entered, and one attempt follows that
  entry.
- With one node advertising, the intended node is adopted. With two, the node
  the watch adopted is the node the entry screen's node field shows, and
  forgetting it again is possible from the screen.
- One truthful terminal result per action, reported after the store answered,
  never before.
- Reconnect, then a send with a reply.
- Clock, offset and unrelated configuration intact.

### 10.7 Fail

Any of: HIL, USB or `erase-flash` required; a peer other than the selected one
affected; the watch left in an endless scan or fault loop; capability or screen
reporting a stale success; a success reported before the store or worker
answered; a scan or connection between the forget and the passkey entry; the
node field showing a node other than the one the watch is talking to.

### 10.8 Artifacts and metrics

Archive: timestamped serial logs from watch and node, the state-transition
transcript with addresses redacted to the last three octets as the firmware
already does, both build manifests, both `sdkconfig`s, exact SHAs, and a packet
trace if a sniffer is available.

Record, each labelled `MEASURED` / `ESTIMATED` / `UNKNOWN`:

| Metric | Note |
| --- | --- |
| RSSI | at pairing and at steady state |
| SNR | only if a Mesh round trip completed; it comes from the node |
| Packet loss, malformed/dropped frame counters | the transport already counts these |
| Command and message latency | |
| Reconnect time | node reappearing → session Ready |
| Connection and session generation | to prove which attempt a log line belongs to |
| ATT MTU | |
| Battery voltage and current | |
| Reset reason | on every watch boot in the run |
| GNSS fix age, C/N0, HDOP, TTFF | **`UNKNOWN` — out of scope.** Not part of this experiment; a separate concurrency run would be needed |

---

## 11. Status of every hardware claim in this report

| Claim | Label |
| --- | --- |
| A factory reset regenerates the node's public key | `MEASURED` — this bench, 2026-08-28, [`MESHCORE_T114_FIRST_CONTACT.md:50`](MESHCORE_T114_FIRST_CONTACT.md) "a factory reset regenerates it" |
| `ble_gap_security_initiate()` encrypts rather than pairs when an LTK is stored | source-traced at a pinned revision (§3). Never observed on this bench |
| The pin refuses a reset node after the bond is deleted | source-traced through this repository's own code (§4). Never observed |
| The node's BLE passkey is random per boot after a reset | source-traced at `d929643` (§5.3). Never observed |
| The node's factory reset also destroys its bond | source-traced (§5.2). Never observed |
| Whether the node's BLE identity address survives a reset | **UNKNOWN** (§3) |
| Stale-bond detection, product-image recovery, physical passkey entry | **NOT EXECUTED — HARDWARE REQUIRED** |
