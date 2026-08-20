# The Firefly node — what is known, and what is not

The node is a separate device carrying LoRa, GNSS and an ESP32. A watch attaches
to it and runs the same applications a watch with its own LoRa would run
([OWNER_DECISIONS OD-1](../research/OWNER_DECISIONS.md)).

**This document establishes no hardware fact.** There is no board, no schematic,
no part number and no photograph. Everything below that is not marked otherwise
is UNKNOWN, and the repository rule applies here exactly as it applies to the two
watch boards: *never write code that depends on a hardware fact you have not
traced to a datasheet, a schematic for the specific revision, or vendor source.*

The node does not appear in
[`HARDWARE_MATRIX.md`](../research/HARDWARE_MATRIX.md). That file is for parts
traced to a source. Putting the node there because it was described in a message
would be exactly the failure the matrix was built to prevent — and this project
has already made that mistake once, with a haptics row that recorded an argument
from absence as a fact.

---

## What is established

| # | Fact | Source | Status |
|---|---|---|---|
| N-a | A separate node exists in the product plan, carrying LoRa, GNSS and an ESP32 | owner, 2026-08-21 | **product decision** — not a hardware fact |
| N-b | The watch runs the *same applications* against it as against on-board LoRa | owner, 2026-08-21 | product decision |
| N-c | With no node attached, the watch is a watch, an audio device, and whatever the installed applications make it | owner, 2026-08-21 | product decision |
| N-d | The specification anticipated it: §32 requires the architecture to account for a Firefly/Doctor node, and lists "additional GNSS" among what it provides | `docs/master-prompt.md` §32 | **specification** |
| N-e | The owner operates a MeshCore node today, whose exposed data model is recorded in OD-2 | owner screenshots, 2026-08-21 | observed, second-hand |

N-e is the closest thing to evidence about a real node that this project has,
and it is evidence about *somebody's MeshCore installation*, not about the
Firefly node's hardware. It is useful for the shape of the data, not for the
shape of the board.

## What is unknown, and why each matters

| # | Question | Blocks |
|---|---|---|
| N1 | **What is the node, physically?** An existing off-the-shelf MeshCore board, a board this project designs, or a T-Watch running node firmware? These are three different projects | everything below |
| N2 | **What is the watch↔node transport?** BLE, LoRa, Wi-Fi, USB, ESP-NOW? See the argument below — this is close to decided by constraint | the protocol ADR, the power budget, the pairing design |
| N3 | **Does the node carry a magnetometer?** If it does, it answers A5 and Q2 and revives five dormant epics. If not, "compass" means GNSS course-over-ground and only works while moving | the compass application; [MAGNETOMETER_BACKLOG](../hardware/MAGNETOMETER_BACKLOG.md) |
| N4 | **Does the node relay, or provide?** Is the watch a client of the node's mesh identity, or does the watch have its own identity that the node carries? These produce different security models and different message histories | the protocol ADR, the security model |
| N5 | **How many watches per node?** One, or several? A shared node is a contended resource with a scheduling problem and a privacy problem | protocol, arbitration |
| N6 | **Does the node have its own display, or a headless one?** Decides whether the node has an error UI of its own or must report every fault over the link | diagnostics |
| N7 | **Node power source and expected runtime.** A node on mains and a node on a cell are different products. OD-2 shows a battery-powered one | the link duty cycle, the "node battery low" states |
| N8 | **Does the node run MeshCore, Firefly firmware, or both?** §32 forbids mixing the Doctor application protocol with MeshCore internals — but does not say the node cannot run MeshCore underneath | the protocol ADR, T-006 |
| N9 | **Is the node in the same regulatory situation as the watch?** A node with a better antenna and a mains supply may be allowed different power. A4 currently asks one question; it may be two | A4, the settings bounds |
| N10 | **What is the intended range and link budget watch↔node?** "In range" is a state the UI must render; nobody has said what distance it means | UX, the reachability model |

## The transport question, argued from constraint

N2 looks open but is nearly forced.

The board that needs the node most is the **Waveshare**, and it has no LoRa at
all ([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md)). So watch↔node cannot be
LoRa on the board where the node matters most. Maintaining two different
transports for one relationship is a standing cost with no corresponding
benefit.

Both boards have Wi-Fi and BLE (ESP32-S3, VERIFIED). BLE is the low-power one and
is the transport the companion architecture already assumes. On the T-Watch it
has a second advantage: node traffic stays off the LoRa air interface entirely,
so it does not compete with mesh airtime — which matters because open question
M9 asks whether MeshCore assumes exclusive ownership of the radio, and a design
that keeps node traffic off that radio is unaffected by the answer.

That is an argument, not a decision. It belongs in
ADR-0005 (not yet written — [TASKS](../../TASKS.md) T-016) with the alternatives stated. Recorded
here so the reasoning is not rediscovered.

## What the node must not become

Two failure modes are worth naming before any code exists, because both are
cheap to prevent now and expensive later.

**The node must not become required.** OD-1 says the watch without a node is
still a watch and an audio device. If the clock, the settings screen or the
launcher start depending on node state, that stops being true quietly, one
reasonable-looking commit at a time. This is the same failure the phone
companion ADR was written to prevent, on a different axis — see
[ADR-0002](../adr/0002-companion-is-optional.md).

**The node must not become a second architecture.** §32 is explicit: do not mix
the node's application protocol with MeshCore's internals. If node support grows
its own services, its own storage and its own UI conventions alongside the
existing ones, the codebase has two of everything. A node-provided GNSS must
arrive at `LocationService` as a position, not as a `NodeGnssService` that
applications learn to ask instead.

## What has to be true before any node code is written

1. N1 answered — you cannot write a driver for a device that has not been chosen.
2. N2 decided in an ADR, with the alternatives recorded.
3. A protocol version negotiated before any payload, because the watch and the
   node will be updated independently from the first release
   (ADR-0005 (not yet written — [TASKS](../../TASKS.md) T-016)).
4. The capability-source model in place, so a node-provided GNSS is an ordinary
   capability rather than a special case ([ADR-0004](../adr/0004-capability-sources.md)).

None of these need hardware. All of them can be wrong in a way that is expensive
to discover later, which is why they come first.
