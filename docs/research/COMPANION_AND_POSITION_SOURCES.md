# Companions and position sources — what must be verified

Status: **open research, two sections closed.** §1 (MeshCore) and §2
(Meshtastic) now have answers; §§3–7 are still questions. Every row is a question
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
| telemetry from a companion | feeds are not capabilities. A telemetry value is a datum with two ages ([ADR-0004](../adr/0004-capability-sources.md)) |
| positions from several places at once | [ADR-0011](../adr/0011-gnss-integrity.md) — `PositionValidity` and `TrustState` are already independent axes. A coordinate out of somebody else's message is what that independence is for |
| choose a source, or fuse several | ADR-0008 §3's selection policy, extended. **Fusion is a different feature** and is not that policy applied twice |
| GNSS asked less often when still | `start_kind()`, the backup domain, and T-045's power states — all present, none of them yet with a customer |

What does **not** yet have a shape is a *position estimator* — anything that
combines two sources into a third number. That is an ADR nobody has written, and
it should not be written before the replay rig can be pointed at multi-source
traces.

## 1. Vanilla MeshCore as a companion — T-072 — **ANSWERED**

Pinned revision `d92964352441e53b93e8667b802e04f6e072b39e`, MIT. **The detail is
in [MESHCORE_COMPANION_PROTOCOL](MESHCORE_COMPANION_PROTOCOL.md)** — framing,
the whole command set, the three position scalings and the caveats. This table is
the summary; do not write a client from it alone.

| Question | State | Answer |
|---|---|---|
| Which transports does the `companion_radio` role expose at the pinned revision? | **ANSWERED** | Five, all compile-time gated, all into one `MultiSerialInterface`: BLE, Wi-Fi/TCP, USB serial, Ethernet/TCP, and a second hardware UART. No shipped build enables more than one, and the env name does not tell you which |
| Is a LAN/TCP companion transport present? | **ANSWERED — yes, two of them** | Wi-Fi (`WIFI_SSID`, ESP32 only, 25 envs) and Ethernet (`ETHERNET_ENABLED`, 2 envs). Both raw TCP servers, both port **5000** by default. **One client at a time**: a new accept silently stops the previous one |
| Which commands does a stock build answer? | **ANSWERED** | 58, in a flat `if/else if` chain. Numbering is **not contiguous** — 44–49 parked, 53 absent. Only `EXPORT`/`IMPORT_PRIVATE_KEY` are build-gated, and a stock build answers both |
| Does a telemetry response carry a position, and under what conditions? | **ANSWERED — conditionally** | One CayenneLPP `LPP_GPS` record on channel 1, 9 bytes, lat/lon ×10⁴ big-endian. Requires `TELEM_PERM_LOCATION`, which is three owner prefs ANDed with per-contact flags, and **the requester can narrow it further** with an inverse mask |
| Does the node report its **own** GNSS fix distinctly from a fix relayed in a message? | **ANSWERED — and the answer is worse than "no"** | There is no relaying; every position a node emits is its own. But `node_lat` is one slot fed from prefs, from the client and from the GNSS loop, and the GNSS write is **inside** an `isValid()` branch, so a lost fix leaves the last value in place. No fix flag, no satellite count, no timestamp, no HDOP is ever transmitted. **A receiver cannot distinguish a live fix from a six-hour-old one from a hand-typed coordinate** |
| Is pairing required before any of it, and what does the pairing state cost across a reconnect? | **ANSWERED** | Pairing is enforced *below* the protocol (MITM bonding, static PIN); the protocol itself has no ordering rules except the signing sequence and a non-re-entrant contacts iterator. Across a reconnect the device keeps `app_target_ver`, the offline queue, the flood-scope override and any 8 KB signing buffer — **all of it until reboot, not until disconnect.** The ledger's `app_target_ver` finding is confirmed, with the precision that a client must re-send `CMD_DEVICE_QUERY` on **every** connection or inherit the previous session's frame format |

Two things this changes for us, both recorded in the protocol document's §6 and
neither of them a design decision yet:

- **176 bytes is the frame budget** and it is a bare `#define` with no `#ifndef`
  guard, so it cannot be raised by a build flag. Every queue and buffer size on
  our side is bounded by it.
- **A companion position arrives with no provenance and no age.** Attadipa must
  supply both from outside — the arrival time is the only age we will ever have.
  That lands on [ADR-0011](../adr/0011-gnss-integrity.md),
  [OD-8](OWNER_DECISIONS.md#od-8--every-source-of-position-and-the-watch-as-the-instrument)
  and [OD-10](OWNER_DECISIONS.md#od-10--a-standing-person-does-not-need-a-new-fix)
  at once, and it means motion-gated GNSS cannot lean on a companion's fix to
  decide whether the wearer moved.

**Read from source, never observed.** `NOT EXECUTED — HARDWARE REQUIRED`. A
vanilla node is reachable behind Home Assistant on `doctor` and a USB node is
coming; confirming this against one of them is **T-072a** and would be the first
honest `OBSERVED` in this area.

## 2. Meshtastic as a companion — T-073 — **ANSWERED: not supported**

**The owner chose option 4 on 2026-08-22**, on
[#41](https://github.com/hleserg/Attadipa/issues/41):
[OD-12](OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence).
T-073 is `REJECT` in the reuse ledger and closed, not blocked and not deferred.

The `BLOCKED:` box below is kept **as the record of what was put to the owner**,
because a decision without the alternatives it was chosen over is not a decision
anybody can revisit. Its recommended next action was an owner decision, and that has happened.
Read it as history; do not act on it.

And read OD-12 for what the answer actually turns on: the licence closed the
cheap path, but the decision is that the feature is not worth an honest
clean-room. Recording this as "blocked on licensing" would leave the next agent
thinking a licence change reopens it. It does not.

The gate question has an answer and the answer closed the cheap door.

| Question | State | Answer |
|---|---|---|
| Are the protocol definitions (`protobufs`) licensed separately from the firmware? | **ANSWERED — no** | `meshtastic/protobufs` **is** a separate repository with its **own** `LICENSE` file — and that file is **GPL-3.0**, the same licence as the firmware. `packages/ts/package.json` declares `"license": "GPLV3"`; `packages/rust/Cargo.toml` points `license-file` at the same `LICENSE`. No exception clause, no SPDX header in any `.proto`, no dual licensing |
| If they are not usable, can a client be written from published documentation alone? | `UNKNOWN` | not investigated — it is the substance of the blocker's second option below |
| Which transports does a stock node expose to a client? | `UNKNOWN` | not reached; blocked behind the licence |
| What does its position payload contain? | `UNKNOWN` | not reached; blocked behind the licence |

Verified at `meshtastic/protobufs` submodule commit `aca181b`, `remote.origin.url
= https://github.com/meshtastic/protobufs.git`, under firmware `68bfe015e`.

```
BLOCKED:
Reason:   Meshtastic's protocol definitions are GPL-3.0, in their own repository
          with their own LICENSE file, and that file is the same licence as the
          firmware. The reuse ledger's rule — read it, learn from it, copy
          nothing — therefore applies to the .proto files as much as to the C++.
          Generating code from those .proto files and linking it into Attadipa
          would make Attadipa's firmware a derivative work under GPL-3.0.
Evidence: /root/upstream/meshtastic/protobufs/LICENSE — "GNU GENERAL PUBLIC
          LICENSE Version 3, 29 June 2007", full text, no exception paragraph.
          packages/ts/package.json:10 "license": "GPLV3".
          packages/rust/Cargo.toml:7 license-file = "LICENSE".
          No .proto file carries an SPDX identifier of its own.
Impact:   T-073 cannot produce a Meshtastic provider as scoped. T-074 (several
          providers at once) loses its second concrete provider and must be
          written against MeshCore plus a hypothetical, or wait.
          OD-7 asked for Meshtastic "вместо (или вместе)" MeshCore, so this is a
          product-level shortfall and not only a technical one.
Possible options:
  1. Clean-room from published documentation only. Meshtastic's protocol is
     publicly documented; a client written from documentation, by someone who
     has not read the .proto files, is the standard remedy. It is slow, it is
     easy to do wrong, and "I read the docs" is not a defence if the .proto is
     also open in another window.
  2. Ship a Meshtastic provider as a separately distributed GPL-3.0 component
     rather than linking it into the firmware. Whether that is coherent for a
     monolithic ESP-IDF image is itself a question, and it is a licensing
     opinion this repository is not qualified to give.
  3. Ask upstream for a licence exception for the protocol definitions. Others
     have asked before; the outcome is not this repository's to predict.
  4. Do not support Meshtastic. MeshCore is MIT and answers OD-7's actual need —
     "not everyone will want to build our node" — on its own.
Recommended next action: OWNER DECISION — taken 2026-08-22, option 4. This is a licensing and product call,
  not a technical one. Options 1 and 4 are the only two an agent can execute
  without legal advice, and they differ in months of work. Recommend option 4
  for now and option 1 only if the owner wants Meshtastic badly enough to fund
  a clean-room, because a half-clean-room is worse than neither.
```

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
