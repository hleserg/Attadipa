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

**And the application learns of the confirmation from a field, never by sniffing
the source.** The line above forbids the second, so `NavState` carries one
`bool`-shaped fact — "this `own` was filled by a confirmed companion" — written
by the same firmware that fills the slot and read by `format_navigation()` where
decision 3 needs it. That is one field on a struct the router already owns,
which is why this ADR adds no mechanism to carry it.

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
node, still stating nothing. So `own_ok` gains one clause and it is the narrowest
one that works: the `validity != NoFix` conjunct is **waived** when the
confirmation holds, and nothing else in the ladder moves —
`apps/src/navigation.cpp:148` — "  const bool own_ok = usable(state.own) &&".
The clause branches on the confirmation, **never on `validity`**, so a later
§4.1 amendment cannot silently retitle the line.

**No age enters this gate, and an earlier draft said one did.** `own_ok` has no
arrival-age term today and neither does the target's gate; the only age in the
ladder picks a *label* further down —
`apps/src/navigation.cpp:191` — "state.target.position.age_at_us_ms >= state.target_stale_after".
So "judged by arrival age, exactly as `target` is" described neither slot.
Decision 7 says what the age is for instead, and it is display rather than
gating.

**And the waiver admits a coordinate that may be a number somebody typed.**
`RESP_CODE_SELF_INFO` carries a lat/lon with nothing behind it:
`docs/research/NODE_POSITION_FROM_MESHCORE.md:56` — "| **Present when the node has no GNSS** | **yes** — it is whatever is in prefs".
On a node with no receiver it is a preference — restored from flash at boot,
settable over `CMD_SET_ADVERT_LATLON` by any client, and range-checked to
±90/±180 and nothing else.

**An earlier draft required the node to have published a `gps` key, and that is
withdrawn.** The key proves a receiver was detected. It proves nothing about the
coordinate, and the research says so on the row the requirement was resting on:
`docs/research/NODE_POSITION_FROM_MESHCORE.md:166` — "| `gps:0` | receiver detected, switched off | a fix from earlier in this boot,".
`gps:1` is no better — both values leave the number "possibly a fix, still
indistinguishable". So the precondition bought a `CMD_GET_CUSTOM_VARS` round trip
per session, refused nodes that are telling the truth, and still admitted the
typed coordinate it was written to stop. A test that cannot fail the case it
names is not a gate.

**What that coordinate gets instead is what this project already gives the same
number in the other slot.** A node's self-position that may be a stored
preference is rendered here today — as `target`, with no proof of fix demanded
of it, under a line saying what is unproven about it and how long ago it was
heard. A confirmed companion is that same number in the `own` slot. The `gps`
key was asking of `own` a proof nothing has ever asked of `target`, and one the
wire cannot supply; the compensation is therefore the compensation already in
use — a status that names the confirmation rather than claiming a fix, and a
caveat that carries the age. Decisions 7 and 8 are where that is written, and
they are why the waiver is safe without a discriminator.

**One value is refused, and it is refused because it is not a coordinate.**
Exactly `(0, 0)` is the unset preference —
`docs/research/NODE_POSITION_FROM_MESHCORE.md:620` — "The provider's frame cases are value cases: a coordinate of exactly (0, 0)," —
and the usability test is a range check that admits it:
`apps/src/navigation.cpp:33` — "  return state.has_position && core::in_range(state.position.value);".
An unset pref worn as the wearer's own position is the one output no caveat can
make honest, because every distance drawn from it is arithmetic on a
placeholder. So the firmware of decision 1 does not place that value in `own`,
the slot stays empty, and the readout is the second state of decision 7 rather
than a distance. This is a refusal at the slot and **not** a change to the
shared test: `target`'s exposure to the same value is exactly what it is today,
neither widened nor narrowed here, and belongs to whoever takes it up.

**4. It is entered on the watch's own screen, and offered by capability.**
OD-26's channel, unchanged; no second consent path is invented, and nothing here
licenses skipping the confirmation. The control appears only where `own` has no
local provider — `Availability::Unprovisioned` — which is a fact about this
device's configuration and not about which board it is. The self-contained watch
never shows it because its receiver answers.

**And only where there is a peer to name.** Decision 5 holds the confirmation
against a bonded peer, so a device with no node connected has nothing to confirm
and nothing to hold it against — a T-Watch built with
`CONFIG_ATTADIPA_GNSS_LOCAL=n` is `Unprovisioned` too, and must not be offered a
control that would ask the wearer to vouch for a companion that is not there.
The precondition is both halves: `own` is `Unprovisioned` **and** a node session
is live. That conjunction gates the control and nothing else — decision 7's
second state is the broader fact that this board has no receiver fitted, true
whether or not a session is live, and the two are deliberately not one test.

**And the control branches on the confirmation, never on the readout.** Once a
confirmation is live the same screen offers revoking it, per decision 6's first
bullet. So a companion that was confirmed and whose coordinate never arrived, or
whose coordinate was the unset pref decision 3 refuses, is **not** asked to
confirm a second time: the wearer already answered the question the control
asks, and the screen has nothing to add by asking again. Nothing in the offer
reads `NavStatus`.

**5. It lives in RAM, against the bonded peer and a last-heard time, and is
never persisted.** A stored confirmation is precisely the stale one, so there is
no NVS key and a reboot clears it. What it names is the peer it was given about
and when that peer was last heard — not the transport session, and an earlier
draft of this ADR named the session generation instead.

**Why that changed, because it is the interesting half.** Binding to the
generation —
`docs/adr/0015-transport-session-ownership.md:48` — "generation is allocated once when a session begins, is never reused, and is" —
made *every* reconnect lapse the confirmation, and a reconnect is not the event
this decision cares about. The event it cares about is **separation**, and the
generation cannot see it: a two-second RF stumble and a walk to another building
both allocate exactly one new generation. Binding to the peer and the gap sees
the difference, costs one timestamp instead of a generation to track, and is a
smaller thing to carry.

**6. It lapses on four things, and the gap that separates two of them is
`UNKNOWN`.**

- the wearer revokes it, on the screen that set it;
- **a different peer** connects. Another node is another body's business, and
  there is nothing to carry across;
- **the same peer is unheard for longer than a separation gap.** A reconnect on
  its own does not lapse it, and an earlier draft of this ADR said it did — see
  below;
- **eight hours** since it was given, as a backstop for the case the radio
  cannot see: the node stays in range and the wearer walks away from it inside
  that range. The number is chosen, not measured — long enough for a day out,
  short enough that a forgotten confirmation cannot survive a change of
  activity. What would move it is bench evidence about how long a real outing
  runs and how often the arrangement changes underneath it, not taste;
- a reboot, per 5.

**A reconnect used to cost a tap, and that was wrong.** The old bullet said the
watch cannot tell "reconnected to the node still in your pocket" from
"reconnected after you left it on a table". It can, and the evidence is the one
thing this feature has that nothing else does: **the link itself is a continuous
measurement of the distance between the two devices, and it is running whether
or not anybody looks at it.** The fact of a reconnect does not use it. **The
length of the gap does**, and it costs the one timestamp decision 5 already
carries.

**And the rest of this decision is a hypothesis, not a fact.** Reading the gap
that way assumes two things about radio behaviour that nobody here has measured:
that a companion carried on the wearer's own body holds the link rather than
stumbling through them, and that between "an RF stumble" and "the wearer walked
away" there is a duration that separates the two at all. Both are plausible;
neither is established, and the second is the load-bearing one, because if those
two ranges overlap there is no constant to pick and the rule has no window to
live in. They are recorded as `R4` in
[OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md), and this ADR is **conditional
on that measurement**.

**What happens if the measurement says there is no window.** The rule degrades
to "any loss of the peer lapses the confirmation", which is the session-bound
behaviour this decision replaced — worse to use, and honest. Nothing else in
this ADR moves: decision 7 enumerates on the confirmation and never on the link,
so the readout is right under either answer, and the only edit is which bullet
this list carries. That is why the rest may be implemented before the
measurement runs and this bullet may not.

**And the value stays `UNKNOWN` even if the window exists.** An implementation
reads it from one named constant and must not ship a number this ADR has not
been given. `docs/research/` gets the measurement, not this file.

**What the mechanism buys, stated as a bound rather than a promise, and only
where the transport is short-range.** While the link holds, the wearer's
confirmation cannot be wrong by more than the transport's reach, because being
wrong by more than that breaks the link and a long enough break lapses the
confirmation. On the BLE link this arrangement actually uses, that reach is
small enough for the bound to be worth having — but the bound is a property of
the *transport*, not of this decision, and `core` is deliberately not nailed to
BLE. On a long-range transport the bound is wide enough to be worthless, and
what carries the feature there is what carries the residue here anyway: the
eight-hour backstop, the revoke control, and a readout that never claims the
position is the wearer's own.

**And a sleep is no longer a rule of its own, which is the second thing the
gap rule buys.** An earlier draft gave sleep its own bullet, first exempting it
and then — correctly — refusing to, because whether the link survives is a
hardware claim with no source. Under the gap rule the question stops mattering
to this decision: a sleep that keeps the link changes nothing, and a sleep that
drops it is a gap like any other, measured against the same constant. The
watch's own sleep is not a special kind of absence.

Sleep on this board is an unbounded loop of
`esp_light_sleep_start()` —
`firmware/main/board_power.cpp:416` — "    for (;;) {" —
entered on the power key with NimBLE up, and whether the link survives it is
already written down here as unmeasured:
`docs/adr/0016-one-power-owner.md:23` — "   because nothing tells it not to; whether NimBLE survives that on this board".
Under the old session-generation rule, a link that does not survive meant the
wake reconnected, the reconnect allocated the next generation, and the
confirmation lapsed on **every** wake — the "feature nobody can use" that draft
was written to prevent, arriving through the door it left open. That is now
answered by the same constant as everything else, and the measurement below is
still worth taking, because it says whether a wake is a gap at all.

**The measurement that settles it** needs no instrument: confirm a companion,
sleep the watch on the power key, wake it past the link's supervision timeout,
and read whether the peer was lost and how long it went unheard. Until that
runs, an implementation may assume neither answer, and the screen must be right
under both — which decision 7 is, because it enumerates on the confirmation and
never on the link.

**7. The readout gains two sentences, and `Ready` goes out of reach while a
confirmation holds.** `NavStatus` gains one state for an `own` filled by a
confirmed companion, and one for a device with no receiver of its own and no
usable `own` coordinate — which is one state covering three arrivals: no
confirmation was given, a confirmation was given and its coordinate has not
arrived, and a confirmation was given and its coordinate was the unset pref
decision 3 refuses. All three are the same sentence to a wearer, and splitting
them would report the plumbing rather than the situation.

The numbers render under the first, exactly as they render under `NodePositionStale`
and for the same reason: a coordinate that was real is still drawn, under a line
that says what is wrong with it. `Ready` stays reachable on a watch whose own
receiver answers — the self-contained board, and a Waveshare that ever gets one
fitted — and goes **out of reach while a confirmation is what filled the slot**,
because the position is then a companion's, vouched for by a person rather than
by a receiver, and the line must keep saying so.

The second state replaces a sentence that is false on this board today —
`firmware/main/waveshare_board.cpp:970` — "exactly what would change the answer, and the readout still says" —
because a watch with no receiver fitted is not waiting for GPS.

**The two states go in two different places, and an earlier draft of this
decision put them both in one.** They have opposite relationships to the same
branch:
`apps/src/navigation.cpp:172` — "  } else if (!own_ok) {".

**The second state goes inside that branch, replacing what it answers.** All
three of its cases — no confirmation, a confirmation whose coordinate has not
arrived, a confirmation whose coordinate was the unset pref — leave `own` empty,
so `!own_ok` is true and this branch fires first. Placed after it the state is
simply unreachable, and the sentence it was written to remove prints anyway:
`apps/src/navigation.cpp:173` — "    text.status_code = state.own.fix_type == core::FixType::NoFix".
So inside the branch, a device with no local provider answers the receiverless
state where a device with a receiver keeps `NoFix` and `WaitingForGps`. The test
is the one decision 4 already uses — no local provider — and not which board it
is.

**The first state goes after that branch**, and **before** the validity
branches —
`apps/src/navigation.cpp:176` — "  } else if (state.own.validity == core::PositionValidity::Stale) {".
Decision 2 is the reason. A confirmed `own` carries `NoFix` at every age, so
`Stale` and `Degraded` never fire on it, and a state placed after them falls
through to the target branches — which on the split arrangement are reading an
empty slot (decision 8) and would answer `NodeUnavailable` or
`NodePositionUnknown` about the node the wearer has just confirmed is in their
pocket. Placing it here also makes this decision's `Ready` sentence structural
rather than a second rule: `Ready` is the last rung, and a confirmation answers
before it.

That the target branches sit behind these two is the intent, not an oversight.
They are sentences about the *other* node's coordinate, and while a confirmation
holds the wearer's own line is the one they can act on — the same ordering
argument the caveat block below already makes. What the link is doing does not
enter here either, because decision 6 already answered it: a link that goes away
for longer than the separation gap takes the confirmation with it, and inside
that gap the coordinate is the same one the wearer confirmed, aging under the
caveat exactly as it does when the link is up. There is no state in which this
branch speaks for a companion the watch has decided is gone.

**The age is shown, not thresholded, and this is a correction.** An earlier
draft split the live confirmation into a fresh state and a stale one at
`target_stale_after`, reasoning that the coordinate "arrives on the same link at
the same cadence". There is no cadence. It arrives **once per session**, in the
`RESP_CODE_SELF_INFO` that answers `CMD_APP_START`, and asking again means
sending `CMD_APP_START` again —
`docs/research/NODE_POSITION_FROM_MESHCORE.md:424` — "**And on path A alone there is no second read, so `Degraded` is not reachable" —
so a 120 s bound would put every confirmed coordinate on its far side two minutes
into every session and leave it there until a reconnect, which lapses the
confirmation anyway. A threshold that is true almost always is not a threshold.
The age is rendered instead, through the caveat this repository already has for
exactly this — `nav_caveat_node_unverified`, "node fix unverified, heard %s ago"
— and **no second age field is added to `NavState`**.

**That age is when the coordinate arrived, and nothing else — including after a
reconnect.** Decision 6 makes reconnects free, and a reconnect re-reads
`RESP_CODE_SELF_INFO`, so a coordinate the watch had held for forty minutes is
heard again and the caveat says three seconds. That is not the caveat drifting:
the string says *heard*, the number is arrival age, and the coordinate genuinely
did arrive again. What it is not, and never was on this path, is the age of a
fix — the node states no fix and no fix time, which is the whole reason the
caveat exists. An implementation must not relabel this age as anything else, and
must not carry the previous arrival forward to make it look older; the honest
reading is "this is when we last heard it", and a fresh number after a reconnect
is that reading working.

**That caveat's gate widens, because as written it cannot reach this case.** The
sentence is right; the branch it sits in asks about the wrong slot:
`apps/src/navigation.cpp:283` — "  } else if (usable(state.target) && !target_states_a_fix(state.target)) {".
On the split arrangement a confirmed `own` arrives long before any target does —
that is the whole point of the arrangement — so the age this decision promises to
render would never be drawn at all. The gate becomes *the slot a node filled that
states no fix*: a confirmed `own` first, then `target` as today. Own first for the
reason the block's own comment already gives, that the wearer's sentence is the
one they can act on.

**The argument changes with the branch, and an earlier draft said it did not.**
The confirmed branch renders `state.own.position.age_at_us_ms`, where the branch
beside it renders the target's —
`apps/src/navigation.cpp:290` — "    format_age(state.target.position.age_at_us_ms, state.locale, age," —
because on the split arrangement the target slot is empty (decision 8) and an
age read off an empty slot is zero. "Node fix unverified, heard 0 s ago", under
a coordinate nobody has heard since the handshake, is worse than no caveat: it
is the caveat lying about the one number it exists to carry. The string and the
ordering rule are unchanged, and no second age field appears.

**What `own.availability` reads, because the ladder asks it first.** The status
ladder tests availability before anything else —
`apps/src/navigation.cpp:170` — "  if (state.own.availability == core::Availability::Unreachable) {" —
and answers `OwnReceiverSilent`, which is a sentence about a receiver *on this
body* going quiet. There is no receiver on this body, so the question does not
apply, and the link's own availability must **not** be copied into the slot: a
node that walks out of BLE range would otherwise print "Receiver silent" on a
board with nothing fitted to be silent — the same false sentence the second
state above exists to remove. So while the confirmation holds and a coordinate
has arrived, the router sets `own.availability` to `Availability::Ready`; when
the confirmation lapses, the slot returns to what it was without one, which on
the split arrangement is `Availability::Unprovisioned`.

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

**One bullet of this ADR is conditional on a measurement that has not run.**
Decision 6 lapses the confirmation on a gap longer than a named constant, and
that rests on two unmeasured claims about this radio on a body — recorded as
`R4` in [OPEN_QUESTIONS](../research/OPEN_QUESTIONS.md). Everything else here
may be implemented before that run; the gap bullet may not, and if the run says
the two ranges overlap, the rule degrades to lapsing on any loss of the peer and
nothing else in this ADR changes.

Trust, GNSS power gating and `body_of()` are unchanged, which is the point, and
it has a price: wrist motion evidence stays neutral against the node's
coordinate, so a lapse cannot be triggered by "the watch moved and the node did
not". That is a real detector this ADR chooses not to have. Building it would
mean changing the mapping, which is the alternative rejected first.

A reconnect costs the wearer nothing; a gap longer than the separation bound
costs a tap, and the bound is the number decision 6 leaves `UNKNOWN`. That is
where this feature is still exposed: set it too short and a wearer walking past
a wall re-confirms all day, set it too long and a node on a table keeps
answering for a body that left. The measurement decision 6 names is what settles
it, and it is worth running before any of this is implemented rather than
after.

Two new `NavStatus` values need English and Russian strings, and the readout on
a receiverless board stops claiming it is waiting for GPS. `NavState` gains one
field for the confirmation and **no** second age bound.

Nothing here supplies `target` on the split arrangement. The vertical slice
still needs a remote node's coordinate, and that path is open.
