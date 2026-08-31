# Image assets

Three directories and two arrows, which is what final §45 asks for:

```
ui/assets/source/     the art, committed, exact target size
        ↓  tools/assets/generate_images.py   (the pipeline)
ui/assets/generated/  LVGL C arrays, committed, plus INPUTS.sha256 — which
                      records the inputs *and* a hash of every file here
        ↓  attadipa_icons.cpp                (the one lookup)
a screen asks for an IconSize and a Metrics, and gets an lv_image_dsc_t or nothing
```

## The rules, and where each is enforced

**An icon has no colour.** Every icon is `LV_COLOR_FORMAT_A8` — a mask.
Colour arrives at draw time through a `ColorRole`, the same way it does for
text, so a theme reaches an icon and `legible_as_graphic()` can refuse a role
that cannot carry a thin shape on that theme's page. A baked colour would be a
raw value living in an asset, which is what `tools/ui/check_raw_values.py`
refuses in source. *Enforced by:* `--cf A8`, and `tests/test_ui_icons.cpp`
asserts the format of every linked descriptor.

**Nothing is ever resampled.** A pixel size with no source file is an error and
never a neighbouring size scaled to fit. Final §86 says small sizes are drawn
deliberately, and this is the one place a script can make that true rather than
aspirational. *Enforced by:* `generate_images.py` refusing a missing source,
`icon()` returning `nullptr` for a size that was not generated, and a test that
asserts the `nullptr` rather than tolerating it.

**A pixel size is a pixel size.** Assets are named by pixels, not by token and
not by board. Naming them by board would put the same picture in flash twice
whenever two boards landed on one size, and would teach the firmware which board
it is on, which is the thing `CLAUDE.md` forbids above `platform/`. At the
measured 220 dpi (D15) and 315 dpi the two boards' sizes are in fact disjoint —
the 39 px collision this file used to cite was an artefact of the 1.3"
placeholder — but the rule is about the unit, not about that coincidence. *Enforced by:* the manifest, and a test that asserts
the two lookups return the same pointer.

**Reference art is not asset art.** `docs/ui/reference/` holds 1440-pixel
concept sheets and `pics/` holds finished brand marks; §41 says neither is ever
compiled into firmware. *Enforced by:* the pipeline refusing any source under
those directories, and a dimension cap of 512 px — a little over the taller
board's 502, so a genuine full-screen asset would still pass.

## Regenerating

```
python3 tools/assets/draw_icons.py        # authoring: the drawings → source masks
python3 tools/assets/generate_images.py   # the pipeline: source masks → C arrays
python3 tools/assets/contact_sheet.py     # the review sheet, day and night, 1:1
```

The first two need **Pillow**, and `generate_images.py` additionally needs
**pypng** and **lz4** — module-scope imports inside the vendored `LVGLImage.py`,
required even though compression is off. `apt install python3-pil` plus
`pip install pypng lz4`.

Nothing in the build runs any of them. The generated tree is committed and four
tests notice when it goes stale.

The full-screen Clock background is deliberately different: original Attadipa
art at the panel's exact 410x502 geometry, converted to uncompressed RGB565.
It is not reference-sheet art and it is not a vendor image. Its prompt and
provenance live beside the source PNG in `source/backgrounds/README.md`.

`ui_images_are_current` is the primary gate and it **needs nothing installed**.
It compares two things, and the second one was missing until issue #69:

* the **inputs** digest, covering the art, the manifest, the **converter** and
  the **drawings** — an encoder that changes its output is the asset changing,
  and hashing `icon_drawings.py` means an edited stroke weight with nothing
  regenerated is caught by arithmetic rather than by a package;
* the **outputs**, each mask, the background and the generated header against
  its recorded SHA-256. An inputs digest alone says the tree was once built from
  these sources and nothing at all about the bytes in it, so a hand-edited
  bitmap byte used to pass green — verified as a reproducer, not a worry. The
  format is `tools/integrity/stamp.py`, and the font tree is bound by the same
  contract.

`ui_generated_outputs_reject_mutations` is the check on that check. It mutates
each of the fourteen committed outputs in turn — in a copy of the tree, never
this one — and fails if either pipeline accepted the mutation. It needs nothing
installed either.

`ui_icon_drawings_are_current` and `ui_image_pipeline_rejects_mistakes` have to
*draw* something, so they need **Pillow**. Without it CMake registers a
deliberately failing `ui_image_checks_unavailable` rather than skipping them,
because a skipped check reads as a passing one in a summary. CI installs it.

The one thing none of them can do is prove the committed bytes are what these
sources actually produce — a stamp beside a wrong file records the wrong file
faithfully. That takes regeneration:
`tools/integrity/reproducibility.py` builds both trees twice from two different
absolute paths and compares all sixteen files against what is committed. It has
been run and it passes; the CI job that would run it on every push is written,
not applied, and waiting on a permission — `tools/integrity/README.md`, T-128.

## Cost

426 097 B of `.rodata`: 14 457 B for the nine A8 masks and 411 640 B for the
410x502 RGB565 Clock background. Every declaration in
`generated/attadipa_images.h` carries its own number, and
[RESOURCE_BUDGET](../../docs/architecture/RESOURCE_BUDGET.md) carries the
reasoning about why three sizes and not seven.

`CALCULATED`, not `MEASURED`. Nothing has been linked into a firmware image, and
`idf.py size` is the only thing that settles the difference between an array's
size and its cost after alignment.

## What is not here yet

**The mascot.** [`docs/ui/reference/lumar_mascot_sheet.png`](../../docs/ui/reference/README.md)
supplies four named poses and DESIGN_SYSTEM §7 maps them to states. None of them
is in this directory, and the reason is the rule above: a pose lifted from a
1440-pixel sheet and shrunk to 40 px is noise, and the sheet is a desktop
concept drawing rather than source art. What it needs is a **derivation**
decision the owner makes looking at pixels — a cleaned source at a size the
pipeline will accept, or a redraw. Tracked as **T-034a**.
