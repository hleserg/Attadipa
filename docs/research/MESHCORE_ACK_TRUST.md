# What a MeshCore delivery confirmation proves

Research for [#634](https://github.com/hleserg/Attadipa/issues/634). It changes
no production code. It answers one question: when the companion node pushes
`PUSH_CODE_SEND_CONFIRMED` (`0x82`) and this client sets
`MeshDelivery::Confirmed`, what has actually been established?

**The answer:** the companion node received a packet carrying four bytes equal
to a value it was waiting for. Nothing in the path checks who sent those
bytes, and nothing binds them to the recipient the message was addressed to.
`Confirmed` means **"the companion node observed a matching four-byte ACK
value"**. It does not mean "the intended recipient's node acknowledged", and it
is no stronger property than that. Confidence: **high**, read from source.

Under honest conditions the two meanings coincide: the recipient's node is the
only party that computes and sends the value. They come apart only if somebody
else sends the bytes. Whether that happens on a real mesh is the open part
(§5).

---

## 0. Provenance

| Source | Revision | Licence | How it was read |
|---|---|---|---|
| MeshCore, `meshcore-dev/MeshCore` | `main@e94125987ed87497e706a0b54d1e80c709343980` | MIT (`license.txt`) | source read on 2026-10-01; no code taken |
| Atta-dipa | `main@5329550a` | this repository | source read; one host test run (§4) |
| Meshtastic [#11932](https://github.com/meshtastic/firmware/pull/11932) | head `85d7cdac`, merged as `57bdedf3` into `develop` 2026-09-24 | GPL-3.0 | PR state read 2026-10-01; `master` does not contain it, so no release does |
| Meshtastic [#11900](https://github.com/meshtastic/firmware/pull/11900) | head `3a6ebf86` | GPL-3.0 | open, unmerged, read 2026-10-01 |

The fleet runs `v1.17.1` (`d92964352441e53b93e8667b802e04f6e072b39e`), not
`e9412598`. Every file cited below — `src/Mesh.cpp`, `src/Utils.cpp`,
`src/MeshCore.h`, `src/helpers/BaseChatMesh.cpp` and
`examples/companion_radio/{MyMesh.cpp,MyMesh.h,main.cpp}` — is byte-identical
between the two (`git diff --stat` empty, 2026-10-01), so the line numbers hold
for the fleet too.

## 1. How the four bytes are made

Sender, `src/helpers/BaseChatMesh.cpp:431`:

```cpp
mesh::Utils::sha256((uint8_t *)&expected_ack, 4, temp, 5 + text_len, self_id.pub_key, PUB_KEY_SIZE);
```

`Utils::sha256` with two fragments (`src/Utils.cpp:36`) is a plain SHA-256 over
`frag1 ‖ frag2`. It is **not keyed**: the sender's public key is appended
data, and it is public. The inputs are `timestamp ‖ (attempt & 3) ‖ text ‖
sender public key`. The recipient is not an input; it is used only to address
and encrypt the datagram (`:439`, `createDatagram`).

Recipient, `BaseChatMesh.cpp:243`, computes the same four bytes over the
decrypted message and the sender's public key, sets byte 5 from the attempt and
byte 6 at random (`:244-246`), and answers one of two ways:

- **Flood-routed message.** The ACK is folded into a `PAYLOAD_TYPE_PATH` return
  (`:250-251`, `createPathReturn`). That return is encrypted and MAC'd with the
  pairwise secret. The MAC is **two bytes** (`src/MeshCore.h:17`,
  `#define CIPHER_MAC_SIZE      2`).
- **Direct-routed message.** `sendAckTo(from, ack_hash, 6)` (`:254`) builds a
  bare `PAYLOAD_TYPE_ACK`. `Mesh::createAck` (`src/Mesh.cpp:560`) copies the
  bytes into the payload (`:568`). No encryption, no MAC, no sender field.

## 2. How the four bytes are accepted

Three routes reach the companion's ACK table, and none of them asks whose ACK it
is:

1. **Bare ACK, direct route with a path** — `Mesh.cpp:78-85`, the "early
   received" check. It calls `onAckRecv` on any direct ACK it hears, with no
   `wasSeen` check and before checking whether this node is the next hop.
2. **Bare ACK, flood route** — `Mesh.cpp:117-125`. Deduplicated by `wasSeen`,
   then `onAckRecv`. No other check.
3. **ACK inside a PATH return** — `Mesh.cpp:147-159` tries every contact whose
   hash matches the source byte, and the first whose `MACThenDecrypt` succeeds
   wins. Then `BaseChatMesh.cpp:336-338` passes the ACK to `processAck`. The
   contact that produced the MAC is never compared with the contact the message
   was sent to.

`BaseChatMesh::onAckRecv` (`:347-349`) passes the four bytes to `processAck`.
The companion's `MyMesh::processAck` (`examples/companion_radio/MyMesh.cpp:414-426`)
scans an eight-entry circular table (`MyMesh.h:248-254`) by `memcmp` of four
bytes. On a match it writes `[0x82][ack:4][trip_time:4]` to the app, zeroes the
entry, and returns the stored contact. The frame carries **no contact
identity**. The returned contact is used only to cancel a timer and to retry a
path; nothing compares it with the packet's origin.

Entries are added at `MyMesh.cpp:1113-1117` and never expire by age.
`onSendTimeout()` is empty (`:860`).

## 3. What Atta-dipa does with it

`link/src/meshcore_companion.cpp:60` — "constexpr std::uint8_t kPushSendConfirmed = 0x82;".
The arm at `link/src/meshcore_companion.cpp:1660` — "case kPushSendConfirmed:"
refuses fewer than five bytes, refuses when nothing is pending
(`link/src/meshcore_companion.cpp:1698` — "if (expected_ack_ == std::array<std::uint8_t, 4>{}) {"),
compares the four bytes with `expected_ack_`, and on a match sets
`link/src/meshcore_companion.cpp:1705` — "status_.delivery = core::MeshDelivery::Confirmed;".
The catalogue renders that as `delivered` / `доставлено`
(`l10n/strings.toml:929` — "[mesh_delivery_confirmed]").

Two source comments in that arm call the tag a "keyed hash"
(`link/src/meshcore_companion.cpp:1694` — "// keyed hash, so that costs one message in 2^32 an upgrade it was owed",
`link/src/meshcore_companion.cpp:1724` — "// different request -- which matters, because the tag is a keyed hash").
§1 shows it is not keyed. The comments are left as they are because this is a
research change; the conclusions they draw (rarity of an all-zero tag,
repetition for identical messages) do not depend on the word.

## 4. The six cases

| Case | Possible? | Confidence | Why |
|---|---|---|---|
| (a) Honest ACK loss | yes | **high** | The ACK is one unacknowledged packet. Its loss leaves `Unconfirmed`, which [ADR-0023](../adr/0023-unconfirmed-is-not-failed.md) already handles |
| (b) Honest 32-bit collision | yes | **medium**; the rate is **ESTIMATED** negligible | Four bytes of SHA-256 against at most eight pending entries. Separately, identical text in the same second with the same `attempt & 3` gives the *same* tag by construction, to any recipient |
| (c) Replay of an old ACK | not for a consumed entry; **yes** if the same tag is armed again | **high** | `processAck` zeroes the entry on a match (`MyMesh.cpp:425`). That, not packet dedup, is what stops a replay. A second send with the same text, second and `attempt & 3` re-arms the same tag, and an ACK captured from the first send then confirms the second, whoever it was addressed to |
| (d) Forgery by an RF observer with no key | **yes** in principle | **medium**; practicality **UNKNOWN** | A bare ACK needs no secret (§1). To forge one the observer must guess the text: the timestamp is close to the time it heard the message, `attempt & 3` is two bits, the sender's key is public, and the ciphertext length leaks the text length to a 16-byte block. A built-in canned phrase is easy to guess; free text is not |
| (e) Forgery by a known mesh participant | **yes** | **high** | Everything in (d), plus a MAC'd PATH return under its own pairwise secret carrying any ACK bytes it likes — route 3 accepts it without comparing that participant with the addressed recipient |
| (f) A genuine ACK from the wrong recipient | **yes** | **high** | The same text in the same second to two recipients gives one tag; whichever ACK arrives first confirms. `0x82` cannot say whose ACK it was |

**Run here, 2026-10-01:** `meshcore_companion` host test on `main@5329550a`,
`100% tests passed, 0 tests failed out of 1`. Its `test_send_and_receive` feeds
`[0x82, 1, 2, 3, 4]` after a `RESP_CODE_SENT` carrying tag `01 02 03 04` and
checks that the delivery becomes `Confirmed`
(`tests/test_meshcore_companion.cpp:741` — "const std::uint8_t ack[] = {0x82, 1, 2, 3, 4};").
That documents what the client does with any matching frame. It says nothing
about where the frame came from, because no part of the client can know.

**Not run:**

- An upstream host harness that injects a bare `PAYLOAD_TYPE_ACK` into a
  `MyMesh` and observes the `0x82` push. MeshCore has no native test target at
  this revision, and building one is a production-sized piece of work this
  research does not grow. The conclusion rests on the source trace in §2.
- **HIL: two nodes plus an injector**, the intended recipient powered off, a
  forged bare ACK, and a third-peer injector. **NOT EXECUTED — HARDWARE
  REQUIRED.**

One more edge, read from source: the companion object is a namespace-scope
global (`examples/companion_radio/main.cpp:98`), so its ACK table starts
zero-filled, and a consumed entry is zeroed. A bare ACK of four zero bytes
therefore matches such an entry and produces a `0x82` push of zeros. This client
ignores it unless its own pending tag is zero, and it refuses to treat a zero
tag as pending.

## 5. What this changes

- **`Confirmed` keeps its behaviour.** Decision 2a of ADR-0023 still holds: a
  match is the best evidence this protocol offers, and discarding it would push
  the owner toward a duplicate send. What changes is the claim made for it.
  ADR-0023 is amended to stop calling it "positive proof" and to stop saying
  the recipient's node generated it.
- **The wording is an open question, not a change made here.** `delivered` /
  `доставлено` is true under honest conditions and overstates under cases
  (c)–(f). Whether the UI should say less is a product decision in another
  agent's zone — [OPEN_QUESTIONS](OPEN_QUESTIONS.md) M49.
- **What would make it recipient-bound.** Only a change on the wire: an ACK
  whose integrity is keyed with the pairwise secret, carried on every route,
  and checked against the contact the message was addressed to. This is the
  invariant Meshtastic #11932 fixed in its own protocol: the proof must be
  bound to the node that was addressed, not to whoever claims to have sent the
  ACK. **ADAPT the invariant, take no code** — GPL-3.0, a different wire
  protocol. #11900 is a second warning of the same kind (an implicit ACK
  correlated on clear header fields) and is only **MONITOR**.
- **What would size the risk.** A HIL injection run and a radio threat model
  for the fleet — [OPEN_QUESTIONS](OPEN_QUESTIONS.md) M50.
