"""What image assets exist, at which pixel sizes, and why.

This is the single source of truth for the asset pipeline. `draw_icons.py`
authors from it and `generate_images.py` converts from it, so the two cannot
drift into disagreeing about which files should exist.

The unit here is the **pixel**, not the token and not the board. Keying on the
board would teach the firmware which board it is on, which is the one thing
`CLAUDE.md` forbids the layers above `platform/` to learn, and it would ship the
same picture twice whenever two boards landed on one size. They no longer do: at
the measured 220 dpi (D15, closed 2026-08-28) and 315 dpi the two size sets are
disjoint, and the 39 px collision this file used to cite as its example was an
artefact of the 1.3" placeholder. `SIZE_REASONS` still records every
(token, board) pair per pixel size — now one pair each, which is a fact about
these two panels and not a change of rule.
"""

# The two token systems this has to agree with. Both are duplicated from C++ on
# purpose and both are asserted against it: `tests/test_ui_tokens.cpp` pins the
# dp values and the simulator's board profiles carry the densities, so a change
# on either side that is not made here fails a test rather than silently
# producing an asset nobody asked for.
REFERENCE_DPI = 160                       # ui/include/attadipa/ui/metrics.h
ICON_DP = {"sm": 16, "md": 20, "lg": 24, "xl": 32}   # ui/.../tokens.h dp_of(IconSize)
BOARD_DPI = {"t-watch-s3-plus": 220, "waveshare-amoled-206": 315}


def px(dp: int, dpi: int) -> int:
    """`Metrics::px`, in Python. Integer, round to nearest, never zero."""
    if dp == 0:
        return 0
    half = REFERENCE_DPI // 2
    rounded = (dp * dpi + (half if dp > 0 else -half)) // REFERENCE_DPI
    return rounded if rounded != 0 else (1 if dp > 0 else -1)


def size_reasons() -> dict:
    """Every (token, board) pair that would ask for a given pixel size."""
    out: dict = {}
    for token, dp in ICON_DP.items():
        for board, dpi in BOARD_DPI.items():
            out.setdefault(px(dp, dpi), []).append((token, board))
    return out


SIZE_REASONS = size_reasons()

# The sizes actually generated, and the reason each is here rather than the
# whole cross-product.
#
# Eight distinct pixel sizes exist across four tokens and two boards; generating
# all of them for every icon would cost about 39 kB of flash for three icons
# nothing draws yet. These three cover T-Watch `lg` (33 px) and Waveshare `md`
# (39) and `lg` (47). Flash is not free and a cross-product is not a decision.
#
# T-Watch `md` is 28 px at the measured 220 dpi and is deliberately **not**
# generated: nothing draws it, and `icon()` returns nullptr for a size that was
# not generated rather than substituting a wrong one, so the omission fails
# loudly. A screen that wants it adds 28 here *and* an entry to
# `icon_drawings.GEOMETRY` — geometry is authored per size, never scaled.
SIZES = (33, 39, 47)

# name -> the drawing, by its function name in `icon_drawings.py`.
#
# Alpha-only masks (LVGL `A8`). An icon carries **no colour of its own**: it is
# recoloured at draw time through a `ColorRole`, the same way text is, so a theme
# change reaches it and `legible_as_graphic()` can refuse a role that cannot
# carry a thin shape on that theme's page. A baked-in colour would be a raw
# value in an asset, which is exactly what `tools/ui/check_raw_values.py` exists
# to prevent in source.
ICONS = {
    "mesh": "One node linked to two others. Mesh reachability, in the Clock's "
            "status row and in Settings. Not a triangle of nodes, deliberately: "
            "that silhouette collides with `warning` at 33 px.",
    "position": "A location pin. A position that is known — the shape says "
                "'a place', not 'a satellite', because the source of a fix is "
                "deliberately not visible to an application.",
    "warning": "A triangle with a bar and a dot. Exists because DESIGN_SYSTEM "
               "§3.1 forbids signalling a state by colour alone: a degraded "
               "state needs a shape, and on the day palette no accent clears "
               "4.5:1 anyway.",
}


def assets():
    """Every (icon, size) pair the pipeline is responsible for."""
    for name in sorted(ICONS):
        for size in SIZES:
            yield name, size


def source_name(name: str, size: int) -> str:
    return f"{name}_{size}.png"


def symbol(name: str, size: int) -> str:
    return f"attadipa_icon_{name}_{size}"
