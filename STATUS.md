# Status

Last updated: 2026-08-21

## Current

Milestone **M0 — Repository and research**.

The repository exists, the research gate is populated, and both target boards
have been surveyed down to the pin map from vendor sources. No firmware code
has been written yet, and that is deliberate: the specification requires the
research gate before large-scale implementation, and the survey changed the
architecture materially.

Working on: mapping every peripheral on both boards to a place in the core
(T-001), and the capability-model ADR that follows from it (T-002).

## Next ready

- T-003 — host build and CI that genuinely runs
- T-004 — pin ESP-IDF and LVGL versions
- T-006 — read MeshCore upstream, especially whether it assumes exclusive
  ownership of the radio

## Lookahead research

Running ahead of implementation, so the next task is never waiting on a search:

- MeshCore internals — architecture, crypto format, threading, radio
  abstraction, memory footprint
- Existing open-source firmware for these exact boards, for the reuse ledger
- LVGL 9 on QSPI AMOLED — draw-buffer strategy and realistic frame rates
- A-GNSS: what the fitted module actually supports, once the module is known

## Long-running operations

None.

## Verified hardware

None physically. Both boards are verified **on paper only** — vendor
documentation, vendor BSP source, published schematics. No board has been
powered on by this project, and no measurement has been taken.

Nothing in this repository may be described as hardware-tested.

## Blocked

- **T-010 board bring-up** — no physical board; exact variant unknown
- **T-011 interference measurement** — same, and neither board has a
  magnetometer, so the headline haptics-vs-compass concern cannot be measured
  on current hardware at all

## Waiting on the project owner

1. Is either board physically available, and which revision?
2. If a T-Watch: which of the five radio chips, and which of the two GNSS
   modules? This decides sub-GHz vs 2.4 GHz, which is a regulatory question as
   much as a driver one.
3. Is there a second radio device, so mesh can be tested at all?
4. Which regulatory region governs LoRa here?
5. **What should the Waveshare board be?** It has neither LoRa nor GNSS, so
   mesh and navigation — two headline features — cannot exist on it. It can
   still be a watch, an audio device, and the UI development platform. That is
   a product decision, not an engineering one.

## Build and test status

| Target | State |
|---|---|
| Host / native | skeleton smoke build only |
| Simulator | not started — SDL2 not installed, LVGL version not chosen |
| ESP32-S3 firmware | not started — ESP-IDF not installed, version not chosen |
| Hardware tests | `NOT EXECUTED — HARDWARE REQUIRED` |

The development host has cmake and gcc. It does not have ESP-IDF, ninja, SDL2,
clang-format or ccache.

## Assumptions currently in force

- The LilyGO PlatformIO pin to IDF 4.4.7 does not constrain Firefly, which is
  ESP-IDF-native and does not use the Arduino layer. Flagged, not proven.
- Both boards' SoC is an ESP32-S3 — from product naming and vendor BSP targets,
  not yet from a chip readback.

## Known failures

None. Nothing runs yet, which is not the same as everything working.

## What changed most recently

The board survey. Three findings reshaped the plan:

1. **The Waveshare board has no LoRa and no GNSS.** The two boards are not
   variants of one product; they share only the SoC and the PMU.
2. **Neither board has a magnetometer.** All magnetometer work is
   architectural — an API that can accept one later, not a driver, and not a
   measurable interference problem.
3. **The T-Watch radio and GNSS are purchase-time variants** — five radio chips
   and two GNSS modules — so the board name does not identify the hardware, and
   a capability cannot be a plain boolean.
