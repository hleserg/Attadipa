#!/usr/bin/env python3
"""The rendered head and the HTML head are one thing in two files.

`docs/assets/site.js` assigns `document.title` and six `<meta>` contents on
every load, from a hardcoded `copy` object. A crawler that runs JavaScript --
Googlebot does -- indexes what that object says; a crawler that does not --
Facebook, X, Slack, Discord -- reads what `docs/index.html` says. So the two
have to agree for English, or one URL has two identities and nobody can tell
which one is live.

This has already gone wrong twice. An SEO pass rewrote the `<title>` and left
`site.js` holding the previous one, which would have put the old title straight
back into the rendered DOM; the fix for that then assigned the meta description
to `og:description` as well, overwriting a purpose-written social-card string
with a search-result one. Both were caught by review reading two files side by
side, and `SEO.md` recorded the second as "verified by hand, and they do match"
while it did not. A hand-verification that does not hold is worse than none,
because it is what the next agent trusts instead of re-checking.

So it is a check. It reads both files, extracts the English strings, and exits
non-zero on the first divergence, naming the field and both values.

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


def html_meta(html, attr, value):
    pattern = r'<meta\s+%s="%s"\s+content="([^"]*)"\s*/?>' % (
        re.escape(attr),
        re.escape(value),
    )
    found = re.search(pattern, html)
    return found.group(1) if found else None


def html_field(html, source):
    kind, value = source
    if kind == "title-tag":
        found = re.search(r"<title>(.*?)</title>", html, re.S)
        return found.group(1) if found else None
    return html_meta(html, kind, value)


def js_block(js, lang):
    """The body of `copy.<lang>`, from its opening brace to the matching one."""
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
        in_html = html_field(html, source)
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
        in_html = html_meta(html, "name", tag)
        in_js = js_field(english, field)
        if in_html is None:
            problems.append("index.html has no %s" % tag)
        elif in_js is not None and in_html != in_js:
            problems.append(
                "%s differs from copy.en.%s\n    index.html: %r\n    site.js:    %r"
                % (tag, field, in_html, in_js)
            )

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
        "%d Russian strings present." % (len(HTML_SOURCES), len(MIRRORED), len(HTML_SOURCES))
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
