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

# THE THIRD KIND OF DIVERGENCE: two tags INSIDE index.html holding the same
# string, with nothing keeping them that way. MIRRORED above catches the two
# that site.js also assigns, because both sides are compared to the same `copy`
# field. These three are never assigned by anything -- they are static bytes,
# duplicated by hand, and the JSON-LD one sits fifty-odd lines below the tag it
# repeats. Editing one and not the other is a silent divergence in the HTML
# alone, so neither half above can see it. Found in review.
#
# A locator is (attr, value) for a meta tag, or ("json-ld", key) for a key in
# the JSON-LD graph.
HTML_DUPLICATES = (
    (("property", "og:image"), ("name", "twitter:image")),
    (("property", "og:image:alt"), ("name", "twitter:image:alt")),
    (("name", "description"), ("json-ld", "description")),
)

# A DUPLICATE PAIR WHOSE TWO HALVES ARE NOT BOTH REWRITTEN AT RUNTIME agrees in
# the file and disagrees in the browser. HTML_DUPLICATES says an edit to one
# half is an error on the other -- but `setLanguage()` rewrites the meta
# description on every load and nothing rewrites the JSON-LD `description`
# fifty lines below it, so a Russian-locale reader gets a Russian meta
# description beside an English JSON-LD one at the same URL. The pair the table
# holds together in the bytes comes apart in the DOM, and nothing said so.
# Found in review.
#
# So the asymmetry is declared, with the reason, or it is reported. The rule is
# checked in both directions: an undeclared pair with one half wired is a
# finding, and a declaration for a pair that is no longer asymmetric is a stale
# one -- because the day somebody DOES wire the graph, this note becomes wrong
# and would otherwise sit here being read.
RUNTIME_DIVERGENCE = {
    frozenset((("name", "description"), ("json-ld", "description"))): (
        "the JSON-LD graph is English-only by design. The page has one canonical "
        "URL and its hreflang declares that URL English (x-default); the Russian "
        "toggle is a reader convenience that changes neither. Rewriting the graph "
        "under a client-side toggle would give one URL two machine-readable "
        "identities -- which is the failure this whole file exists to prevent, "
        "arriving through the fix for it. Recorded in docs/site/SEO.md."
    ),
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


def strip_line_comments(block):
    """Drop `//` line comments, leaving everything else at its own offsets.

    A COMMENT IS NOT A FIELD, and until this existed one could be. `js_field`
    takes the FIRST regex match in the block, so a line reading

        // title: 'something else entirely'

    placed above the real `title:` inside `copy.en` was returned as the title --
    and the checker then reported the rendered head as diverging from
    `index.html` when nothing had diverged. Loud rather than silent, so it is a
    nuisance rather than a hole, but it is a false FAILURE on a check whose whole
    value is that a failure means something.

    A test named "a comment naming a field is not a field" was meant to cover
    this and did not: it inserted the comment BEFORE `const copy = {`, outside
    the span `js_block` reads, so it could not have failed for its stated
    reason. Moving it inside proved the defect. Found in review.

    `//` inside a string literal is not a comment. None of the copy strings
    contains one today -- they are prose for a search result and a social card --
    but a URL would, so the scan tracks whether it is inside a quote rather than
    assuming.
    """
    out = []
    for line in block.split("\n"):
        quote = None
        cut = None
        index = 0
        while index < len(line):
            char = line[index]
            if quote:
                if char == "\\":
                    index += 2
                    continue
                if char == quote:
                    quote = None
            elif char in "'\"`":
                quote = char
            elif char == "/" and line[index + 1 : index + 2] == "/":
                cut = index
                break
            index += 1
        out.append(line if cut is None else line[:cut])
    return "\n".join(out)


def js_field(block, field):
    # Single-quoted string literals, which is what this file uses throughout.
    found = re.search(
        r"\b%s:\s*'((?:[^'\\]|\\.)*)'" % re.escape(field), strip_line_comments(block)
    )
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


def assigned_targets(body):
    """Every local name `setLanguage()` writes a `copy[lang].X` into.

    The mirror of `assigned_field`: that one asks "what does this target get?",
    this one asks "what gets anything?", so the two together can say whether
    ASSIGNMENTS covers the function or merely intersects it.

    `document.title` is spelled as itself rather than as a bare identifier, so
    it is matched separately and reported under the same name the table uses.
    """
    found = set()
    if re.search(r"\bdocument\.title\s*=\s*copy\[\s*lang\s*\]\.\w+", body):
        found.add("document.title")
    for match in re.finditer(r"\b(\w+)\.content\s*=\s*copy\[\s*lang\s*\]\.\w+", body):
        found.add(match.group(1))
    # An inline selector has no `\w+` before `.content`, so the loop above walks
    # straight past it and the completeness check never sees the tag. Review
    # found the hole in the guard written to close a hole. It is reported under
    # the selector itself, which is enough to name the tag in the message: the
    # fix is a `const` at the top of the file and a row in ASSIGNMENTS, and both
    # halves of the wiring check need the named variable anyway.
    # The quote character is captured and back-referenced rather than excluded:
    # a CSS attribute selector is routinely 'meta[property="og:image:alt"]',
    # which carries the OTHER quote inside it. A character class barring both
    # matches nothing at all -- which is how the first draft of this loop
    # silently kept the hole it was added to close.
    for match in re.finditer(
        r"document\.querySelector\(\s*(['\"])(.*?)\1\s*\)\.content"
        r"\s*=\s*copy\[\s*lang\s*\]\.\w+",
        body,
    ):
        found.add("document.querySelector(%s%s%s)" % (match.group(1), match.group(2), match.group(1)))
    return sorted(found)


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


def json_ld_value(html, key):
    """One key out of the JSON-LD graph, read as text rather than parsed.

    Parsing would be stricter and is not what is wanted: this check is about
    the bytes a duplicate holds, and a JSON parse would normalise escapes that
    a hand edit would leave different.
    """
    block = re.search(
        r'<script[^>]+type="application/ld\+json"[^>]*>(.*?)</script>', html, re.S
    )
    if block is None:
        return None
    found = re.findall(r'"%s"\s*:\s*"([^"]*)"' % re.escape(key), block.group(1))
    return found[0] if found else None


def locate(html, locator, problems):
    """The string a locator names, whether it lives in a meta tag or the graph."""
    attr, value = locator
    if attr == "json-ld":
        return json_ld_value(html, value)
    return html_meta(html, attr, value, problems)


def head_strings(html):
    """Every string in the head this check can name, keyed by locator.

    Used for the completeness half: a duplicate nobody declared is a pair that
    will rot, and the whole point of HTML_DUPLICATES is that the list is the
    contract. og:image:alt was already the next one.
    """
    found = {}
    for attr, name, content in re.findall(
        r'<meta\s+(name|property)="([^"]+)"\s+content="([^"]*)"\s*/?>', html
    ):
        found[(attr, name)] = content
    graph = json_ld_value(html, "description")
    if graph is not None:
        found[("json-ld", "description")] = graph
    return found


def check_html_duplicates(html, problems):
    """The declared pairs agree, and no undeclared pair exists."""
    declared = set()
    for left, right in HTML_DUPLICATES:
        declared.add(frozenset((left, right)))
        one = locate(html, left, problems)
        two = locate(html, right, problems)
        if one is None:
            problems.append("index.html has no %s" % (left,))
            continue
        if two is None:
            problems.append("index.html has no %s" % (right,))
            continue
        if one != two:
            problems.append(
                "%s and %s are duplicates of each other and have diverged\n"
                "    %s: %r\n    %s: %r\n"
                "    Nothing assigns either one, so only this check looks at them."
                % (left, right, left, one, right, two)
            )

    # The two site.js also assigns are held in step through `copy.en`, so they
    # are declared here without being compared twice.
    for tag, field in MIRRORED.items():
        partner = HTML_SOURCES.get(field)
        if partner is not None and partner[0] != "title-tag":
            declared.add(frozenset(((partner[0], partner[1]), ("name", tag))))

    # COMPLETENESS. Anything else byte-identical is an undeclared duplicate.
    # Short values are excluded: `en_US` and a locale are equal by definition
    # and mean nothing to each other.
    strings = head_strings(html)
    by_value = {}
    for locator, value in strings.items():
        if len(value) >= 24:
            by_value.setdefault(value, []).append(locator)
    for value, locators in sorted(by_value.items()):
        if len(locators) < 2:
            continue
        for index, left in enumerate(sorted(locators)):
            for right in sorted(locators)[index + 1 :]:
                if frozenset((left, right)) in declared:
                    continue
                problems.append(
                    "%s and %s hold the same string and no rule pairs them\n"
                    "    %r\n"
                    "    Either they are duplicates -- add them to HTML_DUPLICATES so an\n"
                    "    edit to one is an error on the other -- or the repeat is an\n"
                    "    accident and one of them is wrong." % (left, right, value)
                )


def check_runtime_divergence(problems):
    """Declared duplicates must survive the language switch, or say why not.

    A statement about this file's own two tables rather than about the tree, so
    no edit to index.html or site.js can reach it -- which is exactly why it is
    here: the model those tables hold is what every other check reasons from,
    and it was wrong in a way no amount of reading the HTML would show.
    """
    assigned = {
        (attr, value) for attr, value, _ in ASSIGNMENTS.values() if attr is not None
    }
    pairs = set()
    for left, right in HTML_DUPLICATES:
        pair = frozenset((left, right))
        pairs.add(pair)
        rewritten = [locator for locator in (left, right) if locator in assigned]
        if len(rewritten) == 1:
            if pair not in RUNTIME_DIVERGENCE:
                problems.append(
                    "%s and %s are declared duplicates, but setLanguage() rewrites only\n"
                    "    one half (%s), so they agree in the file and diverge in the DOM\n"
                    "    the moment a visitor switches language.\n"
                    "    Either wire the other half -- a row in ASSIGNMENTS -- or declare the\n"
                    "    asymmetry in RUNTIME_DIVERGENCE with the reason. Silence is what\n"
                    "    HTML_DUPLICATES exists to stop." % (left, right, rewritten[0])
                )
        elif pair in RUNTIME_DIVERGENCE:
            problems.append(
                "%s and %s carry a RUNTIME_DIVERGENCE note that no longer describes them:\n"
                "    setLanguage() now rewrites %s of the two. Delete the note -- it reads as\n"
                "    a decision and is a leftover." % (left, right, "both" if rewritten else "neither")
            )
    for pair in RUNTIME_DIVERGENCE:
        if pair not in pairs:
            problems.append(
                "RUNTIME_DIVERGENCE declares %s, which is not a pair in HTML_DUPLICATES.\n"
                "    Nothing compares those two, so the exemption exempts them from nothing."
                % sorted(pair)
            )


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

    # THE TABLE MUST BE COMPLETE, and until now nothing said so. Every check
    # above iterates ASSIGNMENTS, so a localised field added to setLanguage()
    # and forgotten here falls outside BOTH halves: the data comparison does not
    # know the tag is localised, and the wiring check never looks at it. It
    # would stay unchecked for as long as the file lives, silently -- which is
    # exactly the substitution this artefact exists to stop, applied to itself.
    # og:image:alt is the next likely one. Found in review.
    for name in assigned_targets(body):
        if name not in ASSIGNMENTS:
            problems.append(
                "setLanguage() assigns `%s`, which is not in ASSIGNMENTS in this file.\n"
                "    Nothing checks it: neither that it selects the right tag, nor that it\n"
                "    is given the right copy field. Add a row -- the table is the contract."
                % name
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

    check_html_duplicates(html, problems)

    check_runtime_divergence(problems)

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
        "%d in-HTML duplicate pairs agree (%d of them declared English-only "
        "under the language switch) and no undeclared duplicate exists, "
        "%d assignments in setLanguage() are wired to the right tag, "
        "%d Russian strings present."
        % (
            len(HTML_SOURCES),
            len(MIRRORED),
            len(HTML_DUPLICATES),
            len(RUNTIME_DIVERGENCE),
            len(ASSIGNMENTS),
            len(HTML_SOURCES),
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "."))
