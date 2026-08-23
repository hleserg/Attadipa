#!/usr/bin/env python3
"""Mutation tests for check_head_sync.py.

A checker that passes everything is worse than no checker, because it reads as
evidence -- and this one exists precisely because a hand-verification that did
not hold was trusted instead of re-checked. So each case below takes the real
`docs/index.html` and `docs/assets/site.js`, breaks exactly one thing, and
asserts the checker notices; the cases that assert it does *not* fire are the
half that would otherwise land a job failing on `main`.

Both defects this checker was written for get a case of their own, and they are
different kinds: a `<title>` changed on one side only is a DATA divergence, and
`og:description` overwritten with the meta description is a WIRING defect that
leaves every string byte-identical. The second case used to join both edits
with `and`, so its verdict came from the string and the named defect
contributed nothing -- a case passing for a reason other than its name, inside
the artifact built to stop that. It is now the assignment alone, and it fails
against the checker as it was.

Run: python3 tools/site/test_check_head_sync.py
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_head_sync  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FAILURES: list[str] = []


def case(name: str, condition: bool) -> None:
    print(("  ok   " if condition else "  FAIL ") + name)
    if not condition:
        FAILURES.append(name)


def fixture(root: str) -> tuple[str, str]:
    """A copy of the live pair, which the caller then breaks."""
    os.makedirs(os.path.join(root, "docs", "assets"), exist_ok=True)
    html = os.path.join(root, "docs", "index.html")
    js = os.path.join(root, "docs", "assets", "site.js")
    shutil.copyfile(os.path.join(REPO, "docs", "index.html"), html)
    shutil.copyfile(os.path.join(REPO, "docs", "assets", "site.js"), js)
    return html, js


def edit(path: str, old: str, new: str) -> bool:
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    if old not in text:
        return False
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text.replace(old, new, 1))
    return True


def run(root: str) -> tuple[int, str]:
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        status = check_head_sync.main(root)
    return status, buffer.getvalue()


def scenario(name: str, mutate, expect_fail: bool, needle: str = "") -> None:
    """Copy the live pair, apply `mutate`, assert the checker's verdict."""
    with tempfile.TemporaryDirectory() as root:
        html, js = fixture(root)
        if mutate is not None and not mutate(html, js):
            case(name + " [fixture]", False)
            return
        status, output = run(root)
        if expect_fail:
            case(name, status != 0 and (not needle or needle in output))
        else:
            case(name, status == 0)


def main() -> int:
    print("check_head_sync.py")

    # The tree as it stands has to pass, or every case below is measuring the
    # fixture rather than the mutation.
    scenario("the live pair agrees", None, expect_fail=False)

    # Defect one, as it actually happened: an SEO pass rewrote the <title> and
    # left site.js holding the previous one.
    scenario(
        "a <title> changed on one side only",
        lambda html, js: edit(html, "<title>Attadipa", "<title>Firefly OS — Attadipa"),
        expect_fail=True,
        needle="title differs",
    )
    scenario(
        "a title changed in site.js only",
        lambda html, js: edit(js, "title: 'Attadipa", "title: 'Attadipa Firmware"),
        expect_fail=True,
        needle="title differs",
    )

    # DEFECT TWO, AS IT ACTUALLY HAPPENED, and it is a different kind from
    # defect one. The fix for defect one assigned the meta description to
    # og:description as well, so the search-result string went onto the social
    # card for renderer-based crawlers only -- while every string in both files
    # stayed byte-identical. That is a WIRING defect, and the string comparison
    # cannot see it.
    #
    # This case used to join two edits with `and`: the assignment, then the
    # string. Since nothing in the checker parsed an assignment, the verdict
    # came entirely from the second edit and the named defect contributed
    # nothing -- the case passed for a reason other than its name, inside the
    # artifact built to stop exactly that. Found in review. It is now the
    # assignment alone, and it fails against the checker as it was.
    scenario(
        "og:description overwritten with the meta description — the assignment alone",
        lambda html, js: edit(
            js,
            "if (ogDescription) ogDescription.content = copy[lang].cardDescription;",
            "if (ogDescription) ogDescription.content = copy[lang].description;",
        ),
        expect_fail=True,
        needle="wires the wrong copy field into ogDescription",
    )

    # The string half of the same historical defect, kept as its own case so
    # that losing either half shows up as a case that stopped firing.
    scenario(
        "og:description's string diverging is caught by the comparison",
        lambda html, js: edit(
            js, "cardDescription: 'LoRa MeshCore", "cardDescription: 'Open-source ESP32-S3"
        ),
        expect_fail=True,
        needle="cardDescription differs",
    )

    # THE OTHER THREE ONE-TOKEN REVERSIONS. Each reproduces a real defect or a
    # state this branch fixed, each leaves copy.en byte-identical to the HTML,
    # and each exited 0 before the wiring table existed.
    scenario(
        "twitter:description wired to the search-result string",
        lambda html, js: edit(
            js,
            "if (twitterDescription) twitterDescription.content = copy[lang].cardDescription;",
            "if (twitterDescription) twitterDescription.content = copy[lang].description;",
        ),
        expect_fail=True,
        needle="wires the wrong copy field into twitterDescription",
    )
    scenario(
        "og:title wired to the <title> string",
        lambda html, js: edit(
            js,
            "if (ogTitle) ogTitle.content = copy[lang].ogTitle;",
            "if (ogTitle) ogTitle.content = copy[lang].title;",
        ),
        expect_fail=True,
        needle="wires the wrong copy field into ogTitle",
    )
    scenario(
        "og:locale:alternate wired to og:locale — the state fixed at a6abab4",
        lambda html, js: edit(
            js,
            "if (ogLocaleAlternate) ogLocaleAlternate.content = copy[lang].localeAlternate;",
            "if (ogLocaleAlternate) ogLocaleAlternate.content = copy[lang].locale;",
        ),
        expect_fail=True,
        needle="wires the wrong copy field into ogLocaleAlternate",
    )

    # A field dropped from setLanguage() entirely leaves it showing the other
    # language's text after a switch, which is the defect og:locale:alternate
    # had before this branch in its milder form.
    scenario(
        "a field setLanguage() stops assigning at all",
        lambda html, js: edit(
            js, "if (twitterTitle) twitterTitle.content = copy[lang].ogTitle;", ""
        ),
        expect_fail=True,
        needle="never assigns twitterTitle",
    )

    # And the selector half: renaming the tag site.js reaches for would leave
    # every assignment correct and every string identical, and write nothing.
    scenario(
        "site.js selecting a tag that is not the one the table names",
        lambda html, js: edit(
            js,
            "document.querySelector('meta[property=\"og:description\"]')",
            "document.querySelector('meta[name=\"description\"]')",
        ),
        expect_fail=True,
        needle="selects the wrong tag for ogDescription",
    )

    # setLanguage() moving or being renamed must not pass silently: without the
    # body, none of the wiring above was checked, and saying so is the point.
    scenario(
        "setLanguage() renamed, so nothing below it was checked",
        lambda html, js: edit(js, "function setLanguage(", "function applyLanguage("),
        expect_fail=True,
        needle="could not find setLanguage()",
    )

    # Each remaining field, one at a time, so a field dropped from HTML_SOURCES
    # shows up as a case that stopped firing rather than as silence.
    scenario(
        "og:title diverges",
        lambda html, js: edit(js, "ogTitle: 'Attadipa", "ogTitle: 'Attadipa OS"),
        expect_fail=True,
        needle="ogTitle differs",
    )
    scenario(
        "the meta description diverges",
        lambda html, js: edit(js, "description: 'Open-source", "description: 'The open-source"),
        expect_fail=True,
        needle="description differs",
    )
    scenario(
        "og:locale diverges",
        lambda html, js: edit(js, "locale: 'en_US'", "locale: 'en_GB'"),
        expect_fail=True,
        needle="locale differs",
    )
    scenario(
        "og:locale:alternate diverges",
        lambda html, js: edit(js, "localeAlternate: 'ru_RU'", "localeAlternate: 'de_DE'"),
        expect_fail=True,
        needle="localeAlternate differs",
    )

    # The twitter: pair are mirrors of the og: strings rather than strings of
    # their own, and a divergence there is the same defect wearing a different
    # attribute -- one crawler reads the card, another reads the mirror.
    scenario(
        "twitter:title drifts from og:title",
        lambda html, js: edit(
            html,
            '<meta name="twitter:title" content="Attadipa',
            '<meta name="twitter:title" content="Attadipa OS',
        ),
        expect_fail=True,
        needle="twitter:title differs",
    )
    scenario(
        "twitter:description drifts from og:description",
        lambda html, js: edit(
            html,
            '<meta name="twitter:description" content="LoRa MeshCore',
            '<meta name="twitter:description" content="MeshCore',
        ),
        expect_fail=True,
        needle="twitter:description differs",
    )

    # A field deleted from either side is the same failure as a field changed:
    # site.js still assigns something, or stops assigning what the HTML claims.
    scenario(
        "a meta tag deleted from index.html",
        lambda html, js: edit(html, '<meta property="og:locale:alternate"', "<meta data-removed"),
        expect_fail=True,
        needle="og:locale:alternate",
    )
    scenario(
        "a field deleted from copy.en",
        lambda html, js: edit(js, "cardDescription: '", "cardDescriptionX: '"),
        expect_fail=True,
        needle="has no cardDescription",
    )

    # Russian has no counterpart in the HTML by construction, so it is checked
    # for presence: a Russian visitor left holding the English string is the
    # failure, and an empty string is that failure with a value in it.
    scenario(
        "copy.ru lost a field",
        lambda html, js: edit(js, "localeAlternate: 'en_US'", "localeAlternateX: 'en_US'"),
        expect_fail=True,
        needle="copy.ru has no localeAlternate",
    )
    scenario(
        "copy.ru has an empty string",
        lambda html, js: edit(js, "ogTitle: 'Attadipa — открытая", "ogTitle: '' , ogTitleOld: 'Attadipa — открытая"),
        expect_fail=True,
        needle="copy.ru has no ogTitle",
    )

    # And the checker has to say so rather than pass when it cannot find what it
    # is checking -- a rename that moves `copy` is not a clean bill of health.
    scenario(
        "copy.en is not where it was",
        lambda html, js: edit(js, "    en: {", "    english: {"),
        expect_fail=True,
        needle="could not find copy.en",
    )
    scenario(
        "index.html is missing",
        lambda html, js: (os.remove(html) or True),
        expect_fail=True,
        needle="MISSING",
    )
    scenario(
        "site.js is missing",
        lambda html, js: (os.remove(js) or True),
        expect_fail=True,
        needle="MISSING",
    )

    # Cases that must NOT fire. The Russian strings differ from the English by
    # design, and the check has to tolerate that or it fails on `main` from the
    # first commit. Likewise a comment mentioning a field is not a field.
    scenario(
        "Russian differing from English is not a divergence",
        lambda html, js: edit(js, "title: 'Attadipa — открытая", "title: 'Attadipa — свободная"),
        expect_fail=False,
    )
    scenario(
        "a comment naming a field is not a field",
        lambda html, js: edit(
            js,
            "  const copy = {",
            "  // title: 'something else entirely'\n  const copy = {",
        ),
        expect_fail=False,
    )
    # This case was called "whitespace around the <title> body" and mutated
    # site.js without touching a title -- and the behaviour it named is absent
    # anyway, since html_field() does not strip, so an indented <title> WOULD
    # diverge. Found in review. It is now what it always actually tested, under
    # its real name, and a second case covers the indentation claim honestly by
    # asserting the checker DOES fire on it.
    scenario(
        "a blank line in site.js is not a divergence",
        lambda html, js: edit(js, "\n  const copy", "\n\n  const copy"),
        expect_fail=False,
    )
    # A duplicated tag used to be invisible: html_meta took the first match, so
    # the copy the checker read could agree while a second one said otherwise.
    scenario(
        "a duplicated og:description in index.html",
        lambda html, js: edit(
            html,
            '<meta property="og:locale" ',
            '<meta property="og:description" content="Something else entirely">\n  '
            '<meta property="og:locale" ',
        ),
        expect_fail=True,
        needle="only one can be the live one",
    )
    scenario(
        "an indented <title> body IS a divergence — the checker does not strip",
        lambda html, js: edit(
            html,
            "<title>Attadipa —",
            "<title>\n    Attadipa —",
        ),
        expect_fail=True,
        needle="title differs",
    )

    print()
    if FAILURES:
        print("%d case(s) failed:" % len(FAILURES))
        for name in FAILURES:
            print("  - " + name)
        return 1
    print("all cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
