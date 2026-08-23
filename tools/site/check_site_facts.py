#!/usr/bin/env python3
"""Are the numbers in docs/site/SEO.md and docs/index.html the repository's own?

Three of the dimension pairs in that section were wrong on the first pass, and
they were wrong for one reason: they were typed. The head two files away is
guarded by a check; the numbers beside it were guarded by a sentence. Reading
them out of the file headers costs one step and closes the substitution.

What is checked:

1. Every size and every `N x M` pair stated in SEO.md, against the file named in
   the same segment of the same line -- a table cell, or a semicolon-separated
   clause in prose.
2. Every `width=`/`height=` in docs/index.html against the *shape* of the image
   it points at -- the layout-shift source the section is about. What reflows is
   a declared box of the wrong proportion: the browser reserves that box, the
   image arrives with a different aspect ratio and the row changes height. A box
   that is a uniform scale of the file is not that defect and is not reported --
   the brand mark is a 64 x 64 PNG deliberately drawn at 34 and at 28, and a
   check that called those wrong would be training the reader to ignore it.
3. The saving column of a conversion row, against the two files it compares, and
   every statement of the total against the sum of those savings -- the total is
   quoted twice in the document and both copies are held to the same files.
4. The stylesheet and script sizes claimed in the performance section, which are
   not images but are the same typed-number defect in the same document. This
   found the script stated at 6 KB when it had grown past 7.8; the numbers most
   likely to go stale are the ones an unrelated commit changes.
5. The size of the head-sync mutation suite, which SEO.md quotes. That number has
   been wrong twice -- it is the kind a commit changes without looking at the
   prose -- so the suite now prints its own count and this reads it back.

Attribution is by segment, and it is the part that had to be fixed before this
was worth running. The first draft attributed by line and required exactly one
filename on it. Every row in the table names two -- the original and the
converted -- so every row was skipped, and the check reported "9 images
measured" while verifying nothing the document said. A number that cannot be
attributed is therefore reported, never skipped, and the summary line states how
many facts were actually compared: a check that silently finds nothing to do
must not print the same line as one that agrees.

Precision follows the document. `1.32 MB` claims two decimals and is held to
within half of the last one; `43 KB` claims none and is held to within half a
kilobyte. Stating a rounder number is allowed, and is not silently promoted to
a more precise one.

Header parsing rather than Pillow: this runs in the `docs` job, which installs
nothing, and the three formats here are three well-documented headers. An
unreadable file is reported, never skipped.

Run: python3 tools/site/check_site_facts.py [root]
"""

from __future__ import annotations

import re
import struct
import subprocess
import sys
from pathlib import Path

ASSETS = "docs/assets"
IMAGE_SUFFIXES = (".png", ".jpg", ".jpeg", ".webp")

PAIR = re.compile(r"(\d{2,5})\s*[x×]\s*(\d{2,5})")
SIZE = re.compile(r"(\d+(?:\.\d+)?)\s*(MB|KB)\b")
# The total is written "**2.8 MB off the first page view**" in one place and
# "the 2.8 MB image saving" in another; the emphasis falls in different places
# and both are claims about the same two files, so both are matched.
TOTAL = re.compile(
    r"(\d+(?:\.\d+)?)\s*(MB|KB)\**\s*(?:off the first page view|image saving)"
)
# Attribution by keyword rather than by filename: the sentence names these two
# by what they are, and rewording it to carry paths would be worse prose for no
# reader's benefit.
TEXT_ASSETS = (
    (re.compile(r"(\d+(?:\.\d+)?)\s*(MB|KB)\s+stylesheet"), "site.css"),
    (re.compile(r"(\d+(?:\.\d+)?)\s*(MB|KB)\s+script"), "site.js"),
)
UNIT = {"KB": 1000, "MB": 1000 * 1000}


CASES_STATED = re.compile(r"\*\*(\d+)\s+cases\*\*")
CASES_RAN = re.compile(r"^all (\d+) cases passed$", re.M)
SUITE = "tools/site/test_check_head_sync.py"


def suite_size(root):
    """How many cases the head-sync suite actually runs, from the suite itself.

    Running it is the only honest way to ask: counting `case(` calls in the
    source would count the helper's own definition and miss any case a loop
    generates. It is file copies and regexes, so the second run costs nothing
    worth saving.
    """
    try:
        done = subprocess.run(
            [sys.executable, SUITE],
            cwd=str(root),
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return None, "could not run %s: %s" % (SUITE, exc)
    found = CASES_RAN.search(done.stdout)
    if not found:
        return None, (
            "%s did not report a case count. It prints one on success; if it "
            "failed, that is the finding." % SUITE
        )
    return int(found.group(1)), None


def png_size(data):
    # 8-byte signature, then the IHDR chunk: 4 length, 4 type, then w and h.
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


def jpeg_size(data):
    # Walk the segment chain to a start-of-frame marker; the frame header holds
    # height then width, in that order, which is the trap in reading JPEG.
    index = 2
    while index + 9 < len(data):
        if data[index] != 0xFF:
            index += 1
            continue
        marker = data[index + 1]
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            index += 2
            continue
        length = struct.unpack(">H", data[index + 2 : index + 4])[0]
        if 0xC0 <= marker <= 0xCF and marker not in (0xC4, 0xC8, 0xCC):
            height, width = struct.unpack(">HH", data[index + 5 : index + 9])
            return width, height
        index += 2 + length
    return None


def webp_size(data):
    if data[:4] != b"RIFF" or data[8:12] != b"WEBP":
        return None
    chunk = data[12:16]
    if chunk == b"VP8X":
        # 24-bit widths, minus one, little-endian.
        w = int.from_bytes(data[24:27], "little") + 1
        h = int.from_bytes(data[27:30], "little") + 1
        return w, h
    if chunk == b"VP8L":
        bits = int.from_bytes(data[21:25], "little")
        return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
    if chunk == b"VP8 ":
        return struct.unpack("<HH", data[26:30])[0] & 0x3FFF, struct.unpack(
            "<HH", data[28:32]
        )[0] & 0x3FFF
    return None


def measure(path):
    data = path.read_bytes()
    suffix = path.suffix.lower()
    if suffix == ".png":
        return png_size(data)
    if suffix in (".jpg", ".jpeg"):
        return jpeg_size(data)
    if suffix == ".webp":
        return webp_size(data)
    return None


def segments(line):
    """The spans a number can be attributed to: table cells, or prose clauses.

    A markdown row names one file per cell, and a prose sentence names one per
    clause. Splitting on the whole line is what made the first draft useless.
    """
    return line.split("|") if "|" in line else line.split(";")


def tolerance(value_text, unit):
    """Half of the last place the document actually claims."""
    decimals = len(value_text.partition(".")[2])
    return UNIT[unit] / (10 ** decimals) / 2


def conversion_pair(line, sizes):
    """The two files a table row compares, when it compares exactly two."""
    named = []
    for cell in line.split("|"):
        hits = [n for n in sizes if n in cell]
        if len(hits) == 1:
            named.append(hits[0])
    return named if len(named) == 2 else None


def main(root="."):
    root = Path(root)
    seo = root / "docs" / "site" / "SEO.md"
    html = root / "docs" / "index.html"
    for path in (seo, html):
        if not path.is_file():
            print("MISSING %s" % path)
            return 1

    problems = []
    sizes = {}
    bytes_on_disk = {}
    for image in sorted((root / ASSETS).glob("*")):
        if image.suffix.lower() not in IMAGE_SUFFIXES:
            continue
        got = measure(image)
        if got is None:
            problems.append(
                "%s/%s: could not read its header. Not skipped -- an unmeasurable "
                "file and an agreeing one must not print the same line."
                % (ASSETS, image.name)
            )
            continue
        sizes[image.name] = got
        bytes_on_disk[image.name] = image.stat().st_size

    compared = 0

    # 1. Every number in SEO.md, attributed to the file named beside it.
    text = seo.read_text(encoding="utf-8")
    lines = text.split("\n")
    for lineno, line in enumerate(lines, 1):
        bare = []
        for segment in segments(line):
            named = [n for n in sizes if n in segment]
            pairs = PAIR.findall(segment)
            stated = SIZE.findall(segment)
            if len(named) > 1 and (pairs or stated):
                problems.append(
                    "docs/site/SEO.md:%d states a number beside %d filenames "
                    "(%s), so it cannot be attributed to one of them. Split the "
                    "claim rather than leaving it unverifiable."
                    % (lineno, len(named), ", ".join(sorted(named)))
                )
                continue
            if not named:
                if stated:
                    bare.append(stated[0])
                continue
            name = named[0]
            for width, height in pairs:
                compared += 1
                pair = (int(width), int(height))
                if pair != sizes[name]:
                    problems.append(
                        "docs/site/SEO.md:%d states %d x %d for %s, which is "
                        "%d x %d" % (lineno, pair[0], pair[1], name, *sizes[name])
                    )
            for value, unit in stated:
                compared += 1
                claimed = float(value) * UNIT[unit]
                actual = bytes_on_disk[name]
                if abs(actual - claimed) > tolerance(value, unit):
                    problems.append(
                        "docs/site/SEO.md:%d states %s %s for %s, which is %d bytes"
                        % (lineno, value, unit, name, actual)
                    )

        # 3a. A conversion row: two files, then a saving that names neither.
        pair_of_files = conversion_pair(line, sizes)
        if pair_of_files and bare:
            before = bytes_on_disk[pair_of_files[0]]
            after = bytes_on_disk[pair_of_files[1]]
            value, unit = bare[0]
            compared += 1
            claimed = float(value) * UNIT[unit]
            if abs((before - after) - claimed) > tolerance(value, unit):
                problems.append(
                    "docs/site/SEO.md:%d states a saving of %s %s for %s over "
                    "%s, which is %d bytes"
                    % (
                        lineno,
                        value,
                        unit,
                        pair_of_files[1],
                        pair_of_files[0],
                        before - after,
                    )
                )

    # 4. The stylesheet and the script, named in the prose by what they are.
    for lineno, line in enumerate(lines, 1):
        for pattern, filename in TEXT_ASSETS:
            found = pattern.search(line)
            if not found:
                continue
            path = root / ASSETS / filename
            if not path.is_file():
                problems.append(
                    "docs/site/SEO.md:%d states a size for %s/%s, which does not "
                    "exist" % (lineno, ASSETS, filename)
                )
                continue
            compared += 1
            value, unit = found.group(1), found.group(2)
            actual = path.stat().st_size
            claimed = float(value) * UNIT[unit]
            if abs(actual - claimed) > tolerance(value, unit):
                problems.append(
                    "docs/site/SEO.md:%d states %s %s for %s, which is %d bytes"
                    % (lineno, value, unit, filename, actual)
                )

    # 3b. The stated total, against the sum of the savings the table claims.
    saved = 0
    for line in lines:
        pair_of_files = conversion_pair(line, sizes)
        if pair_of_files:
            saved += (
                bytes_on_disk[pair_of_files[0]] - bytes_on_disk[pair_of_files[1]]
            )
    for lineno, line in enumerate(lines, 1):
        found = TOTAL.search(line)
        if not found:
            continue
        compared += 1
        value, unit = found.group(1), found.group(2)
        claimed = float(value) * UNIT[unit]
        if abs(saved - claimed) > tolerance(value, unit):
            problems.append(
                # The document states the total twice and phrases it differently
                # each time; quoting the phrase back makes the finding point at
                # the line that is wrong rather than at the other one.
                "docs/site/SEO.md:%d states \"%s\"; the conversions in the table "
                "above save %d bytes" % (lineno, found.group(0).strip(), saved)
            )

    # 2. The shape of the declared box in index.html, which is what reflows.
    # Scale is the author's business; proportion is the file's. A box declared
    # at half size holds the image exactly, so only the ratio is compared, with
    # a tolerance of half a pixel at the declared width -- the finest
    # distinction the declared box can actually express.
    markup = html.read_text(encoding="utf-8")
    for tag in re.findall(r"<img\b[^>]*>", markup):
        src = re.search(r'src="([^"]+)"', tag)
        width = re.search(r'width="(\d+)"', tag)
        height = re.search(r'height="(\d+)"', tag)
        if not (src and width and height):
            continue
        name = src.group(1).rsplit("/", 1)[-1]
        if name not in sizes:
            continue
        compared += 1
        declared = (int(width.group(1)), int(height.group(1)))
        real = sizes[name]
        if min(declared) <= 0 or min(real) <= 0:
            problems.append(
                "docs/index.html declares %d x %d for %s, which is %d x %d -- a "
                "zero side reserves nothing."
                % (declared[0], declared[1], name, *real)
            )
            continue
        expected_height = declared[0] * real[1] / real[0]
        if abs(expected_height - declared[1]) > 0.5:
            problems.append(
                "docs/index.html declares %d x %d for %s, which is %d x %d -- a "
                "different shape, not a smaller one. At the declared width the "
                "image is %.1f tall, so the row resizes when it arrives."
                % (declared[0], declared[1], name, *real, expected_height)
            )

    # 5. The size of the mutation suite, which the suite itself reports.
    stated_cases = [
        (lineno, int(found.group(1)))
        for lineno, line in enumerate(lines, 1)
        for found in [CASES_STATED.search(line)]
        if found
    ]
    if stated_cases:
        actual_cases, failure = suite_size(root)
        if failure:
            problems.append(failure)
        else:
            for lineno, stated in stated_cases:
                compared += 1
                if stated != actual_cases:
                    problems.append(
                        "docs/site/SEO.md:%d states %d cases in %s, which runs %d"
                        % (lineno, stated, SUITE, actual_cases)
                    )

    # A check with nothing to check is the failure this file was rewritten for.
    if not compared:
        problems.append(
            "no fact in docs/site/SEO.md or docs/index.html could be "
            "attributed to a file. Either the documents lost their numbers or "
            "this check stopped finding them; both need a person."
        )

    if problems:
        print("Site facts: %d disagree with the repository" % len(problems))
        for problem in problems:
            print("  " + problem)
        return 1
    print(
        "Site facts agree with the repository: %d claims compared, %d images "
        "measured from their own headers" % (compared, len(sizes))
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
