# 0002 — The phone companion is optional, and the watch never depends on it

Status: proposed
Date: 2026-08-21

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
  board has neither LoRa nor GNSS
  ([HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md)), so a companion talking to
  it must not send GNSS assistance at all.
- **Several companion features may be impossible.** Whether a companion app can
  override silent mode, read notifications, or obtain redistributable GNSS
  assistance are open Android and licensing questions
  ([ANDROID_PERMISSIONS_AND_LIMITATIONS](../mobile/ANDROID_PERMISSIONS_AND_LIMITATIONS.md)).
  Firmware must not be built on any of them.
- **The link is a standing power cost**, paid whether or not the user is using a
  companion feature.

## Decision

**The companion may only ever improve a capability the watch already has on its
own. It may never provide one.**

Concretely, and testably:

1. Every companion-fed capability has a working non-companion path. The clock is
   correct from the RTC; GNSS acquires unaided; mesh messages travel watch to
   watch over LoRa. The phone shortens, sharpens or enriches — it does not enable.
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

## Alternatives considered

**Companion as a first-class peer that may own capabilities.** Rejected. It is
what produces a watch that is worse alone, and every Android limitation in
§P1–P18 becomes a watch limitation. It also makes the Waveshare board's missing
GNSS a companion problem rather than a capability-model one.

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
must not reference companion state. And to an honest UI — `Paired, disconnected`
is the normal state most of the time and must look boring, not broken.

**Testable.** Every acceptance test for a companion-fed capability must have a
companion-absent counterpart. If a feature cannot be tested with the phone
switched off, it has already violated this ADR.
