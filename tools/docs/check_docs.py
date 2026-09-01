#!/usr/bin/env python3
"""Check repository documentation facts that are cheap to verify mechanically.

Checks resolve relative links, validate inline-code spans, keep decision and
open-question IDs unique, reject unexpected tracked root files, and verify that
line-number citations still land on nonblank content -- and, where the citation
names a source file this repository tracks, on the text it was cited for.

Run: python3 tools/docs/check_docs.py [root]
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

# ](target), ](target#anchor) and ](#anchor). Excludes targets containing
# whitespace, which in Markdown would carry a title string we do not want to
# parse. The target may be EMPTY -- `](#od-16)` is a link into the current
# document, and until check 1 verified anchors there was no reason to capture
# one.
LINK = re.compile(r"\]\(\s*([^)\s#]*)(#[^)\s]*)?\s*\)")

# GitHub builds a heading's anchor by lowercasing it, dropping everything that
# is not a letter, a digit, a space, a hyphen or an underscore, and turning
# spaces into hyphens. Markdown emphasis and inline code disappear with the
# punctuation, which is why `## OD-16 — A1, A2 and A3` answers to
# `#od-16--a1-a2-and-a3`: the em dash goes, its two spaces do not.
HEADING = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
ANCHOR_STRIP = re.compile(r"[^\w\- ]", re.UNICODE)
FENCE = re.compile(r"^\s*(```|~~~)")
# Runs of backticks delimit inline code spans; run length matters for CommonMark.
TICK_RUN = re.compile(r"`+")

# `managed_components` is ESP-IDF's vendored dependency tree: gitignored, absent
# from CI's checkout, and not ours to edit. It was walked anyway, so a local
# firmware build made the checker report two broken anchors and a moved line in
# somebody else's README -- findings nobody here can act on and CI never sees.
SKIP_DIRS = {".git", "build", "node_modules", "external", ".venv",
             "__pycache__", "managed_components"}
EXTERNAL = ("http://", "https://", "mailto:", "tel:", "ftp://", "//")


def scan_lines(text: str):
    """Every line as (1-based number, text, inside a fenced block).

    The fence markers themselves count as inside, so a caller that skips fenced
    lines skips them too.
    """
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
            yield n, line, True
            continue
        yield n, line, fenced


def strip_fences(text: str) -> list[tuple[int, str]]:
    """Lines outside fenced code blocks, as (1-based line number, text)."""
    return [(n, line) for n, line, fenced in scan_lines(text) if not fenced]


def without_code_spans(line: str) -> str:
    """The line with every inline code span blanked, offsets preserved.

    `](#some-anchor)` inside backticks is TEXT -- GitHub renders it as the
    characters, not as a link -- so a document illustrating link syntax was
    being read as making the link. That is the same defect the `EXAMPLE.md`
    reservation exists for, one check over: an illustration became an
    assertion, and the document reporting it was the one describing the fix.
    Blanking rather than deleting keeps every column where it was, so a
    reported line number still points at the right place. An unbalanced
    backtick would swallow the rest of the line here; check 2 exists to report
    exactly that, so it cannot pass unnoticed.
    """
    out = list(line)
    spans = list(TICK_RUN.finditer(line))
    i = 0
    while i < len(spans):
        opener = spans[i]
        for j in range(i + 1, len(spans)):
            if spans[j].group(0) == opener.group(0):
                for k in range(opener.start(), spans[j].end()):
                    out[k] = " "
                i = j + 1
                break
        else:
            break
    return "".join(out)


def markdown_files(root: str) -> list[str]:
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.endswith(".md"):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def heading_anchors(text: str) -> set[str]:
    """Every anchor the headings of one document answer to.

    A repeated heading gets `-1`, `-2` and so on, in document order, which is
    how GitHub disambiguates and how `#od-16-1` would be written by hand.
    Headings inside fenced blocks are not headings.
    """
    seen: dict[str, int] = {}
    anchors: set[str] = set()
    for _lineno, line, fenced in scan_lines(text):
        if fenced:
            continue
        match = HEADING.match(line)
        if not match:
            continue
        slug = ANCHOR_STRIP.sub("", match.group(2).lower()).replace(" ", "-")
        if not slug:
            continue
        count = seen.get(slug, 0)
        seen[slug] = count + 1
        anchors.add(slug if count == 0 else "%s-%d" % (slug, count))
    return anchors


def check_links(root: str) -> list[str]:
    problems = []
    anchor_cache: dict[str, set[str] | None] = {}

    def anchors_of(target: str):
        if target not in anchor_cache:
            try:
                with open(target, encoding="utf-8") as handle:
                    anchor_cache[target] = heading_anchors(handle.read())
            except (OSError, UnicodeDecodeError):
                anchor_cache[target] = None
        return anchor_cache[target]

    for path in markdown_files(root):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        here = os.path.dirname(path)
        for lineno, line in strip_fences(text):
            for match in LINK.finditer(without_code_spans(line)):
                target, anchor = match.group(1), match.group(2)
                if target.startswith(EXTERNAL) or target.startswith("<"):
                    continue
                if not target and not anchor:
                    continue
                # A repository-root-absolute link is written /like/this; treat it
                # as relative to the root rather than to the filesystem.
                base = root if target.startswith("/") else here
                resolved = (
                    path
                    if not target
                    else os.path.normpath(os.path.join(base, target.lstrip("/")))
                )
                rel = os.path.relpath(path, root)
                if not os.path.exists(resolved):
                    problems.append(f"{rel}:{lineno}: link target does not exist: {target}")
                    continue
                # AND THE `#anchor` IS HALF THE LINK. Check 1 captured it and
                # then never looked at it, which is recorded as T-127 and is
                # what makes an OD-number collision survive a merge: renumber
                # one of two `## OD-16` headings and every `#od-16` link in the
                # repository silently lands at the top of the file, CI green.
                # A missing anchor is a 404 the reader only notices by ending
                # up in the wrong place, which is worse than a 404 they see.
                if not anchor or len(anchor) < 2 or not resolved.endswith(".md"):
                    continue
                known = anchors_of(resolved)
                if known is None or anchor[1:] in known:
                    continue
                where = "this document" if resolved == path else target
                problems.append(
                    "%s:%d: no heading in %s answers to the anchor %s"
                    % (rel, lineno, where, anchor)
                )
    return problems


# A citation of the form `path/to/file.md:123` or `file.h:12-34`, as this
# repository writes them: inside backticks, in a link, or bare in prose. The
# suffix list is the file kinds actually cited here; widening it would start
# matching version strings and times.
# The leading class allows `.` because a relative path starts with one:
# `[ADR-0003](../adr/0003-radio-not-lora.md):109-111` was captured from the `a`
# of `adr/`, resolved from nowhere, and silently skipped as "not a file in this
# repository" -- while three documents said citations were now checked. Found in
# review. `\b` cannot open the pattern once `.` may lead it, so the boundary is
# a look-behind for a character that could continue a path.
#
# AND A DOT-DIRECTORY IS A PATH TOO. `(?:\.{1,2}/)*` only admits `./` and `../`,
# so `.github/workflows/ci.yml:281` matched nothing: the pattern cannot start at
# the `.`, and starting at the `g` is what the look-behind exists to refuse. Two
# citations to that file sat 211 lines out of date because the check that three
# documents call the answer to citation drift could not see them at all. Found
# in review; `\.?` before the first path character is the whole fix.
CITATION = re.compile(
    r"(?<![A-Za-z0-9_./-])((?:\.{1,2}/)*\.?[A-Za-z0-9_][A-Za-z0-9_./-]*"
    # `c` sits after `cpp` for a reader, not for the engine: alternation
    # backtracks, so `foo.cpp:1` never matches as `foo.c` with `pp:1` left over.
    # It was missing entirely, and `probe/pedo.c:402` -- the divisor a bench
    # report's every milligravity figure rests on -- was therefore not a
    # citation to this file at all, and could not be asked for a fingerprint.
    r"\.(?:md|cpp|c|h|hpp|py|sh|yml|yaml|json|jq|txt|cmake))"
    # The `)` is a Markdown link closing before the line number:
    # `[ADR-0003](../adr/0003-radio-not-lora.md):109-111`. Not captured.
    #
    # NO WHITESPACE AROUND THE SEPARATOR, and that is the whole of the rule.
    # Allowing it turned "STATUS.md:843 - 26 lines below" -- ordinary English,
    # a correct citation followed by a correct number -- into the range 843-26,
    # which then failed as a descending range and reddened CI for a true
    # sentence. Every real range in this repository is written closed up.
    # Found in review.
    # NOT a compiler diagnostic. `file.cpp:9:10: fatal error:` is line and
    # COLUMN, quoted from a transcript nobody may edit, and it is not this
    # repository's citation syntax -- but `:9` matched, and the mandatory
    # fingerprint below then asked a build log to make a promise.
    r"\)?:(\d+)(?:[-\u2013](\d+))?\b(?!:\d)"
)

# An optional FINGERPRINT after a citation: `HARDWARE_MATRIX.md:357 "Display
# FPC"`. A bare line number rots every time anybody inserts a paragraph above
# it, and it rots SILENTLY -- the line it lands on is real and non-blank, so
# nothing here could see it. Two citations in this repository were thirteen
# lines out and pointed at a real, wrong row for weeks. With a fingerprint the
# check reads the cited line and says where the text actually went, which turns
# drift from an undetectable defect into a one-line fix. Opt-in by design:
# adding one is a promise this check then keeps.
# The optional `](...)` is the tail of a Markdown link: these documents write
# `[HARDWARE_MATRIX.md:357](HARDWARE_MATRIX.md)`, and the citation match ends
# inside it, so a fingerprint written after the link would otherwise be seen by
# nothing -- a promise silently not kept, which is worse than no promise.
# The path a citation links to, immediately after it. These documents write
# `[`core/clock.h:86`](../../core/include/attadipa/core/clock.h)`: a SHORT LABEL
# for the reader and the real path in the href. The label is not a stale path
# and must not be reported as one -- but it is also the only place a line number
# appears, so resolving through the href is what makes `:86` checkable at all.
# Anchored, so a later link on the same line cannot be mistaken for this one.
CITATION_HREF = re.compile(r"\A`?\]\(([^)#]+)")

# What may sit between a citation and its fingerprint: the closing half of a
# link, and backticks. Shared with the wrapped-fingerprint rule in _report so
# the two cannot disagree about where a citation ends.
FINGERPRINT_LEAD = re.compile(r'\A`?(?:\]\([^)]*\))?`?')

FINGERPRINT = re.compile(
    FINGERPRINT_LEAD.pattern + r'\s*[\u2014-]?\s*"([^"]{3,80})"'
)

# HOW TO WRITE AN EXAMPLE THAT IS NOT A CITATION. A fingerprint is an
# assertion, and prose that merely ILLUSTRATES the syntax makes it by accident:
# STATUS.md and TASKS.md both showed the new form with a real path and a real
# line number, so the two documents CLAUDE.md tells the next agent to read
# first were quietly asserting a line number in a third document that neither
# of them is about. Inserting one line above that row reddens CI naming them.
# Found in review, and there was no way to write the example inertly -- fences
# are deliberately not stripped here, and backticks exempt nothing.
#
# The escape is a path that resolves to nothing, since an unresolvable citation
# is already skipped as somebody else's tree. Naming one reserved spelling
# makes that deliberate rather than folklore, and the check then keeps the
# reservation: if a file called EXAMPLE.md is ever added, every illustration in
# the repository silently becomes a live assertion, so the placeholder existing
# is itself reported.
PLACEHOLDER = "EXAMPLE.md"

# These documents also cite a sibling by its bare SHOUTING name --
# `HARDWARE_MATRIX:144`, no extension -- and that spelling is where the defect
# this check was written for actually lived. Resolved against the tree rather
# than a hardcoded list, and only when exactly one file answers to the name.
BARE_CITATION = re.compile(r"\b([A-Z][A-Z0-9_]{3,}):(\d+)(?:[-\u2013](\d+))?\b")

# A CONTINUATION: `:271` with no path, meaning "the file the citation before it
# named". The empty group(1) keeps the group numbering every other citation form
# here uses -- group(2) first line, group(3) last -- so one reporter serves all
# three. The closing backtick is a lookahead rather than part of the match, so
# `match.end()` lands where it does for a backticked citation and the shared
# fingerprint rule reads the same tail.
CONTINUATION = re.compile(r"`():(\d+)(?:[-\u2013](\d+))?(?=`)")


def bare_document_index(root: str) -> dict[str, str]:
    index: dict[str, list[str]] = {}
    for path in markdown_files(root):
        stem = os.path.basename(path)[: -len(".md")]
        index.setdefault(stem, []).append(path)
    return {name: paths[0] for name, paths in index.items() if len(paths) == 1}


# CITED_SUFFIXES mirrors the suffix list inside CITATION: the same file kinds,
# indexed by basename so a citation written without a path can still be
# resolved.
CITED_SUFFIXES = (
    ".md", ".cpp", ".c", ".h", ".hpp", ".py", ".sh", ".yml", ".yaml",
    ".json", ".jq", ".txt", ".cmake",
)


def basename_index(root: str) -> dict[str, str]:
    """Every citable file, by basename, where exactly one file answers to it.

    A citation is written with a path only when the writer thought of one.
    `ARCHITECTURE.md:139` from `docs/research/` resolved neither beside the
    citing document nor at the repository root, so it was skipped as "somebody
    else's tree" -- and it was wrong: 139 is inside a `HardwareFeature` enum
    fence, and the `has()` sites it claims to cite are at 215, 223 and 659.
    Four citations were being skipped this way. Ambiguity is still a skip: two
    files with one basename cannot be told apart from the citation alone, and
    guessing between them would report a line number from the wrong file.
    """
    index: dict[str, list[str]] = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.endswith(CITED_SUFFIXES):
                index.setdefault(name, []).append(os.path.join(dirpath, name))
    return {name: paths[0] for name, paths in index.items() if len(paths) == 1}


def tracked_files(root: str) -> set[str]:
    """Every path this repository tracks, repo-relative.

    A citation into a file we EDIT is the one that rots: documentation lines
    are comparatively stable and are themselves checked, while source is under
    active change. Tracking is the test for "ours" -- a build directory or a
    vendored `managed_components/` tree is neither ours to keep nor present in
    CI, and requiring a promise about it would redden a checkout and not a
    change. Outside a git checkout this returns nothing and the rule is off.
    """
    listing = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    if listing.returncode != 0:
        return set()
    return {name for name in listing.stdout.split("\0") if name}


def check_citation_lines(root: str) -> list[str]:
    """A `file:line` citation points at a line that exists, is not blank, and --
    where the citation carries a fingerprint -- still says what it was cited
    for. A bare `:line` gets the same treatment when, and only when, the
    citation it continues is on the same line and resolved."""
    problems = []
    cache: dict[str, list[str] | None] = {}

    def lines_of(target: str):
        if target not in cache:
            try:
                with open(target, encoding="utf-8") as handle:
                    body = handle.read().split("\n")
                # A file ending in a newline splits to a trailing empty string
                # that is not a line. Left in, it makes the last line number a
                # file has look like a valid one, which is the off-by-one this
                # check exists to catch.
                if body and body[-1] == "":
                    body.pop()
                cache[target] = body
            except (OSError, UnicodeDecodeError):
                cache[target] = None
        return cache[target]

    bare_index = bare_document_index(root)
    by_basename = basename_index(root)
    tracked = tracked_files(root)

    def source_target(resolved: str) -> str:
        """Repo-relative path, when a citation lands in a source file we edit.

        Empty for a `.md`, for a path this repository does not track, and for
        anything outside the tree -- those stay opt-in, which is what #331
        settled. What is NOT opt-in is a line number in a file under active
        edit: inserting a paragraph above it lands the citation on a line that
        is real, non-blank and about something else, and nothing here could
        see it.
        """
        if not resolved or resolved.endswith(".md"):
            return ""
        rel = os.path.relpath(resolved, root)
        return rel if rel in tracked else ""

    for path in markdown_files(root):
        if os.path.basename(path) == PLACEHOLDER:
            problems.append(
                "%s: %s is the reserved placeholder this repository writes "
                "citation EXAMPLES with, so it must not exist -- every "
                "illustration of the syntax would become a live assertion "
                "about this file. Rename it."
                % (os.path.relpath(path, root), PLACEHOLDER)
            )
    for path in markdown_files(root):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        here = os.path.dirname(path)
        rel_self = os.path.relpath(path, root)
        # Fences are NOT stripped here, unlike every other check in this file.
        # TASKS.md keeps its `BLOCKED:` records in fenced blocks, and the
        # citation that sent a reader to a blank line -- on the GNSS rail, the
        # fact CLAUDE.md holds up as the cost of guessing -- was inside one.
        following = text.splitlines()[1:] + [""]
        for lineno, line, _fenced in scan_lines(text):
            nxt = following[lineno - 1]
            # Sorted, because a continuation is checked against whatever citation
            # precedes it and "precedes" is a position on the line, not the order
            # two regexes happened to run in.
            #
            # ON THE SAME LINE, AND RESOLVED. Both halves of that are load-
            # bearing, and the tree says why. Of 138 bare `:NN` forms in these
            # documents, 77 have no citation within five lines of them at all and
            # 55 continue a path into a tree this repository does not contain; a
            # rule that scans backwards for a path binds by proximity, and
            # proximity is not reference. Five lines above the `:134` in
            # CLOCK_STATE_AND_CADENCE.md sits a citation to our `availability.h`,
            # while the sentence carrying the `:134` says "InfiniTime's" -- a
            # backward rule would verify a line of OUR file against a claim about
            # somebody else's, and report the mismatch as a defect in the
            # document. Binding to the anchor's RESOLVED target rather than to
            # its path text is the other half: a citation this tree cannot
            # resolve leaves no anchor at all, and a continuation of nothing is
            # nothing. It does not second-guess the anchor either -- a
            # continuation is exactly as trustworthy as the citation it
            # follows, and never more.
            anchor = ""
            for match in sorted(
                list(CITATION.finditer(line))
                + list(BARE_CITATION.finditer(line))
                + list(CONTINUATION.finditer(line)),
                key=lambda found: found.start(),
            ):
                if match.re is CONTINUATION:
                    if anchor:
                        body = lines_of(anchor)
                        if body is not None:
                            _report(
                                problems, rel_self, lineno,
                                os.path.relpath(anchor, root), match, body,
                                line, nxt, source_target(anchor)
                            )
                    continue
                cited, first, last = match.group(1), int(match.group(2)), match.group(3)
                # A citation that fails to resolve clears the anchor rather than
                # leaving the previous one standing: `:7` after it continues the
                # file it follows, not the one before that.
                anchor = ""
                if cited in bare_index:
                    resolved = bare_index[cited]
                    body = lines_of(resolved)
                    if body is not None:
                        anchor = resolved
                        _report(
                            problems, rel_self, lineno, cited, match, body,
                            line, nxt, source_target(resolved)
                        )
                    continue
                # Resolve beside the citing file first, then from the root. A
                # bare basename -- `TEST_FLEET.md:21` -- is how these documents
                # cite a sibling, and both spellings appear.
                for base in (here, root):
                    resolved = os.path.normpath(os.path.join(base, cited.lstrip("/")))
                    if os.path.isfile(resolved):
                        break
                else:
                    # Then anywhere in the tree, if exactly one file carries
                    # that basename -- see basename_index().
                    # The label may be shorthand for a path the href spells
                    # out in full. Prefer the href: it is what a reader clicks,
                    # and check_links already proves it resolves.
                    href = CITATION_HREF.match(line[match.end():])
                    if href:
                        target = href.group(1)
                        # An href is resolved the way `check_links` resolves
                        # one, because it is the same href: `/path` from the
                        # repository root, and an external target not at all.
                        # Joining `https://...` onto a local directory yields a
                        # path that cannot exist, and the failure then fell
                        # through to the rename heuristic below -- which is how
                        # a correct external citation was reported as a local
                        # file this repository had moved.
                        if target.startswith(EXTERNAL) or target.startswith("<"):
                            continue
                        base = root if target.startswith("/") else here
                        via = os.path.normpath(
                            os.path.join(base, target.lstrip("/"))
                        )
                        if os.path.isfile(via):
                            body = lines_of(via)
                            if body is not None:
                                anchor = via
                                _report(problems, rel_self, lineno, cited,
                                        match, body, line, nxt,
                                        source_target(via))
                            continue
                    resolved = by_basename.get(os.path.basename(cited), "")
                    if not resolved or "/" in cited.strip("./"):
                        # Three cases used to end here alike, and one of them is
                        # ours. An upstream source and a false positive are not
                        # ours to verify -- a line number in somebody else's
                        # tree means nothing here, and a citation naming a
                        # DIRECTORY is not resolved by basename either:
                        # `upstream/foo/ci.yml:12` means their ci.yml, not ours.
                        #
                        # A RENAMED path is different, and skipping it silently
                        # is how eleven citations naming files this repository
                        # no longer has stayed green. `core/clock.h:31` is not
                        # somebody else's tree: `core/` is ours, the file moved
                        # to `core/include/attadipa/core/`, and the citation
                        # now sends a reader nowhere. No fingerprint can catch
                        # it, because resolution fails before there is anything
                        # to compare.
                        #
                        # What separates them is whether we can PROVE the
                        # file is ours. "the first segment is a directory we
                        # track" is not proof: `docs/` is ours and it is also
                        # LilyGoLib's, and HARDWARE_MATRIX cites
                        # `docs/hardware/lilygo-t-watch-s3-plus.md:68` in THEIR
                        # tree. That heuristic reddened a correct citation.
                        #
                        # A unique basename elsewhere in our tree is proof: the
                        # file exists here, at another path, so the citation is
                        # a rename we did and can name the fix for. A basename
                        # we do not have at all stays silent -- upstream, or
                        # deleted, and neither is decidable from here.
                        # `resolved` is the basename lookup above: non-empty
                        # means our tree carries this file at a different path.
                        # ...and a unique basename is NOT enough on its own.
                        # `upstream/meshcore/main.cpp` has a basename this tree
                        # carries exactly once, at `sim/main.cpp`, so the
                        # report would announce a rename between two unrelated
                        # files in two different projects. What makes it proof
                        # is the basename being unique AND the file still
                        # living under the same top directory the citation
                        # names: `core/clock.h` -> `core/include/.../clock.h`
                        # stays inside `core/`, while nothing upstream does.
                        # Stronger than the "first segment is a directory we
                        # track" heuristic rejected above, which asked only
                        # whether `docs/` exists and not where the file went.
                        moved = resolved
                        if moved and cited.strip("./").split("/")[0] != (
                            os.path.relpath(moved, root).split(os.sep)[0]
                        ):
                            moved = ""
                        if moved and "/" in cited.strip("./"):
                            problems.append(
                                f"{rel_self}:{lineno} cites {cited}, which "
                                f"this repository no longer has at that path. "
                                f"It moved to {os.path.relpath(moved, root)} -- "
                                f"repoint the citation, or link the file so the "
                                f"line number can be checked."
                            )
                        continue
                body = lines_of(resolved)
                if body is None:
                    continue
                anchor = resolved
                _report(problems, rel_self, lineno, cited, match, body, line,
                        nxt, source_target(resolved))
    return problems


def _report(problems, rel_self, lineno, cited, match, body, line="",
            next_line="", source="") -> None:
    first = int(match.group(2))
    last = match.group(3)
    span = match.group(0).split(":", 1)[1]
    # A DESCENDING or ZERO range. `range(30, 12)` is empty and `max()` of it
    # raises, so the job died on a traceback instead of naming the document --
    # and `:0` indexed `body[-1]`, quietly approving a citation to the last
    # line of the file. Prose reaches here: the separator allows spaces, so
    # "STATUS.md:843 - 26 lines below" parses as the range 843-26. Found in
    # review; both are now reported rather than crashed on or waved through.
    if first < 1 or (last is not None and int(last) < first):
        problems.append(
            "%s:%d: cites %s:%s, which is not a line range -- a citation reads "
            "first-last, and lines start at 1"
            % (rel_self, lineno, cited, span)
        )
        return
    wanted = [first] if last is None else list(range(first, int(last) + 1))
    if max(wanted) > len(body):
        problems.append(
            "%s:%d: cites %s:%s, but that file has %d lines"
            % (rel_self, lineno, cited, span, len(body))
        )
        return
    if all(not body[n - 1].strip() for n in wanted):
        problems.append(
            "%s:%d: cites %s:%s, which is blank -- the lines it named have moved"
            % (rel_self, lineno, cited, span)
        )
        return
    # And the half a blank-line test cannot see: the cited lines are real, and
    # about something else entirely.
    stamp = FINGERPRINT.match(line[match.end() :])
    if not stamp and next_line:
        # A fingerprint that WRAPPED. Markdown reflows prose, so the quote can
        # sit at the start of the next line while the citation stays on this
        # one -- and end-of-line used to read as "no fingerprint given", which
        # is silence. WAVESHARE_ARRIVAL wrote its citation and its
        # "8 MB **octal**" that way, the citation then drifted ten lines, and
        # the check watched it happen. A promise nothing keeps is worse than no
        # promise: that is the whole thesis of the issue this check comes from,
        # and the check had the defect in itself.
        #
        # Only when the citation line ends there. Anything else after the
        # citation means the author wrote prose, and a quotation further down
        # that prose is not this citation's fingerprint.
        tail = FINGERPRINT_LEAD.sub("", line[match.end() :]).strip()
        if tail in ("", "\u2014", "-"):
            stamp = FINGERPRINT.match(next_line.strip())
    if not stamp:
        if source:
            # MANDATORY for a source file, opt-in everywhere else. Twice on one
            # branch a source citation was moved onto different real code and
            # this checker said `none` -- once onto the same function's
            # `printf`, under a sentence quoting a formula that line does not
            # contain. A reader following it lands on plausible, wrong code.
            # Eighteen citations were in that state when the rule was written,
            # which is what makes it affordable in one change where the blanket
            # rule was not.
            problems.append(
                "%s:%d: cites %s:%s into %s, a file this repository edits, "
                "with no fingerprint -- a line number alone rots silently "
                "there. Quote what it is cited for: `%s:%s` -- \"...\""
                % (rel_self, lineno, cited, span, source, cited, span)
            )
        return
    snippet = stamp.group(1)
    if any(snippet in body[n - 1] for n in wanted):
        return
    elsewhere = [n for n, text in enumerate(body, 1) if snippet in text]
    if elsewhere:
        problems.append(
            '%s:%d: cites %s:%s for "%s", which is now at %s'
            "\n    (illustrating the syntax rather than citing? write it with "
            "the placeholder path %s, which resolves to nothing)"
            % (
                rel_self,
                lineno,
                cited,
                span,
                snippet,
                ", ".join(":%d" % n for n in elsewhere[:3]),
                PLACEHOLDER,
            )
        )
    else:
        problems.append(
            '%s:%d: cites %s:%s for "%s", which is not on those lines and is '
            "not anywhere in that file"
            "\n    (illustrating the syntax rather than citing? write it with "
            "the placeholder path %s, which resolves to nothing)"
            % (rel_self, lineno, cited, span, snippet, PLACEHOLDER)
        )


# `## OD-16 — ...` in OWNER_DECISIONS.md. Level two only: a `### OD-16` under a
# decision is part of that decision, not a second one.
DECISION_HEADING = re.compile(r"^##\s+(OD-\d+)\b")


def check_decision_ids(root: str) -> list[str]:
    """One OD number, one decision.

    Four open pull requests each inserted `## OD-16` at the same line of
    OWNER_DECISIONS.md, for four different owner decisions. Git does conflict on
    that -- an earlier version of this docstring claimed the branches shared no
    file and merged clean, and review refuted it with one counterexample: two of
    them share five files and both insert at the same line. The conflict is real
    and a person resolves it, which is exactly the problem. "Keep both" is the
    obvious resolution and the correct one for the prose; it leaves two `## OD-16`
    headings and two ambiguous anchors, with CI green. Nothing here looked: check
    3 is TASKS.md-only, and check 1 captures a link's `#anchor` and then never
    uses it.

    The register is the file people read to find out what the owner decided. Two
    answers under one number is the same failure as two tasks under one ID, in
    the document where being wrong is more expensive.
    """
    path = os.path.join(root, "docs", "research", "OWNER_DECISIONS.md")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    seen: dict[str, int] = {}
    problems = []
    for lineno, line in strip_fences(text):
        match = DECISION_HEADING.match(line)
        if not match:
            continue
        number = match.group(1)
        if number in seen:
            problems.append(
                f"docs/research/OWNER_DECISIONS.md:{lineno}: {number} is already "
                f"used at line {seen[number]}. One OD number names one decision; "
                f"renumber this one and update every citation of it."
            )
        else:
            seen[number] = lineno
    return problems


def check_code_spans(root: str) -> list[str]:
    """Paragraphs whose inline code spans do not close.

    Scoped to the paragraph, not the line: CommonMark lets a code span wrap a
    soft line break, and this repository's prose does that constantly. A blank
    line does end it, so an odd number of backtick runs between two blank lines
    means one of them never closes.
    """
    problems = []
    for path in markdown_files(root):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        rel = os.path.relpath(path, root)
        runs: list[int] = []
        first = 0
        for lineno, line in strip_fences(text) + [(0, "")]:
            if not line.strip():
                if runs and _unclosed(runs):
                    problems.append(
                        f"{rel}:{first}: unclosed inline code span in this paragraph"
                    )
                runs, first = [], 0
                continue
            if not runs:
                first = lineno
            runs.extend(len(run) for run in TICK_RUN.findall(line))
    return problems


def _unclosed(runs: list[int]) -> bool:
    """CommonMark's rule: a span opened by a run of N backticks closes at the
    next run of *exactly* N. Counting backticks would misread ``a ` b``, which
    is three runs and perfectly balanced."""
    i = 0
    while i < len(runs):
        try:
            i = runs.index(runs[i], i + 1) + 1
        except ValueError:
            return True
    return False

# Everything git tracks at the repository root, and nothing else belongs there.
# Kept as a literal list rather than a pattern because the point is that adding
# a root-level file should be a deliberate act that edits this line.
ROOT_ALLOWED = {
    # Agent-tool entry points point to AGENTS.md or register a tool; they do not
    # copy repository rules.
    ".clinerules",
    ".gitignore",
    ".ignore",
    ".mcp.json",
    ".windsurfrules",
    "AGENTS.md",
    "GEMINI.md",
    "opencode.json",
    "ATTADIPA_RENAME_PLAN.md",
    "CLAUDE.md",
    "CMakeLists.txt",
    "CONTRIBUTING.md",
    "COPYRIGHT.md",
    "DCO",
    "LICENSE",
    "README.md",
    "README.ru.md",
    "STATUS.md",
    "TASKS.md",
}


def check_root_files(root: str) -> list[str]:
    """Tracked files at the repository root that are not on the allow-list.

    This exists because `git add -A` run from the root has twice swept in
    something that was only ever meant to be read: an archive waiting to be
    unpacked, and later a vendor documentation page saved while researching a
    part. Both are somebody else's copyrighted material and the second one
    reached `main`. .gitignore now covers the two shapes seen so far; this
    check covers the shape not yet seen.
    """
    listing = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        capture_output=True,
        text=True,
        check=False,
    )
    if listing.returncode != 0:
        # Not a git checkout — a source tarball, say. Nothing to check.
        return []
    problems = []
    for tracked in listing.stdout.split("\0"):
        if not tracked or "/" in tracked:
            continue
        if tracked not in ROOT_ALLOWED:
            problems.append(
                f"{tracked}: tracked at the repository root and not on the "
                f"allow-list in tools/docs/check_docs.py. If it belongs here, "
                f"add it there in the same commit; if it is a stray, git rm it."
            )
    return sorted(problems)


# `| D19 | ...` or `| ~~D19~~ | ...` at the head of a table row in
# OPEN_QUESTIONS.md. Letter-and-number, optionally with a lowercase suffix
# (`D12a`), because that is how the file already sub-divides a question that
# split in two.
QUESTION_ROW = re.compile(r"^\|\s*(?:~~)?\s*([A-Z]+\d+[a-z]?)\s*(?:~~)?\s*\|")


def check_question_ids(root: str) -> list[str]:
    """One open-question ID, one question.

    Check 4 does this for OWNER_DECISIONS.md. Nothing did it for
    OPEN_QUESTIONS.md, which is the register one step earlier -- the questions
    that become owner decisions -- and carries about four times as many
    identifiers.

    The failure it exists for: a branch filed the panel's wire byte order as
    `D19` while `main`, independently, took `D19` for the display-FPC part
    marking. The branch merged `main` afterwards and the number was not
    re-checked, because nothing re-checks a number. Two unrelated questions then
    shared one ID across nineteen citations in eight files, including
    `OWNER_DECISIONS.md` -- and every check passed, since the rows are in
    different tables and neither is a heading, a task or a link.

    Struck rows are counted. `~~D12~~` is a retired number, not a free one, and
    reusing it produces the same ambiguity with a subtler cause: a reader
    following a citation lands on a question marked RESOLVED and concludes the
    thing they were asking about is settled.

    FOUR BOUNDS, WRITTEN DOWN BECAUSE EACH IS INVISIBLE FROM THE OUTSIDE.
    Named in the third review round of #152; the fourth in the fourth.

    * It is bound to a PATH. A missing `OPEN_QUESTIONS.md` returns no findings,
      and the suite asserts that as intended -- so renaming or moving the file
      removes this guard silently, with CI green. That is the right behaviour
      for a repository where the file may not exist yet and the wrong one for a
      rename, and nothing here can tell those apart.
    * It is bound to a TABLE SHAPE. Any row in that file whose first cell reads
      `[A-Z]+` then digits then an optional letter is treated as a register
      entry -- written out rather than as the pattern, because this docstring is
      not raw and `\\d` in it is an invalid escape sequence: a SyntaxWarning on
      every compile today and a SyntaxError in a future Python. The pattern
      itself is `QUESTION_ROW` above. Found in review -- and it is the only
      backslash of its kind in the file. And the file already
      holds nine tables. A future cross-reference table repeating register IDs
      would redden CI on a correct document, with a message telling the reader
      to renumber a row that is only being cited -- the exact harm this check
      exists to prevent. Clean today: every ID in the register is unique and no
      other table reuses the shape.
    * It is bound to an UNDECORATED ID. `QUESTION_ROW` requires the first cell
      to be the bare identifier, so `| **D22** |` and a backticked one are
      invisible to it -- and this repository backticks identifiers nearly
      everywhere else, including inside the D21 row #152 itself added. All 78
      rows are undecorated today, so this is latent rather than live; it is a
      bound and not a bug, because a register row is a definition and the plain
      form is the one to insist on. Worth knowing that emphasising a row is how
      you silently leave the register. Found in the fourth review round of #152.
    * It does NOT check that a `D<NN>` cited elsewhere resolves to a live row.
      That is the half that cost the nineteen hand-edits, and a citation to a
      retired or renumbered ID is exactly as invisible now as the collision was.
      A fair follow-up; not covered here, and this docstring should not be read
      as implying otherwise.
    """
    path = os.path.join(root, "docs", "research", "OPEN_QUESTIONS.md")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    seen: dict[str, int] = {}
    problems = []
    for lineno, line in strip_fences(text):
        match = QUESTION_ROW.match(line)
        if not match:
            continue
        number = match.group(1)
        if number in seen:
            problems.append(
                f"docs/research/OPEN_QUESTIONS.md:{lineno}: {number} is already "
                f"used at line {seen[number]}. One ID names one question; "
                f"renumber this row and update every citation of it. A struck "
                f"row still owns its number."
            )
        else:
            seen[number] = lineno
    return sorted(problems)


CHECKS = (
    ("Broken relative links", "check_links"),
    ("Unclosed inline code spans", "check_code_spans"),
    ("Duplicate owner-decision numbers", "check_decision_ids"),
    ("Unexpected files tracked at the repository root", "check_root_files"),
    (
        "Citations pointing at a blank line, past the end of a file, "
        "into a source file with no fingerprint, "
        "or at a path this repository no longer has",
        "check_citation_lines",
    ),
    ("Duplicate open-question IDs", "check_question_ids"),
)


def main() -> int:
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    failed = False
    for title, function in CHECKS:
        problems = globals()[function](root)
        if problems:
            failed = True
            print(f"{title}: {len(problems)}", file=sys.stderr)
            for problem in problems:
                print(f"  {problem}", file=sys.stderr)
        else:
            print(f"{title}: none")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
