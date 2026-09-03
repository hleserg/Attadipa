# 0016 — Power, PMU rails and sleep have one owner, and consumers hold leases

Status: **accepted**
Date: 2026-09-01
Constrains: [ADR-0015](0015-transport-session-ownership.md) §1

## Context

Attadipa's firmware today has exactly one function that may put the SoC to
sleep and exactly one that may write an AXP2101 rail. Both are correct, and
both are correct only because there is one consumer. That stopped being true
when the MeshCore BLE transport shipped, and it will stop being true again when
GNSS arrives.

The evidence is in [POWER_OWNERSHIP.md](../research/POWER_OWNERSHIP.md),
researched under [#292](https://github.com/hleserg/Attadipa/issues/292). Four
findings drive this decision, all read in this tree or at a pinned upstream
revision rather than reasoned about:

1. **The Mesh consumer has no power opinion at all.** `meshcore_ble.cpp`
   contains no occurrence of sleep, power or any PM lock. The firmware will
   enter Light-sleep with a connection up and a frame possibly in flight,
   because nothing tells it not to; whether NimBLE survives that on this board
   is **NOT EXECUTED — HARDWARE REQUIRED**. The boundary this ADR draws does not
   yet exist to be adjudicated, which is the reason to draw it now.

2. **The armed wake plan is never reconciled with hardware.** The sleep path
   arms a GPIO wake source and disarms only the timer;
   `esp_sleep_disable_wakeup_source` appears once in the whole tree. The
   configuration therefore persists across cycles, harmlessly while every entry
   arms the same plan. `wake_plan_is_legal()` validates an intent that nothing
   checks against what the SoC actually holds — which is the union of every plan
   ever armed. Two early-return failure paths leave sources armed with no sleep
   entered and no rollback.

3. **Rails carry hazards that only a board-side table can hold.** D13 resolved
   on 2026-08-28: ALDO2 is not a supply but a 10 K pull-up holding `DSI_PWR_EN`
   high, so switching off what reads as a free 3.3 V rail blanks the display
   through a route that looks like a wiring fault. Six other LDOs are on from
   the factory and feed nothing. A generic PMU library exposes all of them as
   equally switchable.

4. **Upstream has already paid for each of these.** Meshtastic PR #11650: a
   transport resurrected by its own callback inside an in-progress shutdown.
   Offband PR #1015: a capability probe driving a shared rail low and leaving a
   radio supply floating. `agrucza/uhrwerk-rs` `1986e3f`, corrected by
   `11437824e0`, on this same board family: a codec teardown issued after its
   clocks had already stopped, and then a teardown whose postcondition was never
   read back.

## Decision

**1. One owner.** A single board-side power owner is the only code permitted to
write an AXP2101 enable or voltage register, to turn the AMOLED off or on, to
arm or disarm an ESP32 wake source, to enter Light or Deep sleep, or to publish
hardware and service `Availability`. Every other subsystem — UI, BLE/Mesh,
USB watch-control, any future GNSS or IMU provider — declares what it needs and
never acts.

This is enforced mechanically, not by review: sleep entry, `esp_sleep_enable_*`
and writes to AXP2101 registers `0x80`, `0x90`, `0x82` and `0x92`–`0x95` must
appear in exactly one translation unit, checked in CI.

**2. Consumers hold leases, not calls.** A lease is a reference-counted entry in
a fixed-capacity array — no heap, no new task — naming a resource bitmask and a
deadline. Three invariants, taken from Zephyr v4.4.2 and verified at
`671f64aa7992`: a refused acquire grants nothing and restores the count; a
release below zero is a reported error, never a wrap; a lease past its deadline
is reported and **not** silently reclaimed, because a consumer believing it
holds hardware it does not is the Meshtastic failure with its polarity reversed.

The lease table takes no lock, and a consumer on another task does not get one.
`sleep()` reads the held set once and then holds hardware for as long as the
sleep lasts, so a lease acquired from a second task inside that window is a
lease the sleeper never saw, and a mutex around the table closes the table
rather than the window. **A consumer that does not run on the owner's task
declares through a snapshot the owner's task already reads, and the owner's
task records the declaration.** #367 item 7 is the first one:
`core/include/attadipa/core/node_link_lease.h:109` — "class NodeLinkLease {" —
takes the BLE transport's phase, published behind the transport's own critical
section, and reconciles the lease immediately before `sleep()` reads it. No
ESP-IDF primitive enters `core/`.

Be precise about what that buys, because it is narrower than "the window is
closed". The lease table is now written and read on one task, so `held()` cannot
miss a lease that exists — that race is gone by construction. The declaration
window is not: a phase published after the sleeper read the snapshot and during
`esp_light_sleep_start()` is still a declaration the sleeper never saw, the same
width as before with the snapshot carrying it instead of the table. Closing
*that* needs the sleep itself to be refusable by the transport, which
`core/include/attadipa/core/power_owner.h:330` — "// does not yet have and which nothing in the current firmware needs, because"
— still records as absent. It is inert while no plan gates a domain a cross-task
consumer declares; **the first plan that does must close this window before it
ships**, not after.

**3. A transition is an ordered transaction with a journal.** Prepare, validate,
suspend consumers in dependency order *recording each success*, apply the rail
plan, sleep, classify, resume exactly the recorded consumers in exact reverse
order, publish. Failure at any step un-does exactly the steps that succeeded,
from the record rather than by re-deriving them.

A device's own low-power command is issued while its clock and bus are still up,
and its postcondition is read back. "Every shutdown callback was called" is not
a postcondition. This is why rail gating comes after consumer suspension and
never before.

**4. A failed rollback publishes `Failed`, never `Ready`.** Unpowered or
unknown-state hardware is never reported Active, and every lease depending on it
is refused until a successful re-initialisation. An honest `Failed` is what lets
the layer above decide to reboot; a hopeful `Ready` is how a watch shows a stale
screen and answers nothing.

The re-initialisation is the owner's own, and it is a retry rather than a hope:
each failed unwind step records *which* step it was, and the next `sleep()`
re-issues exactly those before it will do anything else. It clears `Failed` only
when every one of them reports its postcondition, and refuses again otherwise.
Without that the flag has no consumer — one failed `resume(Display)` leaves the
panel dark and every later sleep refused before it reaches hardware, so nothing
ever asks the display to come back and the honest `Failed` above becomes a watch
that is dark until it is reset.

**5. `core::PowerState` stays where it is and gains nothing.** It remains the
product vocabulary — states, legal transitions, the wake-source whitelist,
provenance — host-buildable with no ESP-IDF. The owner translates it into GPIO
numbers, `esp_pm_lock` handles and rails. The rail graph is board data and lives
with the board: no `#ifdef BOARD_X` in `core/` or `apps/`.

**6. Wake causes are read from the bitmap.** `esp_sleep_get_wakeup_causes()`,
which exists at the pinned ESP-IDF v5.5.5, replaces the single-cause API whose
own header warns that simultaneous sources may be lost. Re-reading a pin is a
corroborating signal, never the classifier.

**7. Deep-sleep is a reboot boundary, not a resume.** It does not share the
resume path, and it is out of scope for the first implementation: the shipping
firmware has never entered it, and adding an untested reboot boundary alongside
a new owner is two risks in one change.

## Consequences

The first implementation changes no observable behaviour except wake
classification, which becomes correct where it was previously coincidental. That
is deliberate: the seam has to exist before any power *policy* can be judged,
and the measurements a policy would need — current per state (H1), whether the
AXP2101 can measure current on these boards at all (H2), usable wake sources and
their cost (H5), AMOLED brightness against power (H6), and what the six idle
rails are costing — are all **UNKNOWN** or **NOT EXECUTED — HARDWARE REQUIRED**.

Gating a rail to save power is therefore explicitly *not* authorised by this
ADR. It is authorised by a measurement, applied through the owner this ADR
creates.

The cost is a layer of indirection between a consumer and hardware it used to
be able to touch, and one more thing to get right in a failure path. The
alternative was assessed and rejected: three upstream projects have shipped the
bug this prevents, one of them on this same board.

XPowersLib is rejected for now on the ground that its API shape — every rail
independently switchable by whoever holds the object — contradicts invariant 1,
not on the Arduino or code-size grounds originally supposed, both of which the
audit disproved. If a battery-state consumer needs its charger and gauge
coverage, it returns as a `WRAP` behind this owner, at a pinned revision.
