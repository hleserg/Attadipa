# Dependencies

No external component may be depended on until it has a row here. "Latest"
is not a version. A dependency without a recorded licence and compatibility
decision cannot be used in this `GPL-3.0-or-later` repository.

Required for every entry:

| Field | Why |
|---|---|
| Source | where it actually comes from |
| Version / commit | pinned, not floating |
| License | compatibility with `GPL-3.0-or-later`, checked not assumed |
| Why selected | so the next person can re-evaluate the choice |
| Upgrade strategy | how it gets bumped, and what has to be retested |

---

## Decided

| Dependency | Pinned at | Licence | Upgrade strategy |
|---|---|---|---|
| **ESP-IDF** | **v5.5.5** — `b774170ff46c393eeb5e495ea37936038d3f4f4f`, 2026-07-16. *(`ff1bac0aeecdd2b797b9c3a558c6bd03629bc013` stood here and is the **annotated tag object**, not a commit: `git/ref/tags/v5.5.5` answers `.object.type == "tag"`, and reaching the commit needs a second call to `git/tags/<sha>`. Nothing checks that SHA out.)* | Apache-2.0 | tagged releases only, and **never below v5.5.5** while this pin is in force: that release adds a useful refusal for `spi_flash_mmap` above `0x1000000`, but it is defence-in-depth for one path, not the safety boundary — read, write and erase remain unguarded, so the binding rule is still that Attadipa places and accesses nothing above the ceiling ([FLASH_ADDRESSING_LIMITS](FLASH_ADDRESSING_LIMITS.md) §§4–5). A bump retests `firmware/` for both boards, re-reads the `sdkconfig.defaults` symbols (Kconfig names move between minor releases) and re-runs the RAM-load route, because that path is the one with no fallback |
| **`espressif/esp_lcd_co5300`** | **2.1.0**, direct in `firmware/main/idf_component.yml` | Apache-2.0, read from the resolved component's `license.txt` | change the manifest version deliberately; rebuild the display path and re-check the licence file |
| **`espressif/esp_lcd_touch_ft5x06`** | **1.1.1**, direct in `firmware/main/idf_component.yml` | Apache-2.0, read from the resolved component's `license.txt` | change the manifest version deliberately; rebuild the touch path and re-check the licence file |
| **`esp_lvgl_port`** | **2.8.0~1**, direct in `firmware/main/idf_component.yml` | Apache-2.0, read from the resolved component's `license.txt` | change the manifest version deliberately; rebuild both display geometries and re-check the licence file |
| **`espressif/esp_lcd_touch`** | **1.2.1**, transitive resolution audited 2026-08-26 | Apache-2.0, read from the resolved component's `license.txt` | this resolution is what `firmware/dependencies.lock` records, and CI fails the build on any drift from it (*Where the resolved graph lives*, above); re-audit the licence when a deliberate bump moves it |
| **`espressif/cmake_utilities`** | **0.5.3**, transitive resolution audited 2026-08-26 | Apache-2.0, read from the resolved component's `license.txt` | this resolution is what `firmware/dependencies.lock` records, and CI fails the build on any drift from it (*Where the resolved graph lives*, above); re-audit the licence when a deliberate bump moves it |
| **LVGL** | **v9.5.0** — `85aa60d18b3d5e5588d7b247abf90198f07c8a63`, 2026-02-18 | MIT | tagged releases only. Retest both geometries, the font-subset size and asset regeneration on every bump |
| **minmea** | `2dd2cd11a359de5583e68053182d5bbf29725934`, 2026-07-15 — **vendored** at `gnss/vendor/minmea/`, unmodified: `minmea.c` SHA-256 `3b30b322…20fbe1`, `minmea.h` SHA-256 `d7f7817c…a9dfce1` | **MIT**, taken under `LICENSE.grants` — upstream's own `COPYING` is WTFPL-2.0 and the grant offers MIT or LGPL-3.0-or-later at the recipient's option; all three files are copied beside the source | replace both files, update the SHA-256 table in `gnss/vendor/minmea/README.attadipa.md` and this row together, and re-run `test_nmea_receiver`. It asserts against sentences a real receiver emitted, not against minmea's own suite — the bump to watch for is kosma/minmea#104, the overflow guard that can be defeated into a negative scale |
| **MeshCore** | `d92964352441e53b93e8667b802e04f6e072b39e`, 2026-08-14, tag `companion-v1.17.1` | MIT | upstream is active. Re-run the radio census (`grep RADIO_CLASS variants/`) on every bump — [ADR-0003](../adr/0003-radio-not-lora.md) is *about* this revision |
| **RadioLib** | `510e00cfb05bbc3c2b7b524262785454944adb6e`, tag **7.7.1**, 2026-08-13 | MIT | follows MeshCore's pin |
| **`lv_font_conv`** | **1.5.3** — npm, integrity `sha512-0xJQThBOw2ipt…TuBIbQ==` | MIT (read from the tarball, not the manifest) | it generates a build artefact that ships in flash, so a bump means re-measuring the subset. [FONT_MEASUREMENTS](FONT_MEASUREMENTS.md) |
| **`LVGLImage.py`** | **v9.5.0**, commit `85aa60d18`, SHA-256 `c4b59a99…1bff3` — **vendored** at `tools/assets/vendor/LVGLImage.py`, unmodified | MIT, copied beside it from the same tree | it emits a build artefact that ships in flash, so a bump re-encodes every asset. Its hash is inside the pipeline's inputs digest, so a bump that changes bytes fails `ui_images_are_current` until the tree is regenerated |
| **`pypng`** | whatever the environment has — `LVGLImage.py` imports it | MIT | a **tool-time** dependency of the vendored converter, not of the firmware. Nothing links it and nothing ships it. Its output is committed, so a machine with no `pypng` can still build and test everything except a regeneration |
| **`lz4` (Python)** | the same | BSD-3-Clause | imported at module scope by `LVGLImage.py` and then never used, because Attadipa passes `--compress NONE`. Required to import the module at all, which is why it is listed |
| **Pillow** | whatever the environment has — `python3-pil` on the CI runners | HPND (GPL-compatible) | tool-time only, for `tools/assets/` — authoring the source masks, the dimension cap, and the contact sheet. Deliberately **not** needed by `generate_images.py --check`, so the primary staleness gate never depends on a package being installed; the two checks that do need it are replaced by a failing test when it is absent |
| **Inter** | `Inter[opsz,wght].ttf`, `google/fonts`, SHA-256 `29160a80…c559031` | **OFL 1.1**, read from the `OFL.txt` beside the file | variable font; used **unmodified**, because instancing it costs its kerning |
| **Nunito Sans** | `NunitoSans[YTLC,opsz,wdth,wght].ttf`, `google/fonts`, SHA-256 `f934d714…ae2491d` | **OFL 1.1**, read from the `OFL.txt` beside the file | variable font; must be instanced to `wght=400`, because its default is 200 |

## GitHub Actions

Privileged workflows execute third-party code. `anthropics/claude-code-action`
in particular receives the Anthropic credential, a GitHub token context, OIDC
and repository write permissions, so *"which bytes run"* is a security question
and not a convenience one. Every ref below is a commit; a tag is a name its
owner can move, and moving it would change what executes here with no change to
any workflow, pull request, review or required check. **No upstream compromise
is claimed or observed** — the exposure is the execution path.

Resolved 2026-08-28. `anthropics/claude-code-action@v1` and
`github/codeql-action@v4` are **annotated** tags: `git/ref/tags/<tag>` returns a
tag object whose SHA is not a commit SHA, and pinning to it pins to something
GitHub will not check out. `.github/tests/action-pin-test.sh` asserts every pin
resolves as a commit, which is the only check that tells the two apart.

### The container image

`ci.yml`'s firmware job runs inside `espressif/idf`, which is the same execution
path as an action reached through a different key: an image tag is a name its
owner can move, and this job builds the firmware that ships. It is pinned by
digest, with the tag kept beside it as provenance only.

| Image | Pinned at | Tag it came from | Resolved | Licence |
|---|---|---|---|---|
| **`espressif/idf`** | `sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2` | `v5.5.5` | 2026-09-01, from `registry-1.docker.io/v2/espressif/idf/manifests/v5.5.5` | Apache-2.0 |

That digest is the **OCI image index**, not a single platform's manifest, so it
resolves on both architectures the index carries — `linux/amd64`
(`sha256:6e2800a6…`, which is what a GitHub runner pulls) and `linux/arm64`
(`sha256:0a952afa…`). Pinning the index rather than the amd64 manifest is
deliberate: a bench machine and CI then run the same recorded reference.

**A digest binds the bytes, not their contents.** It proves the image has not
changed since it was resolved; it proves nothing about the image ever having
been what the ESP-IDF row above says. So `ci.yml`'s first firmware step reads
`git -C "$IDF_PATH" rev-parse HEAD` out of the running image and fails the job
unless it equals `b774170ff46c393eeb5e495ea37936038d3f4f4f`, and checks
`idf.py --version` names `v5.5.5`. That is the same annotated-tag trap as the
ESP-IDF row: `ff1bac0…` is the tag object and `b774170…` is the commit
`rev-parse` returns.

Re-resolving the digest is a deliberate edit in two places that must move
together — `ci.yml` and this table — and `action-pin-test.sh` refuses a
`container:` that is not digest-pinned, and refuses a `ci.yml` that has dropped
the commit assertion.

| Action | Pinned at | Tag it came from | Licence | Upgrade strategy |
|---|---|---|---|---|
| **`actions/checkout`** ×24 | `3d3c42e5aac5ba805825da76410c181273ba90b1`, 2026-07-17 | `v7`, lightweight | MIT | re-resolve the tag, run `action-pin-test.sh` with `ATTADIPA_PIN_CHECK_NETWORK=1`, bump every occurrence together |
| **`anthropics/claude-code-action`** ×3 | `a60f3e1db3edbceed2b1e6c6a9d34c36b8a15eba`, 2026-08-28 | `v1`, **annotated** | MIT | the highest-privilege dependency here. Read the upstream diff before bumping; `orchestration-bundle-test.sh` asserts the model and effort flags on the pinned step |
| **`actions/upload-artifact`** ×2 | `043fb46d1a93c77aae656e7c1c64a875d1fc6a0a`, 2026-04-10 | `v7`, lightweight | MIT | as `checkout` |
| **`github/codeql-action/init`**, **`/analyze`** | `cdf488f595d80d6e07e03d4674febd5ab45fa938`, 2026-08-26 | `v4`, **annotated** | MIT | both sub-paths share one repository and must move together, or `init` and `analyze` disagree about the bundle |
| **`actions/cache`** | `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`, 2026-06-23 | `v6`, lightweight | MIT | as `checkout` |

`v1` moved twice while this pin was being prepared — the tag resolved to a
different commit on 2026-08-27 and again on 2026-08-28. That is ordinary
maintainer behaviour and it is also the whole argument: a resolution recorded
yesterday is not a fact about today, so every SHA above was re-resolved
immediately before the commit that introduced it.

### Where the resolved graph lives, and where notices go

**`firmware/dependencies.lock` is tracked.**
`firmware/main/idf_component.yml` names four components and pins all four to
exact versions. The lock holds seven entries: those four, ESP-IDF itself, and
the two nothing pins — `espressif/cmake_utilities`, which `esp_lcd_co5300` asks
for as `0.*`, and `espressif/esp_lcd_touch`, which `esp_lcd_touch_ft5x06` asks
for as `^1.2.0`.

Those two are the only versions in the linked image a clean build was ever free
to move, and they are exactly the two the audit below records as audited:
`esp_lcd_touch` 1.2.1 and `cmake_utilities` 0.5.3. The lock committed here says
both numbers were right. That is the argument rather than a reprieve — nothing
in the repository could have shown it, and a build that resolved something else
would have left the same page looking equally true.

**The direct pins are load-bearing too, and the lock is where that shows.**
`espressif/esp_lvgl_port` accepts `lvgl/lvgl >=8,<10`; only the manifest's own
`lvgl/lvgl: "9.5.0"` holds it at the version the rest of this file reasons
about. That entry also means LVGL is obtained twice by two routes — from the
component registry for the firmware, and by `FetchContent` at tag `v9.5.0` with
commit `85aa60d1…` verified for the host and simulator builds (*LVGL v9.5.0 —
the reasoning*, below). Both name the same release and nothing ties them
together; a registry component hash is not a git commit, and a bump applied to
one is not a bump to the other. Recorded rather than fixed: both are pinned
exactly, so neither drifts on its own, and the only failure left is a bump made
on one route and forgotten on the other — which a third mechanism could catch
and this paragraph catches more cheaply.

**The lock is binding, and what it binds is narrower than "the graph cannot
move".** The firmware CI job copies the committed lock aside, builds, and runs
`.github/scripts/lock-drift-check.sh` over the two; drift is the failure. It
compares against a copy rather than against the index because `git` cannot read
the repository from inside that container at all: the workspace belongs to the
runner's user and the step runs as another, so git stops at the ownership
boundary and answers "Not a git repository".

What that catches follows from *when the component manager solves at all*,
which is not on every build. `is_solve_required()` returns `False` for a lock
that is still valid — same manifest hash, same target, a compatible IDF
version, the same set of direct dependencies, and every locked version still
present in its source with a matching component hash. So:

- **Caught.** A lock gone stale against a changed manifest, a changed target,
  an incompatible IDF version, a changed set of direct dependencies, or a
  locked version deleted from the registry. Each forces a re-solve, the
  re-solve rewrites the lock, and the rewritten lock differs from the copy.
- **Not caught.** `cmake_utilities` publishing 0.5.4, or `esp_lcd_touch`
  publishing 1.2.2. The lock stays valid, so nothing re-solves and CI is
  silent. Registry movement in those two `0.*` and `^1.2.0` requests is caught
  by re-running the audit on a deliberate bump — by the row above saying so —
  and not by this gate. Committing the lock is what makes "a deliberate bump"
  a thing that exists.
- **Stronger than either.** If the registry ever serves a *different artefact
  under a version this lock names*, the build does not drift — it stops:
  `InvalidComponentHashError`, "This could be due to a potential spoofing of
  the download server, or your lock file may have become corrupted." That check
  has nothing to compare against until the lock is committed, and no version
  pin in the manifest can produce it.

Read in `idf_component_manager/dependencies.py` of **idf-component-manager
2.5.0** — within the `idf-component-manager~=2.2` that
`tools/requirements/requirements.core.txt:16` pins for ESP-IDF v5.5.5 — and the
same function is unchanged in 3.1.0, so a manager bump does not silently
invalidate this paragraph. Bumping a component means committing the new lock and
re-reading the licence of whatever moved, in the same pull request.

**Redistribution notices** are assembled for a release artefact, not carried in
the tree: `LICENSE` (GPL-3.0-or-later) plus, for any binary or source bundle, the
Apache-2.0 notices of the ESP-IDF components and managed components, the MIT
notices of LVGL, MeshCore, RadioLib and the vendored `LVGLImage.py`, and the OFL
1.1 text for Inter and Nunito Sans — each copied from the licence file beside the
artefact it covers rather than retyped. **No release bundle has been produced
yet**, so this is a stated policy and not a described procedure; the first
release is what turns it into one.

### GPL-3.0-or-later compatibility audit — 2026-08-26

| Result | Components | Distribution consequence |
|---|---|---|
| **Compatible** | ESP-IDF and the five managed components above (Apache-2.0); LVGL, FreeRTOS, MeshCore, RadioLib and minmea (MIT); Newlib and the GCC runtime libraries with their runtime exception; Nunito Sans (OFL-1.1) | These may be used with GPLv3 code. Their own copyright, licence and notice obligations remain; the OFL font remains under OFL-1.1 rather than being relicensed |
| **Build/tool only** | CMake (BSD), SDL2 (Zlib), `lv_font_conv` and its npm graph (permissive), `pypng` (MIT), Python `lz4` (BSD-3-Clause), Pillow (HPND) and GitHub Actions used by CI | They are not linked into or shipped as the firmware. Preserve their licences if a tool is redistributed |
| **Requires attention** | the future release notice bundle | The audited graph is compatible and no longer free to move underneath this table: `firmware/dependencies.lock` is tracked and CI fails on drift (*Where the resolved graph lives*, above), so a transitive version that moves is a red build rather than a silently stale audit. What is still open is the other half — the notices themselves, which are assembled per release artefact and no release artefact exists yet |
| **Do not combine into the current distribution** | Espressif `esp-sr`, `esp_audio_codec`, `esp_audio_effects` and `esp_codec_dev` 2.x field-of-use licences; proprietary vendor SDKs; AGPL-3.0 code unless the combined work is distributed under AGPL terms | These are not current dependencies. The field-of-use and proprietary terms are incompatible with the project's GPL terms; an AGPL combination could not be distributed solely as `GPL-3.0-or-later` |

No incompatible component or proprietary blob was found in the current linked
firmware image. Apache-2.0 compatibility here relies on GPLv3, not GPLv2.

**minmea joined this table on 2026-09-05**, later than the audit above and on
its own evidence: upstream's `COPYING` is WTFPL-2.0, and `LICENSE.grants`
beside it grants MIT or LGPL-3.0-or-later at the recipient's option. Attadipa
takes the MIT branch, and all three files are copied into
`gnss/vendor/minmea/` so the grant travels with what it grants. It is the
first vendored dependency that is *linked into the firmware* rather than
tool-time, so unlike `LVGLImage.py` it carries a notice obligation into the
release bundle the row below is still waiting on.

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
| Refuses a flash mapping above `0x1000000` | **yes, for `mmap` only.** The constant appears in v5.5.5 and in no earlier release checked — `v5.5.4`, `v5.5.3`, `v5.5.2`, `v5.5.1`, `v5.4.2`, `v5.3.3`. This is defence-in-depth; it does not guard the destructive read/write/erase paths and does not replace the repository ceiling rule |
| Vendor-supported for the board on the desk | Waveshare states support for **v5.5.5 and v6.0.2**, and its BSP v2.0.0 requires `idf >= 5.3`; the vendor image actually read from this unit reports **v5.5.1-dirty**, so the statement is vendor documentation rather than a measurement of its v5.5.5 BSP path |
| LVGL 9.5.0 | **NOT EXECUTED on the physical board.** The host simulator uses the pin, but device compatibility with the BSP/display path remains hardware work |
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

Pinned above and measured in [FONT_MEASUREMENTS](FONT_MEASUREMENTS.md). The
owner selected **Nunito Sans Regular 400** for T-037 on 2026-08-26 after seeing
the Montserrat prototype. D16 is resolved; arrows are image-pipeline icons.

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
- **LilyGO** `Xinyuan-LilyGO/LilyGoLib` — MIT, pinned for reading at
  `38e6f8dee3ba78b340512af9a013365ef248a7d0`, v0.2.0. Arduino-oriented, covers
  the T-Watch family broadly, and carries the schematics and the authoritative
  pin documentation.
- **T6 — RESOLVED 2026-09-01: take the facts, not the dependency.**
  [ADR-0017](../adr/0017-board-backends-compose-esp-idf-drivers.md). A board
  backend composes official ESP-IDF components and hands the runtime an
  `esp_lcd_panel_handle_t` and an `esp_lcd_touch_handle_t` — the seam
  `waveshare_board.cpp:125-129` — "esp_lcd_panel_handle_t panel" — already
  exposes. **Neither vendor BSP becomes a
  link-time dependency**, and the answer is the same for both because the reason
  is the same: each carries product policy this project has decided differently,
  and each is worth far more as evidence than as code. Evidence in
  [TWATCH_S3_PLUS_BSP_REUSE](TWATCH_S3_PLUS_BSP_REUSE.md), researched under
  [#328](https://github.com/hleserg/Attadipa/issues/328).

  **No dependency is added by this resolution.** `LilyGoLib` is read, not
  pinned; the components the decision relies on —
  `espressif/esp_lcd_touch_ft5x06 1.1.1` and ESP-IDF's own
  `esp_lcd_panel_st7789` — are already in
  [`firmware/main/idf_component.yml`](../../firmware/main/idf_component.yml) and
  already shipping on the Waveshare path. Apache-2.0 is compatible with GPLv3
  but carries notice and patent terms that must be preserved if code is
  vendored; *Where the resolved graph lives, and where notices go* (above) is
  where that obligation is answered.

### SensorLib — evaluated, not adopted

- **Source:** `lewisxhe/SensorLib@2b9e591f245e447d3d00ec8798c3f49b897882d9`,
  version `0.4.1`, MIT, `idf: ">=4.4"` — a real ESP-IDF component rather than an
  Arduino library with a manifest attached. Covers `PCF8563`, `BMA423` and
  `DRV2605`, the three T-Watch parts Attadipa has no driver for.
- **Licence boundary that the root `LICENSE` does not describe:**
  `src/bosch/bma4xx/bma4.h` at that revision opens *"Copyright (c) 2023 Bosch
  Sensortec GmbH … BSD-3-Clause"*, and the component manifest's `exclude` list
  does **not** exclude `src/bosch/`, so those files ship with the component.
  Notices must be retained for both licences.
- **Status: `REJECT` for the PCF8563 path, decided 2026-09-03 under #422.** The
  audit this entry was waiting for has now been done, and the finding is in the
  error path: `SensorPCF8563.hpp` discards a read result over uninitialised
  stack, splits the VL flag across two transactions, does a read-modify-write on
  the flag register that can clear the alarm flag it did not mean to touch, and
  gives `setDateTime()` a `void` return so an I²C failure cannot be reported at
  all. Seven defects in one file, all of them in the half a watch depends on.
  Read as evidence for the register semantics; write a minimal direct driver.
  [TWATCH_RTC_INPUT_WAKE](TWATCH_RTC_INPUT_WAKE.md) §2 has each one with its
  line.
- **The BMA423 half is not decided by this.** It was not audited under #422,
  which scoped the part to a motion interrupt source under OD-20, and the
  feature-blob size question is still open for whichever slice adds the IMU.
- **Do not inherit LilyGoLib's pin:** its `library.json` pins SensorLib `0.3.1`,
  which predates the FT6X36 interrupt and BMA423 fixes released in `0.3.3`.

### XPowersLib (AXP2101 driver) — evaluated, rejected

- **Source:** `lewisxhe/XPowersLib@d6997586e68f65afd51baa775903df930db39821`,
  version `0.3.4`. Used by *both* vendors for the AXP2101 — the one part the two
  boards share.
- **Licence:** MIT, from the header of `src/XPowersAXP2101.hpp`.
- **Status: `REJECT` as a dependency, decided 2026-09-03 under #422; read as
  evidence.** What Attadipa needs from it is four register addresses — INTEN1-3
  at `0x40`-`0x42`, INTSTS1-3 at `0x48`-`0x4A` — and the fact that the status
  registers are write-1-to-clear. Taking the library to obtain them also takes
  `getIrqStatus()`, which assigns `readRegister()`'s `int` return straight into a
  `uint8_t`: a failed I²C read becomes `0xFF` and reads as *every interrupt
  pending*. On the T-Watch that is the worst available direction to fail in.
  [TWATCH_RTC_INPUT_WAKE](TWATCH_RTC_INPUT_WAKE.md) §6.1.
- **The pin matters more than usual here.** `d6997586` (2026-07-01) is
  *"fix axp2101 getIrqStatus byte order"* and touches only that one file, so
  every earlier copy returns a wrong IRQ word — including the `0.2.9` LilyGoLib
  pins. Any future re-evaluation starts at or after that commit.

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
| **Meshtastic (any part)** | **GPL-3.0.** Licence-compatible after the project migration, but still rejected by owner decision [OD-12](OWNER_DECISIONS.md#od-12--meshtastic-is-not-supported-and-the-reason-is-not-the-licence); no code is imported |
| **SensorLib (PCF8563 path)** | evaluated 2026-09-03 at `2b9e591f`; MIT + BSD-3-Clause. Seven defects in `SensorPCF8563.hpp`, all in the error path — a `void` setter that cannot report an I²C failure among them. Read as evidence, driver written directly |
| **XPowersLib** | evaluated 2026-09-03 at `d6997586`; MIT. A failed register read is reported as every interrupt pending. Four register addresses are what was actually needed |
| **LilyGoLib** | evaluated 2026-09-01 at `38e6f8d`; MIT. Arduino-only composition, `assert(0)` on a missing PMU, and it pins an XPowersLib that predates the fix above. Exact-board evidence, never linked — [ADR-0017](../adr/0017-board-backends-compose-esp-idf-drivers.md) |
| **`rweather/Crypto`** | licence **UNVERIFIED**. Not cloned, not read, not usable until that changes. Recorded so it is not re-proposed |

Record rejections here with the reason — it stops the same option being
re-evaluated from scratch later.
