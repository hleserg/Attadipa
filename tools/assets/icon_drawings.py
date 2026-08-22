"""How the first icons are drawn, at each pixel size, deliberately.

Final §86 and DESIGN_SYSTEM §7 both say the same thing: *art is re-derived for
the watch, not scaled down; small sizes are drawn deliberately.* This file is
what that means in practice. Every icon's geometry is a function of the target
**pixel** size and every size has its own entry in `GEOMETRY` — stroke weight,
feature radii and inset are chosen for that size, not multiplied out of another
one. A 47-pixel icon shrunk to 33 pixels is a different, worse drawing, and the
difference is exactly the detail that survives the resampler.

Antialiasing is supersampling: each shape is rasterised at eight times the
target and box-averaged down. That is *not* the thing §86 forbids — the geometry
is defined in the target's own coordinates and the supersample only decides how
a boundary pixel is shaded.

Output is a single-channel mask. An icon has no colour of its own; see
`manifest.ICONS`.
"""

import math

from PIL import Image, ImageDraw

SUPERSAMPLE = 8

# One entry per pixel size in `manifest.SIZES`. These are drawing decisions, not
# a formula: a stroke of 3 at 33 px is about 9 % of the icon's width and a stroke
# of 5 at 47 px is about 11 %, and the small size is deliberately the lighter of
# the two because a heavy stroke closes up the gaps first.
GEOMETRY = {
    33: {"stroke": 3, "inset": 3, "dot": 4.0, "gap": 2.0},
    39: {"stroke": 4, "inset": 3, "dot": 4.5, "gap": 2.5},
    47: {"stroke": 5, "inset": 4, "dot": 5.5, "gap": 3.0},
}


def _canvas(size: int):
    img = Image.new("L", (size * SUPERSAMPLE, size * SUPERSAMPLE), 0)
    return img, ImageDraw.Draw(img)


def _down(img: Image.Image, size: int) -> Image.Image:
    # BOX is an exact area average over each SUPERSAMPLE x SUPERSAMPLE block, so
    # a pixel's alpha is literally the fraction of it the shape covers.
    return img.resize((size, size), Image.Resampling.BOX)


def _disc(draw, cx, cy, r, fill=255):
    s = SUPERSAMPLE
    draw.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=fill)


def _line(draw, x0, y0, x1, y1, width, fill=255):
    s = SUPERSAMPLE
    draw.line([x0 * s, y0 * s, x1 * s, y1 * s], fill=fill, width=int(round(width * s)))


def mesh(size: int) -> Image.Image:
    """One node linked to two others.

    The first drawing was a hub with three peers around it, and at 33 px it was
    a blob: with a hub of nine pixels and peers of eight, the ring that fits
    inside a 33-pixel square leaves under three pixels of visible link, so the
    four discs merge into one lump and the icon stops meaning "connected".
    Three nodes in a triangle solved the crowding and created a worse problem —
    a triangular silhouette one row away from `warning`.

    So: a node on the left, two on the right, two links. The silhouette is
    unmistakably not a triangle, the links are thirteen pixels of clear stroke
    at the smallest size, and the meaning — *this one reaches those* — is the
    one the Clock's status row needs.
    """
    g = GEOMETRY[size]
    img, draw = _canvas(size)
    r = g["dot"]
    left = (g["inset"] + r, size / 2.0)
    right = [(size - g["inset"] - r, g["inset"] + r),
             (size - g["inset"] - r, size - g["inset"] - r)]

    for pxy in right:
        _line(draw, left[0], left[1], pxy[0], pxy[1], g["stroke"])
    for pxy in right + [left]:
        _disc(draw, pxy[0], pxy[1], r)
    return _down(img, size)


def position(size: int) -> Image.Image:
    """A pin: a place, deliberately not a satellite.

    An application is never told where a fix came from — the receiver on the
    wrist, a companion node's, or a coordinate out of somebody else's message —
    so the icon must not draw a satellite, an antenna or a phone. It draws the
    only thing all of those produce.

    The silhouette is a disc and a tapered tail sharing a tangent, with the eye
    knocked out rather than stroked, because at 33 px an outline pin's wall and
    its hole compete for the same three pixels.
    """
    g = GEOMETRY[size]
    img, draw = _canvas(size)
    c = size / 2.0
    head_r = (size - 2 * g["inset"]) / 2.0 * 0.72
    head_y = g["inset"] + head_r
    tip_y = size - g["inset"]

    # Tangent points from the tip to the head circle: the tail meets the disc
    # smoothly instead of crossing it, which is what makes a pin look drawn
    # rather than assembled.
    d = tip_y - head_y
    if d <= head_r:
        raise ValueError(f"pin geometry degenerate at {size} px")
    alpha = math.asin(head_r / d)
    beta = math.acos(head_r / d)
    half = head_r * math.sin(beta)
    tail = [
        (c, tip_y),
        (c - head_r * math.cos(alpha) * 1.0, head_y + head_r * math.sin(alpha)),
        (c + head_r * math.cos(alpha) * 1.0, head_y + head_r * math.sin(alpha)),
    ]
    _ = half  # kept: the tangent half-width, useful if the tail is ever stroked

    s = SUPERSAMPLE
    draw.polygon([(x * s, y * s) for x, y in tail], fill=255)
    _disc(draw, c, head_y, head_r)
    _disc(draw, c, head_y, head_r - g["stroke"], fill=0)
    return _down(img, size)


def warning(size: int) -> Image.Image:
    """A triangle, a bar and a dot.

    It exists because DESIGN_SYSTEM §3.1 forbids signalling a state by colour
    alone — and on the day palette that rule is not merely an accessibility
    courtesy, since no accent in the palette clears 4.5:1 against Warm Ivory. A
    degraded state has to have a shape.

    The triangle is stroked and the bar knocked out of nothing, rather than a
    filled triangle with a knocked-out bar, so that the icon's weight matches
    `mesh` and `position` instead of becoming the heaviest thing on the row.
    """
    g = GEOMETRY[size]
    img, draw = _canvas(size)
    inset = g["inset"]
    w = g["stroke"]
    apex = (size / 2.0, inset)
    left = (inset, size - inset)
    right = (size - inset, size - inset)

    # Stroked as three lines with discs at the corners: a polygon outline in PIL
    # leaves the joins square, and a square join at 33 px reads as a notch.
    for a, b in ((apex, left), (left, right), (right, apex)):
        _line(draw, a[0], a[1], b[0], b[1], w)
    for p in (apex, left, right):
        _disc(draw, p[0], p[1], w / 2.0)

    # The bar sits in the lower two thirds, where the triangle is wide enough
    # for it, and the dot below it with a gap that survives the downsample.
    bar_top = inset + (size - 2 * inset) * 0.38
    dot_r = w / 2.0
    dot_cy = size - inset - g["gap"] - dot_r - w * 0.35
    bar_bottom = dot_cy - dot_r - g["gap"]
    _line(draw, size / 2.0, bar_top, size / 2.0, bar_bottom, w)
    _disc(draw, size / 2.0, dot_cy, dot_r)
    return _down(img, size)


def companion(size: int) -> Image.Image:
    """A handset: the phone this watch is paired with.

    Three drawings were tried at 33 px. A bare rounded-rectangle outline is a
    phone to nobody — it is equally a door, a battery or a toggle, and it sits
    one row away from the battery gauge, which is exactly the collision that
    sent `mesh` back to the drawing board twice. A filled screen with a chin
    reads as a *filled* thing, which on this row means "charged".

    Two interior marks fix it: a speaker slot near the top and a home dot near
    the bottom. Nothing else on a watch face has that pair, and the silhouette
    is portrait where the battery is landscape, so the two cannot be confused
    even out of focus.

    The capability is called `Phone` to a person and `CompanionLink` in the
    code, and the icon is drawn for the person: a phone, not a Bluetooth rune.
    Which transport carries the link is precisely what an application is never
    told (ADR-0008), so the icon must not draw one.
    """
    g = GEOMETRY[size]
    img, draw = _canvas(size)
    s = SUPERSAMPLE
    w = g["stroke"]
    inset = g["inset"]

    body_h = size - 2 * inset
    body_w = round(body_h * 0.66)
    x0 = (size - body_w) / 2.0
    x1 = x0 + body_w
    y0, y1 = float(inset), float(size - inset)
    radius = w * 1.6

    draw.rounded_rectangle([x0 * s, y0 * s, x1 * s, y1 * s], radius=radius * s, fill=255)
    draw.rounded_rectangle([(x0 + w) * s, (y0 + w) * s, (x1 - w) * s, (y1 - w) * s],
                           radius=max(radius - w, 1) * s, fill=0)

    cx = size / 2.0
    slot_w = body_w * 0.34
    slot_y = y0 + w + g["gap"] + w * 0.5
    _line(draw, cx - slot_w / 2.0, slot_y, cx + slot_w / 2.0, slot_y, w * 0.75)
    _disc(draw, cx, y1 - w - g["gap"] - w * 0.5, w * 0.55)
    return _down(img, size)


DRAWINGS = {"mesh": mesh, "position": position, "warning": warning,
            "companion": companion}
