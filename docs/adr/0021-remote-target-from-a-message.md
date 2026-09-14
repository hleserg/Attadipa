# 0021 — A remote target's coordinate arrives in a message, and the contact record is the fallback

Status: **accepted**
Date: 2026-09-14

Supersedes [ADR-0020](0020-remote-target-position-source.md) **on which wire is
paid for, and on nothing else.** Decisions 4, 5, 7 and 9 of that ADR are carried
here unchanged and are load-bearing; the table below says exactly which of its
clauses stop applying. Rests on
[OD-30](../research/OWNER_DECISIONS.md#od-30--a-position-is-shown-to-named-recipients-rather-than-broadcast),
and on the same research: [REMOTE_TARGET_POSITION_FROM_MESHCORE](../research/REMOTE_TARGET_POSITION_FROM_MESHCORE.md),
whose §14 records the evidence that is new here.

## Context

ADR-0020 chose between the two wires that were on the table in September: a
binary telemetry request, and the target's own signed advert as our companion
stores it in a contact record. It took the contact record, and its reasoning is
not disturbed by anything below — in particular its finding that **no path is
fresher at the source**, because every one of them reads the same two variables
on the target node.

**A third wire was not on that table.** The owner's own MeshCore fork appends a
position to the text of a message a person chooses to send, and has been doing
so on the bench. Observed 2026-09-13; the format and its traps are §14 of the
research report. Nothing about it was known when ADR-0020 was written, so this
is not a re-litigation of that decision — it is the arrival of an option the
decision could not have considered.

**Two things make it the better wire, and only one of them is technical.**

The technical one is disclosure, and ADR-0020 already named the axis: it
observed that the telemetry path is *"a narrower disclosure than path C's
broadcast, and the one axis on which path B is the better citizen"*. An advert
goes to every node in radio range and is relayed onward, signed, and cannot be
recalled. A message goes to the contact it was addressed to.

The other is that this is the owner's decision rather than ours (OD-30), taken
against a recommendation from this repository that had ranked the advert first.

**What does not change is what ADR-0020 got right about honesty.** No age may be
claimed for a remote coordinate; `PositionValidity` stays `NoFix`; four values
are refused at the slot; and this repository configures no node and forks no
firmware. Each of those survives this ADR intact, and a reader implementing the
new wire must apply them to it.

## Decision

**1. The remote target's coordinate is read from the text of a message this
companion already accepts.** The message arrives on the path this repository
parses today — `link/src/meshcore_companion.cpp:527` — "bool MeshCoreCompanion::accept_message(const std::uint8_t* data," —
and the coordinate is a substring of the text it copies. The grammar is fixed in
§14 of the research report and is the only shape accepted: nothing is inferred
from a number that does not carry the sigil.

This replaces ADR-0020 decision 1 as the primary source.

**2. A target is named by the full 32-byte public key of the contact the
message's sender prefix resolved to, and a message that resolves to no contact
carries no target.** ADR-0020 decision 2 required the full key and refused a
six-byte prefix. The message frame carries only the prefix —
`link/src/meshcore_companion.cpp:463` — "        if (std::memcmp(peers_[i].id.public_key.data(), prefix, 6) == 0) {" —
so this decision does not weaken that rule, it routes through it: the prefix is a
lookup key into a table that holds full keys, never an identity of its own.

**The cost is named rather than hidden.** A prefix is 48 bits, and a collision
inside one contact table would attribute a coordinate to the wrong contact.
Nothing here measures that risk and nothing should claim it is zero. What is
decided is the failure mode: an unresolved prefix means **no target**, not an
unnamed one, because `link/src/meshcore_companion.cpp:548` — "    const core::MeshPeer* sender = find_peer_prefix(&data[prefix]);" —
already answers `nullptr` while the text is accepted regardless. A coordinate
without a named sender is dropped.

**3. The contact record stays implemented as the fallback, for a node whose
firmware is not ours.** ADR-0020 decisions 1 and 3 — bytes 136–143 of
`RESP_CODE_CONTACT`, refreshed by `CMD_GET_CONTACT_BY_KEY` on a `0x80` push
whose key is the target's — remain the specification for that fallback. They are
demoted from primary, not withdrawn. A stock node never sends a coordinate in a
message, and its advert is the only position it offers unasked. That its bytes
are a record any BLE client of our companion can write is **M31** and is
unchanged by this ADR.

**4. Path B, the telemetry request, stays rejected, and ADR-0020 decision 8's
trigger is superseded.** That clause made telemetry the answer if the advert
cadence proved unusable and the target's owner granted per-contact telemetry.
The message wire now occupies that position: it is narrower in disclosure than
either, needs no permission mask, and cannot be mis-attributed the way
`PUSH_CODE_BINARY_RESPONSE` can, which carries a correlation tag and no identity
at all. Telemetry becomes the answer only if **both** other wires are shown
unusable, which is not a state anything has observed.

**5. No age is claimed. ADR-0020 decision 4 applies to this wire verbatim, and
the message gives it no help.** The advert fields were already forbidden from
reaching `age_at_source_ms` — `docs/research/VERIFIED_FACTS.md:357` — "  decision 4 forbids either field from reaching `age_at_source_ms`." —
because they are on the sender's clock. A message carries no timestamp this
repository reads at all: `accept_message()` copies the text and nothing else
temporal. So there is no age to publish, `Validity::Unknown` travels with the
coordinate exactly as before, and **arrival time is not an age and is not
presented as one.** ADR-0020 decision 6's prohibition on re-stamping arrival
unless the bytes changed applies here too.

A consequence worth stating because it is easy to mistake for a defect: the
readout's honest resting state is still `NodePositionStale`, for the reason
ADR-0020 gave — a companion has no periodic advert at all,
`docs/research/VERIFIED_FACTS.md:383` — "### A companion node has no periodic advert at all" —
and for a new one: a person sends a message when they have something to say,
which is a cadence, not a refresh.

**6. `PositionValidity` is `NoFix` and `PositionSource` stays `NodeGnss`, with
the overstatement recorded.** ADR-0020 decision 5 holds, and this wire widens
what the label overstates rather than narrowing it: the bytes are not a contact
record a BLE client can write but free text a sender composed, which their
firmware may have filled from a receiver and a person may equally have typed.
`NodeGnss` names a receiver and this is a sender's claim. Nothing downstream may
read the label as evidence of a fix; `NoFix` at every age is what keeps the
readout honest while the label is imprecise. Correcting `PositionSource` is an
ADR-0011 amendment and is not decided here, exactly as ADR-0020 declined to
decide it.

**7. The refusals of ADR-0020 decision 7 apply before the number is used, and
the grammar adds two of its own.** Exactly `(0, 0)` is refused; a latitude
outside ±90° and a longitude outside ±180° are refused; a contact the node has
deleted is discarded rather than aged. To those the text wire adds: a coordinate
that does not match the grammar whole is not read at all, and **a value that
fails any bound is dropped, never clamped** — clamping would invent a place.

The sign is part of the grammar, not an afterthought: a leading `-` on either
number is accepted and means the southern or western hemisphere. A parser that
admits no sign either drops every southern coordinate or mirrors it into the
wrong one, and both are silent.

**8. Nothing here configures or forks a node.** ADR-0020 decision 9 unchanged.
Whether a sender appends a position is that sender's firmware and that person's
choice, and this repository neither sets it nor requires it.

## What of ADR-0020 is superseded, precisely

| ADR-0020 | Here |
|---|---|
| 1 — the coordinate comes from the contact record | superseded as **primary**; retained as the fallback (decision 3) |
| 2 — a target is named by the full 32-byte key | **in force**, reached through a prefix lookup (decision 2) |
| 3 — refresh by `CMD_GET_CONTACT_BY_KEY` on a `0x80` push | **in force** for the fallback |
| 4 — no age is claimed | **in force**, and extended to the message wire (decision 5) |
| 5 — `NoFix`, `NodeGnss`, M31 recorded | **in force**, with the overstatement widened (decision 6) |
| 6 — `NodePositionStale` is the resting state; no re-stamping | **in force** (decision 5) |
| 7 — values refused at the slot | **in force**, plus grammar and sign (decision 7) |
| 8 — path B deferred, with a two-part trigger | **superseded**: the trigger is replaced (decision 4) |
| 9 — nothing configures a node | **in force** (decision 8) |

## Alternatives considered

**Leave ADR-0020 standing and add the message wire as an implementation
detail.** Rejected: which wire is paid for is the whole of what ADR-0020
decided, and changing it in a research file would leave `docs/ROADMAP.md` and
`OPEN_QUESTIONS.md` pointing implementers at the old answer — which is exactly
what round 1 of the pull request that carries this ADR found happening.

**Take the message wire and drop the contact record entirely.** Rejected: a node
running unmodified upstream firmware offers a position no other way, and
deleting the fallback would make this product unable to point at any node but
ours.

**Claim an age from the message's arrival.** Rejected under ADR-0020 decision 4,
which this ADR does not reopen. Arrival is a fact about our own receiver and
says nothing about when the coordinate was solved; publishing it as the
coordinate's age is the same lie the advert fields were forbidden for.

## Consequences

**Easier.** The wire needs no new protocol, no permission mask and no request:
the ask is a text message, the consent is a person pressing send, and a standing
"always let this contact find me" is the sending firmware's own per-contact
setting. Nothing is transmitted by this product to obtain a coordinate.

**Harder.** The coordinate is now free text from a remote party rather than a
fixed-width binary field, so the parser is the trust boundary and decision 7 is
where that is paid. Two shapes of the sigil already exist in the wild (§14.2), and
a sender's firmware truncating its own message could in principle cut a
coordinate into a shorter, perfectly well-formed, and wrong one — the observed
firmware truncates the human text and keeps the coordinate whole, which is the
right priority and is not something this repository can enforce.

**Unchanged.** `NoFix` at every age, no claimed age, the refusals, and the
absence of any node configuration. Those are ADR-0020's, and they are the half
of it that this ADR exists to keep rather than replace.
