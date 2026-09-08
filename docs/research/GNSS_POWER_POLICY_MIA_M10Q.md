# GNSS power policy — MIA-M10Q

**Research only.** Nothing here changes production code, and issue
[#479](https://github.com/hleserg/Attadipa/issues/479) forbids it: *"any
implementation must be a separate executable issue after the need is proven."*
This document establishes which mechanism the future policy should rest on, and
says plainly which of its numbers were read and which were measured.

**Nothing in this document was measured.** Every current and every TTFF is a
vendor typical. **NOT EXECUTED — HARDWARE REQUIRED.**

## The answer, and why it is not the one the issue expected

#479 asks which of five modes should become the basis of a wearable power
policy, and treats option 3 — cutting the `BLDO1` rail — as the option whose
viability turns on the daughterboard's `V_BCKP` wiring. That framing put a
schematic fact on the critical path.

It is not on the critical path, and the reason is arithmetic:

| From → to | Module draw | Needs a wiring fact? |
| --- | --- | --- |
| continuous tracking → software standby | 12.9 mA → 46 µA | **no** |
| software standby → `BLDO1` off | 46 µA → 28 µA on `V_BCKP` | **yes** |

The second row's 28 µA is not on the 3.3 V rail at all: that rail is what gets
cut, and the figure is on `V_BCKP`, the cell's supply. The two rows are not
subtractable as rail current, which is a second reason the first step is the
whole of the decision.

The first step is **99.86 % of the saving that is available at all**, and it
needs no backup cell, no rail sequencing and no reading of sheet S4. The second
step is worth 18 µA on the module — before any board-side term, all of which are
`UNKNOWN` — and it costs the whole `V_BCKP` question, a supply-sequencing rule,
and a PIO-isolation hazard this board is not built for.

So the recommended contract is **software standby mode**, entered with
`UBX-RXM-PMREQ` and left on a UART RX edge. It is not one of the issue's five
options as written; it sits between option 2 and option 3 and dominates both.

The `V_BCKP` question came off the critical path, and then closed anyway while
this document was in review: the daughterboard sheet's own vector segments put
ball `J5` on a net named `VRTC`, put the `MS412FE` on that net, and route
`VDD3V3` into it through `D1` (`1N4148`) and `R3` (`1K`) — D23, and
[VERIFIED_FACTS](VERIFIED_FACTS.md) *"The `MS412FE` does reach `V_BCKP`"*. The
answer is **yes to both halves**, and it changes no recommendation here. It was
worth 18 µA before it was answered and it is worth 18 µA now.

## What the receiver actually is

The fitted part is a `MIA-M10Q` running ROM SPG 5.10, protocol 34.10, read off
the bench unit on 2026-09-05 —
[TWATCH_GNSS_READOFF_2026-09-05](TWATCH_GNSS_READOFF_2026-09-05.md). Its supply
is `BLDO1` at 3300 mV, enable net `GPS_LDO` on FPC pin 3.

**Three module supply pins, one rail.** The data sheet's pin table gives `J4`
`V_IO` ("IO voltage supply"), `J5` `V_BCKP` ("Backup voltage supply. Leave open
if no external backup supply.") and `VCC`. Only one supply net crosses the FPC,
so the board must be Table 40 option 1 — *"3.3 V design where VCC and V_IO are
connected together … VIO_SEL pin left open."* **That is an inference from the
connector, not a reading of S4**, and it is the one structural assumption this
document makes. It is falsifiable: a local regulator on the daughterboard would
break it.

## The two backup modes are not variants of one thing

Both come from the integration manual, UBX-21028173 R05 §3.6.3, and the
difference is exactly the difference between option 2/4 and option 3.

**Software standby (§3.6.3.2).** *"Software standby mode is entered using the
UBX-RXM-PMREQ message. V_IO and VCC must be supplied, however VCC supply is
internally disabled to save power. The V_IO supply maintains the battery-backed
RAM (BBR), RTC, and PIOs."*

The rail stays up, so **BBR, RTC and orbit data are retained from `V_IO`** and
`V_BCKP` is irrelevant to them. Four constraints come with it, each from the
same section:

1. *"Entering the software standby mode clears the RAM memory including the
   receiver configuration."* Every RAM-layer setting is gone on wake and the
   receiver comes back on its default configuration. This is a feature here,
   not a defect — see the observability section.
2. *"The 'force' flag must be set in UBX-RXM-PMREQ to enter software standby
   mode."* `flags` bit 2, alongside `backup` bit 1.
3. *"The possible wake-up sources are UART RX and/or EXTINT pin."* `EXTINT`
   (ball `A6`) does not cross the FPC — the daughterboard net list read for
   [#312](https://github.com/hleserg/Attadipa/issues/312) lists six nets on `J1`
   and `EXTINT` is not among them — so on this board **`wakeupSources.uartrx`
   is the only wake that exists.**
4. *"As V_IO is supplied, the PIOs can be driven by an external host processor.
   No buffers are required for isolating the PIOs."*

**Hardware backup (§3.6.3.1).** *"V_BCKP must be supplied to maintain the backup
domain (BBR and RTC) … As V_IO is not supplied, the PIOs cannot be driven by an
external host processor. If driving of the PIOs cannot be avoided, buffers are
required for isolating the PIOs."*

And §4.1.2 states the failure directly: *"A power interruption at V_IO will
erase the battery-backed RAM (BBR) unless there is an external supply connected
to V_BCKP."*

**This is the hazard that makes option 3 more than a power question.** The FPC
is thirteen direct nets with no buffers. Today it is harmless because the
firmware never drives the module: `firmware/main/local_gnss.cpp:413` — "err = uart_set_pin(kPort, UART_PIN_NO_CHANGE, kRxPin," —
it leaves the transmitter unrouted. **Every option but continuous tracking routes
TX**, and from that moment ESP32 GPIO 42 drives the module's `H1` `RXD`. A
`BLDO1` off with that pin still driven is the case the manual says needs a
buffer this board does not have. Any future rail-off must release the pin first,
and that ordering is part of the contract, not an implementation detail.

## `UBX-CFG-RST` cannot report what it did

The issue's option 2 — and the Zephyr path it cites — is `UBX-CFG-RST` with
controlled GNSS stop `0x08` / start `0x09`. The M10 SPG 5.10 interface
description, UBX-21035062 R03 §3.10.2.1, says of that message:

> *"Do not expect this message to be acknowledged by the receiver.*
> *• Newer FW version will not acknowledge this message at all.*
> *• Older FW version will acknowledge this message but the acknowledge may not
> be sent completely before the receiver is reset."*

There is no `ACK`, and there is no status message for the stopped state. The
only postcondition is **NMEA stopping**, which is a negative observable: it is
indistinguishable from a dead module, a wrong baud rate, or a rail that never
came up. Publishing `Stopped` on it is exactly the class of claim
`docs/adr/0011-gnss-integrity.md` exists to forbid, and the issue names the same
risk: *"отсутствие ACK у `CFG-RST` не даёт права публиковать `Stopped/Running`
после одного write."*

Software standby has the positive observable that `CFG-RST` lacks.
`UBX-MON-RXR` (`0x0a 0x21`) — *"The receiver ready message is sent when the
receiver changes from or to backup mode"* — carries `flags` bit 0 `awake`,
*"not in backup mode"*.

Its default output rate on UART1 is **0** (`CFG-MSGOUT-UBX_MON_RXR_UART1`
`0x20910188`), so it must be enabled. That enable is a **RAM-layer**
`CFG-VALSET`, which is permitted on the bench; no configuration save is needed
and none may be used.

The RAM clear on entering standby then resolves the round trip cleanly:

- **entering** — enable `MON-RXR` on the RAM layer, send `PMREQ`, read
  `awake = 0` — **expected, not established.** The enable is a RAM item, and
  the data sheet's Table 11 turns `TXD` (`G1`) from an output into an input
  pull-up in software standby mode, so a report on the entry edge has to leave
  before the pin flips. It is the first thing the bench records.
- **leaving** — a byte on the UART wakes it; it restarts on its **default**
  configuration, which is the configuration that emits NMEA today with no setup
  at all. The observable is NMEA *resuming*, which is positive content, and it
  costs nothing because it is the default.

The `MON-RXR` enable is re-sent once per cycle. That is the correct price: it
keeps the whole contract on the RAM layer and never touches BBR or Flash.

## The contract table

Columns are the ones #479 requires. **Every current and TTFF cell is a vendor
typical, not a measurement.** Currents are data sheet UBX-22015849 R08 Table 16
(3.0 V supply) and Table 18, `GPS+GAL+BDS B1I` — the default column. Whether the
bench unit runs the default constellation set is **UNKNOWN**; it was not read
off. TTFF is Table 2, same column.

| Requested | Command / rail action | Observable postcondition | Retained data | Recovery | Current | TTFF |
| --- | --- | --- | --- | --- | --- | --- |
| **Tracking** (today) | `BLDO1` on; nothing sent | NMEA frames arrive | everything, receiver never stops | — | NOT MEASURED — typ. 10.5 mA `VCC` + 2.4 mA `V_IO` | — |
| **Acquiring** | `BLDO1` on from off | NMEA arrives, no fix yet | — | — | NOT MEASURED — typ. 12.5 mA + 2.4 mA | NOT MEASURED — typ. 27 s cold |
| **Cyclic tracking** (option 4) | `CFG-PM-OPERATEMODE`, rail up; an optimisation *inside* Tracking, not a stop | NMEA continues at the configured rate | everything, while it stays in Tracking or POT; RAM cleared only if it drops to "Inactive for search" past the acquisition timeout | none — it never stopped | NOT MEASURED — typ. 5.5 mA `VCC` + 2.1 mA `V_IO` | — |
| **Engine stop** (option 2) | `UBX-CFG-RST` `0x08`, rail up | **none** — unacknowledged, no status message; NMEA merely stops | RAM and BBR both kept (MAX-M10S integration manual, per #479) | `0x09` start, also unacknowledged | UNKNOWN — no Table 18 row; above standby | UNKNOWN |
| **Standby** (**recommended**) | `RXM-PMREQ` `backup`+`force`, `wakeupSources.uartrx`, rail up | NMEA stops; `MON-RXR` `awake = 0` too if the enable survives to emission — documented, unverified | **BBR, RTC, orbit data — from `V_IO`.** RAM configuration **cleared** | a byte on the UART; NMEA resumes on the default configuration | NOT MEASURED — typ. 46 µA `V_IO` + 120 nA `VCC` | NOT MEASURED — typ. 1 s hot, while orbit data is valid |
| **Rail off** (option 3) | `PMREQ` standby, **release GPIO 42**, then `BLDO1` off | none from the module — it is unpowered | BBR, RTC and orbit data, **from the `MS412FE` on `V_BCKP`** — for as long as the cell holds, which is `UNKNOWN` | `BLDO1` on; hot while the cell held, cold once it did not | NOT MEASURED — typ. 28 µA on `V_BCKP`, plus `UNKNOWN` board-side terms | NOT MEASURED — typ. 1 s hot while the cell holds |

Three cells deserve their reasoning in words rather than a footnote.

**Cyclic tracking is in the issue's scope, and it answers a different
question.** The integration manual §3.6.2 puts PSMCT inside the Tracking state:
the receiver *"does not shut down completely between fixes, but uses low-power
tracking instead"*, and in the POT state it *"continues to output position fixes
according to the `CFG-RATE-*`"*. So it makes tracking cheaper — 7.6 mA against
12.9 mA typical — without stopping the engine, which means it has no stop/start
edge to observe and cannot satisfy `engine_observed`. It is orthogonal to the
contract above and composes with it rather than competing with it.

One consequence of that composition is a contract fact rather than a footnote.
PSM is enabled with `CFG-PM-OPERATEMODE` and configured through the `CFG-PM`
group, so on the RAM layer — the only layer this bench may write — **every
standby entry clears it**, and a policy that alternates standby with cyclic
tracking must re-send the whole `CFG-PM` group on every wake, alongside the
`MON-RXR` enable. The manual's own answer to that (*"store the configuration in
the BBR memory to maintain the settings"*) is a layer the bench rules forbid.

**"while orbit data is valid" is not indefinite.** The integration manual §4.1.3:
*"the GNSS satellite ephemeris data is typically valid for up to 4 hours for hot
starts."* A standby longer than that is a warm start, not a hot one, and a
policy that promises 1 s after an overnight hold would be lying.

**The board-side terms of the rail-off row are not small print.** Cutting
`BLDO1` also removes whatever the daughterboard draws from `VDD3V3` in standby —
the `MS412FE` charge path and the active-antenna LNA among them — and changes
the AXP2101's own consumption. None of those are established. The 18 µA figure
is the module alone, and it is the only part of that row anybody has a number
for.

## The seven states #479 asks to keep apart

They are separate facts, and conflating any two is how a receiver ends up
reporting a position it does not have.

| | What it is | How it is known | Can it be wrong alone? |
| --- | --- | --- | --- |
| `rail_on` | `BLDO1` enabled | AXP2101 register read-back | yes — a register write that did not take |
| `uart_open` | this end's driver installed and pins routed | ESP-IDF return codes | yes — and TX is *deliberately not* routed today |
| `engine_requested` | the policy wants a state | internal | always true by construction |
| `command_transmitted` | the bytes left this end | the UART write's byte count | **yes, and this is the one Zephyr drops** |
| `engine_observed` | the receiver reports the state | `MON-RXR`, or NMEA resuming | yes — the whole reason to prefer `PMREQ` |
| `fix_state` / `fix_age` | what the last solution was and when | NMEA content and the local clock | yes — a stale fix is not a fix |
| BBR / RTC / orbit retention | what survived the last off period | **not directly observable**; inferred from TTFF | yes — and it is the one nothing can report |

The last row is the honest limit of the whole contract. No message says "your
ephemeris survived." It is inferred from how fast the next fix arrives, which
means a policy that *claims* a hot start is claiming something it cannot check.
The model may only ever record what it requested and what it observed.

`command_transmitted` is where the Zephyr implementation the issue cites is said
to fail: #479 reports that `ubx_m10_start()` and `ubx_m10_stop()` ignore the
return value of `u_blox_iface_msg_payload_send()` and can report success for a
command that was never transmitted. **That claim is the issue's, read from
`drivers/gnss/u_blox/gnss_u_blox_m10.c`; it was not re-read here.** It is
recorded because it names the exact column above, not because this document
verified it.

## Two document numbers in #479 are wrong

Recorded because both were followed and both cost a fetch.

- The issue calls **UBX-21035062** the *"Official MIA-M10Q data sheet"*. It is
  the **M10 SPG 5.10 interface description** — the page footer of the local copy
  reads `UBX-21035062 - R03 … u-blox M10 SPG 5.10 - Interface description`. The
  data sheet is **UBX-22015849** (R08), and the **integration manual** — the
  document that actually answers this issue — is **UBX-21028173** (R05).
  Neither is cited in #479.
- The issue cites the **SPG 5.00** interface description, UBX-20053845. The
  fitted module runs **SPG 5.10**, per the 2026-09-05 read-off, and every
  protocol quotation here is from the 5.10 document.

The `UBX-CFG-RST` semantics the issue states are unaffected by either
correction.

## What is still unknown, and what it is worth

1. **How long the `MS412FE` actually holds `V_BCKP`, and whether `VDD3V3` less
   the `1N4148` drop across `1 K` charges it at all.** The topology is now read
   off the sheet (D23); the cell's capacity, its charge window and its state of
   charge after however long this unit sat are not, because **no `MS412FE` data
   sheet has been read**. This is what bounds the rail-off row's hot start, and
   it is the reason that row still says `UNKNOWN` where it matters. Cutting
   `BLDO1` also cuts the charge path, so a duty cycle that lives in rail-off
   never recharges what it is spending.
2. **Which constellations the bench unit is configured for.** Decides which
   column of Table 16 and Table 2 applies. One `CFG-VALGET` read.
3. **Every current and every TTFF on this board.** All typicals above are the
   vendor's, at 25 °C, with an antenna that is not this one.
4. **Whether `MON-RXR` is emitted on either edge.** On the wake edge the RAM
   clear has removed its enable. On the entry edge the enable is still live, but
   `TXD` becomes an input pull-up in standby (data sheet Table 11), so the
   report has to leave before the pin flips. Both are expectations, not facts.
   The contract above depends on neither — NMEA stopping and resuming brackets
   the hold either way — but a `MON-RXR` that does arrive at entry is the
   positive report `CFG-RST` can never give, so it is the first thing to record.

## Reuse verdict

`REJECT` for the mechanism, `INSPIRE ARCHITECTURE` for the shape. See
[REUSE_LEDGER.md](REUSE_LEDGER.md).

#479 proposed `ADAPT`. The research came out differently for one reason that was
not visible when the issue was written: `UBX-CFG-RST` controlled stop has **no
acknowledgement and no status message**, so it cannot satisfy `engine_observed`,
and `RXM-PMREQ` both saves far more power and reports what it did. Adapting an
implementation of the weaker mechanism would have carried its unobservability
into this codebase.

What Zephyr got right is the **separation** — `gnss_stop()`/`gnss_start()` as
receiver-engine operations distinct from device suspend and power removal. That
distinction is exactly the `rail_on` / `engine_requested` / `engine_observed`
split above, and it is worth keeping. The code is not.

## Does this need an executable issue?

**Yes, one, and not yet.** #479 requires this document to say so rather than to
open it.

Its scope, if opened, is bounded by what is above:

- route the GNSS UART TX, which `firmware/main/local_gnss.cpp:411` —
  "        // RX only. `UART_PIN_NO_CHANGE` for TX leaves this end's transmitter" —
  currently and deliberately does not;
- add the standby round trip: RAM-layer `MON-RXR` enable, `PMREQ`
  `backup`+`force`+`uartrx`, wake by byte, resume on defaults;
- record `command_transmitted` from the write's byte count and
  `engine_observed` from what came back — never from the request;
- map the state onto `core/include/attadipa/core/gnss_power.h`, where
  `GnssState` has no member for it: `:33` —
  "    Backup,     // powered down, RTC and ephemeris retained. Hardware-gated" —
  describes hardware backup, and software standby is rail-on with the engine
  asleep. **Naming that is the executable issue's first job, not this one's.**

It should not open until there is a reason to spend the battery figure — the
bench measurement of what the watch idles at with the rail up. 12.85 mA is worth
chasing against a device that idles in milliamps; the 18 µA below it is not,
against anything.

The `SupportState` for `Backup` in `gnss_power.h` stays `Unknown`, and it is
still correct that it does — D23 answers the *wiring*, and `Supported` is a
claim about retention, which only a bench hold-and-restart can make. What
changes is that **the decision no longer waits on it**: `Unknown` buys a cold
start, and the recommended mechanism never needs one.
