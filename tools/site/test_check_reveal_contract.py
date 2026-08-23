#!/usr/bin/env python3
"""Mutation tests for check_reveal_contract.py.

The first case is the one that matters and it is the reviewer's own reproduce
step: put `opacity:0;transform:translateY(18px)` back on the bare `.reveal`
rule -- the exact state that shipped a hero and empty space to anybody whose
script did not run -- and demand the check goes red. Before this file, that
mutation left every job in CI green.

The rest break one strand of the contract at a time, in a copy of the live tree.
The three that assert silence are the ones that would otherwise make this check
annoying enough to switch off: a transition on `.reveal` is the animation, a
`transform:none` is not a displacement, and the reduced-motion block setting
`opacity:1` is the accessibility path working.

Run: python3 tools/site/test_check_reveal_contract.py [--count]
"""

from __future__ import annotations

import io
import os
import shutil
import sys
import tempfile
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_reveal_contract  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SELF = "tools/site/test_check_reveal_contract.py"
COUNT_ONLY = "--count" in sys.argv[1:]

FAILURES: list[str] = []
RAN: list[str] = []
MUST_REPORT: list[str] = []
MUST_STAY_QUIET: list[str] = []


def case(name: str, condition: bool) -> None:
    print(("  ok   " if condition else "  FAIL ") + name)
    RAN.append(name)
    if not condition:
        FAILURES.append(name)


def fixture(root: str) -> tuple[str, str, str]:
    shutil.copytree(os.path.join(REPO, "docs"), os.path.join(root, "docs"))
    return (
        os.path.join(root, "docs", "assets", "site.css"),
        os.path.join(root, "docs", "assets", "site.js"),
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
    if COUNT_ONLY:
        return
    with tempfile.TemporaryDirectory() as root:
        css, js, html = fixture(root)
        if break_it is not None and not break_it(css, js, html):
            case(name + " [FIXTURE DID NOT APPLY]", False)
            return
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = check_reveal_contract.main(root)
        text = out.getvalue() + err.getvalue()
    ok = (code == 1 and (needle is None or needle in text)) if expect_fail else code == 0
    case(name, ok)
    if not ok:
        print("        exit=%d\n        %s" % (code, text.strip()))


def main() -> int:
    if not COUNT_ONLY:
        print("check_reveal_contract.py — mutation tests")

    scenario("the tree as it stands passes", None, expect_fail=False)

    # THE case. This is the defect the inversion fixed, and it was invisible to
    # every check in CI until this file existed.
    scenario(
        "opacity:0 back on the bare `.reveal` rule — the shipped defect",
        lambda css, js, html: edit(
            css,
            ".reveal{transition:opacity .65s ease,transform .65s ease}",
            ".reveal{opacity:0;transform:translateY(18px);"
            "transition:opacity .65s ease,transform .65s ease}",
        ),
        expect_fail=True,
        needle="sets opacity:0",
    )
    scenario(
        "a partial fade on the bare rule is still a fade",
        lambda css, js, html: edit(
            css,
            ".reveal{transition:opacity .65s ease,transform .65s ease}",
            ".reveal{opacity:.35;transition:opacity .65s ease,transform .65s ease}",
        ),
        expect_fail=True,
        needle="opacity:.35",
    )
    scenario(
        "a displacement on the bare rule, with no opacity at all",
        lambda css, js, html: edit(
            css,
            ".reveal{transition:opacity .65s ease,transform .65s ease}",
            ".reveal{transform:translateY(18px);"
            "transition:opacity .65s ease,transform .65s ease}",
        ),
        expect_fail=True,
        needle="transform:translateY(18px)",
    )
    scenario(
        "the hiding rule loses its `.js-reveal` scope and hides nothing",
        lambda css, js, html: edit(
            css, ".js-reveal .reveal{opacity:0;transform:translateY(18px)}", ""
        ),
        expect_fail=True,
        needle="does not exist",
    )
    scenario(
        "the script stops adding the class",
        lambda css, js, html: edit(
            js, "classList.add('js-reveal')", "classList.remove('nothing')"
        ),
        expect_fail=True,
        needle="nothing adds",
    )
    scenario(
        "the <noscript> override is deleted",
        lambda css, js, html: edit(
            html,
            "<noscript><style>.reveal,.js-reveal .reveal"
            "{opacity:1;transform:none}</style></noscript>",
            "",
        ),
        expect_fail=True,
        needle="belt-and-braces",
    )
    scenario(
        "the <noscript> override stops restoring opacity",
        lambda css, js, html: edit(
            html,
            "<noscript><style>.reveal,.js-reveal .reveal"
            "{opacity:1;transform:none}</style></noscript>",
            "<noscript><style>.reveal{transform:none}</style></noscript>",
        ),
        expect_fail=True,
        needle="does not set `opacity:1`",
    )

    # The three that must stay quiet, because a check that fires on these is a
    # check somebody deletes.
    scenario(
        "a transition on `.reveal` is the animation, not a hiding rule",
        lambda css, js, html: edit(
            css,
            ".reveal{transition:opacity .65s ease,transform .65s ease}",
            ".reveal{transition:opacity .9s ease,transform .9s ease}",
        ),
        expect_fail=False,
    )
    scenario(
        "`transform:none` on the bare rule is not a displacement",
        lambda css, js, html: edit(
            css,
            ".reveal{transition:opacity .65s ease,transform .65s ease}",
            ".reveal{transform:none;transition:opacity .65s ease,transform .65s ease}",
        ),
        expect_fail=False,
    )
    scenario(
        "the reduced-motion block setting opacity:1 is the feature working",
        lambda css, js, html: edit(
            css,
            ".reveal,.js-reveal .reveal{opacity:1;transform:none;transition:none}",
            ".reveal,.js-reveal .reveal{opacity:1;transform:none;transition:none;"
            "animation:none}",
        ),
        expect_fail=False,
    )

    if COUNT_ONLY:
        print(
            "%s: %d cases, %d demand a report, %d demand silence"
            % (
                SELF,
                len(MUST_REPORT) + len(MUST_STAY_QUIET),
                len(MUST_REPORT),
                len(MUST_STAY_QUIET),
            )
        )
        return 0

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
    return 0


if __name__ == "__main__":
    sys.exit(main())
