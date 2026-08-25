#!/usr/bin/env python3
"""Verify the measurable claims in the shipping project page.

The checker compares image dimensions, declared aspect ratios, file sizes and
conversion savings in docs/site/SEO.md and docs/index.html with the files on
disk. Unattributed numbers and a zero-facts run fail closed. Test-suite counts
are deliberately not documentation facts: each suite reports its own result.

Run: python3 tools/site/check_site_facts.py [root]
"""

from __future__ import annotations

import re
import struct
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
# A BOUND, which is what that section should have claimed in the first place.
# "17 KB stylesheet" is held to within half a kilobyte, and site.css is 17413
# bytes -- 87 bytes of headroom, so one CSS rule fails a documentation job on a
# pull request that never opened SEO.md. A checker that goes red for a true
# statement teaches people to edit the number until it stops, which is the
# habit this whole file was written against. "under 20 KB" is the claim the
# prose is actually making and it fails only when it stops being true.
BOUNDS = (
    (re.compile(r"stylesheet under\s+(\d+(?:\.\d+)?)\s*(MB|KB)"), "site.css"),
    (re.compile(r"script under\s+(\d+(?:\.\d+)?)\s*(MB|KB)"), "site.js"),
)
UNIT = {"KB": 1000, "MB": 1000 * 1000}



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
                # A DIMENSION PAIR ALWAYS DESCRIBES A FILE. If the segment does
                # not name one, nothing can check it -- and the first version of
                # this file silently dropped it here, which is the same "skipped
                # rather than reported" bug the two-filenames branch above was
                # rewritten to close, surviving in the branch the rewrite did
                # not cover. Found in review.
                for width, height in pairs:
                    problems.append(
                        "docs/site/SEO.md:%d states %s x %s and names no file in "
                        "that clause, so nothing checks it. Name the file, or "
                        "move the number beside one." % (lineno, width, height)
                    )
                # A SIZE is different: a table row's saving column names neither
                # file by design (3a below pairs it with the row's two), and
                # prose legitimately states sizes of things that do not exist
                # yet. So a bare size is a candidate here rather than a problem,
                # and section 3b below catches the prose case a sentence can
                # attribute. What neither can see is a size naming no file on
                # its line or the one above -- documented at the top of this
                # file as the one hole that is deliberate.
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

    # 3b. A size in prose whose sentence names files on the LINE ABOVE. Markdown
    # prose wraps, so "`a.png` and `b.png` are now unreferenced / and together
    # weigh 3.0 MB" puts the files and the number on different lines and
    # segment-based attribution sees neither half. Two files means the sum, one
    # means itself. Found in review, which named the exact line this misses.
    for lineno, line in enumerate(lines, 1):
        if lineno < 2 or "|" in line:
            continue
        stated = SIZE.findall(line)
        if not stated or [n for n in sizes if n in line]:
            continue
        above = lines[lineno - 2]
        named = sorted({n for n in sizes if n in above})
        if not named or "|" in above:
            continue
        actual = sum(bytes_on_disk[n] for n in named)
        value, unit = stated[0]
        claimed = float(value) * UNIT[unit]
        compared += 1
        if abs(actual - claimed) > tolerance(value, unit) * len(named):
            problems.append(
                "docs/site/SEO.md:%d states %s %s for %s, which %s %d bytes"
                % (
                    lineno,
                    value,
                    unit,
                    " and ".join(named),
                    "together are" if len(named) > 1 else "is",
                    actual,
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

    # 4b. The same two files, where the prose states a bound instead. One-sided
    # by design: a file that shrank has not falsified "under 20 KB". What it
    # catches is the direction that matters -- the page growing a payload the
    # section says it does not have.
    # Searched across the line wrap, not line by line. The first draft of this
    # was per-line, Markdown wrapped "one script under / 12 KB" between the
    # noun and the number, and the bound was silently not checked -- the same
    # shape as the prose-size hole 3b exists for, reintroduced by the fix for
    # something else. A match that starts in the following line is left to that
    # line's own turn, so nothing is reported twice.
    for lineno, line in enumerate(lines, 1):
        joined = line + " " + (lines[lineno] if lineno < len(lines) else "")
        for pattern, filename in BOUNDS:
            found = pattern.search(joined)
            if not found or found.start() > len(line):
                continue
            path = root / ASSETS / filename
            if not path.is_file():
                problems.append(
                    "docs/site/SEO.md:%d states a bound for %s/%s, which does "
                    "not exist" % (lineno, ASSETS, filename)
                )
                continue
            compared += 1
            value, unit = found.group(1), found.group(2)
            actual = path.stat().st_size
            if actual >= float(value) * UNIT[unit]:
                problems.append(
                    "docs/site/SEO.md:%d says %s is under %s %s; it is %d bytes"
                    % (lineno, filename, value, unit, actual)
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

    # 3. og:image:width / og:image:height, which are typed numbers about a file
    # this check already has open. A wrong pair is a social card that renders at
    # the wrong shape or is dropped, and unlike an <img> box these are absolute:
    # a scaler is meaningless in an og: tag, so they are compared exactly.
    og_image = re.search(
        r'<meta\s+property="og:image"\s+content="([^"]+)"', markup
    )
    og_dims = {}
    for axis in ("width", "height"):
        found = re.search(
            r'<meta\s+property="og:image:%s"\s+content="(\d+)"' % axis, markup
        )
        if found:
            og_dims[axis] = int(found.group(1))
    if og_image and len(og_dims) == 2:
        name = og_image.group(1).rsplit("/", 1)[-1]
        if name in sizes:
            compared += 1
            declared = (og_dims["width"], og_dims["height"])
            real = sizes[name]
            if declared != real:
                problems.append(
                    "docs/index.html declares og:image:width/height %d x %d for %s, "
                    "which is %d x %d. These are not a scaler -- a card renderer "
                    "reserves exactly what they say."
                    % (declared[0], declared[1], name, *real)
                )
    elif og_image and og_dims:
        problems.append(
            "docs/index.html has og:image:%s and not the other; a card renderer "
            "needs both or neither." % next(iter(og_dims))
        )


    # A check with nothing to check must not print the same result as agreement.
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
