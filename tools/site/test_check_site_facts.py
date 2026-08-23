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
check notices; the two that assert it does *not* fire are the half that would
otherwise land a job failing on `main` -- the brand mark is a 64 x 64 PNG drawn
at 34, and a check that called that a layout shift would be trained away.

Run: python3 tools/site/test_check_site_facts.py
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_site_facts  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FAILURES: list[str] = []
RAN: list[str] = []


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
        lambda seo, html, root: edit(seo, "**43 KB**", "**52 KB**"),
        expect_fail=True,
        needle="states 52 KB for banner.webp",
    )
    scenario(
        "a saving column that is not the difference",
        lambda seo, html, root: edit(seo, "| 1.28 MB |", "| 1.11 MB |"),
        expect_fail=True,
        needle="states a saving of 1.11 MB",
    )
    scenario(
        "the total, where it is stated in the prose",
        lambda seo, html, root: edit(
            seo, "**2.8 MB off the first page view**", "**3.4 MB off the first page view**"
        ),
        expect_fail=True,
        needle="3.4 MB off the first page view",
    )
    scenario(
        "the total, where it is stated again in the checklist",
        lambda seo, html, root: edit(seo, "the 2.8 MB image saving", "the 3.4 MB image saving"),
        expect_fail=True,
        needle="3.4 MB image saving",
    )
    scenario(
        "the stylesheet size",
        lambda seo, html, root: edit(seo, "One 17 KB stylesheet", "One 21 KB stylesheet"),
        expect_fail=True,
        needle="states 21 KB for site.css",
    )
    scenario(
        "the script size — the number that was actually stale",
        lambda seo, html, root: edit(seo, "one 9 KB script", "one 6 KB script"),
        expect_fail=True,
        needle="states 6 KB for site.js",
    )
    scenario(
        "the head-sync case count — the other number that was actually stale",
        lambda seo, html, root: edit(seo, "**40 cases**", "**29 cases**"),
        expect_fail=True,
        needle="states 29 cases",
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
            "| `banner.webp`, **43 KB**",
            "| `banner.webp` from `attadipa-banner.png`, **43 KB**",
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
            '<meta property="og:image:width" content="1734">',
            '<meta property="og:image:width" content="1200">',
        ),
        expect_fail=True,
        needle="og:image:width/height 1200 x 907",
    )

    # The two that must stay quiet. Both are the live page's real behaviour, and
    # a check that fired on either would be switched off within a week.
    scenario(
        "a uniformly scaled box is NOT a layout shift — 64 x 64 drawn at 32",
        lambda seo, html, root: edit(html, 'width="34" height="34"', 'width="32" height="32"'),
        expect_fail=False,
    )
    scenario(
        "a rounder number is allowed — 1.3 MB for a file stated elsewhere as 1.32",
        lambda seo, html, root: edit(seo, "**1.32 MB**", "**1.3 MB**"),
        expect_fail=False,
    )

    print()
    if FAILURES:
        print("%d of %d case(s) failed:" % (len(FAILURES), len(RAN)))
        for name in FAILURES:
            print("  - " + name)
        return 1
    print("all %d cases passed" % len(RAN))
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
