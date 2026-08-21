# 0003 — The part is a `Radio`. Whether it can do LoRa is a fact about it

Status: **accepted**
Date: 2026-08-21

Reserved as 0003 since the first day for "the radio abstraction across five
chips", and deferred because it was blocked on reading MeshCore. MeshCore has
now been read, and the reading changed the question rather than answering it.

## Context

Final §75 item **C**, and final §5. This repository said, in five places
including `CLAUDE.md`:

> the T-Watch ships with **one of five LoRa chips** — SX1262, SX1280, CC1101,
> LR1121, SI4432

Two of those five are not LoRa transceivers. `CC1101` is a Texas Instruments
sub-GHz FSK part; `Si4432` is a Silicon Labs FSK part. Neither has a LoRa
modulator, and no amount of firmware adds one — LoRa is a proprietary chirp
spread-spectrum physical layer implemented in silicon, licensed from Semtech.

The error was not cosmetic. It was load-bearing in exactly the way final §5
warns about:

```cpp
Capability::Lora == true   // because a radio is present
```

A T-Watch fitted with a CC1101 would have advertised mesh messaging, offered the
Mesh application, accepted a message, and delivered nothing — with no error
anywhere, because every layer believed the radio was fine. It *is* fine. It is
simply not speaking the same physical layer as anything it is trying to reach.

There is a second failure hiding behind the same word. `SX1280` **does** do
LoRa, at 2.4 GHz only. A 2.4 GHz LoRa device and an 868 MHz LoRa device are
both "LoRa" and cannot hear each other at all. `has(Lora)` gave the same answer
for a watch that can reach a village and a watch that can reach the next room.

## Evidence

Gathered from source on disk at pinned revisions, because the primary
datasheets could not be retrieved — see *Evidence quality* below.

**RadioLib 7.7.1** (`510e00c`, 2026-08-13) — the driver library MeshCore uses.
The LoRa-only API is `setSpreadingFactor()`. Which modules define it is a
mechanical question with a mechanical answer:

```
$ grep -rl "int16_t setSpreadingFactor" src/modules/
  LLCC68  LR11x0  LR2021  SX126x  SX1272  SX1273  SX1277  SX1278  SX128x
```

`CC1101` and `Si443x` are absent, and `CC1101` is a `PhysicalLayer` subclass
whose modulation API is `setOOK()`. RadioLib's own module list agrees:
*"CC1101 **FSK** radio module"*, *"Si443x series **FSK** modules (Si4430,
Si4431, Si4432)"*, against *"SX126x series **LoRa** modules"*.

**MeshCore** (`d929643`, 2026-08-14). Which radios it can actually drive is
also mechanical — it is the set of `RADIO_CLASS` values across all 87 board
variants:

```
CustomLR1110  CustomLR2021  CustomSTM32WLx  CustomSX1262  CustomSX1268  CustomSX1276
```

and the root `platformio.ini` line 35:

```
-D RADIOLIB_EXCLUDE_CC1101=1
```

CC1101 is not merely unused upstream — it is compiled out.

**There is no T-Watch variant in MeshCore.** 87 variants, several of them
LilyGO (`lilygo_t3s3`, `lilygo_tdeck`, `lilygo_tlora_c6`, `lilygo_techo_lite`),
and not one T-Watch. Whatever the fitted radio, bringing MeshCore up on this
watch is new work rather than selecting an existing target.

### The compatibility matrix

| Chip | LoRa modulator | Bands the driver permits | TX ceiling the driver permits | MeshCore at `d929643` |
|---|---|---|---|---|
| **SX1262** | **yes** | 150 – 960 MHz | −9 … +22 dBm | **supported** — `CustomSX1262Wrapper`, the most common variant upstream |
| **SX1280** | yes, **2.4 GHz only** | 2400 – 2500 MHz | −18 … +13 dBm | **absent** — no wrapper, no variant, no mention |
| **LR1121** | yes | 150 – 960 · 1900 – 2200 · 2400 – 2500 MHz | sub-GHz −9 … +22 (HP) / −17 … +14 (LP); 2.4 GHz −18 … +13 | **not as such.** `CustomLR1110Wrapper` exists, `LR11x0Reset.h` names the LR1121 as family, and RadioLib's `LR1121` derives from `LR1120`. Plausible with work; not a supported target today |
| **CC1101** | **no** — FSK/GFSK/MSK/ASK/OOK | 300 – 348 · 387 – 464 · 779 – 928 MHz | ≤ +10 dBm | **excluded at build time** |
| **Si4432** | **no** — FSK/OOK | 240 – 930 MHz | −1 … +20 dBm | **absent** |

So of the five: **one** works today, **one** is plausible with driver work,
**one** would form a separate 2.4 GHz network that no sub-GHz Firefly can hear,
and **two** cannot join a LoRa mesh in any configuration.

### Evidence quality — read this before quoting the table

Everything above is **PARTIAL**, not VERIFIED, and the distinction matters here
more than usual because the numbers look like datasheet numbers and are not.

- The band and power figures are **what the RadioLib driver enforces**. A driver
  range check is a conservative software limit chosen by a library author; the
  silicon's real limits may be wider or narrower, and the *conducted* PA ceiling
  is not the *radiated* power a regulator cares about
  ([ADR-0006](0006-settings-and-bounded-values.md)).
- The modulation claims are corroborated from two independent readings of
  RadioLib — the class hierarchy and the module list — and from final §5, which
  cites TI and Silicon Labs product material. They are **not** read from the
  datasheets, because `ti.com` and the Silicon Labs document host both refused
  automated retrieval (HTTP 403 and a timeout respectively).
- The T-Watch's radio is a *purchase-time variant* and no board is in hand
  (A1, A2). Which of the five is actually fitted is unknown, so this table
  describes a set of possibilities and not this project's device.

Recorded as **OPEN: R1** — confirm each chip's modulation set, band plan and
conducted PA ceiling against the manufacturer datasheet before any transmit path
depends on a number here.

## Decision

### 1. The part is a `Radio`

`HardwareFeature::Radio`, never `Lora`. The typed descriptor carries the facts
final §5 lists, and modulation is a set rather than a name:

```cpp
enum class RadioChip : uint8_t { Unknown, Sx1262, Sx1280, Lr1121, Cc1101, Si4432 };

enum class Modulation : uint16_t {           // a bitmask; chips do several
    Lora = 1u << 0,  Fsk  = 1u << 1,  Gfsk = 1u << 2,  Msk = 1u << 3,
    Ook  = 1u << 4,  Ask  = 1u << 5,  Flrc = 1u << 6,  LrFhss = 1u << 7,
};

struct BandRange { uint32_t lo_hz, hi_hz; };   // Hz, integer — never float

struct RadioInfo {
    RadioChip  chip;
    uint16_t   modulations;         // Modulation bitmask
    BandRange  bands[3];
    uint8_t    band_count;
    int8_t     max_conducted_dbm;   // the chip's PA ceiling, not a permission
    RadioControl control;           // SPI bus, NSS/RESET/BUSY, which DIO is IRQ,
                                    // whether DIO3 drives a TCXO, RF-switch pins
    MeshCoreSupport meshcore;       // Supported | NeedsWork | Impossible | Untested
};
```

Frequencies are **`uint32_t` Hz**, everywhere, per
[ADR-0006](0006-settings-and-bounded-values.md) §2. A `float` cannot hold
868 731 000 Hz exactly; the measured round-trip error is 18 Hz and one ULP at
that magnitude is 64 Hz.

`max_conducted_dbm` is what the PA can do. It is one of **three** ceilings and
never the effective one — see ADR-0006 §5.

### 2. `MeshMessaging` is derived, never assumed

The product capability
([ADR-0007](0007-two-capability-layers.md)) is computed, and every clause must
hold:

```
supports(MeshMessaging) =
      ( a local Radio is present
        AND its modulations include Lora
        AND its band overlaps the configured network's band
        AND meshcore == Supported )
   OR ( a node provider can supply it )
```

A CC1101 T-Watch with no node reports `Unsupported` — terminal, honest, and
with a sentence the user can act on ("this watch's radio cannot join a Firefly
mesh; a Firefly node adds it"). It does **not** report a mesh that silently
fails, which is what the old model produced.

`MeshCoreSupport::NeedsWork` is deliberately distinct from `Untested`. The
LR1121 is `NeedsWork`: there is a concrete, describable piece of driver work
between here and a working link. `Untested` would imply somebody just needs to
try it.

### 3. What the band difference means

Band is not a detail of the radio; it decides who the device can talk to. Two
devices interoperate only if they share a band **and** a modulation **and** the
network parameters ([ADR-0006](0006-settings-and-bounded-values.md) treats those
as one atomic preset). An SX1280 watch is not a worse mesh device than an SX1262
watch — it is a member of a different, empty network.

Diagnostics must be able to say this in one screen, because it is otherwise
indistinguishable from being out of range.

### 4. Regulatory constraints attach to the band, not the chip

`max_conducted_dbm` says what the silicon can emit. What it *may* emit is
`min(chip PA ceiling, regulatory limit for this band in this region, user
setting)`, and the middle term is unknown until A4 is answered. Until then the
region profile is `Unknown` and the transmit path stays closed. That rule lives
in ADR-0006; it is restated here because this is the ADR somebody will read when
they want to raise the power.

### 5. One firmware image drives one radio

MeshCore's RadioLib wrapper keeps radio state in a **file-static** variable set
from an ISR (`src/helpers/radiolib/RadioLibWrappers.cpp`, OPEN_QUESTIONS M9). So
a single image cannot drive two radios through it, and MeshCore expects
uninterrupted ownership of the one it has.

This is a real constraint on the coordinator and on any future dual-radio
device. It is **not** a reason the watch cannot run mesh — that inference was
made once and is corrected in [ADR-0008](0008-mesh-service-providers.md).

## Alternatives considered

**Keep `Lora` as the hardware name and treat CC1101/Si4432 as "LoRa: false".**
Rejected. It puts a lie in the type name and then annotates it. The parts are
radios; a `Radio` that cannot do LoRa is an ordinary member of the set, whereas
a `Lora` that is not LoRa is a special case somebody will eventually
"simplify" back to `true`.

**Support only the SX1262 and declare the other four out of scope.** Tempting,
and half-right: SX1262 is the only one that works today. Rejected as a
*modelling* decision because it does not remove the problem, it hides it — the
other four still exist in the world, still arrive in a box labelled "T-Watch
S3 Plus", and the device still has to say something truthful when one does. The
scope decision (which chips get a driver) is separate and belongs in TASKS,
where it can be made once A2 is answered. The model must handle all five
regardless.

**A generic modulation-agnostic mesh over FSK, so CC1101 and Si4432 can join.**
Rejected, and worth recording because it sounds appealing. It means abandoning
MeshCore wire compatibility, which is a headline product requirement (final §3,
§14) — a Firefly-only FSK network cannot talk to any MeshCore device in
existence. It is also years of routing behaviour to re-derive. If the owner ever
prioritises "works on every T-Watch" above "interoperates with MeshCore", this
is the shape of that decision, and it is the owner's to make.

**Probe the radio at boot instead of declaring it.** Rejected as the primary
mechanism, same as in ADR-0001: a chip ID read proves which chip answered, but
not which band-specific matching network and antenna are fitted, and an SX1262
board and an SX1280 board differ in the parts you cannot read over SPI. Probing
stays valuable as a *verification* step in diagnostics — declared versus
detected, with a mismatch surfaced as a finding.

## Consequences

**Easier.** A new radio is a descriptor plus a driver, and the mesh capability
falls out of the matrix rather than being asserted. Diagnostics can explain, in
one screen, why a mesh is not available on a device that visibly contains a
radio — which is the single hardest thing to explain about this product.

**Harder.** Somebody must maintain the compatibility matrix against a moving
upstream. MeshCore adds radios; a chip that is `NeedsWork` today may be
`Supported` in three months, and the matrix is wrong the moment it stops being
checked. The check is cheap — it is a grep over `RADIO_CLASS` across the
variants — and it is a task (T-013) rather than a hope.

**Committed to.** A pinned MeshCore revision that the matrix is *about*
(`d929643` today). Frequencies in Hz as integers, everywhere. A `Radio`
descriptor that must be filled in before any board's mesh capability is
non-`Unknown`. And to saying `Unsupported` out loud on hardware that cannot do
the job, rather than shipping a feature that fails quietly.

**Testable.** For each of the five chips, a simulator configuration and an
expected `availability(MeshMessaging)`. The one that matters:
**CC1101 + no node ⇒ `Unsupported`**, and the Mesh application is not in the
launcher. On hardware: `NOT EXECUTED — HARDWARE REQUIRED`, and it will stay that
way until A1 and A2 are answered.

**Open.** **R1** — datasheet confirmation of every modulation, band and power
figure in the matrix. **R2** — whether the LR1121 works through
`CustomLR1110Wrapper` with the LR11x0 reset helper, which is a spike, not a
reading. **A2** — which chip is actually fitted, which is the owner's to answer
and which decides whether any of this is urgent.
