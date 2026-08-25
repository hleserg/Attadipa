#!/usr/bin/env python3
"""Mutation tests for check_site_facts.py.

This checker earned its tests before it was ever committed. Its first draft
attributed a number to a file by requiring exactly one filename on the line --
and every row of the table it was written for names two, the original and the
converted. So it skipped every claim in the document and printed "9 images
measured", which reads exactly like agreement. A checker that passes everything
is worse than none, and this one managed it while its own docstring said so.

Hence the case below that asserts the *zero-facts* state is a failure, and the
case that asserts an ambiguous attribution is reported rather than skipped. The
rest break one real number at a time, in a copy of the live tree, and assert the
check notices; the four that assert it does *not* fire are the half that would
otherwise land a job failing on `main` -- the brand mark is a 64 x 64 PNG drawn
at 34, and a check that called that a layout shift would be trained away.

Run: python3 tools/site/test_check_site_facts.py
"""

from __future__ import annotations

import io
import os
import pathlib
import shutil
import sys
import tempfile
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_site_facts  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FAILURES: list[str] = []
RAN: list[str] = []
# Split by polarity: the CI comment quotes both halves, and a case that changes
# polarity moves them while leaving the total alone.
MUST_REPORT: list[str] = []
MUST_STAY_QUIET: list[str] = []

SELF = "tools/site/test_check_site_facts.py"


def case(name: str, condition: bool) -> None:
    print(("  ok   " if condition else "  FAIL ") + name)
    RAN.append(name)
    if not condition:
        FAILURES.append(name)


def fixture(root: str) -> tuple[str, str]:
    """A copy of the live tree, which the caller then breaks.

    `tools/site` comes along because the case-count check runs the head-sync
    suite to ask how many cases it has; without it every fixture would report a
    missing suite instead of the defect under test.
    """
    shutil.copytree(os.path.join(REPO, "docs"), os.path.join(root, "docs"))
    shutil.copytree(
        os.path.join(REPO, "tools", "site"), os.path.join(root, "tools", "site")
    )
    # STATUS.md and the workflow come along because the check now holds their
    # copies of the head-sync case count too, and reports a claim file that is
    # missing rather than passing over it. A fixture without them would be
    # testing a tree the check refuses to run against.
    shutil.copy(os.path.join(REPO, "STATUS.md"), os.path.join(root, "STATUS.md"))
    os.makedirs(os.path.join(root, ".github", "workflows"))
    shutil.copy(
        os.path.join(REPO, ".github", "workflows", "ci.yml"),
        os.path.join(root, ".github", "workflows", "ci.yml"),
    )
    return (
        os.path.join(root, "docs", "site", "SEO.md"),
        os.path.join(root, "docs", "index.html"),
    )


def edit(path: str, old: str, new: str) -> bool:
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    if old not in text:
        return False
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text.replace(old, new, 1))
    return True


def scenario(name, break_it, expect_fail, needle=None):
    (MUST_REPORT if expect_fail else MUST_STAY_QUIET).append(name)
    with tempfile.TemporaryDirectory() as root:
        seo, html = fixture(root)
        if break_it is not None and not break_it(seo, html, root):
            case(name + " [FIXTURE DID NOT APPLY]", False)
            return
        output = io.StringIO()
        with redirect_stdout(output):
            code = check_site_facts.main(root)
        text = output.getvalue()
    if expect_fail:
        ok = code == 1 and (needle is None or needle in text)
    else:
        ok = code == 0
    case(name, ok)
    if not ok:
        print("        exit=%d\n        %s" % (code, text.strip().replace("\n", "\n        ")))


def main() -> int:
    print("check_site_facts.py — mutation tests")

    scenario("the tree as it stands passes", None, expect_fail=False)

    scenario(
        "a dimension pair in SEO.md that is not the file's",
        lambda seo, html, root: edit(seo, "1774 × 887", "1774 × 880"),
        expect_fail=True,
        needle="states 1774 x 880 for banner.webp",
    )
    scenario(
        "a byte size in SEO.md that is not the file's",
        lambda seo, html, root: edit(seo, "**49 KB**", "**52 KB**"),
        expect_fail=True,
        needle="states 52 KB for banner.webp",
    )
    scenario(
        "a saving column that is not the difference",
        lambda seo, html, root: edit(seo, "| 947 KB |", "| 1.11 MB |"),
        expect_fail=True,
        needle="states a saving of 1.11 MB",
    )
    scenario(
        "the total, where it is stated in the prose",
        lambda seo, html, root: edit(
            seo, "**2.25 MB off the first page view**", "**3.4 MB off the first page view**"
        ),
        expect_fail=True,
        needle="3.4 MB off the first page view",
    )
    scenario(
        "the total, where it is stated again in the checklist",
        lambda seo, html, root: edit(seo, "the 2.25 MB image saving", "the 3.4 MB image saving"),
        expect_fail=True,
        needle="3.4 MB image saving",
    )
    scenario(
        "a bound the stylesheet does not meet",
        lambda seo, html, root: edit(
            seo, "One stylesheet under 20 KB", "One stylesheet under 8 KB"
        ),
        expect_fail=True,
        needle="site.css is under 8 KB",
    )
    scenario(
        "a bound the script does not meet",
        lambda seo, html, root: edit(
            seo, "one script under\n  12 KB", "one script under\n  4 KB"
        ),
        expect_fail=True,
        needle="site.js is under 4 KB",
    )
    scenario(
        # The exact form is still supported -- the bound is what this section
        # needs, not what every section needs -- so it is still tested, by
        # ADDING a claim rather than breaking one. A path with no live instance
        # and no case is a path nobody would notice rotting.
        "an exact size claim, where prose states one instead of a bound",
        lambda seo, html, root: edit(
            seo, "both local, the script", "It is a 3 KB stylesheet. Both local, the script"
        ),
        expect_fail=True,
        needle="states 3 KB for site.css",
    )
    scenario(
        "the head-sync case count — the other number that was actually stale",
        lambda seo, html, root: edit(seo, "**44 cases**", "**29 cases**"),
        expect_fail=True,
        needle="states 29 where",
    )
    scenario(
        "a declared box of the wrong shape in index.html",
        lambda seo, html, root: edit(
            html, 'width="1774" height="887"', 'width="1774" height="600"'
        ),
        expect_fail=True,
        needle="a different shape, not a smaller one",
    )
    scenario(
        "an image whose header cannot be read is reported, not skipped",
        lambda seo, html, root: _truncate(
            os.path.join(root, "docs", "assets", "mascot.webp")
        ),
        expect_fail=True,
        needle="could not read its header",
    )
    scenario(
        "a number beside two filenames is reported, not silently skipped",
        lambda seo, html, root: edit(
            seo,
            "| `banner.webp`, **49 KB**",
            "| `banner.webp` from `attadipa-banner.png`, **49 KB**",
        ),
        expect_fail=True,
        needle="cannot be attributed",
    )
    scenario(
        "a document stating no checkable fact fails rather than passing",
        lambda seo, html, root: _blank(seo, html),
        expect_fail=True,
        needle="could be attributed",
    )

    scenario(
        "og:image:width that is not the card's own width",
        lambda seo, html, root: edit(
            html,
            '<meta property="og:image:width" content="1200">',
            '<meta property="og:image:width" content="1000">',
        ),
        expect_fail=True,
        needle="og:image:width/height 1000 x 630",
    )

    # The two attribution rules the sixth review added. Both close a hole where
    # the checker had *nothing to compare* and said nothing about it -- the same
    # silent-skip shape as the original bug this file was written for, surviving
    # in two branches the first rewrite did not reach.
    scenario(
        "a dimension pair that names no file is reported, not skipped",
        lambda seo, html, root: edit(
            seo, "the 64 × 64 `favicon.png` drawn", "a 64 × 64 mark drawn"
        ),
        expect_fail=True,
        needle="names no file in that clause",
    )
    scenario(
        "a prose size attributed to the files named on the line above",
        lambda seo, html, root: edit(seo, "weigh 2.44 MB", "weigh 4.4 MB"),
        expect_fail=True,
        needle="states 4.4 MB for attadipa-banner.png and attadipa-style-board.png",
    )

    # The three that cover the claim machinery: a suite size is quoted in three
    # files, and until this round only the copy in SEO.md was read back. The
    # workflow comment was found stale by review -- four lines under the
    # paragraph warning that it had already been wrong twice.
    scenario(
        "a stale head-sync count in the CI comment, not just in SEO.md",
        lambda seo, html, root: edit(
            os.path.join(root, ".github", "workflows", "ci.yml"),
            "holds 44 cases: 34 break the",
            "holds 37 cases: 34 break the",
        ),
        expect_fail=True,
        needle="ci.yml:",
    )
    scenario(
        "a stale head-sync SPLIT, where the total is still right",
        lambda seo, html, root: edit(
            os.path.join(root, ".github", "workflows", "ci.yml"),
            "44 cases: 34 break the",
            "44 cases: 33 break the",
        ),
        expect_fail=True,
        needle="(report)",
    )
    # A suite whose size is stated nowhere is a suite whose printed number is
    # compared with nothing, which is the state this whole mechanism exists to
    # refuse. It takes six edits to reach because three files quote it -- and
    # that redundancy is the point: losing one copy is not the failure.
    scenario(
        "every copy of one suite's count disappears at once",
        lambda seo, html, root: all(
            [
                edit(seo, "holds 13 cases: 9 demand", "holds many cases: some demand"),
                edit(seo, "a report, 4 demand silence", "a report, some demand silence"),
                edit(os.path.join(root, "STATUS.md"), "holds 13", "holds many"),
                edit(
                    os.path.join(root, "STATUS.md"),
                    "cases: 9 demand a report, 4 demand silence",
                    "cases: some demand a report, some demand silence",
                ),
                edit(
                    os.path.join(root, ".github", "workflows", "ci.yml"),
                    "holds 13 cases: 9 demand a",
                    "holds many cases: some demand a",
                ),
                edit(
                    os.path.join(root, ".github", "workflows", "ci.yml"),
                    "# report, 4 demand silence",
                    "# report, some demand silence",
                ),
            ]
        ),
        expect_fail=True,
        needle="quotes the size of",
    )

    # REVIEW'S TWO, both about the cue rather than the number. Narrowing the
    # total cue to kill a false positive silently stopped it matching a copy it
    # had been checking, and the completeness rule counted any cue at all --
    # so a document quoting only "9 demand a report" satisfied a rule whose
    # message says "nothing quotes the size".
    scenario(
        "the size written as `Of the N cases` is read, not walked past",
        lambda seo, html, root: edit(
            os.path.join(root, "STATUS.md"),
            "`tools/site/test_check_head_sync.py` holds 44 cases:",
            "`tools/site/test_check_head_sync.py`. Of the 40 cases:",
        ),
        expect_fail=True,
        needle="(total)",
    )
    scenario(
        "a split half does not stand in for the size",
        lambda seo, html, root: all(
            [
                edit(seo, "holds 13 cases", "holds 13 scenarios"),
                edit(
                    os.path.join(root, "STATUS.md"),
                    "cases: 9 demand a report",
                    "scenarios: 9 demand a report",
                ),
                edit(
                    os.path.join(root, ".github", "workflows", "ci.yml"),
                    "holds 13 cases: 9 demand a",
                    "holds 13 scenarios: 9 demand a",
                ),
            ]
        ),
        expect_fail=True,
        needle="is not the size",
    )

    # The four that must stay quiet. Each is the live tree's real behaviour, and
    # a check that fired on any of them would be switched off within a week.
    scenario(
        "one file drops a count that the other two still state",
        lambda seo, html, root: edit(
            os.path.join(root, "STATUS.md"),
            "with 44 mutation tests",
            "with mutation tests",
        ),
        expect_fail=False,
    )
    scenario(
        "a uniformly scaled box is NOT a layout shift — 64 x 64 drawn at 32",
        lambda seo, html, root: edit(html, 'width="34" height="34"', 'width="32" height="32"'),
        expect_fail=False,
    )
    scenario(
        "a rounder number is allowed — 1.4 MB for a file stated elsewhere as 1.44",
        lambda seo, html, root: edit(seo, "**1.44 MB**", "**1.4 MB**"),
        expect_fail=False,
    )

    print()
    if FAILURES:
        print("%d of %d case(s) failed:" % (len(FAILURES), len(RAN)))
        for name in FAILURES:
            print("  - " + name)
        return 1
    print("all %d cases passed" % len(RAN))
    print(
        "%s: %d cases, %d demand a report, %d demand silence"
        % (SELF, len(RAN), len(MUST_REPORT), len(MUST_STAY_QUIET))
    )

    # And the places that quote those three numbers are held to them here,
    # because nothing else can ask: check_site_facts.py runs the head-sync
    # suite to learn its size, and running THIS suite from inside the checker
    # it tests would recurse. The suite knows its own count; it does the
    # holding. Not a case -- a case would change the number it is checking.
    stale, _ = check_site_facts.verify_case_claims(
        pathlib.Path(REPO), SELF, len(RAN), len(MUST_REPORT), len(MUST_STAY_QUIET)
    )
    if stale:
        print()
        print("but %d place(s) quote a size this suite does not have:" % len(stale))
        for problem in stale:
            print("  " + problem)
        return 1
    return 0


def _truncate(path: str) -> bool:
    with open(path, "wb") as handle:
        handle.write(b"not an image at all")
    return True


def _blank(seo: str, html: str) -> bool:
    """Strip every number and filename the check can attribute to a file."""
    with open(seo, "w", encoding="utf-8") as handle:
        handle.write("# SEO\n\nNothing measurable is claimed here.\n")
    with open(html, "w", encoding="utf-8") as handle:
        handle.write("<!doctype html><title>x</title><p>no images</p>\n")
    return True


if __name__ == "__main__":
    sys.exit(main())
