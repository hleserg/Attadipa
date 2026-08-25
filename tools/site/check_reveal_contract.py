#!/usr/bin/env python3
"""The reveal animation must not be able to hide the page again.

`.reveal` shipped at `opacity:0` and the script was what brought it back, so
every way the script could fail -- scripting off, a 404 on site.js, cached HTML
opened offline, a blocking extension, a throw before the observer was
constructed -- rendered a hero and empty space. The fix inverted it: `.reveal`
ships visible, `.js-reveal` on `<html>` is what hides it, and only the script
adds that class. Every failure of the animation now leaves the page readable.

**That is a contract across three files and one class name, and nothing read
any of it.** `check_head_sync.py` opens `site.js` for `copy` and
`setLanguage()`; `check_site_facts.py` only `stat()`s the stylesheet;
`check_docs.py` filters on `.md`. Putting `opacity:0` back on the bare `.reveal`
rule -- the exact state that shipped the defect -- leaves *Documentation
consistency* green. Found in review, which called it the next check to write and
the smallest one here.

What is checked:

1. **No rule that can hide `.reveal` outside the `.js-reveal` scope**, whatever
   its selector. The first version of this check matched a selector *exactly
   equal* to `.reveal`, which review showed is a rule about spelling rather than
   about the page: `body .reveal{opacity:0;transform:translateY(18px)}` is
   (0,1,1), beats both the bare rule and the `<noscript>` override at (0,1,0),
   and reproduces the shipped defect exactly -- while falling into neither
   branch of the check written to catch it. A transition is still fine: that is
   the animation, and it animates nothing until something else moves.
2. Some rule scoped to `.js-reveal` must hide it, or the class is decorative and
   the animation silently does not exist. **Scoped means the class actually
   applies**: `html:not(.js-reveal) .reveal{opacity:0}` contains the string and
   hides the page for exactly the readers the inversion was for, so a selector
   carrying `:not(` is not scope, it is the opposite of scope. Also review's.
3. The class string appears in all three files: the stylesheet that acts on it,
   the script that adds it, and the `<noscript>` override in the HTML.
4. The `<noscript>` block must put `.reveal` back to `opacity:1`, because with
   scripting off nothing will ever add the class -- and if a future edit hides
   `.reveal` unconditionally, this is the only thing standing between a reader
   and an empty page.

Only the declarations matter, never the formatting: the stylesheet is minified
in places and hand-written in others, so everything is compared after
whitespace is stripped.

Run: python3 tools/site/check_reveal_contract.py [root]
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

CSS = "docs/assets/site.css"
JS = "docs/assets/site.js"
HTML = "docs/index.html"

CLASS = "js-reveal"
# `opacity:.35` and `opacity: 0.35` are the same declaration; so are `1` and
# `1.0`. Compared as numbers, because a string compare would call `1.0` a
# hiding rule.
OPACITY = re.compile(r"(?:^|;)\s*opacity\s*:\s*([0-9.]+)")
TRANSFORM = re.compile(r"(?:^|;)\s*transform\s*:\s*([^;]+)")
NOSCRIPT = re.compile(r"<noscript>(.*?)</noscript>", re.S)


def rules(css: str):
    """Every innermost `selector { declarations }`, with its selectors split.

    A hand-rolled walk rather than a parser: this file has no dependencies, runs
    in the `docs` job which installs nothing, and needs exactly one fact about
    each rule -- what it selects and what it declares. `@media` blocks nest one
    level and their inner rules are what matter, so the outer braces are simply
    skipped over.
    """
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    for match in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
        selectors = [s.strip() for s in match.group(1).split(",") if s.strip()]
        # An `@media` condition is the last thing before the block it opens, so
        # a selector carrying one is the media query itself, not a rule.
        selectors = [s.split("\n")[-1].strip() for s in selectors]
        yield selectors, match.group(2).replace("\n", " ")


def hides(declarations: str) -> str | None:
    """What in this declaration block would make an element invisible."""
    opacity = OPACITY.search(declarations)
    if opacity and float(opacity.group(1)) < 1:
        return "opacity:%s" % opacity.group(1)
    transform = TRANSFORM.search(declarations)
    if transform and transform.group(1).strip() not in ("none", "translateY(0)"):
        return "transform:%s" % transform.group(1).strip()
    return None


def main(root_dir=None) -> int:
    root = Path(root_dir or (sys.argv[1] if len(sys.argv) > 1 else "."))
    problems: list[str] = []

    missing = [name for name in (CSS, JS, HTML) if not (root / name).is_file()]
    if missing:
        print(
            "The reveal contract cannot be checked: %s missing" % ", ".join(missing),
            file=sys.stderr,
        )
        return 1

    css = (root / CSS).read_text(encoding="utf-8")
    js = (root / JS).read_text(encoding="utf-8")
    html = (root / HTML).read_text(encoding="utf-8")

    bare = scoped = 0
    for selectors, declarations in rules(css):
        for selector in selectors:
            if ".reveal" not in selector:
                continue
            # SCOPE IS WHAT THE CLASS SELECTS, NOT WHAT THE STRING CONTAINS.
            # `html:not(.js-reveal) .reveal` carries the class name and applies
            # precisely when the class is absent -- which is every reader the
            # inversion exists for.
            in_scope = ("." + CLASS) in selector and ":not(" not in selector
            if selector == ".reveal":
                bare += 1
            if in_scope:
                if hides(declarations):
                    scoped += 1
                continue
            hiding = hides(declarations)
            if hiding:
                problems.append(
                    "%s: `%s` sets %s and is not scoped to `.%s`, so a section "
                    "is hidden before any script runs. That is the state this "
                    "was inverted out of: scripting off, a failed request or a "
                    "throw then leaves a hero and empty space. A selector more "
                    "specific than the `<noscript>` override beats that too."
                    % (CSS, selector, hiding, CLASS)
                )

    if not bare:
        problems.append(
            "%s: no rule selects `.reveal` on its own any more. The contract is "
            "that this rule exists and leaves the section visible; if the class "
            "is gone, this check and the `<noscript>` block go with it." % CSS
        )
    if not scoped:
        problems.append(
            "%s: nothing scoped to `.%s` hides `.reveal`, so adding the class "
            "does nothing and the reveal animation silently does not exist."
            % (CSS, CLASS)
        )

    if ("classList.add('%s')" % CLASS) not in js and (
        'classList.add("%s")' % CLASS
    ) not in js:
        problems.append(
            "%s: nothing adds `%s`. Hiding is opt-in and the script is what "
            "opts in; without this line the animation never runs." % (JS, CLASS)
        )

    blocks = NOSCRIPT.findall(html)
    override = [b for b in blocks if ".reveal" in b]
    if not override:
        problems.append(
            "%s: no `<noscript>` block mentions `.reveal`. With scripting off "
            "nothing adds `.%s`, so this is the belt-and-braces half -- and the "
            "only thing left if a future edit hides `.reveal` unconditionally."
            % (HTML, CLASS)
        )
    else:
        for block in override:
            visible = re.search(r"opacity\s*:\s*1\b", block)
            if not visible:
                problems.append(
                    "%s: the `<noscript>` override does not set `opacity:1`, so "
                    "it no longer guarantees a readable page with scripting off."
                    % HTML
                )

    if problems:
        print("The reveal contract is broken:", file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1
    print(
        "Reveal contract holds: `.reveal` ships visible, %d rule(s) scoped to "
        "`.%s` hide it, the script adds the class and the <noscript> override "
        "puts it back." % (scoped, CLASS)
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
