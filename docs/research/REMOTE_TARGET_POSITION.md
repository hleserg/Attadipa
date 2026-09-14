# A remote node's coordinate — which wire pays for it

Research for [#467](https://github.com/hleserg/Attadipa/issues/467).
**RESEARCH ONLY. No production code changes with this file.**

**Nothing here was executed on hardware by this repository.** Every claim about
the MeshCore wire is read from
[`MESHCORE_COMPANION_PROTOCOL.md`](MESHCORE_COMPANION_PROTOCOL.md), which traced
it to upstream source at a named revision. The one observation that is not from
upstream — the shape of a coordinate carried in a message — is the owner's own
firmware, and §6 says exactly what evidence stands behind it and what does not.

## 0. The question this file closes, and the one it does not

[#412](https://github.com/hleserg/Attadipa/issues/412) chose
`RESP_CODE_SELF_INFO` for the **connected** node's own position and deliberately
left the remote one open.
[OD-28](OWNER_DECISIONS.md#od-28--a-companion-the-wearer-has-confirmed-is-on-their-body-may-fill-own)
then settled which slot that coordinate lands in: `target` by default, and `own`
only once the wearer has confirmed the companion is on their body.

Neither decided **where a second, remote coordinate comes from** when the
companion is on the body and the target is somebody else. That is this file.

It does not decide the user interface, the storage of a target, or anything
about which contact is selected. Those belong to the issue that implements the
path chosen here.

## 1. Three candidate wires, and there are only three

**A — the coordinate rides a message from a contact.** A person sends a text
message and their node appends their position to it. The message arrives on the
path this repository already parses:
`link/src/meshcore_companion.cpp:527` — "bool MeshCoreCompanion::accept_message(const std::uint8_t* data,"
reads the text, and `:548` — "    const core::MeshPeer* sender = find_peer_prefix(&data[prefix]);"
names who sent it.

**B — the coordinate rides a contact's advert.** An advert may carry a position:
`MESHCORE_COMPANION_PROTOCOL.md:41` — "`ADV_LATLON_MASK 0x10`, advert lat/lon `int32` ×1e6".
Whether it does is the sending node's own policy —
`:463` — "Whether a node puts its position in its advert at all is a three-way policy:".

**C — a remote telemetry request.** The companion transmits a request to a named
node and the node answers with a telemetry record.

## 2. Identity — what each wire can prove about *whose* coordinate it is

This is the axis that decides it, so it goes first.

- **A proves it.** The message carries the sender's public-key prefix and is
  correlated against a contact this node already holds. The coordinate and the
  identity arrive in the same frame, from the same sender, on a path whose
  attribution this repository already relies on for the message text itself.
- **B proves it.** The advert is signed and the coordinate is inside the signed
  region: `MESHCORE_COMPANION_PROTOCOL.md:452` — "1. every advert is **Ed25519-signed** over `pub_key ‖ timestamp ‖ app_data`, and".
  A forged advert is discarded by the node before the companion link ever sees
  it, and a replayed one is refused by a monotonic per-contact timestamp.
- **C cannot prove it today.** As reported in #467 from upstream at
  `0679dbef`, the legacy telemetry response carries a six-byte key prefix but
  drops the request tag, and the binary response carries the tag but no
  identity. **This repository has not re-traced those two frames against
  upstream**, and does not need to: a path whose own reviewer states it cannot
  correlate an answer to the node that was asked is not a path this product can
  put an arrow on. If that changes upstream, this section is where to reopen it.

## 3. Cost on the air

- **A costs nothing extra.** The coordinate rides a message that was going to be
  sent anyway. No additional transmission exists to account for.
- **B costs nothing to the receiver**, and the transmission is the sender's, made
  on the sender's own schedule.
- **C costs a transmission per query**, and #467 reports that a node holds only
  one pending mesh request globally, so two clients asking at once displace each
  other. Neither claim is re-traced here; both point the same way as §2.

## 4. Freshness, and how it is shown rather than assumed

Neither A nor B carries a fix age of its own, so age is taken from the frame
that carried it: a message has its own timestamp, and an advert has the signed
timestamp its replay check already depends on.

**Age is displayed, not silently tolerated.** The navigator already refuses to
invent a state it does not have, and the target slot is deliberately more
permissive than the own slot — `apps/src/navigation.cpp:148` — "const bool own_ok = usable(state.own) &&" —
with the reason written four lines above it: a node states no fix type at all,
so refusing on `NoFix` would refuse every node coordinate ever sent.

A known trap for B, reported in #467 and **not re-traced here**: upstream
advances a contact's `last_advert_timestamp` even when the new advert carries no
coordinates, so an old position can look freshly stamped. The mitigation is
structural rather than clever — store the age of the *coordinate*, never the age
of the contact record — and it costs nothing to apply to A as well.

## 5. Decision

**A is the primary path. B is the fallback for a node that is not ours. C is
rejected.**

The order is not a ranking of convenience. A and B both prove identity; C does
not, and an arrow pointing confidently at the wrong person is the exact class of
output this repository forbids everywhere else. Between A and B, A is chosen for
a reason the owner gave and which is a product decision rather than an
engineering one — recorded as
[OD-30](OWNER_DECISIONS.md#od-30--a-position-is-shown-to-named-recipients-rather-than-broadcast).
An advert goes to everyone in radio range and is relayed onward as a signed
packet that cannot be recalled; a message goes to the people the sender chose.

B stays in as the fallback because a stock node running unmodified firmware is
still a node this product should be able to point at, and its advert is the only
position such a node ever offers unasked.

**What A does not require:** a new protocol. The request — *"where are you?"* —
is a text message; the consent is the sender pressing send; the "always let this
contact find me" setting is the sending firmware's own per-contact policy. A
structured request/answer between two of this project's own nodes is a later
issue and is not needed to make the path work.

## 6. The observed format, and what stands behind it

A message from the owner's own MeshCore v4.3 fork carries its position as text
appended to the message body, in the shape:

```
Идём к вам @12.3456,65.4321
```

**Coordinates above are invented.** The evidence is a screenshot of a real
conversation, supplied by the owner on 2026-09-13 and **deliberately not
committed**: `docs/` is this repository's GitHub Pages root, and the capture
carries a contact name and a real position to eleven metres.
[OD-27](OWNER_DECISIONS.md#od-27--a-bench-capture-that-carries-no-position-and-no-identity-may-be-committed)
is the rule it fails.

What the capture shows, and what a parser must therefore accept:

| | observed | consequence for the parser |
|---|---|---|
| sigil | `@` immediately before the first digit | require a line start or whitespace before it, so `@name` and an address never match |
| separator | a single `,`, no space | — |
| precision | five decimal places in every row | accept one to seven; five is about 1.1 m of latitude |
| placement | last thing in the message | not required: take the **last** match, so a quoted older message cannot win |
| variation | a preset message renders as `@ 55.98…` with a space after the sigil, while a typed one does not | accept one optional space; two shapes in the wild is itself worth removing at the source |

**A coordinate is a claim by the sender, not a fix this repository classified.**
Nothing about this path lets a position reach `Valid`, and nothing should: it
lands in `target`, where §4's asymmetry already admits it.

**A truncation hazard, named because it cannot be detected downstream.** The
observed firmware truncates the human text to fit its message limit and keeps
the coordinate whole — visible in the capture as a preset cut mid-character.
That priority is the correct one and should be deliberate rather than
incidental, because the reverse fails silently: a coordinate cut short is still
a syntactically perfect number, and `@55.98` is a kilometre from `@55.9821`
with nothing in its shape to say so.

## 7. What this obliges

1. An executable issue implementing A: parse the coordinate out of an accepted
   message, keep it with its sender and the message's own timestamp, and fill
   `target`. `MeshPeer` has no position field today; adding one is the shape of
   the change.
2. B is implemented only when a node that is not ours actually needs pointing
   at, and not before — it shares the storage A adds.
3. The age stored is the coordinate's, never the contact record's (§4).
4. No new protocol, and no telemetry request (§5).

## 8. Still UNKNOWN, and deliberately left so

- **Whether the companion firmware advertises on a timer at all.** The
  58-command map records exactly one advert emission, and it is a command from
  the client: `MESHCORE_COMPANION_PROTOCOL.md:485` — "| 7 | `CMD_SEND_SELF_ADVERT` | 37 | `CMD_SET_DEVICE_PIN` |".
  A periodic advert belongs to the repeater and room firmwares, which are not
  this one. That map is a command census and not a trace of the main loop, so
  *"a companion never advertises unasked"* is **UNKNOWN** here rather than
  established. It does not change §5: B is the fallback either way, and if the
  answer is "never unasked" then B is simply a narrower fallback than it looks.
- **Whether the two telemetry response frames really cannot be correlated.**
  Taken from #467's reading of upstream and not re-traced (§2).
- **Everything physical.** No node was made to send a coordinate to another node
  for this file. **NOT EXECUTED — HARDWARE REQUIRED.**
