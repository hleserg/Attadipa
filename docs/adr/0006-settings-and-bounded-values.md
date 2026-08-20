# 0006 — Settings, and values the law bounds

Status: proposed
Date: 2026-08-21
Relates to: [ADR-0004](0004-capability-sources.md) · amends the `LoraInfo` descriptor in [ADR-0001](0001-capability-model.md)

## Context

The project owner, shown a live MeshCore node's parameters, said: *these are
settings, and they must not be baked into the core*
([OWNER_DECISIONS OD-2](../research/OWNER_DECISIONS.md)).

§52 of the specification already said the same thing — *«Не hardcode
illegal/default RF settings как универсальные. Region/frequency/power/duty-cycle
должны быть конфигурируемыми»* — so this is an **unimplemented requirement, not
a new one**, exactly as §32 was for the node. §34 also lists a Settings
application, and "mesh configuration" among what it configures.

Three things make this harder than a preferences store.

**There is no settings subsystem, and nothing owns configuration.** The
specification's own `core/` tree (§4) lists `events`, `capabilities`,
`hardware`, `mesh`, `location`, `time`, `power`, `storage`, `crypto`,
`notifications`, `audio`, `companion` — and no `settings`. `settings/` appears
only under `apps/`. A settings *application* with no settings *service* means
the store lives inside the app, and the first radio parameter ends up as a
constant beside the radio driver. The architecture's ownership tables have the
same hole: `StorageService` owns the flash, and nothing owns namespaces, schema
versions, migration, validation or factory reset. By this project's own doctrine
— every part gets a seat in the core — settings is as unowned as the parts the
vendor BSPs ignore.

**Two of the values are bounded by law, and the core must not know the law.**
Frequency and TX power are constrained by region. But `#ifdef BOARD_X` is banned
above the platform layer for a reason, and a region baked into `core/` is the
same mistake with a worse failure mode. Meanwhile A4 — which region applies — is
open and is the owner's to answer.

**The device that transmits may not be the device the user is holding.** A node
has its own radio and its own parameters. Changing them from the watch is a
remote write, and the specific hazard has no analogue in a local settings
screen: **changing the frequency destroys the link you are using to change it.**
That is not an edge case. It is the normal outcome of editing the network
contract.

## Decision

### 1. A `SettingsService` in the core, with a declared schema

Not a `string → string` map. Each key declares its type, unit, allowed range or
enumerated set, default, scope, access level, persistence class, and whether it
is regulatory-relevant. Validation is a property of the schema, so it happens
once rather than at every call site.

**Validation happens on read as well as on write.** This is the rule that is
easy to skip and expensive to skip. A persisted value is re-checked against the
*current* constraint set every time it is loaded, and clamped or refused. A
firmware update can narrow a bound — a corrected regulatory profile, a stricter
chip limit — and an unvalidated load carries a now-unlawful value forward
wearing the authority of "the user chose this".

### 2. A frequency is an integer number of hertz, and never a float

This looks like a detail and is not. It was **measured**, on this machine, with
the compilations recorded below — not reasoned about.

```
868.731f interpreted as Hz  ->  868 731 018      (not 868 731 000)
868 731 000 Hz -> float -> Hz  ->  868 731 008   (8 Hz lost)
one ULP of float32 at 868 MHz  ->  64 Hz
```

A `float` cannot represent the owner's own operating frequency exactly, and one
step of its precision at 868 MHz is 64 Hz. The consequences are not academic:

- **Equality stops working.** A configuration read back cannot be compared with
  the one written, so "is this node on our network's frequency?" has no reliable
  answer.
- **Band edges flap.** A regulatory bound checked in float is a bound that a
  value can sit on both sides of depending on how it was computed.
- **NVS has no float accessor at all** — the ESP-IDF API stores integers, blobs
  and strings — so a float has to be smuggled through a blob, losing the type
  checking that is the point of a typed store.

MeshCore stores frequency as `float`. Meshtastic's region table is float
throughout. Neither can compare a frequency for equality at the least
significant bit. Firefly stores **`uint32_t` hertz**, in which 868 731 000 and
62 500 are both exact, and formats for display at the edge.

The same rule applies to every bound the law sets: integers, in the unit the
regulation is written in.

### 3. Scope is not one thing, and a global radio-settings singleton is a bug

| Scope | Examples | Why it is its own scope |
|---|---|---|
| **User profile** | theme, brightness, haptics, Child Mode | several may exist; a phone may edit them |
| **Per-radio-instance** | TX power, rate limiter, duty-cycle policy | the watch's radio and a node's radio are two instantiations of one schema |
| **Per-network contract** | frequency, bandwidth, spreading factor, coding rate, sync word | every participant must agree, or there is no link — not a degraded link, *no link* |

The middle row is the one that bites. If radio settings are a singleton, the day
a node is added its values overwrite the watch's. Two radios, two instances.

The bottom row changes how the UI must work. These are not five independent
sliders; they are **one preset**, with a name and a hash, applied atomically,
exportable and importable — by QR code, by NFC, by whatever. Five sliders that
must all match across every device in a mesh is an interface that guarantees
mismatches, and the failure is silent: everything looks configured and nothing
can hear anything.

### 4. The core holds the shape of the legal constraint, never its content

```
core/ knows:        a frequency setting is bounded by a set of permitted
                    ranges, a maximum radiated power, a maximum duty cycle
                    and a permitted set of modem parameters — all supplied
                    to it at runtime

core/ never knows:  which ranges, which limits, or that "EU" is a thing
```

A `RegulatoryProfile` is **data**, produced outside `core/`, selected at runtime
and stored as a setting. The core sees an opaque handle with an identifier and a
version, plus the bounds it exposes. Adding a region is adding a data file.

**`Unknown` is a first-class profile state, and it closes the transmit path.**
Not "fall back to a sensible default" — a default that is lawful somewhere is
exactly how unlawful defaults ship. A device whose region has never been set
refuses to transmit and says why, in human language: *Firefly does not know which
radio rules apply here. Choose a region before the radio can transmit.* This
state has to be representable now, because **it is the state this project is
actually in.**

**Three ceilings, three sources, never collapsed into one number:**

```
effective = min( chip PA ceiling      — a hardware fact, from the datasheet
               , regulatory ERP limit — a legal fact, from the profile
               , user setting         — a preference )
```

Collapsing them loses the ability to say *why* 22 dBm was refused, and "your
radio cannot" and "the law here does not allow it" are different sentences with
different remedies — the same discipline ADR-0004 applies to availability.

**The legal limit is on radiated power, not on a chip register.** ERP is
`conducted + antenna gain − loss`. The current descriptor cannot express that,
and **antenna gain is UNKNOWN everywhere in this repository** — the only antenna
fact recorded anywhere is an IPEX jack on the T-Watch GNSS daughterboard. So the
bound is *not computable today*. That is a fact to record, not a reason to design
around: the type carries conducted power, antenna gain and the resulting ERP
separately, gain may be `Unknown`, and **an unknown gain means the ERP bound
cannot be proven — which is itself a reason to refuse to raise power.**

**Refusal is a normal outcome with its own error.** A silent clamp is
indistinguishable from a bug.

**Checkable by a script, which is what makes it survive the next contributor:**
a grep of `core/` and `apps/` for a region identifier or a frequency literal
returns nothing, ever. Same shape as the existing rule that no raw RGB appears in
UI code.

### 5. Writing to a node: stage, confirm, auto-revert

The node transmits, so **the node owns its own regulatory profile and re-validates
every remote write against it.** The watch cannot authorise the node to exceed
its limits any more than a phone can authorise the watch to. That is the existing
untrusted-input rule pointed in a new direction, and it needs restating because
the node is not the phone.

```
watch: propose(config, token)  ──►  node: validate against ITS profile
                                          stage · apply · start revert timer
watch: re-establish on the NEW parameters
watch: confirm(token)          ──►  node: commit
                          (silence) ──►  node: restore last-known-good
```

A plain request/response cannot express this, because after a successful
frequency change **the response can never arrive**. What the pattern requires:

- **Idempotency** through a client-supplied token, plus compare-and-swap on a
  configuration generation counter — so a retry does not double-apply and two
  writers cannot silently clobber each other.
- **Atomic preset application.** The network-contract parameters change as one
  unit or not at all. A node that took the new frequency and the old spreading
  factor is both unreachable *and* misconfigured, which is worse than unreachable.
- **The pending change and its timer survive a node reboot — or a reboot counts
  as a revert.** A node that reboots mid-window and comes up on unconfirmed
  parameters is a node in a field that nobody can reach.
- **An out-of-band recovery path** — a button, USB, a physical factory reset.
  Recovery that depends on the radio can be lost permanently. This is the
  difference between a bricked node and an inconvenience.
- **A failure taxonomy the interface can render:** `refused (validation)` ·
  `refused (regulatory)` · `accepted, not applied` · `applied, unconfirmed —
  reverting in N s` · `reverted` · `unknown, link lost`. Six outcomes, and the
  last one is the honest one.
- **Say "sent", never "changed".** The companion protocol already establishes
  exactly this for Find My Phone — show *sent*, never *your phone is ringing*,
  because the watch cannot know. A settings write across a link the write itself
  may have severed is the same epistemics with higher stakes.

### 6. Factory reset is layered, and one layer is irreversible

Three levels, not one button: **reset interface preferences** · **reset radio
configuration** · **erase identity and keys**. The third destroys something that
cannot be recovered, which puts it under the repository's never-irreversible-
without-being-asked rule. It needs its own confirmation, and it must say what it
is about to destroy *before* it happens.

### 7. What this changes elsewhere

**`LoraInfo` is wrong and this is the cheapest moment to fix it.** ADR-0001 is
still `proposed`, and its descriptor has three defects:

- `max_tx_dbm` conflates the chip's PA ceiling, the regulatory ceiling and the
  user's setting into one integer. Those are a hardware fact, a legal fact and a
  preference; they change at different times for different reasons.
- `RadioBand { SubGhz | Ghz24 }` is a hardware band *class*, but "band" in LoRa
  means *band plan* — a name collision that invites exactly the region-in-core
  mistake this ADR exists to prevent. Rename it.
- No antenna gain, so ERP cannot be computed at all.

**The error vocabulary cannot express a rejected write.** It needs
`INVALID_VALUE`, `OUT_OF_RANGE`, `NOT_PERMITTED_HERE` (regulatory, distinct from
a range violation) and `NOT_CONFIGURED`. And `POWER_RESTRICTED` should be
renamed: it reads as *battery* power and will be misused for *RF* power inside a
week.

**Settings must survive a firmware update, and that dependency is currently
undeclared** — it belongs in the partition and OTA decision
([TASKS](../../TASKS.md) T-025). Settings that vanish on the first update is a
class of bug discovered only after shipping.

**The store must be settable in the simulator.** A settings screen that only
works against NVS is a settings screen that cannot be developed on a desktop,
and the simulator is a first-class target.

**Every growing structure declares a maximum.** The node roster grows with the
node count — and it is telemetry, so it must never reach the settings partition
at all.

## Alternatives considered

**Store frequency as a float, as both reference implementations do.** Rejected
on measurement rather than on principle — see §2. It is worth noting that this
alternative is what the two most mature projects in the field actually chose,
which is a reminder that "the incumbents do it" is evidence about convenience,
not about correctness.

**A key–value store with strings, validated by callers.** Rejected: validation
scattered across call sites is validation that is missing at one of them, and
here one of them is a legal constraint. It also cannot express bounds that come
from a runtime profile.

**Compile the regulatory tables into `core/` behind a build flag.** Rejected. It
produces a binary per region, it puts the law in the layer that is supposed to
be jurisdiction-blind, and it makes the region unchangeable by a user who has
crossed a border — which is the case the feature exists for. It also directly
contradicts §52.

**Ship a default region and let the user change it.** Rejected, and this is the
tempting one because every product does it. A default that is lawful somewhere is
unlawful somewhere else, and it transmits before anyone has chosen. `Unknown`
that refuses to transmit is worse UX and correct behaviour, and the honest thing
to do about the UX is to make choosing a region part of first-run setup.

**Let the watch own the node's radio settings outright.** Rejected: the node is
the device that radiates, so the node is where the legal responsibility sits. A
watch that could override a node's limits would let a compliance failure be
authorised by the device that is not committing it.

**Plain request/response for remote writes, with a longer timeout.** Rejected: no
timeout is long enough when the successful case is the one where the reply cannot
be delivered.

## Consequences

**Easier.** Radio parameters become a screen instead of a rebuild. A region is a
data file. The same schema serves the watch's radio and a node's. The
transmit-path gate makes the project's current legal uncertainty a visible,
correct behaviour rather than a comment in a document.

**Harder.** A settings service exists before anything can use it, and it is not
small: schema, validation, persistence, migration, layered reset, change
notification, simulator backend. The remote-write protocol is genuinely
difficult and cannot be tested properly until two devices exist. Six failure
outcomes need six designed screen states.

**Committed to.** No frequency literal and no region identifier in `core/` or
`apps/`, checkable by grep. Every setting declares its bounds. Every remote write
is idempotent and reversible. The device does not transmit until somebody has
said where it is.

**Open.** A4 — which region — remains the owner's to answer, and now bounds a
profile rather than fixing a constant. Antenna gain is unknown on both boards and
on the node, so ERP cannot yet be computed; the type admits that rather than
hiding it. Whether MeshCore's own parameters are runtime-settable and where it
persists them is part of T-006.
