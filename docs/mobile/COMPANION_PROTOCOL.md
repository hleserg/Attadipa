# Companion protocol

**Status: design only.** Nothing here is implemented. This is the shape of the
protocol, chosen now so the firmware's service boundaries are right; the wire
format is not frozen and the UUIDs below are placeholders.

---

## 1. Principles

1. **Versioned from the first byte.** A protocol without a version field cannot
   be changed after one device ships. The version is negotiated at connect, not
   assumed.
2. **The watch may refuse anything.** Every message can be rejected, and refusal
   is a normal outcome, not an error path bolted on later.
3. **No message is required for correct operation.** If the phone never speaks,
   the watch is fully functional — see the architecture document §1.
4. **Bounded.** Every field has a maximum length and every queue has a maximum
   depth, declared here rather than discovered in the field.
5. **Idempotent where it can be.** A BLE link drops. Re-sending must be safe.

## 2. GATT structure

One primary service, characteristics grouped by direction and lifetime. Splitting
control from bulk matters because a firmware image or a log bundle must not be
able to starve a time-sync write.

| Characteristic | Dir | Properties | Purpose |
|---|---|---|---|
| Control | P→C, C→P | write, notify | version negotiation, capability exchange, session state |
| Time | C→P | write | time, timezone, DST |
| Assistance | C→P | write (chunked) | GNSS assistance payload |
| Notification | C→P | write | relayed phone notification |
| Command | P→C | notify | watch-initiated: Find My Phone |
| Bulk | both | write, notify | logs out, firmware in — explicitly user-initiated |

P = peripheral (watch), C = central (phone).

## 3. Session

```
connect → authenticate → version negotiate → capability exchange → session
```

**Capability exchange is not optional.** The watch tells the phone what it has;
the phone tells the watch what it can supply. This is the same capability model
the firmware uses internally ([ADR-0001](../adr/0001-capability-model.md)) and
for the same reason: the two boards are not the same device.

A companion must learn what the **device** has, not what its board has, and must
never send assistance to a device that cannot use it — rather than sending it and
having it ignored. A Waveshare board has no GNSS; a Waveshare with an Attadipa node
attached does. Which means the capability set **cannot be exchanged once at
connect and then trusted**: it changes mid-session whenever a node attaches or
leaves, and the protocol needs a re-exchange for that.
Wasting a phone's data budget and a BLE window on a payload the watch cannot use
is a bug, and it is only avoidable if capability exchange happens first.

Similarly the phone announces what it cannot do — a phone whose user denied
notification access says so at connect, so the watch's settings screen can show
"the phone has not granted this" rather than "no notifications".

## 4. Message families

| Family | Direction | Notes |
|---|---|---|
| Time sync | phone → watch | absolute time + timezone + DST rule. See §5 on trust. |
| Timezone change | phone → watch | travel; separate from a time correction |
| GNSS assistance | phone → watch | chunked, with expiry. Format depends on MOB2 |
| Notification relay | phone → watch | subject to the on-watch allowlist |
| Incoming call | phone → watch | distinct from a notification: it is time-critical and cancellable |
| Find My Phone | watch → phone | see §6 |
| Diagnostic log | watch → phone | bulk, user-initiated |
| Firmware image | phone → watch | bulk, user-initiated, signed |
| Settings | both | the watch remains authoritative over its own configuration |

Whose configuration, exactly, is a question this table does not yet answer and
[OWNER_DECISIONS](../research/OWNER_DECISIONS.md) OD-2 makes urgent. A node holds
fourteen parameters of its own, two of them legally bounded (frequency, TX
power). Three ownership models are possible — the node owns them, the watch owns
them and pushes, or the watch proposes and the node ratifies — and they differ in
what happens when a change breaks the very link used to make it. Changing the
frequency will disconnect you. That belongs in ADR-0005, not here.

## 5. Trust

**Everything the phone sends is untrusted input from a device the watch does not
control.** Two rules, both concrete:

**Range-check before use.** A time sync that would move the clock by more than a
threshold is not silently applied — it is a prompt, or it is refused. A phone
with a wrong timezone should not be able to silently make the watch wrong, and a
malformed payload must not be able to move the clock to 1970 and take every
scheduled task with it.

**Assistance data expires.** GNSS assistance has a validity period. Data past it
is worse than no data — it makes acquisition slower, not faster. The watch tracks
expiry itself and discards; it does not trust the phone to stop sending.

Authentication is BLE bonding at minimum. Whether an application-layer secret is
also needed is MOB3 and is not yet decided. What *is* decided: pairing requires a
deliberate user action on the watch. A watch that can be paired without anyone
touching it is a watch anyone can pair with.

## 6. Find My Phone

The watch sends one command. What happens next is entirely the phone's problem,
and the interesting engineering is all on the Android side — see
[ANDROID_PERMISSIONS_AND_LIMITATIONS.md](ANDROID_PERMISSIONS_AND_LIMITATIONS.md).

The watch-side contract is deliberately small:

- Send, then show "sent" — **never** "your phone is ringing". The watch cannot
  know that. Reporting it would be a claim about the world the device has no
  evidence for, which is the same discipline the hardware documents apply to
  measurements.
- Provide a cancel that sends a stop command, and accept that it may not arrive.
- Fail visibly when the link is down. A Find My Phone that silently does nothing
  is worse than one that says it cannot reach the phone.

## 7. Bounds

Declared now, so they exist before there is code to violate them.

| Bound | Value | Rationale |
|---|---|---|
| Protocol version | negotiated at connect | §1 |
| Max notification title / body | to be set | must fit the screen and the store |
| Notification queue depth | to be set | bounded, with defined drop behaviour |
| Assistance payload chunk | to be set | must fit the negotiated MTU |
| Assistance total size | to be set | must fit the flash budget — [RESOURCE_BUDGET](../architecture/RESOURCE_BUDGET.md) |
| Max clock step without confirmation | to be set | §5 |

"To be set" means exactly that. Each becomes a number when the constraint it
depends on — screen size, MTU, storage partition — is itself established.

---

## Not decided

- Wire encoding. A compact binary encoding is the obvious fit for a BLE MTU, but
  this is an ADR to write, not a default to assume.
- Whether Bulk shares the BLE link or triggers a Wi-Fi transfer for large
  payloads. The power argument points at Wi-Fi for firmware images.
- Whether the watch may ever initiate a connection, or only advertise.
