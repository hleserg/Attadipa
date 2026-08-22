# Android permissions and limitations

**Status: research agenda, not findings.**

Nothing in this file has been verified against current Android documentation in
this project. It is a list of the things that will decide whether each companion
feature is possible, difficult, or impossible — written down now so that no watch
feature is designed on the assumption that the phone side is easy.

The same discipline as the hardware documents applies: a plausible recollection
of how Android behaves is not evidence. Android's background-execution rules have
changed in most major releases, and answers that were correct three versions ago
are routinely wrong now. **Every claim below must be re-established against
current official documentation and the minimum supported API level before any of
it is built on.**

---

## 1. Why this file gates real firmware decisions

Two watch features have their difficulty set entirely by Android policy, not by
firmware:

- **Notification relay** determines whether the watch needs a notification store,
  an allowlist UI and a per-app icon strategy — or none of it.
- **Find My Phone** determines whether the watch can honestly show any feedback
  at all, which is why `COMPANION_PROTOCOL.md` §6 already forbids the watch from
  claiming the phone is ringing.

Designing either without the answers means building a UI for a capability the
platform may not grant.

## 2. Research agenda

Each row is a question to answer from official Android documentation, with the
minimum supported API level fixed first. The "why it matters" column is what
changes on the watch depending on the answer.

### Connectivity

| # | Question | Why it matters to firmware |
|---|---|---|
| P1 | What BLE connection intervals will Android grant a peripheral's request? | Sets the standing power cost of the link — a line item in the power budget |
| P2 | Which runtime permissions does BLE require at the target API level, and which are location-adjacent? | A permission users refuse is a feature that must degrade gracefully |
| P3 | What MTU can be relied on after negotiation? | Sets the assistance chunk size in COMPANION_PROTOCOL §7 |
| P4 | Can a bonded peripheral trigger reconnection when the phone is idle? | Decides whether the watch may ever expect an unprompted link |

### Background execution

| # | Question | Why it matters to firmware |
|---|---|---|
| P5 | What can a companion app do with the screen off, and for how long? | Decides whether "the phone will handle it" is ever a safe assumption |
| P6 | Is a foreground service required, and what notification must it show? | A permanent notification is a real user cost — it may make a feature not worth shipping |
| P7 | How do OEM battery optimisations affect all of the above? | The honest answer is likely "unpredictably, per manufacturer" — which is itself a design input |
| P8 | Does the Companion Device Manager API relax any of this? | It exists precisely for this class of app; it may change every answer above |

### Notification relay

| # | Question | Why it matters to firmware |
|---|---|---|
| P9 | What is required to read notifications — and is it a permission most users will actually grant? | If adoption is low, the allowlist UI and the store may not be worth the flash |
| P10 | Can notifications be dismissed from the watch, and does that require more? | Decides whether relay is one-way or two-way |
| P11 | What is available per notification — title, body, app identity, icon? | Sets the notification record's fields and its size bound |

### Find My Phone

| # | Question | Why it matters to firmware |
|---|---|---|
| P12 | Can an app override Do Not Disturb and silent mode to make a sound? | If not, the feature fails exactly when it is most needed, and the watch must say so |
| P13 | Can it raise volume, vibrate, and show UI over the lock screen? | Sets what "found" even means |
| P14 | Can any of it be acknowledged back to the watch? | Currently assumed **no** — hence "sent", never "ringing" |

### GNSS assistance

| # | Question | Why it matters to firmware |
|---|---|---|
| P15 | Which assistance mechanism does the **specific fitted module** officially support? | Master plan §18 is explicit: do not call an arbitrary downloaded file "A-GPS" |
| P16 | Is that data redistributable, and under what terms? | A licensing constraint can end the feature regardless of engineering |
| P17 | What is its validity period? | Drives the expiry logic the watch must own itself |
| P18 | Can a phone obtain it without a paid service? | Same |

P15–P18 are the ones to answer first. They are the only rows that can make a
planned feature legally or commercially impossible rather than merely hard, and
P15 additionally depends on which GNSS module is fitted — OPEN_QUESTIONS A2.

## 3. Standing constraint

Whatever the answers turn out to be, this does not move: **no Android limitation
may degrade the watch's standalone behaviour.** If notification relay is
impractical, the watch is a watch without notification relay. If assistance data
is unavailable, GNSS acquires unaided and takes longer.

"Unaided" assumes a receiver to be unaided *with*. On a device whose only GNSS is
in an attached Attadipa node, the fallback is the node's own acquisition rather
than the watch's. The rule holds at device level, which is the level it is about
— but it is not the same sentence as "the watch falls back to its own receiver",
and the difference matters when the node is also away.

The companion's failure modes must be contained inside the companion.
