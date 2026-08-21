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

| Dependency | Pinned at | Licence | Upgrade strategy |
|---|---|---|---|
| **LVGL** | **v9.5.0** — `85aa60d18b3d5e5588d7b247abf90198f07c8a63`, 2026-02-18 | MIT | tagged releases only. Retest both geometries, the font-subset size and asset regeneration on every bump |
| **MeshCore** | `d92964352441e53b93e8667b802e04f6e072b39e`, 2026-08-14, tag `companion-v1.17.1` | MIT | upstream is active. Re-run the radio census (`grep RADIO_CLASS variants/`) on every bump — [ADR-0003](../adr/0003-radio-not-lora.md) is *about* this revision |
| **RadioLib** | `510e00cfb05bbc3c2b7b524262785454944adb6e`, tag **7.7.1**, 2026-08-13 | MIT | follows MeshCore's pin |

MeshCore and RadioLib are pinned as *read* rather than as *linked* — nothing
compiles against them yet. The pin is what makes ADR-0003's compatibility matrix
a statement about something specific rather than about "MeshCore" in general.

### LVGL v9.5.0 — the reasoning

Pinned ahead of everything else because final §51, §76 and §77 make it the
prerequisite for the two pipelines M1 is built on — the Cyrillic font subset and
the image assets. Nothing in M1 can start while it floats.

| Requirement | v9.5.0 |
|---|---|
| A tagged release, not a branch | yes. `master` carries six months of untagged work; final §76 forbids floating "latest" |
| ESP-IDF integration | `idf_component.yml` in-tree; Waveshare BSP v2.0.0 accepts `lvgl >=8,<10` |
| Desktop simulator backend | **in-tree** — `src/drivers/sdl/` (display, mouse, keyboard) and `src/draw/sdl/` |
| Reproducible image conversion | `scripts/LVGLImage.py` — official and scriptable, which final §80 requires and hand-maintained C arrays cannot give |
| Alpha-capable image format | `LV_COLOR_FORMAT_RGB565A8` (`src/misc/lv_color.h:156`) — what the mascot art needs |
| Font subsetting and compression | `lv_font_fmt_txt` with compressed bitmaps. **The converter is `lv_font_conv`, a separate npm tool — not vendored and not yet pinned** |
| Both vendor BSPs | LilyGoLib ships `lv_conf.h` and `lv_conf.h.v8`; Waveshare's examples use LVGL 9 |

**Rejected — LVGL 8.** Both BSPs still accept it and it is the conservative
choice. Rejected because 9.x is where the SDL driver moved in-tree, and the
simulator is a first-class target rather than a convenience: an out-of-tree port
is a maintenance liability on the one build that must never break.

**Rejected — tracking `master`.** Final §76 forbids floating versions, and final
§77 adds the subtler reason: *"Do not architect against 'latest docs' after
pinning another version."*

**Not yet verified, and it is the part that can still surprise:** the measured
size of a Latin + Cyrillic subset at the sizes the design system needs, and
`lv_font_conv`'s own version and licence. The library is pinned; the font
toolchain is a second decision that has to be made against it (T-032).

## Under consideration

### ESP-IDF

- **Status:** version not chosen, but narrowed — and a v5.5 toolchain is
  installed and **verified** by an actual `idf.py set-target esp32s3 && idf.py
  build` on a stock example (`v5.5.5-496-gc197d718bcc`). Installed and verified
  are still not *decided*.
- **Evidence:** Waveshare states support for **v5.5.5 and v6.0.2**; its BSP
  v2.0.0 requires `idf >= 5.3`. LilyGO's library targets the Arduino layer
  (arduino-esp32 ≥ 3.3.0-alpha1) and its PlatformIO path is pinned to the older
  2.0.17 / IDF 4.4.7 — which probably does not bind Firefly, since Firefly is
  ESP-IDF-native and does not use the Arduino layer. That assumption is flagged
  in OPEN_QUESTIONS T7.
- **Constraint:** must be a supported release that LVGL 9.5.0 and both board
  BSPs work with. The version named in the superseded
  [`../master-prompt.md`](../master-prompt.md) is explicitly not a requirement.
- **Blocks:** all embedded work. It does **not** block M1, which is the
  simulator.

### SDL2 — the simulator display backend

- **Decided in principle by the LVGL pin**: 9.5.0 carries SDL2 display, mouse
  and keyboard drivers in-tree, so there is no third-party port to choose.
- `libsdl2-dev` and `libsdl2-image-dev` are installed on the development host.
  The **version is not yet recorded here**, and must be before the simulator
  build can be called reproducible.

### `lv_font_conv` — the font converter

- **Status:** not pinned. It is an npm tool from the LVGL organisation, outside
  the LVGL repository, and it is the only supported way to generate a subset
  font. Its licence must be checked before it enters the build (T-032).
- **Why it matters more than it looks:** the generated font is a build artefact
  that ships in flash. Its size is a budget line, and its glyph coverage is a
  CI check ([ADR-0010](../adr/0010-localization.md) §3).

### Fonts — Nunito Sans and Inter

- **Status:** not pinned, not verified. The owner's design boards specify both.
  Final §51 requires licence, Cyrillic coverage, legibility at real pixel size
  and generated size to be checked before either is adopted — and Cyrillic
  coverage is the one that can eliminate a font outright.
- Both are widely distributed under the SIL Open Font License, which is
  compatible; that has **not** been confirmed from the font files this project
  would embed, which is the only version of the check that counts.

### Radio driver

- RadioLib is pinned above as MeshCore's dependency and as this project's
  evidence base. Whether Firefly's *own* radio layer uses it directly on a local
  mesh path is part of the T-013 spike rather than a separate decision — two
  competing radio abstractions in one image is a design smell, and the
  integration mechanism decides which survives
  ([ADR-0008](../adr/0008-mesh-service-providers.md) §5).

### Host test framework

- **Status:** undecided. Unity ships with ESP-IDF; Catch2 and doctest are
  reasonable for host-only tests. Small decision, no ADR needed. The current
  smoke test uses plain CTest with no framework, which is enough until there is
  something to assert about.

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

### nanopb

- **Status:** evaluated, not adopted. Measured on the target compiler while
  arguing [ADR-0005](../adr/0005-node-protocol.md): runtime 7 029 B, descriptor
  tables 13 148 B for 24 message types.
- It returns to consideration under final §18, which requires the node
  protocol's encoding to be benchmarked against *a Firefly-specific streaming
  schema* rather than against Meshtastic's whole `FromRadio` union before the
  TLV choice can be accepted (T-016).

## Rejected

| Dependency | Why |
|---|---|
| **LVGL 8.x** | the SDL simulator driver is out-of-tree there, and the simulator is a first-class target |
| **LVGL `master`** | floating versions are forbidden (final §76), and pinning docs to one version while building against another is worse than either |
| **Meshtastic (any part)** | **GPL-3.0.** Incompatible with this MIT repository. Read for evidence and measurement only; not one line may be copied |
| **`rweather/Crypto`** | licence **UNVERIFIED**. Not cloned, not read, not usable until that changes. Recorded so it is not re-proposed |

Record rejections here with the reason — it stops the same option being
re-evaluated from scratch later.
