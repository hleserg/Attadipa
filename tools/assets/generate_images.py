#!/usr/bin/env python3
"""The image asset pipeline: `ui/assets/source/` -> `ui/assets/generated/`.

Final section 45 names three directories and this is the arrow between the
second and the third. It converts committed source art into LVGL C arrays with
the vendored `LVGLImage.py`, writes a header that declares them, and records an
inputs digest so that a stale generated tree is a failing test rather than a
surprise on a panel.

Deliberate properties, each of which had to be chosen rather than inherited:

* **Alpha only.** Every icon is `LV_COLOR_FORMAT_A8` — a mask with no colour of
  its own, recoloured at draw time through a `ColorRole`. A baked colour would
  be a raw value living in an asset, exactly what `tools/ui/check_raw_values.py`
  refuses in source, and it would make a theme change stop at the icon.
* **No compression.** `--compress NONE`. RLE and LZ4 both cost decode time and
  a scratch buffer on a device with 8 MB of PSRAM behind a QSPI bus and no
  measurement yet of what that costs. Nine masks are 15 kB; the trade is not
  worth making before anything has been measured.
* **It refuses to invent a size.** A pixel size with no source file is an error
  and never a resample of a neighbouring one. That is what final section 86
  actually requires — *small sizes are drawn deliberately* — and it is the one
  rule in the whole pipeline that a script can enforce and a person cannot.
* **It refuses the reference art.** `docs/ui/reference/` holds 1440-pixel
  concept sheets that the specification says are never compiled into firmware.
  A dimension cap turns that sentence into a check.

`--check` needs neither the converter nor a configured build to run, so CI can
fail a stale tree in a second. It compares the recorded digest against the
current inputs **and** every committed output against its recorded SHA-256 —
`tools/integrity/stamp.py` holds that contract, and the font pipeline is bound
by the same one. An inputs digest alone says the tree was once made from these
sources and nothing about the bytes in it, so a hand-edited mask used to pass.
"""

import argparse
import hashlib
import io
import subprocess
import sys
import textwrap
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(ROOT / "tools" / "integrity"))

import manifest  # noqa: E402
import stamp  # noqa: E402

SOURCE_DIR = ROOT / "ui" / "assets" / "source" / "icons"
OUT_DIR = ROOT / "ui" / "assets" / "generated"
VENDOR = HERE / "vendor" / "LVGLImage.py"
DIGEST_FILE = OUT_DIR / "INPUTS.sha256"
HEADER = OUT_DIR / "attadipa_images.h"

# No source image may be larger than this in either dimension. It is a little
# over the Waveshare's 502-pixel height, so a genuine full-screen asset for the
# larger board would still pass while a 1440-pixel concept sheet cannot.
MAX_SOURCE_EDGE = 512

# Directories a source image may never come from, regardless of its size.
FORBIDDEN_ROOTS = (ROOT / "docs", ROOT / "pics")


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    h.update(path.read_bytes())
    return h.hexdigest()


def inputs() -> list:
    """Everything whose change should invalidate the generated tree.

    The converter is in here as well as the art: an encoder that changes its
    output *is* the asset changing, and a digest that only covered the PNGs
    would call a re-encoded tree current.

    So are the drawings, even though this stage never reads them. `draw_icons.py
    --check` is the direct test of "the masks match their drawings", but it needs
    Pillow, and a machine without Pillow should still be able to notice that
    somebody edited a stroke weight and committed nothing. Hashing the drawing
    module costs nothing and closes that hole with arithmetic instead of a
    dependency.
    """
    files = [HERE / "manifest.py", HERE / "generate_images.py",
             HERE / "icon_drawings.py", VENDOR]
    files += [SOURCE_DIR / manifest.source_name(n, s) for n, s in manifest.assets()]
    return files


def digest() -> str:
    h = hashlib.sha256()
    for f in inputs():
        rel = f.relative_to(ROOT).as_posix()
        h.update(rel.encode())
        h.update(b"\0")
        h.update(f.read_bytes())
        h.update(b"\0")
    return h.hexdigest()


def outputs() -> list:
    """Every file this pipeline is responsible for: nine masks and the header.

    The header is in the list because it is generated too. It declares the nine
    symbols and carries the X-macro the C++ lookup is built from, so a truncated
    or hand-edited header is a firmware defect exactly as a corrupted mask is —
    and it was the one output the old existence check could not tell apart from
    a correct one.
    """
    return [OUT_DIR / f"{manifest.symbol(n, s)}.c" for n, s in manifest.assets()] + [HEADER]


STAMP_EXPLANATION = """\
What the generated image tree was built from, and what was built. The file is
still called INPUTS.sha256 because several documents cite it by name; it has
recorded outputs as well since issue #69, and this line is here so that nobody
has to infer that from the name.

`inputs` covers the source art, the manifest, the drawings and the vendored
converter — an encoder that changes its output *is* the asset changing. Each
`output` line is the SHA-256 of a committed file, so an edited bitmap byte, a
truncated header or a deleted mask fails `--check` on a machine with neither
Pillow nor a build.

Written only by tools/assets/generate_images.py. Editing a line here to make a
check pass is the one repair that fixes nothing.\
"""


def check_source(path: Path) -> None:
    for root in FORBIDDEN_ROOTS:
        try:
            path.relative_to(root)
        except ValueError:
            continue
        raise SystemExit(
            f"generate_images: {path} is under {root.name}/, which holds reference "
            f"and brand art. Those are never compiled into firmware — final section 41. "
            f"Derive a source asset into ui/assets/source/ instead.")
    from PIL import Image
    with Image.open(path) as im:
        w, h = im.size
    if max(w, h) > MAX_SOURCE_EDGE:
        raise SystemExit(
            f"generate_images: {path.name} is {w}x{h}; the cap is {MAX_SOURCE_EDGE} px. "
            f"A desktop-sized illustration is not a watch asset, and shrinking it here "
            f"would be the scaling final section 86 forbids. Draw the size you need.")


def convert(name: str, size: int) -> Path:
    src = SOURCE_DIR / manifest.source_name(name, size)
    if not src.exists():
        raise SystemExit(
            f"generate_images: no source for {name} at {size} px ({src.name}). "
            f"This pipeline never resamples one size into another — author it in "
            f"tools/assets/icon_drawings.py and run draw_icons.py.")
    check_source(src)
    from PIL import Image
    with Image.open(src) as im:
        if im.size != (size, size):
            raise SystemExit(
                f"generate_images: {src.name} is {im.size[0]}x{im.size[1]} but its name "
                f"claims {size}. The name is the contract; fix one of them.")
    subprocess.run(
        [sys.executable, str(VENDOR), "--ofmt", "C", "--cf", "A8",
         "--compress", "NONE", "--name", manifest.symbol(name, size),
         "-o", str(OUT_DIR), str(src)],
        check=True, stdout=subprocess.DEVNULL)
    return OUT_DIR / f"{manifest.symbol(name, size)}.c"


def write_header(sizes: dict) -> None:
    out = io.StringIO()
    out.write("// GENERATED by tools/assets/generate_images.py — do not edit.\n")
    out.write("//\n")
    out.write("// Alpha-only masks. An icon here has no colour: it is recoloured at draw\n")
    out.write("// time through a ColorRole, so a theme reaches it and legible_as_graphic()\n")
    out.write("// can refuse a role that cannot carry a thin shape on that theme's page.\n")
    out.write("//\n")
    out.write("// The number in each name is a **pixel** size, not a token and not a board.\n")
    out.write("// 39 px is icon.size.lg on the T-Watch's 261 dpi panel and icon.size.md on\n")
    out.write("// the Waveshare's 315 dpi one — the same file, because it is the same icon.\n")
    out.write("#pragma once\n\n")
    out.write("#include \"lvgl.h\"\n\n")
    out.write("#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n")
    for name in sorted(manifest.ICONS):
        for line in textwrap.wrap(manifest.ICONS[name], 74):
            out.write(f"// {line}\n")
        for size in manifest.SIZES:
            sym = manifest.symbol(name, size)
            out.write(f"LV_IMAGE_DECLARE({sym});   // {sizes[sym]} B of .rodata\n")
        out.write("\n")
    # The list as an X-macro, so that the C++ that resolves an IconSize and a
    # density into one of these cannot drift from what was generated. A picker
    # with a hand-written table is a picker that silently keeps returning the
    # icon you deleted.
    out.write("// Every generated asset, for a table that must not be maintained by hand.\n")
    out.write("// X(name, pixel_size, symbol)\n")
    out.write("#define ATTADIPA_ICON_LIST(X) \\\n")
    rows = list(manifest.assets())
    for i, (name, size) in enumerate(rows):
        tail = " \\" if i + 1 < len(rows) else ""
        out.write(f"    X({name}, {size}, {manifest.symbol(name, size)}){tail}\n")
    out.write("\n")
    out.write("#ifdef __cplusplus\n}\n#endif\n")
    HEADER.write_text(out.getvalue(), encoding="utf-8", newline="\n")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="verify the generated tree matches the sources; write nothing")
    args = ap.parse_args()

    missing = [f for f in inputs() if not f.exists()]
    if missing:
        for f in missing:
            print(f"generate_images: missing input {f}", file=sys.stderr)
        return 2

    want = digest()

    if args.check:
        problems = stamp.verify(DIGEST_FILE, want, outputs())
        if problems:
            print(f"generate_images: {OUT_DIR.relative_to(ROOT)} does not match its "
                  f"stamp:\n", file=sys.stderr)
            for problem in problems:
                print(f"  * {problem}", file=sys.stderr)
            print("\n  run: python3 tools/assets/generate_images.py", file=sys.stderr)
            return 1
        print(f"images: {len(outputs())} generated file(s) match their recorded hashes, "
              f"inputs unchanged")
        return 0

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    sizes = {}
    total = 0
    for name, size in manifest.assets():
        c = convert(name, size)
        sym = manifest.symbol(name, size)
        # The .rodata cost is the pixel count: A8 is one byte per pixel with a
        # stride equal to the width, plus a 12-byte descriptor the linker places
        # separately. Reported rather than guessed at, so RESOURCE_BUDGET can be
        # written from it.
        sizes[sym] = size * size
        total += sizes[sym]
        print(f"  {c.name:38s} {size:3d}x{size:<3d} {sizes[sym]:6d} B")
    write_header(sizes)
    # Last, and in one replace: the stamp binds the inputs to the bytes that
    # were just written, so it must not exist in a state that describes half of
    # them. `want` was computed before anything was generated, which is correct
    # — the inputs are what they were when the run started.
    stamp.write(DIGEST_FILE, STAMP_EXPLANATION, want, outputs())
    print(f"images: {len(sizes)} asset(s), {total} B of .rodata "
          f"({total / 1024.0:.1f} kB), A8, uncompressed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
