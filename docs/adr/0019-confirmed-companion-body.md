# 0019 — A companion the wearer confirmed is on their body, and how that lapses

Status: **accepted**
Date: 2026-09-07

Implements [OD-28](../research/OWNER_DECISIONS.md#od-28--a-companion-the-wearer-has-confirmed-is-on-their-body-may-fill-own).
Adds one application-level exception on top of [ADR-0013](0013-node-motion.md)
and takes [OD-26](../research/OWNER_DECISIONS.md#od-26--owner-consent-for-provisioning-is-a-finger-on-the-watchs-own-screen)'s
consent channel unchanged.

## Context

The owner runs two arrangements. On the self-contained one the watch and its
companion are one board, so the receiver is already on the wearer's body and
nothing below applies. On the split one they are two nodes — a wrist and a
companion carried on the same person — and the wrist has no receiver at all.

Without a coordinate for the wearer there is no distance to anything, so the
whole readout is dead on that arrangement. OD-28 unblocks it: a companion the
wearer has **explicitly confirmed is on their body** may fill `own`. That is a
decision about a physical arrangement, and the sentence it qualifies is a
comment in the header an implementer opens first, where it stands unqualified —
`apps/include/attadipa/apps/navigation.h:19` — "// **own** position comes from a receiver on this body, **target** position is".

**Confirming is necessary and it is not sufficient, and the code says exactly
where it stops being enough.** Every coordinate off this channel is born with no
stated fix — `link/src/node_position_provider.cpp:38` — "out.observation.fix_type = core::FixType::Unknown;" —
which `classify()` turns into `NoFix`, and `NoFix` is the one thing an own
position may not be:
`apps/src/navigation.cpp:148` — "const bool own_ok = usable(state.own) &&".
Route a confirmed coordinate to `own` and change nothing else, and the face
still says "Waiting for GPS". This ADR exists for that half.

## Decision

**1. The confirmation is a routing fact. Provenance does not move.**
`PositionSource` stays `NodeGnss` —
`link/src/node_position_provider.cpp:39` — "out.observation.source = core::PositionSource::NodeGnss;" —
`body_of()` still maps it to `SensorBody::Node`, and ADR-0013's two consumers,
the trust engine and GNSS power gating, are untouched. What changes is which
slot of `NavState` the coordinate is placed in, and that decision is made by the
firmware that fills `NavState`, not by the application that reads it. It has to
be: the source is not an application's to read —
`core/include/attadipa/core/position.h:78` — "// Where the position came from. Applications never see this — ADR-0004 §2 —".

**2. A confirmed coordinate carries `NoFix`, at every age, unchanged.** This is
the question OD-28 said none of the rest renders without, and the answer is that
the confirmation changes nothing about it. A confirmation is a statement about a
body; validity is a statement about a fix; the node stated no fix and the wearer
cannot state one for it. `classify()` is not amended here, no provider gains an
opinion, and the `validity` binding OD-28 struck stays struck.

**3. The gate that moves is `own_ok`, and its own comment is the argument.**
Three lines above it the code already says why `own` refuses `NoFix` and why
`target` does not:
`apps/src/navigation.cpp:143` — "// is the point: a receiver on this body reports its own fix state, so `NoFix`"
and `apps/src/navigation.cpp:145` — "// last held. A node states no fix state at all, so `NoFix` there means only".
A confirmed companion is the second case wearing the first case's slot: still a
node, still stating nothing. So `own_ok` gains one clause — a confirmed `own` is
judged by arrival age, exactly as `target` is — and nothing else in the ladder
moves. The clause branches on the confirmation, **never on `validity`**, so that
a later §4.1 amendment cannot silently retitle the line.

**4. It is entered on the watch's own screen, and offered by capability.**
OD-26's channel, unchanged; no second consent path is invented, and nothing here
licenses skipping the confirmation. The control appears only where `own` has no
local provider — `Availability::Unprovisioned` — which is a fact about this
device's configuration and not about which board it is. The self-contained watch
never shows it because its receiver answers.

**5. It lives in RAM, against a session generation, and is never persisted.**
The confirmation names the transport session that was live when it was given —
`docs/adr/0015-transport-session-ownership.md:48` — "generation is allocated once when a session begins, is never reused, and is" —
and is refused under any other. A stored confirmation is precisely the stale one,
so there is no NVS key and a reboot clears it.

**6. It lapses on five things, and sleep is not one of them.**

- the wearer revokes it, on the screen that set it;
- the session generation changes — a reconnect, a re-bond, a different node. The
  watch cannot tell "reconnected to the node still in your pocket" from
  "reconnected after you left it on a table", so it asks again;
- **eight hours** since it was given. This is the backstop for the one case the
  others miss: the node stays in range and the wearer walks away from it. The
  number is chosen, not measured — long enough for a day out, short enough that
  a forgotten confirmation cannot survive a change of activity. What would move
  it is bench evidence about how often a real outing reconnects, not taste;
- a reboot, per 5;
- **not** a sleep or a wake. The watch sleeps constantly, and a confirmation
  that dies on wake is a feature nobody can use.

**7. The readout gains three sentences and loses `Ready`.** `NavStatus` gains a
state for a live confirmation with a fresh coordinate, one for a live
confirmation whose coordinate is past the age bound, and one for no live
confirmation on a device with no receiver of its own. The numbers render under
the first two, exactly as they render under `NodePositionStale` and for the same
reason: a coordinate that was real is still drawn, under a line that says what
is wrong with it. `Ready` is **not reachable** while `own` is filled this way —
the position is a companion's, vouched for by a person rather than by a
receiver, and the line must keep saying so.

The third state replaces a sentence that is false on this board today —
`firmware/main/waveshare_board.cpp:970` — "exactly what would change the answer, and the readout still says" —
because a watch with no receiver fitted is not waiting for GPS.

The age bound is the one already in `NavState` for the target. The coordinate
arrives on the same link at the same cadence, so it is the same bound; an
implementer reading `target_stale_after` against `own` is reading it correctly
and should not add a second field.

**8. The coordinate moves. It is never in both slots.** A confirmed companion's
self-position is `own` and is then not `target`, because the same coordinate in
both renders `0 m` and a needle pointing at nothing — the output this project
exists to refuse. On the split arrangement `target` is therefore **empty** until
a remote node's coordinate can be fetched, which OD-28 leaves open. The first
thing the wearer sees after confirming, today, is the node stating no
coordinate — not a distance. That is the honest state of the slice, and an
implementation that produces a distance here has duplicated the coordinate.

## Alternatives considered

**Resolve a confirmed companion to `SensorBody::Watch`.** The reading that
matches OD-28's word. It re-bodies the coordinate at the mapping, which means
`previous_body_` never changes across a confirmation or a lapse —
`core/src/trust.cpp:362` — "body_of(observation.source); body != previous_body_" —
so no baseline is ever dropped, and the defect the block above it records
returns through a state the documentation calls same-body, with no second
receiver on this board to contradict it.

**Carry the confirmation in `validity`.** Struck by OD-28 before it was written
down. It would make `classify()` answer a question about a body, and would let
one screen's consent silently change what the trust engine reads.

**Amend `classify()` per §4.1 so a node coordinate reads `Stale`.** This is the
route OD-28 pointed at and it is deliberately not taken here. It is a general
change to every node coordinate rather than to a confirmed one — it reopens
ADR-0011, moves the pinned verdicts in §8.1 of
[NODE_POSITION_FROM_MESHCORE](../research/NODE_POSITION_FROM_MESHCORE.md), and
clears a trust reason that is currently held for every node source —
`core/src/trust.cpp:404` — "set(engine_, TrustReason::FixLost, validity == PositionValidity::NoFix, now);".
None of that is needed to answer OD-28, and the research that argues it says so
itself: `docs/research/NODE_POSITION_FROM_MESHCORE.md:430` — "ladder **tops out at `Stale`**: `Degraded` becomes reachable only when a safe".
It stays deferred. If it lands later it does not conflict with this ADR, because
decision 3 branches on the confirmation rather than on the validity.

**Persist the confirmation across reboots.** Convenient, and it is the failure
mode named in the decision itself: the confirmation that survives longest is the
one most likely to be wrong.

**A second consent channel — a phone, a BLE command, a config flag.** OD-26
refuses it, and a device that can be told over the air that it is being worn has
no confirmation at all.

**Fuse the node coordinate with wrist motion instead of confirming.** The header
refuses it in the same comment the decision qualifies: two inputs, one consumer,
and a provenance that would have to be invented for the answer.

## Consequences

The split arrangement can produce a distance for the first time, and the
self-contained one is unaffected — it never reaches any of this.

Trust, GNSS power gating and `body_of()` are unchanged, which is the point, and
it has a price: wrist motion evidence stays neutral against the node's
coordinate, so a lapse cannot be triggered by "the watch moved and the node did
not". That is a real detector this ADR chooses not to have. Building it would
mean changing the mapping, which is the alternative rejected first.

A reconnect costs the wearer a tap. If the bench shows reconnects are frequent
on a real outing, the fix is a measurement and a revised generation rule, not a
persisted confirmation.

Three new `NavStatus` values need English and Russian strings, and the readout
on a receiverless board stops claiming it is waiting for GPS.

Nothing here supplies `target` on the split arrangement. The vertical slice
still needs a remote node's coordinate, and that path is open.
