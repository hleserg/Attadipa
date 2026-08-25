#!/usr/bin/env python3
"""Generate both asset trees twice, from two different absolute paths, and compare.

The stamp in each generated tree binds committed bytes to a hash, which catches
a tree that changed. It cannot catch a tree that was never what its inputs
produce — a stamp written beside a wrong file records the wrong file faithfully.
Only regeneration settles that, and regeneration is only evidence if it is
reproducible.

Reproducible from *where* is the part that was broken. lv_font_conv writes its
own argv into the generated file, so until the `Opts:` line was normalized the
committed fonts carried one developer's `/mnt/e/projects/...` and every fresh
generation anywhere else differed in bytes while being identical in every glyph.
A byte-for-byte gate on top of that could only ever produce false alarms, which
is how a repository ends up with a check nobody trusts and therefore nobody
reads.

So this makes the path the variable. Two copies of the working tree, one at a
deliberately short path and one at a long one, both generated with the same
pinned converter, and three comparisons rather than one:

1. the two copies agree with each other — nothing that varies with the path
   reached the output;
2. each agrees with what is committed — the committed tree is what these inputs
   actually produce, not what something once produced;
3. the stamps agree too, so the thing every host CI job checks against is
   itself a product of this generation.

It needs Node and the pinned `lv_font_conv` for the fonts, and Pillow, pypng and
lz4 for the images. That is why it belongs in one CI job rather than in every
host build: the cheap check in `tools/integrity/stamp.py` runs everywhere and
catches mutation, and this runs once and catches the thing mutation checks
cannot see. That job is written and not yet applied — see `README.md` beside
this file, and T-128.

  python3 tools/integrity/reproducibility.py \\
      --ttf /tmp/NunitoSans.ttf --converter ./node_modules/.bin/lv_font_conv
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))

import stamp  # noqa: E402

# Everything a generation reads or writes.
TREE = (
    "tools",
    "assets/fonts/generated",
    "ui/assets/generated",
    "ui/assets/source",
)

# Two names that differ in length as well as in content. A path-dependent output
# usually leaks the path itself, but a length-dependent one — a wrapped comment,
# a column-aligned table — would survive two same-length directories.
COPIES = ("a", "a-second-checkout-with-a-deliberately-longer-absolute-path")

FONT_OUTPUTS = [f"assets/fonts/generated/attadipa_nunito_sans_{size}.c"
                for size in (14, 16, 20, 28, 64, 84, 96)]
FONT_OUTPUTS.append("assets/fonts/generated/INPUTS.sha256")

IMAGE_OUTPUTS = [f"ui/assets/generated/attadipa_icon_{name}_{size}.c"
                 for name in ("mesh", "position", "warning")
                 for size in (33, 39, 47)]
IMAGE_OUTPUTS.append(
    "ui/assets/generated/attadipa_background_clock_meadow_night_410x502.c")
IMAGE_OUTPUTS += ["ui/assets/generated/attadipa_images.h",
                  "ui/assets/generated/INPUTS.sha256"]


def copy_tree(destination: Path) -> None:
    for relative in TREE:
        shutil.copytree(ROOT / relative, destination / relative,
                        ignore=shutil.ignore_patterns("__pycache__"))


def generate(checkout: Path, ttf: Path, converter: Path, skip_images: bool) -> None:
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    runs = [[sys.executable, "-B", str(checkout / "tools/font/generate_ui_fonts.py"),
             "--ttf", str(ttf), "--converter", str(converter)]]
    if not skip_images:
        runs.append([sys.executable, "-B",
                     str(checkout / "tools/assets/generate_images.py")])
    for command in runs:
        result = subprocess.run(command, capture_output=True, text=True,
                                cwd=str(checkout), env=environment)
        if result.returncode != 0:
            raise SystemExit(
                f"generation failed in {checkout.name}:\n"
                f"  {' '.join(command)}\n{result.stdout}{result.stderr}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ttf", required=True, help="path to the pinned Nunito Sans variable TTF")
    parser.add_argument("--converter", required=True, help="path to lv_font_conv")
    parser.add_argument("--skip-images", action="store_true",
                        help="fonts only, for a machine without Pillow, pypng and lz4")
    arguments = parser.parse_args()

    ttf = Path(arguments.ttf).resolve()
    converter = Path(arguments.converter).resolve()
    expected = list(FONT_OUTPUTS)
    if not arguments.skip_images:
        expected += IMAGE_OUTPUTS
    else:
        print("images: SKIPPED at the caller's request — fonts only")

    problems = []
    with tempfile.TemporaryDirectory() as directory:
        checkouts = []
        for name in COPIES:
            checkout = Path(directory) / name
            copy_tree(checkout)
            generate(checkout, ttf, converter, arguments.skip_images)
            checkouts.append(checkout)
            print(f"generated in {checkout}")

        first, second = checkouts
        print(f"\npath lengths differ by {len(str(second)) - len(str(first))} characters\n")

        for relative in expected:
            digests = {
                "first copy": stamp.sha256_of(first / relative),
                "second copy": stamp.sha256_of(second / relative),
                "committed": stamp.sha256_of(ROOT / relative),
            }
            if len(set(digests.values())) == 1:
                print(f"  ok    {relative}  {digests['committed'][:16]}")
                continue
            problems.append(relative)
            print(f"  FAIL  {relative}")
            for label, digest in digests.items():
                print(f"          {label:<12} {digest}")

    if problems:
        print(f"\n{len(problems)} generated file(s) are not reproducible or are not what "
              f"is committed:", file=sys.stderr)
        for relative in problems:
            print(f"  * {relative}", file=sys.stderr)
        print("\nIf the two copies agree with each other and disagree with what is "
              "committed, the committed tree is stale: regenerate and commit the result.\n"
              "If the two copies disagree with each other, something path-dependent "
              "reached the output and normalizing it is the fix — not widening this "
              "check.", file=sys.stderr)
        return 1

    print(f"\nreproducible: {len(expected)} generated file(s) are identical across two "
          f"checkout paths and identical to what is committed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
