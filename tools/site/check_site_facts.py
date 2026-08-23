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
   likely to go stale are the ones an unrelated commit changes. Where the prose
   states a BOUND -- "a stylesheet under 20 KB" -- the bound is what is checked,
   one-sidedly. An exact figure there was held to within half a kilobyte over a
   file with 87 bytes of headroom, so any CSS rule would have failed a
   documentation job on a pull request that never opened the document, and a
   check that goes red for a true statement gets edited until it stops.
5. The size of BOTH mutation suites, wherever it is quoted: SEO.md, STATUS.md
   and the CI comment. Those numbers have now been wrong three times -- the
   third found by review four lines below the paragraph explaining the hazard,
   under an instruction to fix the remaining copies by hand. So each suite
   prints its size and its split, and every copy is read back. The split is
   held too: a case that changes polarity moves it while leaving the total
   alone. This file does the reading for the head-sync suite, which it already
   runs; the site-facts suite does its own on the way out, because running it
   from inside the checker it tests would recurse.

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


# Note what is NOT here any more: a bare `**N cases**` pattern compared to the
# head-sync suite whatever suite the sentence was about, so writing the second
# suite's size into SEO.md the obvious way failed. Claims are anchored to a
# suite by name below. Found in review.
CASES_SPLIT = re.compile(
    r"^(\S+): (\d+) cases, (\d+) demand a report, (\d+) demand silence$", re.M
)
SUITE = "tools/site/test_check_head_sync.py"
# Run for its count as well, for the same reason and by the same route. It does
# not test this file, so there is no recursion to guard against.
REVEAL_SUITE = "tools/site/test_check_reveal_contract.py"

# Where a suite size is quoted, and by what wording. SEO.md's copy was already
# enforced; STATUS.md's and the CI comment's were not, and the CI one was found
# stale by review four lines below the paragraph warning that this exact number
# had already been wrong twice. A count nobody reads back is a count that rots,
# and "fix it here by hand if it moves" is the instruction that had just failed.
# A block is attributed to a suite by naming the suite OR the checker it guards;
# prose reasonably says "check_head_sync.py, with 40 mutation tests" without
# spelling out the second path. `files` is where that suite's size is quoted --
# per suite, because SEO.md names check_site_facts.py twice without ever
# quoting its size, and demanding a count there would be inventing a claim.
SUITES = {
    "tools/site/test_check_head_sync.py": {
        "aliases": ("check_head_sync.py",),
        "files": ("docs/site/SEO.md", "STATUS.md", ".github/workflows/ci.yml"),
    },
    "tools/site/test_check_site_facts.py": {
        "aliases": ("check_site_facts.py",),
        "files": ("STATUS.md", ".github/workflows/ci.yml"),
    },
    "tools/site/test_check_reveal_contract.py": {
        "aliases": ("check_reveal_contract.py",),
        "files": ("docs/site/SEO.md", "STATUS.md", ".github/workflows/ci.yml"),
    },
}
# The three numbers, by the CUE PHRASE that introduces each -- not by any digit
# standing near the word "cases". The first version matched the latter, and
# review showed what that costs: the true sentence "it has caught 2 cases of
# drift since", added to a STATUS.md paragraph that happens to name a checker,
# turns the documentation job red on a pull request that changed nothing about
# the suite. A check that reddens for a true statement gets edited until it
# stops, which is the argument this very file makes twice about something else.
# So a claim is a claim when it is phrased as one.
CUES = (
    (
        "total",
        re.compile(
            r"(?:holds|with)\s+(\d+)\s+(?:cases|mutation tests)\b"
            r"|\*\*(\d+)\s+cases\*\*"
        ),
    ),
    ("report", re.compile(r"(\d+) (?:break the pair|demand a report)")),
    ("quiet", re.compile(r"(\d+) (?:leave it valid|demand silence)")),
)


def numbers(cue, block):
    """Every number a cue finds, whichever of its alternatives matched.

    The total cue has two spellings -- "holds 40 cases" and "**40 cases**" --
    so `findall` hands back a tuple per match with one half empty.
    """
    out = []
    for found in cue.findall(block):
        parts = found if isinstance(found, tuple) else (found,)
        out.extend(int(part) for part in parts if part)
    return out


def blocks(text):
    """Paragraphs, with YAML comment markers stripped and whitespace collapsed.

    One implementation for Markdown and for a workflow file: a CI comment block
    is a paragraph that happens to start every line with `#`, and the numbers
    inside it rot exactly like prose. Blank lines and bare `#` lines separate.
    """
    out, current, start = [], [], 1
    for lineno, line in enumerate(text.split("\n") + [""], 1):
        stripped = line.strip()
        bare = re.sub(r"^#\s?", "", stripped)
        if not bare:
            if current:
                out.append((start, " ".join(current)))
                current = []
            continue
        if not current:
            start = lineno
        current.append(bare)
    return out


def verify_case_claims(root, suite, total, must_report, must_stay_quiet):
    """Hold every quoted copy of a suite's size to what the suite actually ran.

    Shared by check_site_facts.py (for the head-sync suite, which it already
    runs) and by test_check_site_facts.py (for its own count, which it knows
    and which nothing else can ask for without recursion).
    """
    problems, compared = [], 0
    names = (suite,) + SUITES[suite]["aliases"]
    other = [
        name
        for key, entry in SUITES.items()
        if key != suite
        for name in (key,) + entry["aliases"]
    ]
    expected = {"total": total, "report": must_report, "quiet": must_stay_quiet}
    total_quoted = 0
    for relative in SUITES[suite]["files"]:
        path = root / relative
        if not path.exists():
            problems.append(
                "%s is missing, so the size of %s cannot be held to anything. "
                "It is one of the files that quotes it." % (relative, suite)
            )
            continue
        quoted = 0
        for lineno, block in blocks(path.read_text(encoding="utf-8")):
            if not any(name in block for name in names):
                continue
            if any(name in block for name in other):
                found = [c for _, cue in CUES for c in numbers(cue, block)]
                if found:
                    problems.append(
                        "%s:%d quotes a count in a paragraph naming two suites, "
                        "so it cannot be attributed to one of them. Split the "
                        "claim rather than leaving it unverifiable."
                        % (relative, lineno)
                    )
                continue
            for kind, cue in CUES:
                for stated in numbers(cue, block):
                    quoted += 1
                    compared += 1
                    if stated != expected[kind]:
                        problems.append(
                            "%s:%d states %d where %s runs %d (%s)"
                            % (relative, lineno, stated, suite, expected[kind], kind)
                        )
        total_quoted += quoted
    # A claim must exist SOMEWHERE, and that is the whole completeness rule.
    # The first version demanded one per listed file, which made trimming a
    # STATUS.md paragraph -- the routine maintenance of a file whose own first
    # lines call it "a status file, not a history" -- fail a job. Requiring the
    # number to be quoted and correct at least once keeps the guard; requiring
    # it in every file was a tripwire on ordinary editing. Found in review.
    if not total_quoted:
        problems.append(
            "nothing in %s quotes the size of %s any more, so the number the "
            "suite prints is checked against nothing. Quote it in one of them "
            "-- \"holds N cases\" or \"with N mutation tests\" -- or drop the "
            "suite from SUITES deliberately."
            % (", ".join(SUITES[suite]["files"]), suite)
        )
    return problems, compared


def suite_size(root, suite=SUITE):
    """How big a suite is, and how it splits, asked of the suite itself.

    Asking it is the only honest way: counting `case(` calls in the source would
    count the helper's own definition and miss any case a loop generates.
    `--count` registers every scenario and runs none, which matters because
    this checker's own mutation tests invoke it inside a fixture whose
    index.html and site.js have been broken on purpose -- running the head-sync
    cases there would fail for reasons unrelated to the case under test, and
    the failure would be reported as "the suite printed no count". Found in
    review.
    """
    try:
        done = subprocess.run(
            [sys.executable, suite, "--count"],
            cwd=str(root),
            capture_output=True,
            text=True,
            timeout=120,
        )
    except (OSError, subprocess.SubprocessError) as exc:
        return None, "could not run %s: %s" % (suite, exc)
    found = CASES_SPLIT.search(done.stdout)
    if not found or found.group(1) != suite:
        return None, (
            "%s did not report its case count and split. It prints them on "
            "success; if it failed, that is the finding." % suite
        )
    return (int(found.group(2)), int(found.group(3)), int(found.group(4))), None


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

    # 5. The size of the head-sync mutation suite, which the suite itself
    # reports -- in SEO.md, in STATUS.md and in the CI comment. The first was
    # enforced from the start and has not drifted since; the other two were
    # guarded by a sentence saying to fix them by hand, and review found the CI
    # one stale. Both halves of the split are held too: a case that changes
    # polarity moves 31 and 9 while leaving 40 alone.
    claims = 0
    for which in (SUITE, REVEAL_SUITE):
        counts, failure = suite_size(root, which)
        if failure:
            problems.append(failure)
            continue
        found, held = verify_case_claims(root, which, *counts)
        problems.extend(found)
        claims += held

    # A check with nothing to check is the failure this file was rewritten for.
    # Counted separately from the case claims above deliberately: those live in
    # STATUS.md and the workflow, so counting them here would let a document
    # that lost every number of its own still look like a document with facts
    # in it -- which is the exact substitution this guard exists to refuse.
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
        "measured from their own headers, %d quoted suite sizes held to what "
        "the suite ran" % (compared, len(sizes), claims)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
