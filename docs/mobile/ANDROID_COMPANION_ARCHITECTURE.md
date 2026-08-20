# Android companion — architecture

**Status: design only.** No Android code exists and none is planned for the
first stage. This document exists so that the watch firmware does not acquire
assumptions now that would have to be broken later. Per the master plan §17: the
architecture is settled now, the implementation is not.

Everything here is a design position, not a verified fact. Positions that depend
on Android platform behaviour are marked **NEEDS RESEARCH** and are listed in
[ANDROID_PERMISSIONS_AND_LIMITATIONS.md](ANDROID_PERMISSIONS_AND_LIMITATIONS.md).

---

## 1. The one decision that matters now

**The watch must be fully useful with no phone, ever.** Not degraded-but-working
— useful. The companion adds convenience, never capability.

This is a firmware constraint, not a phone-side design goal, and it is the
reason to write this document before writing any watch code:

- The clock is correct from the RTC. Phone time sync **improves** accuracy; it
  is not how the watch learns what time it is.
- GNSS acquires a fix unaided. A-GNSS **shortens** time-to-first-fix; it is not
  a prerequisite for a fix.
- Mesh messaging is watch-to-watch over LoRa. The phone is not in that path and
  must never become a required relay.

Anything that violates this is a design error however convenient it is. A
companion that becomes load-bearing turns a standalone device into an accessory.

## 2. Transport

**BLE GATT**, with the watch as peripheral and the phone as central.

| Alternative | Why not |
|---|---|
| Wi-Fi | The watch's radio budget cannot afford an associated Wi-Fi link as a normal state. Reserve it for bulk transfer and firmware update, on explicit user action. |
| Classic Bluetooth SPP | Not available on the ESP32-S3, which is BLE-only. **VERIFIED** from the SoC datasheet. |
| USB | Wired only. Useful for development and log extraction; not a companion transport. |
| LoRa | The mesh radio is not a phone link. Different band, different duty-cycle rules, and it would put phone traffic in the mesh's airtime. |

BLE is therefore not a preference — for a phone link on this SoC it is the only
wireless option. Wi-Fi remains available and is the right answer for firmware
images and log bundles, where a BLE link would take minutes to move what Wi-Fi
moves in seconds.

## 3. Layering on the watch side

The companion is a **service**, not a special case threaded through the system.

```
Apps  ──────────────┐
                    ↓
          CompanionService
                    │
        ┌───────────┼────────────┐
        ↓           ↓            ↓
   TimeService  LocationService  NotificationStore
        └───────────┼────────────┘
                    ↓
           ConnectivityService (BLE)
```

Three rules follow, and they are what actually protects the firmware:

1. **No app talks to the companion.** Apps ask `TimeService` for the time. Where
   that time came from is `TimeService`'s business. An app that knows whether a
   phone is connected has already coupled the UI to the transport.
2. **The companion is a source, not an owner.** It offers time, assistance data
   and notifications to services that already own those domains. It does not own
   the clock.
3. **Every companion input is untrusted.** It arrives over a radio link from a
   device the watch does not control. Range-check it, timestamp it, and be able
   to reject it. See [COMPANION_PROTOCOL.md](COMPANION_PROTOCOL.md) §5.

## 4. State model

The link has four states, and the UI must be able to render all four honestly:

| State | Meaning | What the user sees |
|---|---|---|
| `Unpaired` | no companion has ever been bonded | features that need a phone are absent, not broken |
| `Paired, disconnected` | bonded, phone not in range | last-sync age, not a spinner |
| `Connected` | link up, authenticated | normal |
| `Connected, degraded` | link up, a specific capability unavailable | the specific thing is unavailable, named |

`Paired, disconnected` is the normal state most of the time — the phone is in
another room. It must be visually boring. A companion status that looks like an
error whenever the user walks away trains people to ignore it.

## 5. Power position

The BLE link is a background cost the user did not explicitly ask for, so it
has to justify itself in the power budget:

- Connection interval is a **negotiated parameter with a power consequence**, not
  a default. It goes in the budget as a measured line item.
- The watch is the peripheral and therefore does not choose the interval on its
  own — it requests. **NEEDS RESEARCH**: what Android actually grants.
- No polling. Everything the phone sends is a notification or a write; the watch
  does not wake to ask.
- The link must be disableable entirely, and the watch must remain fully useful
  with it off — which is §1 restated as a power feature.

## 6. What this buys us later

If the three rules in §3 hold, adding the companion months from now touches
`CompanionService`, `ConnectivityService` and the settings screen. If they do
not hold, it touches every app that learned to ask about the phone.

That is the entire reason this file exists before the code does.

---

## Open

| # | Question | Status |
|---|---|---|
| MOB1 | What connection intervals does Android actually grant a peripheral? | NEEDS RESEARCH |
| MOB2 | Which GNSS assistance mechanism does the fitted module officially support? | NEEDS RESEARCH — see §18 of the master plan: do not call an arbitrary downloaded file "A-GPS" |
| MOB3 | Is BLE bonding sufficient, or is an application-layer pairing secret required? | design decision, see COMPANION_PROTOCOL §5 |
| MOB4 | Does the notification relay require an accessibility service on Android, and what does that cost the user? | NEEDS RESEARCH |
