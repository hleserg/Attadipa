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

## Upstream sources examined, and at which revision

The addendum forbids writing "taken from Meshtastic" or "similar to Zephyr". It
requires repository, tag or version, **commit hash**, the relevant source files
and the licence — so that it is possible to return to the specific
implementation. These were cloned in full, with history, on 2026-08-21; the
records below cite them by hash.

Full history rather than a shallow clone is deliberate: the addendum also
requires reading issues, pull requests, changelogs and bug fixes, because closed
bugs show which obvious-looking solutions already broke for other people. We
want to inherit the experience, not only the code.

| Project | Repository | Commit at examination | Last commit | Why it is here |
|---|---|---|---|---|
| `MeshCore` | github.com/meshcore-dev/MeshCore | `d92964352441e53b93e8667b802e04f6e072b39e` | 2026-08-14 | the mesh stack Firefly builds on; T-006 |
| `meshtastic` | github.com/meshtastic/firmware | `68bfe015e6ab9ec2ab8f1657066898b7880eaf63` | 2026-08-20 | ~200 board variants, worldwide regulatory regions, nanopb phone API |
| `InfiniTime` | github.com/InfiniTimeOrg/InfiniTime | `825056574f47a8187b410b860f326050566553e2` | 2026-08-19 | mature LVGL watch firmware with a real app lifecycle, on far less RAM |
| `RadioLib` | github.com/jgromes/RadioLib | `510e00cfb05bbc3c2b7b524262785454944adb6e` | 2026-08-13 | radio abstraction across many chips; candidate for ADR-0003 |
| `lvgl` | github.com/lvgl/lvgl | `7cc13aafaa2e7acab6cf3c1977ab6ca70b6c2ed7` | 2026-08-20 | the UI toolkit; version choice is open question T2 |
| `T-Watch-S3` | github.com/Xinyuan-LilyGO/TTGO_TWatch_Library | `e5a0f825a21198f97d2bafee03ea853766483d20` | 2025-02-28 | LilyGO vendor library for one of the two target boards |
| `waveshare-bsp` | github.com/espressif/esp-bsp | `2f519317d5375f7bbb0190b29a4988c2ea2453e2` | 2026-08-13 | Espressif BSP collection, including the Waveshare board; compile-time BSP_CAPS_* |
| `Gadgetbridge` | codeberg.org/Freeyourgadget/Gadgetbridge | `40326980ca871989961ba2442e7cabd4d204b1b6` | 2026-08-21 | host side of many watch protocols; companion protocol prior art |
| `WatchyOS` | github.com/sqfmi/Watchy | `d1d233c43b36cac23bccc6abeae998aa3e27724e` | 2025-08-18 | ESP32 watch firmware |
| `esp-brookesia` | github.com/espressif/esp-brookesia | `01939b5e58fd50d18339b1c35fb74c4e808962c7` | 2026-08-10 | ESP32 UI framework with an application model |

Licences are recorded per record below, not here — a licence that is convenient
to look up is a licence that gets assumed.

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
