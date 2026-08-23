#!/usr/bin/env python3
"""The rendered head and the HTML head are one thing in two files.

`docs/assets/site.js` assigns `document.title` and seven `<meta>` contents on
every load -- eight fields -- from a hardcoded `copy` object. A crawler that
runs JavaScript -- Googlebot does -- indexes what that object says; a crawler
that does not -- Facebook, X, Slack, Discord -- reads what `docs/index.html`
says. So the two have to agree for English, or one URL has two identities and
nobody can tell which one is live.

This has already gone wrong twice. An SEO pass rewrote the `<title>` and left
`site.js` holding the previous one, which would have put the old title straight
back into the rendered DOM; the fix for that then assigned the meta description
to `og:description` as well, overwriting a purpose-written social-card string
with a search-result one. Both were caught by review reading two files side by
side, and `SEO.md` recorded the second as "verified by hand, and they do match"
while it did not. A hand-verification that does not hold is worse than none,
because it is what the next agent trusts instead of re-checking.

THE TWO DEFECTS ARE NOT THE SAME KIND, and the first version of this file only
caught one of them. The stale title is a DATA divergence: `copy.en.title` and
`<title>` hold different strings, so comparing the two finds it. Assigning the
meta description to `og:description` is not -- it is a WIRING defect, and it
leaves every string in both files byte-identical. Reverting `site.js:75` from
`.cardDescription` to `.description` reproduces that historical defect exactly
and the string comparison exits 0 on it; so do three more one-token reversions
(`:76`/`:82` `.ogTitle` to `.title`, `:83` the same as `:75`, `:81`
`.localeAlternate` to `.locale` -- which is the state this branch fixed). Found
in review, in the artifact built to prevent exactly this.

So the check has two halves. The data half compares strings. The WIRING half
reads `setLanguage()` itself and asserts, for each of the eight fields, that
the DOM target selected in `site.js` is the tag we think it is and that the
`copy` field assigned into it is the field we think it is. Neither half can
see the other's defect.

It prints every problem it finds rather than stopping at the first: four
independent wiring defects reported one per run is four runs.

The Russian strings have no counterpart in the HTML by construction -- the
document ships in English and the script switches it -- so they are checked for
presence and non-emptiness only.

Run: python3 tools/site/check_head_sync.py .
"""

import re
import sys
from pathlib import Path

# field name in site.js's `copy` -> how to find the same string in index.html
HTML_SOURCES = {
    "title": ("title-tag", None),
    "ogTitle": ("property", "og:title"),
    "description": ("name", "description"),
    "cardDescription": ("property", "og:description"),
    "locale": ("property", "og:locale"),
    "localeAlternate": ("property", "og:locale:alternate"),
}

# Two tags are duplicates of an og: tag rather than strings of their own. They
# are assigned from the same `copy` field, so they are checked against the HTML
# too -- a divergence here is the same defect wearing a different attribute.
MIRRORED = {
    "twitter:title": "ogTitle",
    "twitter:description": "cardDescription",
}

# THE WIRING TABLE. One row per field `setLanguage()` writes, and it is the
# whole of the second half of this check:
#
#   local name in site.js -> (how site.js selects the DOM target,
#                             which `copy` field must be assigned into it)
#
# `document.title` has no selector, so its target is None.
#
# Every entry here is a defect that has happened or is one token away from
# happening. Keep this table and `setLanguage()` in step; changing one without
# the other is the failure, not the inconvenience.
ASSIGNMENTS = {
    "document.title":      (None,      None,                    "title"),
    "metaDescription":     ("name",     "description",          "description"),
    "ogDescription":       ("property", "og:description",        "cardDescription"),
    "ogTitle":             ("property", "og:title",              "ogTitle"),
    "ogLocale":            ("property", "og:locale",             "locale"),
    "ogLocaleAlternate":   ("property", "og:locale:alternate",   "localeAlternate"),
    "twitterTitle":        ("name",     "twitter:title",         "ogTitle"),
    "twitterDescription":  ("name",     "twitter:description",   "cardDescription"),
}


def html_meta(html, attr, value, problems=None):
    """The content of one meta tag -- and a complaint if there are two of it.

    Taking the first match made a duplicated tag invisible: the copy the
    checker read could agree with `site.js` while a second one further down the
    head said something else, and a non-rendering crawler reads whichever its
    parser prefers. Found in review.
    """
    pattern = r'<meta\s+%s="%s"\s+content="([^"]*)"\s*/?>' % (
        re.escape(attr),
        re.escape(value),
    )
    found = re.findall(pattern, html)
    if not found:
        return None
    if len(found) > 1 and problems is not None:
        problems.append(
            "index.html has %d <meta %s=\"%s\"> tags; only one can be the live one\n"
            "    values: %r" % (len(found), attr, value, found)
        )
    return found[0]


def html_field(html, source, problems=None):
    kind, value = source
    if kind == "title-tag":
        found = re.search(r"<title>(.*?)</title>", html, re.S)
        # Deliberately not stripped: an indented <title> body renders with that
        # whitespace, so it IS a divergence from a `copy.en.title` without it.
        # A test case asserts this rather than the reverse, which an earlier
        # case claimed.
        return found.group(1) if found else None
    return html_meta(html, kind, value, problems)


def js_block(js, lang):
    """The body of `copy.<lang>`, from its opening brace to the matching one.

    Brace counting with no string awareness: a `{` or `}` inside one of the copy
    strings would end the block early. None of them contains one today and none
    plausibly will -- they are prose for a search result and a social card -- but
    if one ever does, this is where it goes wrong, and it goes wrong by finding
    fewer fields rather than by passing silently.
    """
    start = re.search(r"\b%s:\s*\{" % lang, js)
    if not start:
        return None
    depth = 0
    for index in range(start.end() - 1, len(js)):
        if js[index] == "{":
            depth += 1
        elif js[index] == "}":
            depth -= 1
            if depth == 0:
                return js[start.end() : index]
    return None


def js_field(block, field):
    # Single-quoted string literals, which is what this file uses throughout.
    found = re.search(r"\b%s:\s*'((?:[^'\\]|\\.)*)'" % re.escape(field), block)
    return found.group(1) if found else None


def js_selector(js, name):
    """The `document.querySelector('...')` a `const NAME = ...` line holds."""
    found = re.search(
        r"\bconst\s+%s\s*=\s*document\.querySelector\(\s*'([^']*)'\s*\)" % re.escape(name),
        js,
    )
    return found.group(1) if found else None


def set_language_body(js):
    """The body of `setLanguage(...)`, brace-matched from its opening brace."""
    start = re.search(r"\bfunction\s+setLanguage\s*\([^)]*\)\s*\{", js)
    if not start:
        return None
    depth = 0
    for index in range(start.end() - 1, len(js)):
        if js[index] == "{":
            depth += 1
        elif js[index] == "}":
            depth -= 1
            if depth == 0:
                return js[start.end() : index]
    return None


def assigned_field(body, target):
    """Which `copy[lang].X` is written into TARGET.content -- or into the title.

    Matches `TARGET.content = copy[lang].FIELD` regardless of what guards it,
    because `setLanguage` writes each one behind an `if (TARGET)`. The title is
    `document.title = copy[lang].FIELD` and has no `.content`.
    """
    if target == "document.title":
        pattern = r"\bdocument\.title\s*=\s*copy\[\s*lang\s*\]\.(\w+)"
    else:
        pattern = r"\b%s\.content\s*=\s*copy\[\s*lang\s*\]\.(\w+)" % re.escape(target)
    found = re.search(pattern, body)
    return found.group(1) if found else None


def check_wiring(js, problems):
    """The half a string comparison cannot see: which field lands in which tag.

    Every defect this catches leaves `copy.en` byte-identical to index.html, so
    the data half above exits 0 on all of them.
    """
    body = set_language_body(js)
    if body is None:
        problems.append(
            "site.js: could not find setLanguage(); the head rewrite may have moved, "
            "and nothing below this line has been checked"
        )
        return

    for name, (attr, value, field) in ASSIGNMENTS.items():
        if attr is not None:
            selector = js_selector(js, name)
            wanted = 'meta[%s="%s"]' % (attr, value)
            if selector is None:
                problems.append("site.js has no `const %s = document.querySelector(...)`" % name)
                continue
            if selector != wanted:
                problems.append(
                    "site.js selects the wrong tag for %s\n    selects: %r\n    should:  %r"
                    % (name, selector, wanted)
                )

        got = assigned_field(body, name)
        if got is None:
            problems.append(
                "setLanguage() never assigns %s, so a language switch leaves it "
                "showing the other language's text" % name
            )
        elif got != field:
            problems.append(
                "setLanguage() wires the wrong copy field into %s\n"
                "    assigns: copy[lang].%s\n    should:  copy[lang].%s\n"
                "    Both strings stay identical to index.html, so the comparison above "
                "cannot see this." % (name, got, field)
            )


def main(root):
    root = Path(root)
    html_path = root / "docs" / "index.html"
    js_path = root / "docs" / "assets" / "site.js"

    for path in (html_path, js_path):
        if not path.is_file():
            print("MISSING %s" % path)
            return 1

    html = html_path.read_text(encoding="utf-8")
    js = js_path.read_text(encoding="utf-8")

    english = js_block(js, "en")
    russian = js_block(js, "ru")
    if english is None or russian is None:
        print("site.js: could not find copy.en and copy.ru; the head rewrite may have moved")
        return 1

    problems = []

    for field, source in HTML_SOURCES.items():
        in_html = html_field(html, source, problems)
        in_js = js_field(english, field)
        if in_html is None:
            problems.append("index.html has no %s" % (source,))
        elif in_js is None:
            problems.append("site.js copy.en has no %s" % field)
        elif in_html != in_js:
            problems.append(
                "%s differs\n    index.html: %r\n    site.js:    %r" % (field, in_html, in_js)
            )

    for tag, field in MIRRORED.items():
        in_html = html_meta(html, "name", tag, problems)
        in_js = js_field(english, field)
        if in_html is None:
            problems.append("index.html has no %s" % tag)
        elif in_js is not None and in_html != in_js:
            problems.append(
                "%s differs from copy.en.%s\n    index.html: %r\n    site.js:    %r"
                % (tag, field, in_html, in_js)
            )

    check_wiring(js, problems)

    for field in HTML_SOURCES:
        value = js_field(russian, field)
        if not value:
            problems.append("site.js copy.ru has no %s, so a Russian visitor keeps the English one" % field)

    if problems:
        print("The rendered head and index.html disagree:")
        for problem in problems:
            print("  - %s" % problem)
        print(
            "\nsite.js assigns these on every load, so what a rendering crawler indexes\n"
            "is what site.js says. Change both files together."
        )
        return 1

    print(
        "Rendered head matches index.html: %d fields and %d mirrored tags agree, "
        "%d assignments in setLanguage() are wired to the right tag, "
        "%d Russian strings present."
        % (len(HTML_SOURCES), len(MIRRORED), len(ASSIGNMENTS), len(HTML_SOURCES))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
