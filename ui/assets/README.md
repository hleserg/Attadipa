# Image assets

Three directories and two arrows, which is what final §45 asks for:

```
ui/assets/source/     the art, committed, one file per icon per pixel size
        ↓  tools/assets/generate_images.py   (the pipeline)
ui/assets/generated/  LVGL C arrays, committed, plus INPUTS.sha256
        ↓  attadipa_icons.cpp                (the one lookup)
a screen asks for an IconSize and a Metrics, and gets an lv_image_dsc_t or nothing
```

## The rules, and where each is enforced

**An icon has no colour.** Every asset is `LV_COLOR_FORMAT_A8` — a mask.
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
not by board. `icon.size.lg` on the T-Watch's 261 dpi panel and `icon.size.md`
on the Waveshare's 315 dpi one are **both 39 px** — the same file, because it is
the same picture. Naming them by board would put two identical files in flash
and teach the firmware which board it is on, which is the thing `CLAUDE.md`
forbids above `platform/`. *Enforced by:* the manifest, and a test that asserts
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

Nothing in the build runs any of them. The generated tree is committed and three
tests notice when it goes stale.

`ui_images_are_current` is the primary gate and it **needs nothing installed** —
it compares a digest. That digest covers the **converter** and the **drawings**
as well as the art: an encoder that changes its output is the asset changing, and
hashing `icon_drawings.py` means an edited stroke weight with nothing regenerated
is caught by arithmetic rather than by a package.

`ui_icon_drawings_are_current` and `ui_image_pipeline_rejects_mistakes` have to
*draw* something, so they need **Pillow** — `apt install python3-pil`, or
`pip install pillow`. Without it CMake registers a deliberately failing
`ui_image_checks_unavailable` rather than skipping them, because a skipped check
reads as a passing one in a summary. CI installs it.

## Cost

14 457 B of `.rodata` for all nine masks — `A8` is one byte per pixel with
`stride == width`, so an icon costs exactly its pixel count. Every declaration
in `generated/attadipa_images.h` carries its own number, and
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
