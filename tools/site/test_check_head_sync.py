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

# The name this suite is quoted by, so the line it prints identifies itself.
SELF = "tools/site/test_check_head_sync.py"

# `--count` registers every case and runs none, so a caller can ask how big
# this suite is without executing it against whatever tree it happens to be
# standing in.
COUNT_ONLY = "--count" in sys.argv[1:]

FAILURES: list[str] = []
RAN: list[str] = []
# Split by polarity, because the split is quoted too. docs/site/SEO.md states
# "31 break the pair ... 9 leave it valid", and a case that changes polarity
# moves both halves while leaving the total alone -- the one drift the total
# cannot see. Counted here rather than by hand, and printed below.
MUST_REPORT: list[str] = []
MUST_STAY_QUIET: list[str] = []


def case(name: str, condition: bool) -> None:
    print(("  ok   " if condition else "  FAIL ") + name)
    RAN.append(name)
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
    (MUST_REPORT if expect_fail else MUST_STAY_QUIET).append(name)
    if COUNT_ONLY:
        # Registered, not run. check_site_facts.py asks this suite how big it
        # is once per invocation, and its own mutation tests invoke it inside
        # a fixture whose index.html and site.js have been broken on purpose --
        # so actually running the cases there would fail for a reason that has
        # nothing to do with the case under test, and would report "the suite
        # did not print a count" over a real finding. Asking for the count
        # touches no file at all.
        return
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


def summary() -> None:
    """The two lines everything else reads: the size, and the split.

    The size is the number of cases REGISTERED, not the number that reported a
    verdict: under `--count` nothing runs, and in a full run a fixture that
    fails to apply adds a case name of its own. The two agree on a green run,
    which is the only run whose count anybody quotes.
    """
    if not COUNT_ONLY:
        print("all %d cases passed" % len(RAN))
    print(
        "%s: %d cases, %d demand a report, %d demand silence"
        % (
            SELF,
            len(MUST_REPORT) + len(MUST_STAY_QUIET),
            len(MUST_REPORT),
            len(MUST_STAY_QUIET),
        )
    )


def main() -> int:
    if not COUNT_ONLY:
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
    # THIS CASE USED TO BE DECORATIVE. It inserted the comment before
    # `const copy = {` -- outside the span js_block() reads -- so it never
    # entered the text the checker looks at and could not have failed for its
    # stated reason. Moved inside the `en:` block, it failed immediately:
    # js_field() took the first regex match with no comment awareness and
    # returned the decoy, reporting a divergence that did not exist. The fix is
    # strip_line_comments(); these four cases are what hold it.
    scenario(
        "a comment above the real field is not the field",
        lambda html, js: edit(
            js,
            "    en: {\n      title:",
            "    en: {\n      // title: 'something else entirely'\n      title:",
        ),
        expect_fail=False,
    )
    scenario(
        "a comment below the real field is not the field either",
        lambda html, js: edit(
            js,
            "      ogTitle: 'Attadipa",
            "      // title: 'decoy'\n      ogTitle: 'Attadipa",
        ),
        expect_fail=False,
    )
    scenario(
        "a trailing comment on the field's own line does not eat the field",
        lambda html, js: edit(
            js,
            "',\n      ogTitle:",
            "',  // the rendered <title>\n      ogTitle:",
        ),
        expect_fail=False,
    )
    # The other direction: stripping comments must not strip a `//` that is
    # inside a string. No copy string holds one today, but a URL would, and a
    # naive strip would truncate the value and report a divergence.
    scenario(
        "a // inside a copy string is not a comment",
        lambda html, js: edit(
            js,
            "      locale: 'en_US',",
            "      locale: 'en_US',\n      note: 'see https://example.invalid/x',",
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

    # THE THIRD KIND OF DIVERGENCE: two strings inside index.html alone. No
    # assignment touches these, so both halves above exit 0 on every one.
    scenario(
        "og:image and twitter:image diverge inside index.html",
        lambda html, js: edit(
            html,
            '<meta name="twitter:image" content="https://hleserg.github.io/Attadipa/assets/og-card.jpg">',
            '<meta name="twitter:image" content="https://hleserg.github.io/Attadipa/assets/banner.webp">',
        ),
        expect_fail=True,
        needle="have diverged",
    )
    scenario(
        "og:image:alt and twitter:image:alt diverge inside index.html",
        lambda html, js: edit(
            html,
            '<meta name="twitter:image:alt" content="The Attadipa firefly mark',
            '<meta name="twitter:image:alt" content="A firefly mark',
        ),
        expect_fail=True,
        needle="have diverged",
    )
    # The JSON-LD copy is the one most likely to rot: it is fifty-odd lines
    # below the meta description it repeats, and nothing points at it.
    scenario(
        "the JSON-LD description drifts from the meta description",
        lambda html, js: edit(
            html,
            '"description": "Open-source ESP32-S3 smartwatch firmware:',
            '"description": "Open source ESP32-S3 smartwatch firmware:',
        ),
        expect_fail=True,
        needle="have diverged",
    )
    # And the completeness half: a NEW duplicate nobody declared. This is the
    # shape the three above arrived in -- added in one pass, unrecorded, and
    # found by a reviewer rather than by the checker built for the job.
    scenario(
        "a new undeclared duplicate in the head",
        lambda html, js: edit(
            html,
            '<link rel="icon" type="image/png" href="assets/favicon.png">',
            '<meta name="apple-mobile-web-app-title" content="The Attadipa firefly mark to the '
            'left of the wordmark Attadipa, with the line Independent by design beneath it.">\n  '
            '<link rel="icon" type="image/png" href="assets/favicon.png">',
        ),
        expect_fail=True,
        needle="no rule pairs them",
    )
    scenario(
        "two SHORT strings that happen to match are not a duplicate",
        lambda html, js: edit(
            html,
            '<link rel="icon" type="image/png" href="assets/favicon.png">',
            '<meta name="theme-color" content="en_US">\n  '
            '<link rel="icon" type="image/png" href="assets/favicon.png">',
        ),
        expect_fail=False,
    )

    # THE COMPLETENESS CHECK ITSELF HAD NO CASE, which review pointed out and
    # is the same defect one layer up: the rule added so the table could not
    # silently stop covering setLanguage() was itself uncovered, and deleting
    # it left all 32 other cases green. Two cases now, because a guard needs
    # both halves -- it fires on a target the table does not list, and it stays
    # quiet on one the table does.
    scenario(
        "a localised tag wired in setLanguage() but missing from ASSIGNMENTS",
        lambda html, js: edit(
            js,
            "    if (ogLocale) ogLocale.content = copy[lang].locale;",
            "    if (ogLocale) ogLocale.content = copy[lang].locale;\n"
            "    if (ogImageAlt) ogImageAlt.content = copy[lang].imageAlt;",
        ),
        expect_fail=True,
        needle="which is not in ASSIGNMENTS",
    )
    # The hole in the guard, found by review: no `\w+` before `.content`, so the
    # completeness scan walked past an inline selector entirely.
    scenario(
        "a localised tag wired through an INLINE querySelector",
        lambda html, js: edit(
            js,
            "    if (ogLocale) ogLocale.content = copy[lang].locale;",
            "    if (ogLocale) ogLocale.content = copy[lang].locale;\n"
            "    document.querySelector('meta[property=\"og:image:alt\"]').content = "
            "copy[lang].imageAlt;",
        ),
        expect_fail=True,
        needle="which is not in ASSIGNMENTS",
    )
    scenario(
        "a tag setLanguage() writes a CONSTANT into is not a table row",
        lambda html, js: edit(
            js,
            "    if (ogLocale) ogLocale.content = copy[lang].locale;",
            "    if (ogLocale) ogLocale.content = copy[lang].locale;\n"
            "    if (ogImageAlt) ogImageAlt.content = 'the same in both languages';",
        ),
        expect_fail=False,
    )

    if COUNT_ONLY:
        summary()
        return 0

    print()
    if FAILURES:
        print("%d case(s) failed:" % len(FAILURES))
        for name in FAILURES:
            print("  - " + name)
        return 1
    # The count is printed rather than left to be counted by hand: the number
    # of cases here is quoted in docs/site/SEO.md, it has been wrong twice, and
    # check_site_facts.py reads this line to hold the document to it.
    # The machine-readable line. check_site_facts.py reads both of these back
    # and holds every place that quotes them -- SEO.md, STATUS.md and the CI
    # comment -- to what the suite actually ran.
    summary()
    return 0


if __name__ == "__main__":
    sys.exit(main())
