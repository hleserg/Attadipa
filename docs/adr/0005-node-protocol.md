# 0005 — The watch↔node protocol

Status: **provisional — scope corrected, encoding not yet accepted** (2026-08-21)
Date: 2026-08-21
Required by: master plan §32 · builds on [ADR-0004](0004-capability-sources.md) · constrained by [ADR-0006](0006-settings-and-bounded-values.md)

> **Correction notice.** The final master specification reviews this ADR
> directly, in four places. Nothing below is deleted; four things are scoped or
> downgraded, and the ADR may not be treated as settled until they are closed.
>
> **1 — §2 is about the node path only** (final §13). *"The watch links no
> MeshCore code at all"* is a good decision for the watch-as-companion-client,
> and false as a statement about the product: a T-Watch with a supported radio
> is a local mesh device. [ADR-0008](0008-mesh-service-providers.md) carries the
> corrected model — one `MeshService`, a local provider and a node provider —
> and explicitly leaves the local integration *mechanism* to a measured spike,
> because deciding it from taste is what produced the sentence being corrected.
>
> **2 — the encoding is provisional** (final §18). The TLV *goals* are endorsed
> by name: versioning, request IDs, a bounded parser, session reset, capability
> re-announcement, fragmentation, a hostile-frame corpus, freshness and validity,
> and separation from MeshCore internals. The *choice* is not, and the reason is
> a fair hit: this ADR compared a hypothetical small Attadipa TLV against
> Meshtastic's entire `meshtastic_FromRadio` union and called the question
> settled. That is not a comparison. Before acceptance, benchmark an Attadipa TLV
> prototype against nanopb with an Attadipa-specific streaming/callback schema and
> at least one other compact option, on the target compiler, measuring peak
> internal RAM, static RAM, flash, encoded bytes, malformed-input behaviour,
> schema-evolution cost, tooling, fragmentation interaction and test burden. If
> TLV still wins, accept it with the evidence. **T-016.**
>
> **3 — the node's software architecture must be settled before the protocol is
> frozen** (final §17). Whether the node runs stock MeshCore, Attadipa firmware,
> or both decides which component terminates this protocol, how it coexists with
> companion traffic, who owns pairing and identity, and what happens when the two
> firmwares are upgraded independently. Recorded in
> [NODE_PROFILE](../node/NODE_PROFILE.md). **T-019.**
>
> **4 — the demultiplexing rule must be explicit** (final §19). Two protocols on
> one physical relationship is a diagram, not a design, until it says how a
> parser tells them apart — separate GATT characteristics, separate UART
> channels, or an outer mux frame. *"A parser must never mistake log text,
> MeshCore frames and Attadipa frames for one another"*, and a node's serial port
> emits all three. **T-016.**

## Context

§32 mandates this ADR by name, and is unusually specific about it:

> «Не смешивать application protocol Doctor с внутренностями MeshCore.»
> «Нужен versioned high-level protocol поверх транспорта.»
> «Не выбирать JSON/protobuf/CBOR только потому, что они перечислены здесь.»
> «Сделать ADR с анализом: packet size · ESP32 memory · versioning ·
> backward compatibility · debuggability.»

Two prohibitions, one mandate, five axes. §74 item 23 — which §73 forbids
changing without explicit grounds — adds that the node must be integrable
without rewriting the system.

Reading MeshCore at `d92964352441e53b93e8667b802e04f6e072b39e` changed the shape
of the problem ([OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md), M1–M14):

- MeshCore is **Arduino/PlatformIO throughout** — no `CMakeLists.txt`, no
  `idf_component.yml`, `<Arduino.h>` in its interface headers. The watch is
  ESP-IDF. Porting it means maintaining a divergent tree forever, which the
  development addendum forbids outright.
- It **already ships a companion protocol** as a first-class configuration,
  byte-identical across UART, BLE, USB, Wi-Fi and Ethernet.
- Its RadioLib wrapper keeps radio state in a **file-static** variable set from
  an ISR, so one firmware image drives one radio (M9).

The last point used to be the scariest open question in the project. The node
dissolves it **for the node path**: when the radio lives in a separate device,
nothing in the watch contends for it.

> **Corrected.** The sentence that stood here concluded that the watch therefore
> *never* runs MeshCore. That does not follow — it dissolves M9 on one path and
> says nothing about the other. On a T-Watch with a supported radio, M9 is a
> live constraint on a local mesh stack. See
> [ADR-0008](0008-mesh-service-providers.md).

## Decision

### 1. Two protocols, layered, never merged

This is the load-bearing decision and the one §32 constrains most tightly.

```
  ┌─────────────────────────────────────────────┐
  │  Attadipa Node Protocol   (this ADR)         │  weather, object coordinates,
  │  versioned, TLV, Attadipa-owned              │  Home Assistant, quests,
  ├─────────────────────────────────────────────┤  telemetry, capabilities
  │  MeshCore companion protocol                │  contacts, messages,
  │  MeshCore-owned, spoken verbatim            │  channels, mesh state
  ├─────────────────────────────────────────────┤
  │  Transport — BLE presumed, UART for bring-up│
  └─────────────────────────────────────────────┘
```

**Is speaking MeshCore's companion protocol "mixing with MeshCore internals"?
No — and the distinction is worth stating precisely, because it is the one a
reviewer will ask about.** MeshCore's companion protocol is its *published
external boundary*: the interface it offers to clients that are not MeshCore, on
which four first-party clients already depend. Speaking a published boundary is
the opposite of reaching into internals. What §32 forbids is concrete and this
design does none of it:

- Attadipa's own high-level data must not be carried as extensions to MeshCore's
  opcode space.
- Attadipa's protocol version must not be derived from, or coupled to,
  MeshCore's `FIRMWARE_VER_CODE`.
- MeshCore's frame limits, packet structures and payload cipher must not shape
  the Attadipa protocol's design.
- No Attadipa type may be defined in terms of a MeshCore type.

The practical test: **it must be possible to replace the mesh stack under the
node without touching the Attadipa protocol.** If that is true, they are not
mixed.

### 2. On the node path, the watch links no MeshCore code at all

> **Scope corrected.** As written this heading claimed the whole product. It is
> true of the node path and only of the node path — see the correction notice
> above and [ADR-0008](0008-mesh-service-providers.md). A T-Watch with a
> supported radio may well link mesh code; how, and how much, is an open spike.

The node's firmware consumes MeshCore unmodified as a PlatformIO library in its
`companion_radio` role. The watch — ESP-IDF, `core/` — implements a
Attadipa-authored client of the companion wire format, roughly 700 lines of
framing and marshalling.

This is not a reimplementation of MeshCore. It is a client of a published
protocol, which is what MeshCore's own JavaScript and Python clients are. Porting
MeshCore's Arduino-flavoured client into `core/` would import `Arduino.h` into
the layer that must stay board-agnostic.

The wire, read from source rather than from documentation
(`src/helpers/ArduinoSerialInterface.cpp:24-73`, `BaseSerialInterface.h:5`):

```
node -> host:  '>' , len_lo , len_hi , payload[len]
host -> node:  '<' , len_lo , len_hi , payload[len]
MAX_FRAME_SIZE = 176        payload = [opcode:u8][data], little-endian
```

Bring-up starts on UART, because every transport carries identical frames and
three shipped variants already wire the companion link to a serial port. Moving
to BLE later changes no protocol byte.

### 3. Encoding: an Attadipa-owned binary TLV. The reason is RAM, and it was measured

§32 forbids choosing an encoding because it appears in a list, so here is the
evidence rather than a preference.

**Provenance, first.** Everything numeric below was compiled by this project on
**`xtensa-esp32s3-elf-gcc` 14.2.0 at `-Os -ffreestanding`** — the actual target
compiler for the actual target ISA, not a proxy. Sources: Meshtastic at
`68bfe015e6ab9ec2ab8f1657066898b7880eaf63`, nanopb `0.4.x` from upstream. Where
a number is not measured it says so.

**ESP32 memory — the decisive axis.** Meshtastic's top-level protobuf messages,
sizes taken from the compiled object:

```
meshtastic_FromRadio :  wire maximum 510 bytes  ->  C struct 768 bytes
meshtastic_ToRadio   :  wire maximum 504 bytes  ->  C struct 508 bytes
```

768 bytes of RAM to carry a 510-byte message, and roughly 1.3 kB of scratch per
`PhoneAPI` instance before any transport buffer. **That is a nanopb
fixed-size-struct cost, not a protobuf wire-format cost** — every arm of every
`oneof` is allocated at its maximum. The distinction decides the remedy: it means
the answer is not "use CBOR instead", it is *do not materialise a union*. A TLV
reader that walks the frame in place materialises nothing.

**Packet size — roughly a wash, and not the discriminator.** Protobuf varints
beat fixed-width TLV on small integers; TLV wins where it can size a field
exactly. Both reference systems are MTU-bound rather than encoding-bound —
MeshCore caps at 176 bytes, Meshtastic at 512 with a compile-time `#error` if the
schema outgrows it. Attadipa's real packet-size problem is **fragmentation**,
which neither upstream provides and which no encoding choice solves.

**Flash — real, measured, and not decisive.** On the target compiler:

| Component | `.text` |
|---|---|
| nanopb runtime — `pb_encode.c` + `pb_decode.c` + `pb_common.c` | 7 029 B |
| Meshtastic's generated descriptor tables, all 24 units | 13 148 B |
| — of which `mesh.pb` alone | 2 880 B |
| **total** | **≈ 20.2 kB** |

The Attadipa TLV codec is **not written, so its size is UNKNOWN** and no number
is quoted for it here. It would have to be implausibly large to change the
conclusion, but that is an argument, not a measurement, and this repository does
not let one stand in for the other.

Twenty kilobytes of flash is in any case nothing on a 16 or 32 MB part. It is the
**512 KB of internal SRAM** that rules — the same constraint that dominates
[RESOURCE_BUDGET](../architecture/RESOURCE_BUDGET.md) — which is why the struct
figure above is the one that decided this and the flash figure is context.

**Backward compatibility — the one axis where protobuf genuinely wins, and it
wins on enforcement rather than on format.** `buf breaking` in CI, plus
`reserved` on every retired tag, is mechanical discipline that a hand-rolled TLV
must reproduce by review. That is a real cost and it is accepted knowingly, with
a mitigation in §7 below. It is also worth recording that the same schema
machinery produced the worst compatibility break found in this survey: nanopb's
`max_size` is part of the wire ABI and nanopb *halts* on string overflow rather
than truncating, so shrinking one field by fifteen bytes made peers undecodable
and had to be reverted.

**Debuggability — §32 names it, and it is where TLV wins outright.** A tagged
binary frame is readable in a hex dump with a one-page table. A protobuf frame
without its schema is an unlabelled bag of varints, and the schema lives in a
code-generation step that must run before anything can be built or inspected.

### 4. The envelope

```
 0      1        2      3      4      5      6      7      8     9     10    11
+------+--------+-------------+-------------+-------------+-------------+
| ver  | class  |   req_id    |     op      |  body_len   |   crc16     |
| u8   | u8     |    u16      |    u16      |    u16      |   ccitt     |
+------+--------+-------------+-------------+-------------+-------------+
body := TLV*        tag:u8  len:u8 (0xFF escapes to u16)  value[len]
```

Unknown tags are skipped. Unknown opcodes are answered with a typed error, never
ignored. `req_id` exists because MeshCore's companion protocol has no request
correlation at all, which makes concurrent requests unanswerable.

### 5. Version and capability set are orthogonal, and this is where both upstreams are wrong

Both reference implementations collapse *protocol version* and *capability set*
into one monotonic integer. Attadipa cannot, because
[ADR-0004](0004-capability-sources.md) says capabilities appear and disappear at
runtime, and §32's list of what a node may provide is explicitly open-ended.

- MeshCore's `app_target_ver` is one byte, negotiated once at
  `CMD_DEVICE_QUERY`, and **never reset**. A client that reconnects inherits the
  previous session's assumption.
- Meshtastic's `excluded_modules` is a compile-time bitfield inside a
  `DeviceMetadata` sent once and never re-sent.

So:

- **`proto_major` is negotiated once per link**, in a `HELLO` exchange, and the
  reply carries a **session epoch**. The epoch is reset unconditionally on link
  loss — no state survives a reconnect implicitly.
- **The capability list is re-announceable at any time**, and each capability
  carries **its own version byte**. A node that gains a magnetometer, or loses
  GNSS to a power decision, says so mid-session. One monotonic integer cannot
  express that, which is why it is not asked to.
- Version skew has a **direction**, because "update your node" and "update your
  watch" have opposite remedies. That is `Availability::Incompatible` plus
  `VersionSkew` in ADR-0004, surfaced here.

The debug class carries this epoch since #348, and it is the first place the
watch's own protocol has needed it: on USB Serial/JTAG a host process that
exits with the cable in is not a link loss — the vendor driver reports
`connected` on the host's SOF packets alone, tty open or not
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md), "USB Serial/JTAG counts as
connected while the host sends SOF") — so nothing reset the session, and
the next process — restarting its request ids at 1 — read its predecessor's
late replies as its own. `HelloBody` carries a 32-bit session the host draws
per connection
(`debug/include/attadipa/debug/protocol.h:242` — "std::uint32_t session"); the
device ends the previous session before it echoes the value in `HelloOk`, and
the host discards everything it read ahead of that echo. A correlation, not a
credential — `docs/testing/WATCH_CONTROL.md`, "The trust boundary".

### 6. Push a small event, pull the detail

The node pushes a fixed, tiny notification; the watch pulls the body by
identifier if it wants it. Borrowed from Apple's ANCS, which solved this on a
link with the same constraints.

Two things fall out for free. The node never volunteers a payload the watch did
not ask for, which matters on a power budget and matters more on a shared node.
And forward compatibility costs nothing: a watch that does not understand an
event class simply never pulls it.

### 7. Every value-bearing frame carries reserved tags for age, source and validity

Not optional, not per-message. ADR-0004 §3 requires two ages on any datum that
crosses a link, and [OD-2](../research/OWNER_DECISIONS.md) records that the
reference model carries **no timestamp on anything** — a four-hour-old
coordinate and a two-second-old one are the same two numbers there.

Reserved tags, defined before the first opcode:

| Tag | Meaning |
|---|---|
| `age_at_source` | how old the value was when the provider sampled it |
| `sampled_at` | the provider's own timestamp, for cross-checking |
| `validity` | three-valued: `Valid` · `Stale` · `Unknown` — never two-valued |
| `source` | which provider, for diagnostics only; never reaches an application |

`age_at_us` is computed by the receiver, not sent.

### 8. Fragmentation is mandatory, not an extension

BLE is the presumed transport ([NODE_PROFILE](../node/NODE_PROFILE.md) N2) and
§32's payload list — weather, Home Assistant events, quest events — will exceed
any ATT MTU. Neither reference implementation provides fragmentation; both cap
the message instead and fail at the cap. Designing it in afterwards means a
second envelope.

### 9. Before the first opcode is defined

The codec ships with a property test and a corpus of hostile frames — truncated
headers, lying lengths, tags that run past the end, nested escapes, a CRC that
matches a shorter body. A parser for untrusted input from a device we do not
control is exactly the code that must be attacked before it is used, and
[ADR-0002](0002-companion-is-optional.md) rule 4 already says node input is
untrusted.

## Alternatives considered

**Protocol Buffers with nanopb.** Rejected on the measured RAM figure — 768
bytes of struct for a 510-byte message, ~1.3 kB of scratch per instance, on a
part with 512 KB of internal SRAM that LVGL already wants 804 KB of on the
larger panel. A second, sharper reason emerged from upstream's own history:
**nanopb's `max_size` is wire ABI, and nanopb halts on overflow rather than
truncating.** Shrinking one field by fifteen bytes made peers built against the
old schema undecodable, and the change was reverted within the hour. The lesson
generalises past protobuf and is adopted below in §7: a field-width limit must be
an *acceptance* property, never a *decode* property. Accept generously, clamp on
store and on transmit, and never let a compiled-in width be the thing that
rejects a peer. Its genuine advantage, mechanically enforced schema evolution, is
real and is what §7's discipline has to substitute for by review. It also
introduces a code-generation step between a developer and a build, which §32
names as a debuggability concern.

**CBOR.** Rejected. It solves the self-description problem that TLV also solves,
at a larger wire size and with a general-purpose parser to review. Choosing it
here would be choosing it because it was on a list, which §32 forbids by name.

**JSON.** Rejected. Readable, and that is its whole case. Multiples of the wire
size on a fragmented BLE link, a parser with a much larger attack surface, and
float round-tripping that ADR-0006 §2 has already measured to be lossy at the
frequencies this system uses.

**Extend MeshCore's companion protocol with Attadipa opcodes.** Rejected: it is
what §32 prohibits, and it would tie Attadipa's protocol lifecycle to another
project's release cadence. It also fails the practical test in §1 — the mesh
stack could never be replaced under the node.

**Port MeshCore to ESP-IDF and run mesh on the watch.** Rejected. Against the
grain of an Arduino-first codebase, an indefinite divergent fork, and — since the
radio is in a separate device — solving a problem the product does not have.

**Run no protocol of our own; expose the node as a transparent mesh bridge.**
Rejected: §32's list is mostly *not* mesh traffic. Weather, Home Assistant events
and quest events have no representation in a mesh packet, and forcing them into
one is the mixing the prohibition is about.

## Consequences

**Easier.** The watch stays pure ESP-IDF with no Arduino dependency. The node
gets a mature, field-tested mesh stack unmodified. Mesh and Attadipa concerns
evolve independently, and either can be replaced. Bring-up starts over a wire, on
a bench, with a hex dump that a person can read.

**Harder.** Two protocols to understand, and a client to write and keep current
against an upstream that moves — `origin/dev` was 29 commits ahead of the pinned
tag on the day it was read. Schema discipline is by review rather than by CI, and
that is a standing cost that will be paid or quietly not paid. Fragmentation and
request correlation are ours to get right, and neither upstream can be copied
from.

**Committed to.** A node protocol replaceable-under without touching Attadipa
types. Version negotiated per link, capabilities re-announceable, session epoch
reset on every reconnect. Age, source and three-valued validity on every value.
A hostile-frame corpus before the first opcode.

**Open.** The transport is not decided (N2), and nothing above depends on which
it is. Whether the node runs an Attadipa-built image or MeshCore's stock companion
firmware is a product question (N8) that does not change the watch-side work
either way. `rweather/Crypto`'s licence is unverified (M14) and matters only if
Attadipa ever builds the node image itself. The security review that M10 and M11
call for — AES-128-ECB, and a two-byte authentication tag — is a separate ADR
and needs someone competent in it.
