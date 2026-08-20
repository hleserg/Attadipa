# Reuse Ledger

Firefly prefers proven work over new code. This file records, for every
non-trivial problem, what already existed, what was examined, and why the
project did or did not use it.

A "we wrote our own" entry is allowed. An *undocumented* one is not — the point
of the ledger is that the next person can tell the difference between a
considered decision and an unexamined reflex.

## Decision vocabulary

| Decision | Meaning |
|---|---|
| `USE AS-IS` | taken unchanged |
| `USE AS DEPENDENCY` | pinned and consumed as an external component |
| `WRAP` | used behind a Firefly interface |
| `PORT` | moved to this platform, logic preserved |
| `ADAPT` | modified for Firefly's constraints |
| `EXTRACT ALGORITHM` | only the algorithm taken, code rewritten |
| `INSPIRE ARCHITECTURE` | only the design idea taken, nothing copied |
| `UPSTREAM PATCH` | change contributed back rather than forked |
| `REIMPLEMENT` | written fresh, with a reason recorded |
| `REJECT` | examined and not used, with a reason recorded |

## Record template

```
### <problem being solved>

Problem:
Projects investigated:
Useful implementation:
License:
Strengths:
Weaknesses:
Decision:
Reason:
Source revision:
Firefly integration:
Tests required:
```

Copy it whole. A half-filled record is worse than none — it looks like the
question was answered.

## Rules

- License is checked **before** the code is depended on, never after. Anything
  incompatible with MIT does not enter this repository.
- Pin a revision. "Latest" tells the next reader nothing.
- Prefer an upstream patch to a fork. If a fork is unavoidable, keep the delta
  small and record what it is and why.
- Reusing code does not mean trusting it. Every reused component needs tests
  that prove it does what Firefly needs, on Firefly's target.
- Vendor examples are a source of *knowledge*. Do not import a vendor demo's
  architecture into the project along with the one fact you needed.

---

## Records

*Empty.* No decision has been made yet.

## Candidates identified, not yet evaluated

Found during the 2026-08-21 board survey. Each needs a full record before
anything equivalent is written by hand.

| Candidate | Why it is relevant | License |
|---|---|---|
| `meshcore-dev/MeshCore` | the mesh protocol the product is specified around | MIT |
| `Xinyuan-LilyGO/LilyGoLib` | vendor library for the T-Watch family; schematics and authoritative pin map | MIT |
| `waveshare/esp32_s3_touch_amoled_2_06` | vendor BSP for the second board — display, touch, audio, SD only | Apache-2.0 |
| `waveshare/esp_lcd_sh8601` | the driver the vendor uses for the CO5300 AMOLED panel | to check |
| XPowersLib | AXP2101 driver used by **both** vendors — covers the one shared part | to check |
| `MarcoRR/S3NTRY` | an existing smartwatch firmware for the Waveshare 2.06 | to check |
| `joaquimorg/OLEDS3Watch` | another, built on ESP-Brookesia | to check |
| `infinition/waveshare-watch-rs` | a Rust `no_std` watch firmware for the same board — unusable directly, potentially instructive | to check |
| Meshtastic | mature ESP32 LoRa firmware with T-Watch support; solves overlapping problems | to check |
| ESP-Brookesia | Espressif application UI framework — overlaps the application framework requirement | to check |

Rust and Arduino candidates are still worth reading. `EXTRACT ALGORITHM` and
`INSPIRE ARCHITECTURE` are decisions in this ledger for exactly that reason —
a project does not have to be usable to be useful.
