"""Render the glyphs LVGL will actually draw, at the size it will draw them.

Final section 51 asks for legibility verified "at actual pixel sizes". A font
specimen from a foundry is rendered by a desktop rasteriser at a size nobody
ships; what reaches the wrist is lv_font_conv's own 1/2/4-bpp bitmap. So this
uses lv_font_conv's `dump` format -- the same rasterisation the firmware gets --
and lays it out with the same metrics the firmware will: advance width, left
side bearing and kerning, all read from the `font_info.json` the dump writes
beside the images.

Nearest-neighbour zoom, never smooth: the question is what the pixels are, and
a smoothing filter answers a different one.

Usage:
  python3 tools/font/contact_sheet.py --font F.ttf --lv-font-conv PATH \\
      --sizes 14,16,20 --bpp 4 --text "Привет Attadipa 12:34" --out sheet.png
"""
import argparse, json, math, os, subprocess, tempfile
from PIL import Image, ImageChops, ImageDraw

ZOOM = 4
PAD = 8
LABEL_W = 92


def dump_glyphs(conv, font, size, bpp, text, workdir):
    out = os.path.join(workdir, f"dump-{size}")
    args = [conv, "--font", font, "--size", str(size), "--bpp", str(bpp),
            "--format", "dump", "--symbols", text, "-o", out]
    p = subprocess.run(args, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"lv_font_conv dump failed at {size} px:\n{p.stdout}{p.stderr}")
    return out


def coverage(path):
    """The glyph's ink as 8-bit ink-on-paper, with the dump's annotation removed.

    lv_font_conv's dump writer (lib/writers/dump.js) stores each pixel as
    `(255 - value) * colour / 255`, where `colour` is white inside the advance
    width and the typo ascent/descent band, and PINK -- (255, 127, 184) -- for
    every pixel outside it. A glyph whose bounding box overhangs its advance
    therefore gets a full-height pink column, and `convert("L")` reads that
    column as mid-grey ink: a bar through the specimen that is not in the font.

    Both colours have red = 255, so the red channel is exactly `255 - value`
    for every pixel of every glyph. Take it, and the annotation disappears
    without touching a single pixel of coverage.
    """
    return Image.open(path).convert("RGB").split()[0]


def row_for(conv, font, size, bpp, text, workdir):
    """One line of `text`, laid out the way LVGL will lay it out.

    Not a row of tiles with a gap between them. The dump crops each PNG to the
    ink bounding box, so pasting them end to end throws away the side bearings
    and reports spacing the firmware will never draw -- and letter spacing is
    half of whether 14 px Cyrillic is readable at all. Advance width, left side
    bearing and kerning all come out of font_info.json.
    """
    d = dump_glyphs(conv, font, size, bpp, text, workdir)
    info = json.load(open(os.path.join(d, "font_info.json"), encoding="utf-8"))
    glyphs = {g["code"]: g for g in info["glyphs"]}
    typo_asc, typo_desc = info["typoAscent"], info["typoDescent"]

    # The dump's PNG spans y = max(bbox top, typoAscent) down to
    # y = min(bbox bottom, typoDescent), so every glyph inside the typo band is
    # already the same height and aligned on the same baseline. Glyphs that
    # exceed it -- an accented capital at a small size -- make the row taller,
    # which is why the extent is computed rather than assumed.
    top = typo_asc
    bottom = typo_desc
    for ch in text:
        g = glyphs.get(ord(ch))
        if g is None or g["bbox"]["width"] == 0:
            continue
        b = g["bbox"]
        top = max(top, b["y"] + b["height"] - 1)
        bottom = min(bottom, b["y"])
    height = top - bottom + 1

    placed = []          # (x, y, tile)
    pen = 0.0            # fractional, because adv_w is fractional in the font
    for i, ch in enumerate(text):
        g = glyphs.get(ord(ch))
        if g is None:
            raise RuntimeError(f"no glyph rendered for U+{ord(ch):04X} {ch!r} at {size} px")
        b = g["bbox"]
        if b["width"] > 0:
            path = os.path.join(d, f"{ord(ch):x}.png")
            if not os.path.exists(path):
                raise RuntimeError(f"font_info has U+{ord(ch):04X} but {path} is missing")
            tile = coverage(path)
            glyph_top = max(b["y"] + b["height"] - 1, typo_asc)
            placed.append((int(round(pen)) + b["x"], top - glyph_top, tile))
        pen += g["advanceWidth"]
        if i + 1 < len(text):
            # Kerning is keyed by the *next* codepoint, in pixels, and it is
            # fractional. LVGL applies it; a specimen that does not is a
            # specimen of a font nobody ships.
            pen += g.get("kerning", {}).get(str(ord(text[i + 1])), 0.0)

    width = max([math.ceil(pen)] + [x + t.width for x, _y, t in placed] + [1])
    # 255, not 0: the dump is dark ink on light paper, so the space between
    # words has to be paper too. Filling with 0 leaves black gaps that turn
    # into bright bars the moment the night sheet inverts the row.
    row = Image.new("L", (width, height), 255)
    for x, y, tile in placed:
        # Darken rather than paste: side bearings can be negative and two
        # glyphs can legitimately overlap, and a paste would rub out the ink
        # of the one underneath.
        box = (x, y, x + tile.width, y + tile.height)
        row.paste(ImageChops.darker(row.crop(box), tile), box)
    return row


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", required=True)
    ap.add_argument("--lv-font-conv", required=True)
    ap.add_argument("--sizes", default="14,16,20,28")
    ap.add_argument("--bpp", type=int, default=4)
    ap.add_argument("--text", required=True)
    ap.add_argument("--theme", choices=("day", "night"), default="night",
                    help="Both are checked, because the Definition of Done says both are. "
                         "The dump is ink-on-paper; night inverts it.")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    sizes = [int(s) for s in a.sizes.split(",")]
    work = tempfile.mkdtemp(prefix="attadipa-sheet-")
    rows = [(s, row_for(a.lv_font_conv, a.font, s, a.bpp, a.text, work)) for s in sizes]

    # The dump is dark ink on light paper. Day keeps it; night inverts it,
    # because an OLED at 2 am is the other half of the same question and a
    # stroke that survives one does not automatically survive the other.
    if a.theme == "night":
        rows = [(s, ImageChops.invert(r)) for s, r in rows]
    # Night paper is 0, not a dark grey: the row itself inverts to 0, and a
    # page one shade off it would frame every line in a rectangle that is an
    # artefact of this script rather than anything the panel does.
    paper, label, sub = (255, 60, 110) if a.theme == "day" else (0, 190, 140)

    W = LABEL_W + PAD * 2 + max(r.width for _s, r in rows) * ZOOM
    H = PAD * 2 + sum(r.height * ZOOM + PAD for _s, r in rows) + 22
    sheet = Image.new("L", (W, H), paper)
    draw = ImageDraw.Draw(sheet)
    draw.text((PAD, PAD), f"{os.path.basename(a.font)}  bpp {a.bpp}  {a.theme}  "
                          f"lv_font_conv dump, {ZOOM}x nearest neighbour, "
                          f"advance + kerning from font_info.json", fill=sub)

    y = PAD + 22
    for size, row in rows:
        draw.text((PAD, y + 2), f"{size} px", fill=label)
        big = row.resize((row.width * ZOOM, row.height * ZOOM), Image.NEAREST)
        sheet.paste(big, (LABEL_W, y))
        y += big.height + PAD

    sheet.save(a.out)
    print(f"{a.out}  {sheet.width}x{sheet.height}  sizes {sizes} bpp {a.bpp}")


if __name__ == "__main__":
    main()
