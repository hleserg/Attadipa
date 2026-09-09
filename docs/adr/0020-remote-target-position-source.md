# 0020 — A remote target's coordinate comes from the contact record, and no age is claimed for it

Status: **accepted**
Date: 2026-09-07

Decides the one seam [#450](https://github.com/hleserg/Attadipa/issues/450) had
left on the wire half, researched under
[#467](https://github.com/hleserg/Attadipa/issues/467). Evidence:
[REMOTE_TARGET_POSITION_FROM_MESHCORE](../research/REMOTE_TARGET_POSITION_FROM_MESHCORE.md),
building on [NODE_POSITION_FROM_MESHCORE](../research/NODE_POSITION_FROM_MESHCORE.md).
Takes [ADR-0011](0011-gnss-integrity.md)'s validity rules and
[ADR-0019](0019-confirmed-companion-body.md)'s slot routing unchanged.

## Context

The wearable slice needs two coordinates. ADR-0019 settled the first: a
companion the wearer has confirmed is on their body fills `own`, and every other
node coordinate stays `target`. That leaves the second with no wire —
`core/include/attadipa/core/mesh_service.h:27` — "struct MeshPeer {" — is a key
and a name, and nothing in this repository parses a remote peer's position.

Two upstream paths could carry one, and #412 costed neither. **Path B** is a
binary telemetry request to the target over the mesh, answering with a Cayenne
LPP record. **Path C** is the target's own signed advert, which our companion
stores in its contact table and hands over in a frame we already parse.

Three facts out of the research decide it, and the first is the one that removes
the argument the choice looked like it would turn on.

**1. Both paths read the same two variables.** `sensors.node_lat` and
`sensors.node_lon` on the target node are the source of the self-info frame, the
LPP record and the advert alike, and they are written only inside
`if (_location->isValid())`. So there is no path that is fresher at the source,
and no request that makes one — `NODE_POSITION_FROM_MESHCORE.md:204` — "fix the last valid coordinate stays in `node_lat` and keeps being" — so
whichever path fetched it, the coordinate is the last one the receiver solved.

**2. Path C's bytes are already in a frame this repository parses and
discards.** `RESP_CODE_CONTACT` is 148 bytes and the session already demands all
148 — `link/src/meshcore_companion.cpp:769` — "        if (size < 148) { ++malformed_frames_; return false; }" —
then reads the key and the name and drops bytes 132–147, which are the advert
timestamp, the coordinate at ×10⁶, and a modification stamp.

**3. Neither path can state an age for the coordinate**, and path C offers two
timestamps that look like one. `last_advert_timestamp` is the target's clock and
advances on an advert that carried no coordinate; `lastmod` is our companion's
clock and advances on a text message. Either, read as an age, manufactures
exactly the freshness that does not exist.

## Decision

**1. The remote target's coordinate is read from the companion's contact
record.** Bytes 136–143 of `RESP_CODE_CONTACT` (3) or `PUSH_CODE_NEW_ADVERT`
(0x8A), two little-endian `int32` of degrees ×10⁶.

**2. A target is named by its full 32-byte public key and by nothing else.** Not
the advertised name, not a six-byte prefix, not advertisement order, and not a
correlation tag. The key is present in every frame this path uses, so nothing
has to be recovered — which is the difference from path B, whose
`PUSH_CODE_BINARY_RESPONSE` carries a tag and no identity at all.

**3. The refresh is `CMD_GET_CONTACT_BY_KEY` (30), triggered by a
`PUSH_CODE_ADVERT` (0x80) whose key is the target's.** One command, one answer
or `ERR_CODE_NOT_FOUND`, no iterator. Not `CMD_GET_CONTACTS`, which is what both
reference clients use and which can answer with a whole table; and not
`CMD_APP_START`, which is the only re-read path A has and which aborts a
contacts iteration (**M27**). The `0x80` push carries the full key, so the
filter that keeps this off a busy mesh is exact and costs nothing.

**4. No age is claimed, and this is the load-bearing half.**
`age_at_source_ms` has no defensible value; the published `Timed<Position>`
carries `Validity::Unknown` and a consumer reads that before either age, exactly
as the path-A provider already requires. `last_advert_timestamp` and `lastmod`
may be **displayed as themselves, with whose clock they are on**, and may never
be summed, differenced against our clock, or mapped onto `age_at_source_ms`.

**5. `PositionValidity` is `NoFix` at every age and `PositionSource` stays
`NodeGnss`.** ADR-0011 is not amended, `classify()` gains no case, and no
provider acquires an opinion. This is the same answer path A already gets —
`link/src/node_position_provider.cpp:38` — "    out.observation.fix_type = core::FixType::Unknown;" —
and it is what ADR-0019 decision 2 already relies on.

**And `NodeGnss` overstates this coordinate's provenance, knowingly.** The enum
says a receiver —
`core/include/attadipa/core/position.h:84` — "    NodeGnss,       // an Attadipa node's receiver, over the node link" —
and what path C actually reads is a *third* node's record out of our companion's
contact table, which §2.2 of the report shows any BLE client of that companion
can write with `CMD_ADD_UPDATE_CONTACT` and which is then persisted unmarked. So
the owner's own phone can put a coordinate there and it will publish as though a
receiver had solved it. This ADR does **not** fix that, and the reason is scope:
a new source value is an ADR-0011 amendment and a change every consumer of
`PositionSource` has to answer for, which is a decision of its own and not a
detail of this one. What is decided here is that the gap is recorded rather than
carried silently — it is **M31**, and an implementer copying decision 5 is
copying a known overstatement. Nothing downstream may treat `NodeGnss` as
evidence of a fix; `PositionValidity` stays `NoFix` at every age, which is the
property that keeps the readout honest while the source label is imprecise.

**6. The readout will say `NodePositionStale` most of the time, and that is the
decision rather than a defect to be fixed.** A companion node has no periodic
advert: `advert_interval` and `flood_advert_interval` are commented out of its
prefs, and `MyMesh::advert()` is reached only from a button on the node's own
screen. So the target's cadence is a person pressing a button, the arrival age
will routinely exceed
`apps/include/attadipa/apps/navigation.h:77` — "    core::Millis target_stale_after{120000};" —
and the label at
`apps/src/navigation.cpp:191` — "state.target.position.age_at_us_ms >= state.target_stale_after" —
is the true sentence. **A resync does not re-stamp arrival unless the bytes
changed.** Re-stamping is the one edit that would turn this ADR into a lie, and
it is forbidden here so that it has to be argued rather than slipped in.

**7. Four values are refused at the slot**, each because it is not a coordinate:
exactly `(0, 0)`, which is the never-set field and which ADR-0019 already
refuses for `own`; a raw latitude outside ±90 000 000 or longitude outside
±180 000 000 -- the wire is degrees x 10^6, so those are the poles and the
antimeridian -- checked **before** scaling because nothing upstream
range-checks either; a contact whose type is not `ADV_TYPE_CHAT`; and a contact the node has
deleted, whose retained coordinate is discarded rather than aged.

**8. Path B is deferred, not rejected, and its trigger is named.** It becomes
the answer if a measured advert cadence is unusable **and** the target's owner
grants per-contact telemetry — which is a narrower disclosure than path C's
broadcast, and the one axis on which path B is the better citizen. If it is
taken, the request body carries an explicit inverse permission mask rather than
relying on AES zero-padding to supply one.

**9. Nothing here configures a node.** `advert_loc_policy` on the target is
bench configuration owned by whoever owns that node. This firmware does not set
it, and no MeshCore firmware is forked or changed.

## Alternatives considered

**Path B as the first slice.** Loses on every axis except one. It is 100×
coarser (×10⁴ against ×10⁶), needs a command builder, a `RESP_CODE_SENT`
correlation, a timeout policy, a serialisation lock against the node's single
global pending request slot, and an LPP decoder for untrusted bytes whose
upstream reference reads out of bounds. It costs two packets per read where path
C costs none. And its answer does not say who sent it. The one axis it wins is
that the watch can *pull* — decision 8 is where that is kept.

**The legacy `CMD_SEND_TELEMETRY_REQ` (39) / `0x8B` pair.** Carries a six-byte
key prefix and drops the tag. Six bytes is enough to label a message that has
already arrived and not enough to choose a destination. Compatibility fallback
only, if decision 8 ever fires.

**`CMD_GET_CONTACTS` (4) with `since`, which is what `meshcore_py` does.**
Correct for a client that mirrors the whole table; wrong for one that wants one
contact. On a `MAX_CONTACTS=350` build it can answer with ~52 kB over a link
whose notifications carry 173 bytes, and its cursor resets to 0 whenever a delta
matches nothing, so a client that stores the reported value verbatim does a full
sync next time.

**Waiting for `PUSH_CODE_NEW_ADVERT` (0x8A) to deliver coordinates.** It is the
frame for a contact the node did **not** store — `is_new` in `onAdvertRecv` is
declared `false` and never assigned `true` on the path that stores one. With
stock defaults (`manual_add_contacts = 0`) every stored contact is announced by
a bare `0x80`, so a client that waits for `0x8A` waits forever and fails
silently. It is handled, as a record that needs no round trip, and it is not
depended on.

**Treating `last_advert_timestamp` or `lastmod` as the coordinate's age.** The
first advances on an advert that omitted the coordinate; the second advances on
a text message. Both are refused by decision 4, and the research names the test
that keeps them refused.

**Amending `classify()` so a node coordinate can reach `Degraded`.** Out of
scope and unnecessary: the ladder argued in `NODE_POSITION_FROM_MESHCORE.md`
§4.1 needs a second read of a *changing* coordinate from a *running* receiver,
and path C gives neither — the coordinate arrives when somebody presses a
button, and nothing on this path says whether a receiver is on.

## Consequences

**Easier.** The slice is small and adds no parser: one `0x80` handler, one
command, and sixteen bytes of a frame whose `RESP_CODE_CONTACT` arm already
length-checks it. **The four arms it adds — `0x80`, `0x8A`, `0x8F` and `0x90` —
each carry their own guard**, because the
dispatcher owns no shared one — `link/src/meshcore_companion.cpp:659` — "    if (data == nullptr || size == 0 || size > kMeshCoreFrameBytes ||" — rejects
only an empty or over-long frame, and nothing after it inherits a bound: an arm
that reads a fixed-size field checks its own length, the one arm with no
fixed-size field at all passes the length through instead —
`link/src/meshcore_companion.cpp:807` — "        accept_custom_vars(&data[1], size - 1);" — and an arm
that reads nothing past the opcode checks nothing.
`REMOTE_TARGET_POSITION_FROM_MESHCORE.md` §9.1 states all four bounds and §12.1
tests them. It costs no
airtime, needs no timer, no queue and no *mesh* request table — what it does add
is a third claimant to the `RESP_CODE_ERR` attribution this file already keeps by
hand, which §9.1 makes a rule of — and it is testable on the
host with no node at all. The target's identity is exact, so the failure mode
that would matter most — an arrow pointing at the wrong person — is closed by
construction rather than by a probability argument.

**Harder.** The readout is honest and therefore unimpressive: a coordinate that
is often minutes or hours old, labelled as such. Anyone who later wants a
livelier number has to open decision 8 rather than tune a threshold.

**Committed to.** No claim of freshness on any node coordinate, ever, on any
path — which is now stated in three places that must move together: this ADR,
ADR-0011's `NoFix` verdict, and ADR-0019's waiver, which admits a coordinate to
`own` on a routing fact and explicitly not on a validity one.

**Not committed to.** A target *selection* mechanism. Something has to name the
target key, and where that lives — settings, a contact list on the watch, an
owner decision — is not decided here and does not block the wire work: a test
fixture supplies a key, and the first consumer is a diagnostics surface rather
than a map, for the reason `NODE_POSITION_FROM_MESHCORE.md` §6 already gives.

**A ceiling on enumeration, and only on enumeration.** This watch retains
sixteen peers —
`link/include/attadipa/link/meshcore_companion.h:176` — "    static constexpr std::size_t kRetainedPeers = 16;" —
against a contact table that is 350 on the T114 build. It does **not** gate the
read: `accept_contact` parses the whole 148-byte frame and copies key and name
out before the cap is consulted at all, and the cap then decides storage alone —
`link/src/meshcore_companion.cpp:432` — "    if (peer_count_ < peers_.size()) {". The primary read is
`CMD_GET_CONTACT_BY_KEY`, which answers with its own frame and never consults
`peers_`. So a target beyond the sixteenth is readable; what the ceiling limits
is which targets can be *offered* to choose from, and target selection is the
one thing this ADR declines to decide. `peers_truncated` still has to reach the
operator, as a statement about the list they are picking from and not about
whether a coordinate can arrive.
