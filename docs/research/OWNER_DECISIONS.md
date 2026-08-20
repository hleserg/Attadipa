# Owner Decisions

Product decisions made by the project owner, with the date they were made and
what they oblige. This file exists because of the rule in
[`../../CLAUDE.md`](../../CLAUDE.md): *a fact that lives only in a chat log does
not exist.* An architectural decision recorded only in conversation will be
silently re-litigated by whoever picks the project up next.

This is not the ADR log. An ADR records a decision *we* made and why we rejected
the alternatives. This records a decision that was **given to us** and is not
ours to overturn — the equivalent of a requirement, arriving after the
specification was written.

Format: what was decided · when · what it obliges · what it invalidates.

---

## OD-1 — There is a separate Firefly node, and the watch uses it

**Decided:** 2026-08-21.

**As stated:**

> «там будет отдельная нода с lora, GPS и esp32, часы будут подключаться к ней и
> использовать те же приложения, типа карты, компас, и проч, что и в lora часах.
> когда нода не подключена — будут часами, аудиоустройством и прочим зависит от
> установленных приложений которые мы ещё не написали. все возможности должны
> быть учтены на уровне ядра, а реализовывать будем уже позже.»

**In English:** a separate node carrying LoRa, GNSS and an ESP32 exists. The
watch connects to it and runs *the same applications* — maps, compass and the
rest — that it would run on a watch with its own LoRa. With no node connected
the device is a watch, an audio device, and whatever else the installed
applications make it. All of these possibilities must be **accounted for in the
core now**; the implementation comes later.

**What it obliges:**

1. A capability may be provided by something that is not on the board. The
   capability model may no longer assume the BSP is the only source.
2. A capability may **appear and disappear while an application is running**.
   Boot-time-static capability discovery is insufficient.
3. The same application binary must run against a local capability and a
   node-provided one without knowing the difference. This is the existing rule —
   *applications ask what the device can do, never which device it is* — under
   real load for the first time.
4. Applications not yet written must be installable, and an installed
   application may outlive the capability it was installed for.

**What it invalidates:** the claim in
[`../adr/0002-companion-is-optional.md`](../adr/0002-companion-is-optional.md)
that an external device may never *provide* a capability, only improve one. That
rule was written about a phone and is correct about a phone. It was stated too
broadly. See [ADR-0004](../adr/0004-capability-sources.md).

**What it does not do:** it establishes no hardware fact. No board, no
schematic, no part numbers exist for the node. Everything about the node's
hardware is UNKNOWN and lives in [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md) as such
— not in [HARDWARE_MATRIX.md](HARDWARE_MATRIX.md), which is for parts traced to
a source.

**Corroboration:** this is not a new direction. The specification already
requires it — §32 *DOCTOR / FIREFLY NODE* mandates that the architecture account
for a separate node, and lists "additional GNSS" among what it provides.

---

## OD-2 — MeshCore radio parameters are settings, not constants

**Decided:** 2026-08-21.

**As stated:** «Вот настройки для MashCore, но они не должны зашиваться в ядро,
это настройки» — *these are the MeshCore settings, but they must not be baked
into the core; they are settings.*

**Evidence supplied:** two screenshots of a live MeshCore node's exposed
parameters — the second one complete. Recorded here in full because it is the
only description of the node's data model that exists anywhere in this project,
and because what it *omits* turns out to matter more than what it contains.

The complete model, all fourteen entities:

| # | Parameter | Observed | Kind |
|---|---|---|---|
| 1 | Frequency | 868.731 MHz | setting — **regulated** |
| 2 | Bandwidth | 62.5 kHz | setting |
| 3 | Spreading factor | 7 | setting |
| 4 | TX power | 22 dBm | setting — **regulated** |
| 5 | Request rate limiter | 20.0 tokens | setting |
| 6 | Companion prefix | 04 | setting — identity |
| 7 | Node status | Online | **link state** |
| 8 | Last message delivery | Idle | operation state |
| 9 | Node count | *Unknown* | telemetry — three-valued |
| 10 | Battery voltage | 3.847 V | telemetry |
| 11 | Battery percentage | 70.58 % | telemetry — derived |
| 12 | Ch1 voltage | 3.80 V → 3.84 V | telemetry — observed changing between the two screenshots |
| 13 | Latitude | *(withheld)* | telemetry — position |
| 14 | Longitude | *(withheld)* | telemetry — position |

The position values are deliberately **not** recorded. They are a real location
and this repository is public.

### What the model gets right, and Firefly must copy

**`Node status` is a separate entity from every value it carries.** The vendor
model does not infer "the node is there" from "a number arrived". That is the
single most important thing in the table, and it is the distinction a naive
design collapses first.

**`Node count: Unknown` is a third value, not zero.** Even the vendor's own
integration has a field that is neither a number nor absent. The core needs the
same three-way distinction — *known* · *known to be none* · *not known* — and
the UI must never render the third as the second. "0 nodes nearby" and "we have
no idea how many nodes are nearby" are different sentences, and one of them is
a lie.

### What the model is missing, and Firefly must not copy

This is a fine inventory of what a node *has*. It is not sufficient as a core
data model, and the gaps are instructive because each one is a decision the
Firefly core has to make deliberately:

- **No timestamp on anything.** Latitude and longitude with no age are unusable
  for navigation. A coordinate that is four hours old and a coordinate from two
  seconds ago are the same two numbers here. Every datum crossing the link must
  carry its age.
- **No fix state for the position.** No satellite count, no HDOP, no fix/no-fix
  flag, no altitude. So there is no way to tell a *current GNSS fix* from a
  *last-known* or *manually configured* position — the exact collapse of "the
  provider is reachable" into "the provider has an answer" that the capability
  model has to keep apart.
- **No link quality.** No RSSI or SNR for the last received packet, so nothing
  can tell "connected" from "connected and about to drop".
- **No protocol or firmware version.** Nothing to negotiate against. Two
  independently updated devices with no version field is a compatibility
  problem waiting for its first firmware release.
- **No airtime or duty-cycle counter.** The two regulated settings in the table
  are bounded by rules that constrain *airtime*, and nothing here measures it.

None of this is a criticism of MeshCore, which is solving a different problem.
It is the argument for §32's requirement that the Doctor/Firefly application
protocol not be the MeshCore internals wearing a different name.

**What it obliges:**

1. No RF parameter may be a compile-time constant anywhere in `core/`. Frequency,
   bandwidth, spreading factor and TX power are runtime-settable, persisted
   values.
2. There must therefore be a settings subsystem in the core — typed values,
   validated ranges, defaults, persistence, factory reset — before there is a
   radio service that reads them.
3. Two of these settings are **legally bounded** (frequency, TX power). The core
   has to express "user-settable, but bounded by a regulatory profile" without
   the core knowing which region it is in. See A4.
4. A settings screen is a first-class part of the product, not a debug menu.
5. Every value that crosses the link carries its **age** and, where it is a
   measurement, its **validity** — because the reference model carries neither,
   and a position without those two fields cannot be navigated by.

**Open, arising directly from this:** 22 dBm is 158 mW. Whether that is lawful
at 868.731 MHz in the region of operation is exactly question **A4**, and this
screenshot makes it concrete rather than theoretical — the owner's existing node
is already transmitting at a power level whose legality this project has not
established. Firefly is not responsible for that node, but it must not ship a
default that assumes it.

---

## Still with the owner

Nothing here answers A1–A3, A5 or the compass question. Those remain in
[OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).
