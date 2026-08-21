"""Render the glyphs LVGL will actually draw, at the size it will draw them.

Final section 51 asks for legibility verified "at actual pixel sizes". A font
specimen from a foundry is rendered by a desktop rasteriser at a size nobody
ships; what reaches the wrist is lv_font_conv's own 1/2/4-bpp bitmap. So this
uses lv_font_conv's `dump` format -- the same rasterisation the firmware gets --
and tiles it into one image a person can look at.

Nearest-neighbour zoom, never smooth: the question is what the pixels are, and
a smoothing filter answers a different one.

Usage:
  python3 tools/font/contact_sheet.py --font F.ttf --lv-font-conv PATH \\
      --sizes 14,16,20 --bpp 4 --text "Привет Firefly 12:34" --out sheet.png
"""
import argparse, os, subprocess, tempfile
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


def row_for(conv, font, size, bpp, text, workdir):
    """One rendered line of `text`, laid out in codepoint order of the string."""
    d = dump_glyphs(conv, font, size, bpp, text, workdir)
    tiles = []
    for ch in text:
        if ch == " ":
            tiles.append(None)
            continue
        # lv_font_conv names the dump files by lowercase hex codepoint, no
        # padding and no "U+" -- 41f.png, not U+041F.png.
        path = os.path.join(d, f"{ord(ch):x}.png")
        if not os.path.exists(path):
            raise RuntimeError(f"no glyph rendered for U+{ord(ch):04X} {ch!r} at {size} px")
        # The dump PNGs are RGBA with a constant-255 alpha and the coverage in
        # the colour channels, so the alpha channel is not the glyph -- reading
        # it gives solid rectangles. Luminance is the glyph.
        tiles.append(Image.open(path).convert("L"))

    space = max(2, size // 3)
    width = sum(space if t is None else t.width + 1 for t in tiles)
    height = max(t.height for t in tiles if t is not None)
    # 255, not 0: the dump is dark ink on light paper, so the gaps between
    # words have to be paper too. Filling with 0 leaves black gaps that turn
    # into bright bars the moment the night sheet inverts the row.
    row = Image.new("L", (max(width, 1), height), 255)
    x = 0
    for t in tiles:
        if t is None:
            x += space
            continue
        # Bottom-align: the dump has no baseline, so this is a legibility
        # sheet and not a metrics proof. It says so on the image.
        row.paste(t, (x, height - t.height))
        x += t.width + 1
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
    work = tempfile.mkdtemp(prefix="firefly-sheet-")
    rows = [(s, row_for(a.lv_font_conv, a.font, s, a.bpp, a.text, work)) for s in sizes]

    # The dump is dark ink on light paper. Day keeps it; night inverts it,
    # because an OLED at 2 am is the other half of the same question and a
    # stroke that survives one does not automatically survive the other.
    if a.theme == "night":
        rows = [(s, ImageChops.invert(r)) for s, r in rows]
    paper, label, sub = (255, 60, 110) if a.theme == "day" else (16, 200, 150)

    W = LABEL_W + PAD * 2 + max(r.width for _s, r in rows) * ZOOM
    H = PAD * 2 + sum(r.height * ZOOM + PAD for _s, r in rows) + 22
    sheet = Image.new("L", (W, H), paper)
    draw = ImageDraw.Draw(sheet)
    draw.text((PAD, PAD), f"{os.path.basename(a.font)}  bpp {a.bpp}  {a.theme}  "
                          f"lv_font_conv dump, {ZOOM}x nearest neighbour, bottom-aligned", fill=sub)

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
