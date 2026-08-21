# Companions and position sources — what must be verified

Status: **open research.** Nothing here is established. Every row is a question
with a named source to answer it from, and the file exists so that the answers
land somewhere other than a chat log.

Raised by the owner on 2026-08-22 and recorded as
[OD-7](OWNER_DECISIONS.md#od-7--the-companion-is-any-node-not-only-ours),
[OD-8](OWNER_DECISIONS.md#od-8--every-source-of-position-and-the-watch-as-the-instrument),
[OD-9](OWNER_DECISIONS.md#od-9--the-node-may-carry-a-cellular-modem) and
[OD-10](OWNER_DECISIONS.md#od-10--a-standing-person-does-not-need-a-new-fix).
Those say what was decided. This says what nobody knows yet.

## 0. The one architectural observation worth writing down now

All of it fits shapes the ADRs already have, and saying so is the point — the
next agent starts from the ADRs rather than from a blank file.

| The request | The shape it already has |
|---|---|
| any node, any stack, any transport | [ADR-0008](../adr/0008-mesh-service-providers.md) — one service, providers behind it, applications that never learn which answered. Widen the second provider; do not add a second code path |
| telemetry from a companion | [T-029](../../TASKS.md) — feeds are not capabilities. A telemetry value is a datum with two ages ([ADR-0004](../adr/0004-capability-sources.md)) |
| positions from several places at once | [ADR-0011](../adr/0011-gnss-integrity.md) — `PositionValidity` and `TrustState` are already independent axes. A coordinate out of somebody else's message is what that independence is for |
| choose a source, or fuse several | ADR-0008 §3's selection policy, extended. **Fusion is a different feature** and is not that policy applied twice |
| GNSS asked less often when still | `start_kind()`, the backup domain, and T-045's power states — all present, none of them yet with a customer |

What does **not** yet have a shape is a *position estimator* — anything that
combines two sources into a third number. That is an ADR nobody has written, and
it should not be written before the replay rig can be pointed at multi-source
traces.

## 1. Vanilla MeshCore as a companion — T-072

Pinned revision `d92964352441e53b93e8667b802e04f6e072b39e`, MIT. What is already
established about its companion protocol is in the
[reuse ledger](REUSE_LEDGER.md) and must not be re-derived.

| Question | State | Answer from |
|---|---|---|
| Which transports does the `companion_radio` role expose at the pinned revision — BLE, USB serial, Wi-Fi/TCP? | `UNKNOWN` | `src/helpers/BaseSerialInterface.h`, `MultiSerialInterface.h`, the variant build flags |
| Is a LAN/TCP companion transport present, or only in a fork or a later tag? | `UNKNOWN` | the same, plus the tag history around `companion-v1.17.1` |
| Which commands does a stock build answer — send, receive, contacts, telemetry, self-status? | `UNKNOWN` | the command table in the companion sources; cross-check against the first-party JS and Python clients |
| Does a telemetry response carry a position, and under what conditions? | `UNKNOWN` | the telemetry frame definition |
| Does the node report its **own** GNSS fix distinctly from a fix relayed in a message? | `UNKNOWN` | the same |
| Is pairing required before any of it, and what does the pairing state cost across a reconnect? | `UNKNOWN` | the companion pairing path; note the ledger's existing finding that `app_target_ver` is never reset |

**Do not begin a client before this table has answers.** The ledger already
records what happens when a packed descriptor is read as a plain integer.

## 2. Meshtastic as a companion — T-073, licence first

The firmware is **GPL-3.0** — read it, learn from it, copy nothing.

| Question | State | Answer from |
|---|---|---|
| Are the protocol definitions (`protobufs`) licensed separately from the firmware? | `UNKNOWN` — **this is the gate** | the `protobufs` repository's own `LICENSE`, not the firmware's |
| If they are not usable, can a client be written from published documentation alone? | `UNKNOWN` | the public protocol documentation |
| Which transports does a stock node expose to a client — BLE, Wi-Fi/TCP, serial? | `UNKNOWN` | published client documentation |
| What does its position payload actually contain, and how is a relayed position distinguished from an own fix? | `UNKNOWN` | the same |

If the first row comes back "same licence as the firmware", the honest outcome is
a **blocker with options**, not a client written carefully. The ledger's rule is
not a preference.

## 3. Several providers at once — T-074

No external source answers these; they are design questions and belong in an ADR
amendment rather than a document.

- What does `availability(MeshMessaging)` mean when two providers are up and one
  is degraded? ADR-0008 §3's table has two rows and needs more.
- A message arrives over two providers. Deduplication is MeshCore's problem
  *inside* one network and Attadipa's problem *across* two.
- Two networks are not one network. A watch bridging a MeshCore mesh and a
  Meshtastic mesh is a gateway, and a gateway is a product decision with an
  airtime cost — **not** something that should happen as a side effect of both
  being configured.
- Power: two BLE links plus a local radio is a draw nobody has budgeted.

## 4. Position sources — T-075, T-076

The inventory, and what each may honestly claim. The column that matters is the
last one.

| Source | Typical accuracy | Provenance |
|---|---|---|
| the watch's own receiver | to be measured | the wearer |
| a companion node's receiver | to be measured | **the node**, which may be elsewhere |
| a phone over the companion link | to be measured | the phone, which is usually but not always co-located |
| a coordinate inside an incoming message | whatever the sender's was | **somebody else** |
| a coordinate inside a telemetry frame | the same | the reporting device |
| dead reckoning from the IMU | drifts; T-071 | derived, never observed |
| cell towers | hundreds of metres to kilometres | the network |

Open, and none of it guessable: what a phone will actually hand over and over
which protocol (T-076); whether any of this survives the phone's own permission
model; and whether "the watch is the primary instrument" survives a phone that
only offers a smoothed, already-fused position rather than raw measurements.

## 5. AGPS as a payload — T-077

| Question | State | Answer from |
|---|---|---|
| Which assistance formats does each candidate receiver accept? | `UNKNOWN` | the receiver documents — T-051 (MIA-M10Q), T-052 (Quectel LS550G) |
| How large is a useful assistance set, and what is its validity window? | `UNKNOWN` | the same |
| Does anything useful fit a LoRa channel's budget under the duty cycle? | `UNKNOWN` | the above, against the airtime accounting in T-027 |
| Where does the data come from, and under what licence and terms? | `UNKNOWN` | the provider's terms — a service that forbids redistribution is not a channel-agnostic payload |

The owner's framing — *"буду стараться как-то их получить и пропихнуть в любом
случае"* — is why the **payload** is defined once and the **delivery** is
answered per channel.

## 6. Cellular in the node — T-078, T-079

Blocked on a part that does not exist yet; [NODE_PROFILE](../node/NODE_PROFILE.md)
has no modem because it has no part numbers. Recorded so the question is not
reopened from scratch:

- module class, bands, and current while registered — a property of a specific
  part, `UNKNOWN`;
- whether the tower database may lawfully be shipped: licence, size, regional
  coverage, update cadence. Four answers, and "an open one exists" is none of
  them;
- type approval and SIM ownership — the owner's, not this repository's;
- privacy: a registered device is locatable by the network. Child Mode makes that
  a legal question in some jurisdictions and the tracker threat model (T-069)
  gains a section.

## 7. Motion-gated GNSS — T-080

The feature is a claim about a specific receiver's low-power behaviour, so it is
blocked behind the same two receiver tasks.

| Question | State | Answer from |
|---|---|---|
| Which low-power modes does each receiver have, what does each retain, and what does each draw? | `UNKNOWN` | T-051, T-052 |
| How long is a hot, warm and cold start on each, measured rather than quoted? | `UNKNOWN` | hardware, once it exists |
| Can the BMA423 raise a motion interrupt while the SoC sleeps, and at what current? | `UNKNOWN` | the BMA423 datasheet — T-060 already asks |
| What is the longest interval a held fix may be trusted for before the receiver is asked anyway? | a **setting**, not a constant | design, with a default that needs measurement |

The trap, restated because it is the whole difficulty: switching the receiver off
is what makes the next fix a cold start. Saving current between fixes and paying
for it at the next one is not a saving until somebody measures both halves.
