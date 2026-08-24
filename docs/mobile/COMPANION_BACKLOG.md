# Companion backlog

The mandatory mobile epics from master plan §66. At this stage these are
**predominantly DESIGN and RESEARCH**, as the plan requires — no Android
implementation is scheduled.

Nothing here is scheduled against a date. Ordering reflects dependency, and the
gate column says what must be true before the epic can start.

| # | Epic | Kind | Gate |
|---|---|---|---|
| M-01 | Android Companion foundation | DESIGN | [ANDROID_COMPANION_ARCHITECTURE](ANDROID_COMPANION_ARCHITECTURE.md) accepted as an ADR |
| M-02 | Pairing / authentication | DESIGN | MOB3 decided — bonding alone, or an application-layer secret |
| M-03 | Time synchronization | DESIGN | `TimeService` exists and owns the clock; §5 trust rules settled |
| M-04 | Timezone synchronization | DESIGN | M-03; DST rule representation chosen |
| M-05 | A-GNSS assistance | RESEARCH | **P15–P18** and OPEN_QUESTIONS A2. A2 is answered for the radio and *recalled* for the GNSS — MIA-M10Q, from the owner rather than from the order listing, which is silent on it — so which module is fitted is still unread, and MIA-M10Q against LS550G decides the assistance mechanism this row depends on |
| M-06 | Find My Phone | RESEARCH | P12–P14. May prove to be limited by Android policy rather than by design |
| M-07 | Notification relay | RESEARCH | P9–P11 |
| M-08 | Notification app allowlist | DESIGN | M-07 viable |
| M-09 | Incoming call display | DESIGN | M-07 viable; distinct path — time-critical and cancellable |
| M-10 | Companion settings | DESIGN | M-01; the watch stays authoritative over its own configuration |
| M-11 | Diagnostic log transfer | DESIGN | logging subsystem exists and has a defined on-device format |
| M-12 | Firmware update | DESIGN | partition layout ADR; image signing decided; **irreversible-operation rules apply** (master plan §50) |
| M-13 | Backup / restore | DESIGN | what constitutes device state is defined — including what must *not* leave the watch |

## Notes that change the shape of the work

**M-05 is the most likely to die.** It depends on which GNSS module a physical
board turns out to carry, on that vendor's official assistance mechanism, and on
whether the data may be redistributed. Any of the three can end it. Do not build
UI for it before P15 is answered.

**M-06 may only ever be half a feature.** If Android will not let a companion
override silent mode, Find My Phone fails precisely when the phone is on silent —
the case people actually need. The watch-side contract already assumes the worst
and reports only "sent".

**M-12 and M-13 carry the sharpest safety edges.** Firmware update is an
irreversible operation on a device someone is wearing; backup/restore moves
personal data off the watch. Both need an explicit ADR on what may leave the
device and what a failed operation leaves behind, before either has code.

**M-03 and M-04 are separable and should stay separate.** A correction to the
clock and a change of timezone are different events with different user
consequences. Merging them produces a watch that silently shifts by an hour when
someone lands.
