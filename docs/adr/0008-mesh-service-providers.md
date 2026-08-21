# 0008 — One `MeshService`, two providers, and the local path is real

Status: **accepted** for the service shape · the local integration *mechanism* is deliberately undecided
Date: 2026-08-21

## Context

Final §75 item **D**, and final §13. The repository contradicted itself, and the
contradiction was sitting in the ADR index where it was most likely to be read
as settled:

> *"the watch does not run mesh, because the radio is in the node"*
> — `docs/adr/README.md`

against `ARCHITECTURE.md`, which mapped the T-Watch's on-board radio to
`RadioService → MeshService`, and against the product description, in which the
T-Watch is the full device.

Final §13 is unambiguous about which side is right:

> That last statement is not universally true. The intended product includes
> watches capable of running the same mesh applications against **local mesh
> hardware** where the fitted hardware and upstream support permit it.

How the wrong statement got written is worth recording, because the reasoning
was sound and the conclusion was still too broad. Reading MeshCore established
that its RadioLib wrapper holds radio state in a file-static variable set from
an ISR, so one firmware image drives one radio (M9). That was the scariest open
question in the project. [OD-1](../research/OWNER_DECISIONS.md) then introduced
the node, and the node *dissolves* M9 for the node path — the radio is in
another device, so nothing in the watch contends for it.

The error was concluding that because M9 stops mattering **on one path**, the
other path does not exist. It does. A T-Watch with an SX1262 is a complete mesh
device with no node in sight, and refusing to model that would delete the
product's flagship configuration to tidy up an architecture diagram.

## Decision

### 1. One service, two providers, one application-facing contract

```
                    ┌───────────────────────┐
     applications ──┤     MeshService       ├── availability(MeshMessaging)
                    └───────────┬───────────┘
                    ┌───────────┴───────────┐
        LocalMeshProvider            NodeMeshProvider
     on-board Radio, when the      Attadipa node over the
     matrix says it can            companion link
```

Applications use `MeshService`. They do not select a provider, do not learn
which one served a message, and do not have two code paths. This is
[ADR-0002](0002-companion-is-optional.md) rule 2 and
[ADR-0007](0007-two-capability-layers.md) §3.4 applied to the one subsystem
where the temptation to break them is strongest, because the two providers have
genuinely different latency, different failure modes and different power cost.

Diagnostics and Settings see providers by name, because inspecting and
configuring them is their purpose (final §9).

### 2. `LocalMeshProvider` exists only when the hardware can actually do it

It is instantiated when, and only when,
[ADR-0003](0003-radio-not-lora.md) §2 says the fitted radio can join a
MeshCore-compatible network: a `Radio` is present, its modulations include
`Lora`, its band overlaps the configured network, and the pinned MeshCore
supports the chip.

At the pinned revision that is **one of the five candidate T-Watch chips**. A
CC1101 or Si4432 watch has no local provider and never will; an SX1280 watch
would form a 2.4 GHz network that no sub-GHz Attadipa can hear. Final §13 says
this out loud: *"do not promise local MeshCore on every T-Watch radio variant …
If a hardware variant cannot support MeshCore, report that honestly."*

### 3. Provider selection is a policy, and it is boring on purpose

| Situation | Behaviour |
|---|---|
| local only | local |
| node only | node |
| both available | **local**, by default |
| local fails or its radio is `Off` for power | fall back to node, and say so in diagnostics |
| neither | `Unprovisioned` if either could be provisioned; `Unsupported` if neither ever can |

Local wins by default because it has no link in the path, no second battery to
go flat, and no attach state to lose. This is a **setting**, not a constant —
a user with a node on a roof antenna and a watch in a pocket may reasonably
prefer the node, and the preference is stored per the scope rules in
[ADR-0006](0006-settings-and-bounded-values.md).

**Both providers are never active at once for the same network.** Two radios
transmitting the same identity into the same mesh is a duplicate-suppression
problem for every other node in range, not a redundancy feature. Failover is a
transition with a defined settle time, not a load balance.

### 4. What ADR-0005 actually decided, and its corrected scope

[ADR-0005](0005-node-protocol.md) §2 says *"the watch links no MeshCore code at
all."* That statement is **true of the node path and only of the node path**,
and it is a good decision there: on that path the watch is a *client of a
published companion protocol*, exactly as MeshCore's own JavaScript and Python
clients are, and porting MeshCore's Arduino-flavoured client into `core/` would
drag `Arduino.h` into the layer that must stay board-agnostic.

Read as a statement about the whole product it is false, and it has been scoped
rather than deleted. ADR-0005 carries a correction notice.

### 5. How the local path integrates is **not decided here**

This is deliberate, and it is the part of this ADR most likely to be mistaken
for an omission.

Final §14 lists the options — direct component integration, an isolated
compatibility layer, upstreamable ESP-IDF work, a narrow Arduino compatibility
island, something else, or supporting only the combinations that are actually
viable — and then forbids choosing between them from taste:

> Do not decide "port all MeshCore to ESP-IDF" or "never run MeshCore on watch"
> before a **measured spike** establishes the costs.

This project has already made that mistake once, in the sentence this ADR
exists to correct. So the mechanism is an open task (**T-013**) with a spike
that must produce numbers: flash and RAM cost, what an Arduino compatibility
shim actually pulls in, whether the file-static radio state can be tolerated
under a coordinator, and how much of MeshCore's routing behaviour would have to
be re-derived under each option.

What *is* decided, and constrains every option: **`Arduino.h` does not enter
`core/`.** Whatever the local provider is built from lives below the platform
boundary, where board-specific and vendor-specific code is already allowed.

### 6. M9 is a live constraint on the local path

MeshCore expects uninterrupted ownership of its radio, and its wrapper keeps
state in a file-static set from an ISR. On the node path that is somebody else's
problem. On the local path it is ours, and it bears directly on whether
`HardwareCoordinator` can schedule around a transmission or must simply stay out
of the way. That question is part of the spike, not an assumption.

## Alternatives considered

**Node-only, as the repository had it.** Rejected — final §13 rejects it, and it
deletes the T-Watch-with-SX1262 configuration, which is the product's flagship.
It is also the more expensive mistake of the two available: a system built for
one provider does not grow a second one cheaply, whereas a system built for two
runs a node-only device without complaint.

**Two services — `LocalMeshService` and `NodeMeshService`.** Rejected. Every
application would have to know which existed, which is precisely the coupling
[ADR-0002](0002-companion-is-optional.md) forbids, and a device that can do both
would have two inboxes. Message history, contacts and unread state are
properties of the *user's* mesh identity, not of the radio that carried a
packet.

**One service, provider chosen at build time.** Rejected: it produces a separate
binary per configuration — the same failure mode ADR-0001 rejected compile-time
capability flags for — and makes the simulator unable to present attach and
detach, which is the state the whole node design turns on.

**Let the local provider be a thin wrapper over the same companion protocol, so
both paths share one client.** Genuinely appealing: one framing implementation,
one test corpus, no second code path. Rejected for now because it means the
watch talks to *itself* over a synthetic companion link, paying serialisation
and a task boundary to reach a radio on its own SPI bus. Worth revisiting if the
spike shows the local stack is expensive — but it must be a measurement, not an
elegance argument.

## Consequences

**Easier.** A Waveshare board and a CC1101 T-Watch and an SX1262 T-Watch run the
same Mesh application with no conditional code in it. Adding a third provider
later — a second node, a phone-bridged path — is a registration, not a redesign.

**Harder.** Two providers to test, and the interesting bugs are in the
transitions: failover mid-conversation, a message queued on one provider while
the other becomes preferred, and an ordering guarantee that must hold across a
switch. Those need simulator support before they need firmware.

**Committed to.** A `MeshService` interface defined before either provider is
written, so that neither provider's shape becomes the interface by accident. A
spike that produces numbers before the local integration mechanism is chosen. A
compatibility matrix that is re-checked against upstream rather than assumed.

**Testable.** In the simulator: local-only, node-only, both, and every
transition between them, with an application open and messages in flight. On
hardware: `NOT EXECUTED — HARDWARE REQUIRED`, and the local path additionally
needs **two** radio devices to test at all (A3).

**Open.** T-013 — the integration spike and its numbers. Whether the coordinator
can schedule around MeshCore's radio ownership (M9). Whether the physical
multiplexing rule in [ADR-0005](0005-node-protocol.md) — two protocols on one
link — survives having a local radio in the same device (final §19).
