# Dependencies

No external component may be depended on until it has a row here. "Latest"
is not a version. A dependency without a recorded license cannot be used in an
MIT-licensed repository.

Required for every entry:

| Field | Why |
|---|---|
| Source | where it actually comes from |
| Version / commit | pinned, not floating |
| License | compatibility with MIT, checked not assumed |
| Why selected | so the next person can re-evaluate the choice |
| Upgrade strategy | how it gets bumped, and what has to be retested |

---

## Decided

*Empty.* Nothing is pinned yet.

## Under consideration

### ESP-IDF

- **Status:** version not chosen, but narrowed.
- **Evidence:** Waveshare states support for **v5.5.5 and v6.0.2**; its BSP
  v2.0.0 requires `idf >= 5.3`. LilyGO's library targets the Arduino layer
  (arduino-esp32 >= 3.3.0-alpha1) and its PlatformIO path is pinned to the
  older 2.0.17 / IDF 4.4.7 — which probably does not bind Firefly, since
  Firefly is ESP-IDF-native and does not use the Arduino layer. That
  assumption is flagged in OPEN_QUESTIONS T7.
- **Constraint:** must be a supported release the chosen LVGL version and both
  board BSPs work with. The version named in
  [`../master-prompt.md`](../master-prompt.md) is explicitly not a requirement.
- **Blocks:** all embedded work.

### MeshCore

- **Source:** `https://github.com/meshcore-dev/MeshCore`
  (the older `ripplebiz/MeshCore` path redirects here).
- **License:** MIT, as reported by the GitHub API on 2026-08-21 — compatible.
- **Status:** revision not pinned; source not yet read.
- **Why:** it is the mesh protocol the product is specified around.
- **Upgrade strategy:** upstream is actively developed. Firefly should stay
  compatible and prefer upstreamable patches over a fork. Pin a revision before
  integration and re-test the protocol on every bump.
- **Unresolved:** everything in the MeshCore section of
  [OPEN_QUESTIONS.md](OPEN_QUESTIONS.md).

### LVGL

- **Status:** major version not chosen, but narrowed.
- **Evidence:** Waveshare BSP v2.0.0 accepts `lvgl >=8,<10` and its ESP-IDF
  examples use LVGL 9. LilyGoLib carries both an `lv_conf.h` and an
  `lv_conf.h.v8`, so it supports both generations.
- **Constraint:** must have working ESP-IDF integration *and* a desktop
  simulator backend, since the simulator is a first-class target.
- **Blocks:** all UI work.

### Radio driver (RadioLib or MeshCore's own)

- **Status:** undecided; depends on how MeshCore abstracts the radio.
- **Note:** two competing radio abstractions in one firmware is a design smell.
  Decide once, in an ADR.

### Simulator display backend (SDL2 or alternative)

- **Status:** undecided; follows from the LVGL version.
- **Note:** SDL2 is not currently installed on the development host.

### Host test framework

- **Status:** undecided. Unity ships with ESP-IDF; Catch2 and doctest are
  reasonable for host-only tests. Small decision, no ADR needed.

### Vendor board support packages

- **Waveshare** `waveshare/esp32_s3_touch_amoled_2_06` v2.0.0 — Apache-2.0,
  from the ESP Component Registry. Covers display, touch, audio and SD only;
  declares `BSP_CAPS_IMU 0` and does **not** drive the QMI8658, AXP2101 or
  PCF85063 that are on the board.
- **LilyGO** `Xinyuan-LilyGO/LilyGoLib` — MIT. Arduino-oriented, covers the
  T-Watch family broadly, and carries the schematics and the authoritative pin
  documentation.
- **Open question T6:** depend on these, or take only the pin facts and write
  Firefly's own BSP? Apache-2.0 is compatible with an MIT project but carries
  notice and patent terms that must be preserved if code is vendored. This is a
  reuse-ledger decision, not a default.

### XPowersLib (AXP2101 driver)

- **Source:** used by *both* vendors for the AXP2101 — the one part the two
  boards share.
- **Status:** not evaluated. A strong reuse candidate precisely because it
  covers both targets.
- **License:** not yet checked.

## Rejected

*Empty.*

Record rejections here with the reason — it stops the same option being
re-evaluated from scratch later.
