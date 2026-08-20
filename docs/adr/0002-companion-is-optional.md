# 0002 — The phone companion is optional, and the watch never depends on it

Status: **proposed — scope corrected 2026-08-21**
Date: 2026-08-21

> **Correction notice.** The decision below was stated about "the companion" and
> meant "the phone". Within hours the distinction became load-bearing: the
> project owner established that a separate **Firefly node** exists, carrying
> LoRa, GNSS and an ESP32, and that a watch attached to one runs the same
> applications a LoRa watch runs
> ([OWNER_DECISIONS OD-1](../research/OWNER_DECISIONS.md)). The node *does*
> provide capabilities. Read as written, this ADR forbids the product.
>
> The rule that survives is narrower and, I think, the one that was actually
> meant: **no capability may require a general-purpose phone.** The reasoning in
> this ADR — every argument in it — is about phones, Android permissions and an
> app store, and none of it transfers to a dedicated device the user bought for
> this purpose. See "What this ADR does not cover" below, and
> [ADR-0004](0004-capability-sources.md) for the model that does.

## Context

The master plan requires an Android companion to be **designed now and
implemented later** (§17), with thirteen mandatory epics (§66) covering time
sync, GNSS assistance, notification relay, Find My Phone, firmware update and
backup.

Designing it later is not the risk. The risk is that between now and then the
firmware quietly acquires a dependency on it — a screen that asks whether a
phone is connected, a clock that treats phone time as authoritative, a mesh path
that routes through the handset. Each is individually reasonable and collectively
turns a standalone device into an accessory. By the time the companion is
actually built, undoing them means touching every app that learned to ask.

Three facts sharpen this:

- **The two boards differ in what a companion could even serve.** The Waveshare
  board has neither LoRa nor GNSS on it
  ([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md)). *(Amended: the rule that
  follows from this is about the **device**, not the board. A Waveshare with a
  node attached does have GNSS and can use assistance. So the companion sends
  assistance when the device's capability exchange says GNSS is available, and
  the capability set must be re-exchanged when it changes mid-session — not
  decided once from a board identity.)*
- **Several companion features may be impossible.** Whether a companion app can
  override silent mode, read notifications, or obtain redistributable GNSS
  assistance are open Android and licensing questions
  ([ANDROID_PERMISSIONS_AND_LIMITATIONS](../mobile/ANDROID_PERMISSIONS_AND_LIMITATIONS.md)).
  Firmware must not be built on any of them.
- **The link is a standing power cost**, paid whether or not the user is using a
  companion feature.

## Decision

**The phone companion may only ever improve a capability the watch already has
on its own. It may never provide one.**

Concretely, and testably:

1. Every phone-fed capability has a working non-phone path. The clock is
   correct from the RTC; GNSS acquires unaided *or from an attached node*; mesh
   messages travel watch to watch over LoRa *or via a node*. The phone shortens,
   sharpens or enriches — it does not enable. "Non-phone" is the requirement;
   "on-board" is not.
2. **No application code may query companion state.** Apps ask the owning service
   (`TimeService`, `LocationService`); provenance is that service's business. The
   only exceptions are the companion settings screen and diagnostics, which exist
   to show the link itself.
3. The companion is a `CompanionService` behind `ConnectivityService`, feeding
   existing services as an input source. It owns no domain of its own.
4. All companion input is untrusted: range-checked, expiry-checked, refusable.
5. Capability exchange happens before any payload, so the phone learns what the
   watch has rather than discovering it by having data ignored.
6. The link is fully disableable, and the watch remains fully useful with it off.

## What this ADR does not cover

**A Firefly node is not a companion, and the rule above does not apply to it.**
The line is not "on the board versus off it" — that line would forbid the
product. It is this:

| | Phone | Firefly node |
|---|---|---|
| What it is | a general-purpose device the user already owns | a dedicated device bought for this system |
| Who controls the software | Google, the OEM, an app store, a battery optimiser | this project |
| Why it might not deliver | a permission was refused, the OS killed the process, the vendor's power manager decided | it is out of range, or its battery is flat |
| What the user expects when it is gone | everything to keep working | the features they bought it for to be unavailable |
| Was that expectation ever accepted? | no | **yes — explicitly, by the owner, in OD-1** |

The last row is the whole argument. The owner has accepted that without a node
the device "is a watch, an audio device, and whatever the installed applications
make it" — losing whole applications is the stated, intended behaviour. Nobody
has accepted that for a phone, and the reasons a phone fails are ones this
project cannot fix or even observe.

Two rules from the decision above are **not** phone-specific and extend to the
node unchanged, because they are about trust and about layering rather than
about phones:

- **Rule 2 — no application code may query the provider.** An app asks
  `LocationService` for a position. Whether it came from an on-board GNSS, a
  node's GNSS or nothing at all is that service's business. This is the rule
  that makes the whole capability model checkable by review, and it gets
  *stronger* with a node in the picture, not weaker.
- **Rule 4 — all input from outside the device is untrusted.** Range-checked,
  expiry-checked, refusable. A node is closer to us than a phone is; it is not
  more trusted for it.

Rules 1, 3, 5 and 6 are about phones and stay about phones.

## Alternatives considered

**Companion as a first-class peer that may own capabilities.** Rejected. It is
what produces a watch that is worse alone, and every Android limitation in
§P1–P18 becomes a watch limitation. It also makes the Waveshare board's missing
GNSS a companion problem rather than a capability-model one.

> **Still rejected, and the last sentence turned out to be the important one.**
> Making the Waveshare board's missing GNSS a *capability-model* problem is
> exactly what ADR-0004 does — and because the model was built to answer it, a
> node providing GNSS is an ordinary case in it rather than a special one. The
> rejection stands for the phone. It never applied to a dedicated node.

**Defer the decision until the companion is actually built.** Rejected — this is
the failure mode the ADR exists to prevent. The coupling accumulates silently in
the months before anyone writes Android code, and it is invisible until it is
expensive.

**Let apps query companion state directly, for better UI.** Rejected. It is the
single change that would make the rest unenforceable: once one app knows about
the phone, "the companion is optional" stops being checkable by review. Where an
app genuinely needs to explain staleness, the owning service exposes *data age*
— which is meaningful with or without a phone.

**Require the companion for a defined subset (e.g. notifications only).**
Rejected for the first stage. It sounds narrow, but it makes phone-side
permission failures into watch features that visibly do not work, and P9 says we
do not yet know whether users will grant that permission at all.

## Consequences

**Easier.** Companion work is contained: it touches `CompanionService`,
`ConnectivityService` and one settings screen. Android limitations stay on the
Android side. Both boards run the same firmware regardless of what a phone can
offer. The watch is demonstrable with no phone in the room.

**Harder.** Some features are genuinely better with a required companion, and we
give that up. Notification relay in particular will feel second-class. Every
companion-fed capability needs a standalone path built and maintained even when
most users have a phone — that is real duplicated effort, accepted deliberately.

**Commits us to.** A review rule that is mechanically checkable: application code
must not reference companion state — and, per ADR-0004, must not reference node
state either. And to an honest UI — `Paired, disconnected` is the normal state
most of the time and must look boring, not broken.

**Testable.** Every acceptance test for a **phone**-fed capability must have a
phone-absent counterpart. If a feature cannot be tested with the phone switched
off, it has already violated this ADR.

This does not extend to the node, and applying it there would forbid the
product: a node-fed capability has no node-absent counterpart by design. The
node's equivalent obligation is different and stricter in its own way — every
node-fed capability must be testable **through** attach, detach and reattach
while an application is open, and all of it in the simulator, because the node
does not exist yet ([TASKS](../../TASKS.md) T-022, T-030).
