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
Section 9 is the field-test plan that would change that; section 8 is the one
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
[`firmware/main/meshcore_ble.cpp:850`](../../firmware/main/meshcore_ble.cpp) —
"BLE_HS_HCI_ERR(BLE_ERR_PINKEY_MISSING)) {", which calls
[`:851`](../../firmware/main/meshcore_ble.cpp) —
"(void)record_stale_bond(event->enc_change.conn_handle," and then falls into
[`:854`](../../firmware/main/meshcore_ble.cpp) —
"disconnect_fault(generation, " like every other pairing failure.

| State | Where it lives | What the fault does to it | Survives a reboot |
| --- | --- | --- | --- |
| The bond (LTK, IRK, CCCD) | NimBLE's own NVS, `CONFIG_BT_NIMBLE_NVS_PERSIST=y` — [`firmware/sdkconfig.defaults:108`](../../firmware/sdkconfig.defaults) "CONFIG_BT_NIMBLE_NVS_PERSIST=y", one slot, [`:116`](../../firmware/sdkconfig.defaults) "CONFIG_BT_NIMBLE_MAX_BONDS=1" | **untouched.** The whole of #325 is that the firmware does not delete it | **yes** |
| The conflict record | RAM, `BondRecovery::conflicted_`, written at [`firmware/main/meshcore_bond_recovery.h:85`](../../firmware/main/meshcore_bond_recovery.h) — "if (!conflicted_.valid && peer.valid) conflicted_ = peer;" | set, once, to the **first** conflicting peer | **no** — and this is the load-bearing asymmetry, see below |
| The pinned node public key | NVS, `attadipa_mesh` / [`firmware/main/meshcore_ble.cpp:225`](../../firmware/main/meshcore_ble.cpp) — "constexpr const char* kNodeKeyNvsKey" | **untouched, and untouchable** — section 4 | **yes** |
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
[`firmware/main/meshcore_ble.cpp:1456`](../../firmware/main/meshcore_ble.cpp) —
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
**UNKNOWN** and section 9 measures it:
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
[`firmware/main/meshcore_ble.cpp:1545`](../../firmware/main/meshcore_ble.cpp) —
"const int deleted = ble_store_util_delete_peer(&address);" and re-arms one
attempt at [`:1580`](../../firmware/main/meshcore_ble.cpp) —
"reconnect_allowed.store(true);".

The watch then reconnects, pairs afresh (no LTK now, so section 3's other
branch), completes the Companion handshake, and reads the node's public key out
of `RESP_CODE_SELF_INFO`. A factory-reset node's key is a **new** key —
[`MESHCORE_T114_FIRST_CONTACT.md:50`](MESHCORE_T114_FIRST_CONTACT.md) —
"a factory reset regenerates it" is `MEASURED` on this bench. So:

- [`link/src/meshcore_companion.cpp:397`](../../link/src/meshcore_companion.cpp) —
  "if (pinned_set_ && !(status_.node_id == pinned_)) {" latches `wrong_node_`;
- [`firmware/main/meshcore_node_pin.h:194`](../../firmware/main/meshcore_node_pin.h) —
  "if (!ops.wrong_node()) return PinOutcome::Pinned;" therefore falls through to
  [`:200`](../../firmware/main/meshcore_node_pin.h) —
  "return PinOutcome::Refused;";
- the address is cooled down for 60 s and the connection is terminated.

The scan then walks back to the same node a minute later and refuses it again,
indefinitely. Mesh does not come back.

**And nothing erases the pin.** The only writer of `attadipa_mesh/node` is
`store_node_pin()` — [`firmware/main/meshcore_ble.cpp:383`](../../firmware/main/meshcore_ble.cpp) —
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

**The tree already knew.** Two comments say it, and both point at #356 to close
it:

- [`firmware/main/meshcore_ble.cpp:219`](../../firmware/main/meshcore_ble.cpp) —
  "re-pin**: nothing erases the ";
- [`core/include/attadipa/core/mesh_service.h:61`](../../core/include/attadipa/core/mesh_service.h) —
  "until #356 there is no in-image way to re-pin".

**#356 does not close it.** PR #406 — the second and last change for #356 —
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

1. A recovery that keeps the stored passkey is wrong. The stored passkey is
   stale the moment the node is reset, and stale again after the node's next
   power cycle.
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

This is source-traced at a pinned revision, not measured. Section 9 measures it.

---

## 6. The minimum atomic scope of a revocation

Issue question 4. Given sections 2–5, the smallest operation that can actually
return a product watch to service is:

**Must be cleared, together or not at all:**

| Item | Where | Why it cannot be left |
| --- | --- | --- |
| The conflicting bond | NimBLE NVS, one slot | §3 — while it exists, every attempt encrypts instead of pairing |
| The pinned node public key | `attadipa_mesh/node` | §4 — while it exists, the reset node is refused after pairing |
| The stored passkey | `attadipa_mesh/passkey` | §5.3 — it is stale by construction, and a boot replay of a stale passkey is a failed pairing at every start |
| The live session and `reconnect_allowed` | RAM | the deletion cannot happen under a live encrypted session — the existing worker already terminates first, [`firmware/main/meshcore_ble.cpp:1545`](../../firmware/main/meshcore_ble.cpp) "const int deleted = ble_store_util_delete_peer(&address);" is preceded by a terminate |
| The refusal cooldown and `has_refused` | RAM / `MeshStatus` | otherwise the screen keeps reporting a refusal that has been revoked, and the scan skips the node for up to a minute after the owner acted |

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
reports success after the first of three writes repeats #378 with more state.
The existing `ForgetBondOperation` slot is the right shape for this and is
discussed next.

**What must *not* be in scope, and this is not a UI opinion:** peer-triggered
deletion. Nothing a radio peer does may cause any of the five clears. §325's
whole argument — [`firmware/main/meshcore_bond_recovery.h:85`](../../firmware/main/meshcore_bond_recovery.h) —
"if (!conflicted_.valid && peer.valid) conflicted_ = peer;" keeps the **first**
conflict rather than the newest, so a peer cannot aim the owner's next action.
A revocation that clears the pin as well as the bond makes that property *more*
important, not less: the pin is what stops a stranger's node being adopted, so
an attacker who can provoke a revocation gets adoption, not just a dropped link.

---

## 7. Reuse: what already exists and what it costs

Issue question 5. Full entry added to [`REUSE_LEDGER.md`](REUSE_LEDGER.md); the
short form:

| Candidate | Location | Reusable for a recovery operation? |
| --- | --- | --- |
| `BondRecovery` | [`firmware/main/meshcore_bond_recovery.h:97`](../../firmware/main/meshcore_bond_recovery.h) — "bool take_forget(BondIdentity& out)" | **Yes, unchanged.** It answers "which bond, and only that one". It says nothing about the pin, and it should not — a second record would be a second thing to keep in sync with a single conflict |
| `ForgetBondOperation` | [`firmware/main/meshcore_forget_outcome.h:59`](../../firmware/main/meshcore_forget_outcome.h) — "class ForgetBondOperation {" | **Yes, with one honest caveat.** The slot crosses the same two tasks and enforces the same one-at-a-time rule. Its outcome enum is named for a bond (`Deleted`, `Refused`, `Nothing`); an operation that also clears the pin and the passkey either widens those names or reports a partial completion under a name that says "the bond" |
| The worker `ForgetBond` event | [`firmware/main/meshcore_ble.cpp:1519`](../../firmware/main/meshcore_ble.cpp) — "taken = recovery.take_forget(peer);" | **Yes as the seam**, and it is the only place that may touch the bond store: it already runs on the mesh worker, already terminates the live session first, and already re-arms exactly one attempt |
| `erase_passkey()` | [`firmware/main/meshcore_ble.cpp:372`](../../firmware/main/meshcore_ble.cpp) — "esp_err_t err = nvs_erase_key(handle, kPasskeyNvsKey);" | **Yes.** Already correct about a missing key being success |
| An erase for the pin | — | **Does not exist.** This is the one new line of storage code any implementation needs, and it is the mirror of `erase_passkey()` on `kNodeKeyNvsKey` |
| `core::Provisioner` | PR #406, `core/include/attadipa/core/provisioning.h` | **The right seam, and it is two methods wide.** A revocation is a third; ADR-0018 already argues why `apps/` must not reach `configure_meshcore_ble()` directly |

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
section 6 it is one fifth of it, and the two pieces it omits are the two that
actually block a reconnect. The correction now sits in the ADR beside the claim:
[`docs/adr/0018-owner-consent-for-provisioning.md:75`](../adr/0018-owner-consent-for-provisioning.md) —
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
Server or the repeater. Section 8's owner decision covers the free bench node
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
  recovery: it must not become the recorded conflict and must not be adopted.
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
- One truthful terminal result per action, reported after the store answered,
  never before.
- Reconnect, then a send with a reply.
- Clock, offset and unrelated configuration intact.

### 10.7 Fail

Any of: HIL, USB or `erase-flash` required; a peer other than the selected one
affected; the watch left in an endless scan or fault loop; capability or screen
reporting a stale success; a success reported before the store or worker
answered.

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
