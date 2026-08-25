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
| **ESP-IDF** | **v5.5.5** — `ff1bac0aeecdd2b797b9c3a558c6bd03629bc013`, 2026-07-16 | Apache-2.0 | tagged releases only, and **never below v5.5.5**: the `spi_flash_mmap` refusal above `0x1000000` was added in that release and is a safety property on the Waveshare's 32 MB part, not a convenience — [FLASH_ADDRESSING_LIMITS](FLASH_ADDRESSING_LIMITS.md) §4.1. A bump retests `firmware/` for both boards, re-reads the `sdkconfig.defaults` symbols (Kconfig names move between minor releases) and re-runs the RAM-load route, because that path is the one with no fallback |
| **LVGL** | **v9.5.0** — `85aa60d18b3d5e5588d7b247abf90198f07c8a63`, 2026-02-18 | MIT | tagged releases only. Retest both geometries, the font-subset size and asset regeneration on every bump |
| **MeshCore** | `d92964352441e53b93e8667b802e04f6e072b39e`, 2026-08-14, tag `companion-v1.17.1` | MIT | upstream is active. Re-run the radio census (`grep RADIO_CLASS variants/`) on every bump — [ADR-0003](../adr/0003-radio-not-lora.md) is *about* this revision |
| **RadioLib** | `510e00cfb05bbc3c2b7b524262785454944adb6e`, tag **7.7.1**, 2026-08-13 | MIT | follows MeshCore's pin |
| **`lv_font_conv`** | **1.5.3** — npm, integrity `sha512-0xJQThBOw2ipt…TuBIbQ==` | MIT (read from the tarball, not the manifest) | it generates a build artefact that ships in flash, so a bump means re-measuring the subset. [FONT_MEASUREMENTS](FONT_MEASUREMENTS.md) |
| **`LVGLImage.py`** | **v9.5.0**, commit `85aa60d18`, SHA-256 `c4b59a99…1bff3` — **vendored** at `tools/assets/vendor/LVGLImage.py`, unmodified | MIT, copied beside it from the same tree | it emits a build artefact that ships in flash, so a bump re-encodes every asset. Its hash is inside the pipeline's inputs digest, so a bump that changes bytes fails `ui_images_are_current` until the tree is regenerated |
| **`pypng`** | whatever the environment has — `LVGLImage.py` imports it | MIT | a **tool-time** dependency of the vendored converter, not of the firmware. Nothing links it and nothing ships it. Its output is committed, so a machine with no `pypng` can still build and test everything except a regeneration |
| **`lz4` (Python)** | the same | MIT | imported at module scope by `LVGLImage.py` and then never used, because Attadipa passes `--compress NONE`. Required to import the module at all, which is why it is listed |
| **Pillow** | whatever the environment has — `python3-pil` on the CI runners | HPND (MIT-compatible) | tool-time only, for `tools/assets/` — authoring the source masks, the dimension cap, and the contact sheet. Deliberately **not** needed by `generate_images.py --check`, so the primary staleness gate never depends on a package being installed; the two checks that do need it are replaced by a failing test when it is absent |
| **Inter** | `Inter[opsz,wght].ttf`, `google/fonts`, SHA-256 `29160a80…c559031` | **OFL 1.1**, read from the `OFL.txt` beside the file | variable font; used **unmodified**, because instancing it costs its kerning |
| **Nunito Sans** | `NunitoSans[YTLC,opsz,wdth,wght].ttf`, `google/fonts`, SHA-256 `f934d714…ae2491d` | **OFL 1.1**, read from the `OFL.txt` beside the file | variable font; must be instanced to `wght=400`, because its default is 200 |

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
| Font subsetting and compression | `lv_font_fmt_txt` with compressed bitmaps. The converter is `lv_font_conv`, a separate npm tool — pinned above at 1.5.3, and measured against this LVGL |
| Both vendor BSPs | LilyGoLib ships `lv_conf.h` and `lv_conf.h.v8`; Waveshare's examples use LVGL 9 |

**Rejected — LVGL 8.** Both BSPs still accept it and it is the conservative
choice. Rejected because 9.x is where the SDL driver moved in-tree, and the
simulator is a first-class target rather than a convenience: an out-of-tree port
is a maintenance liability on the one build that must never break.

**Rejected — tracking `master`.** Final §76 forbids floating versions, and final
§77 adds the subtler reason: *"Do not architect against 'latest docs' after
pinning another version."*

**How it is obtained.** `cmake/AttadipaLvgl.cmake`, by `FetchContent` — cloning
the *tag* and then verifying the *commit*. The tag is only the transport: it is
what CMake's generated `git checkout` can resolve in a shallow clone. The pin is
the SHA, checked with `git rev-parse HEAD` after the clone, because a tag can be
moved and a commit cannot — and a re-tagged v9.5.0 would still say `9.5.0` in
`lv_version.h`, so the version check alone cannot catch it.
`ATTADIPA_LVGL_SOURCE_DIR` points the build at a tree already on disk for offline
work; it skips the fetch and neither check. Both failures are configure errors
rather than behaviour discovered later.

Two things checked rather than assumed, on 2026-08-21:

| Claim | How |
|---|---|
| `85aa60d18b3d5e5588d7b247abf90198f07c8a63` **is** v9.5.0 | `git ls-remote https://github.com/lvgl/lvgl.git refs/tags/v9.5.0` returns that SHA, and its commit message is `chore: release v9.5.0 (#9753)` |
| GitHub serves a shallow fetch of a bare SHA | `git fetch --depth 1 origin <sha>` succeeds — by hand. It is **not** what FetchContent emits, which is why the build clones the tag instead |
| The whole fetch-and-verify path works from nothing | **OBSERVED** in CI on 2026-08-21, cold cache, no LVGL on the machine: `-- LVGL: cloning v9.5.0` → `-- LVGL commit verified: 85aa60d1…` → build → 6/6 tests → a screenshot per geometry, in 2 min 2 s for the whole job. The commit check is therefore known to fire on a real clone and not only on a tree that was already right |
| `GIT_SHALLOW TRUE` is **not** a small download | CMake 3.28's generated `lvgl-populate-gitclone.cmake` runs `clone --no-checkout --depth 1 --no-single-branch --progress` and then `checkout "v9.5.0" --`. One commit off *every* ref, not off one branch. **MEASURED** on a GitHub runner with a cold cache (run `32462413273`): the clone takes **22.8 s** and leaves a `_deps` tree that caches at **366 761 925 B — 350 MiB**. Recorded because the opposite is the natural reading of `GIT_SHALLOW`, and preventing exactly that is what this file is for |

**What a bump has to revisit, beyond the two geometries.**
`tools/ui/check_raw_values.py` carries a written-out inventory of which LVGL
entry points take a pixel length, a duration or a colour, and at which argument
position. It was read out of this pin's own headers — `lv_obj_style_gen.h`,
`lv_style_gen.h`, `lv_obj_style.h`, `lv_obj_pos.h`, `lv_obj_scroll.h`,
`lv_anim.h` and `lv_api_map_v9_1.h` — rather than remembered, and it is a list
rather than a parse because `ui_no_raw_values` runs in every CI job while LVGL
itself is behind `ATTADIPA_BUILD_SIMULATOR`, which is **OFF** by default. A
checker that needed the headers would silently stop checking on four of the five
jobs. The cost of that choice is this line: **a version bump re-derives the
inventory from the new tree**, because a setter LVGL adds is a setter the
checker will not know about, and it will not say so. The reasoning is in
[REUSE_LEDGER](REUSE_LEDGER.md) under *Reading a C++ call expression well enough
to police it*.

**Since answered (T-032):** the subset is 181 codepoints, `lv_font_conv` is
pinned at 1.5.3 under MIT, and a Latin + Cyrillic subset has been generated and
compiled with the ESP32-S3 toolchain at seven sizes and three bit depths —
[FONT_MEASUREMENTS](FONT_MEASUREMENTS.md). What is still open is which font,
and render performance.

## Under consideration

### ESP-IDF — the reasoning

**Decided 2026-08-25, T-004: `v5.5.5`.** The row is in *Decided* above. Version
had been narrowed for weeks and never chosen, and the cost of leaving it open
was not abstract — the development host had drifted onto `master`
(`v6.1-dev-7351-ge37a7ae137c`) simply because nothing said otherwise.

| Requirement | v5.5.5 |
|---|---|
| A tagged release, not a branch | yes. `master` cannot be reproduced by a CI image or by the next person to install a toolchain, and final §76 forbids floating versions for the same reason it forbids them for LVGL |
| Refuses a flash mapping above `0x1000000` | **yes, and this is the deciding one.** The constant appears in v5.5.5 and in no earlier release checked — `v5.5.4`, `v5.5.3`, `v5.5.2`, `v5.5.1`, `v5.4.2`, `v5.3.3`. Below this line `spi_flash_mmap` maps an address that aliases into the low half of a 32 MB part and reports success |
| Vendor-supported for the board on the desk | yes. Waveshare states support for **v5.5.5 and v6.0.2**, and its BSP v2.0.0 requires `idf >= 5.3` |
| LVGL 9.5.0 | yes — the `idf_component.yml` path, which is how LVGL is consumed on device |
| Continuity with what this repository has already measured | yes. Every hardware result on file — the RAM-load route, the I2C scan, the `ota_1` finding — was produced by a v5.5.x toolchain, and `FLASH_ADDRESSING_LIMITS` traces v5.5.5's sources specifically |

**Rejected — `v6.0.2`.** Waveshare supports it and it is newer. Rejected for
*now* on one ground only: nothing this project has measured was measured on it,
and the first device milestone is the wrong moment to hold two variables. It is
the obvious next bump once `firmware/` builds and boots on the pinned version,
and the row above says what a bump has to retest.

**Rejected — `master` / `v6.1-dev`.** Not a release. It is what happened to be
installed, which is the argument against it rather than for it.

**Not a constraint after all — LilyGO's IDF 4.4.7.** Its PlatformIO path pins
the Arduino layer, and Attadipa is ESP-IDF-native and does not use it. Flagged
as an assumption in OPEN_QUESTIONS T7 and still worth confirming against a real
T-Watch bring-up; it does not bind this decision.

### SDL2 — the simulator display backend

- **Decided by the LVGL pin**: 9.5.0 carries SDL2 display, mouse and keyboard
  drivers in-tree, so there is no third-party port to choose. LVGL does *not*
  link SDL itself — the application does, which is why `sim/CMakeLists.txt`
  carries `find_package(SDL2 REQUIRED)`.
- **Development host: SDL2 2.30.0** (`libsdl2-dev` 2.30.0+dfsg-1ubuntu3.1,
  Ubuntu 24.04). CI installs `libsdl2-dev` from `ubuntu-latest`, so the two
  will drift; that is acceptable because SDL is a host-side dependency of a
  development tool and never ships in firmware.
- **Minimum version: not established.** Nothing in the simulator uses an API
  newer than SDL 2.0, and no lower bound has been tested. Recorded as an
  assumption rather than a claim.
- The headless path is `SDL_VIDEODRIVER=dummy` with `LV_SDL_ACCELERATED 0`,
  which is how CI renders frames with no display attached. **OBSERVED** —
  the CI job writes a screenshot per geometry and fails if either is missing.

### Fonts — Nunito Sans and Inter

Pinned above; measured in [FONT_MEASUREMENTS](FONT_MEASUREMENTS.md). **Neither
is chosen** — the numbers a choice needs now exist, and the choice itself is a
design decision (OPEN_QUESTIONS D16).

The three facts that change what "pick a font" means:

- **Nunito Sans has no arrows.** U+2190–U+2193 are absent, and `lv_font_conv`
  refuses the range rather than substituting. Either the arrows become icons
  from the image pipeline (T-034 — **done**, and it emits A8 masks, so an arrow
  drawn as an icon would be recoloured through a `ColorRole` exactly like a
  glyph), or Nunito Sans is not the whole answer.
- **Both ship as variable fonts only**, and the converter takes the *default*
  instance. Inter's default is Regular 400. Nunito Sans's is **200,
  ExtraLight** — converting the downloaded file gives a font nobody chose.
- **Instancing Inter destroys its kerning.** Measured: 1 012 B of kern data
  before the `fontTools` round-trip, exactly zero after, at its own default
  weight. So the pipeline instances only when it must.

Licences were read from the `OFL.txt` shipped beside each font file rather than
from a distributor's description, which is the only version of that check that
counts.

Still **UNKNOWN**: render performance. Final §51 asks for it; it needs the
simulator driving timed frames, or a board.

### Radio driver

- RadioLib is pinned above as MeshCore's dependency and as this project's
  evidence base. Whether Attadipa's *own* radio layer uses it directly on a local
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
  Attadipa's own BSP? Apache-2.0 is compatible with an MIT project but carries
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
  protocol's encoding to be benchmarked against *an Attadipa-specific streaming
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
