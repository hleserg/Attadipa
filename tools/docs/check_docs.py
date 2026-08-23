#!/usr/bin/env python3
"""Seven checks on the documentation, each of a failure that already happened here.

1. Relative links resolve. This repository's documents cite each other
   constantly, and a link that 404s reads exactly like one that works until
   somebody clicks it. A rename with a stale reference behind it is the normal
   way this happens.

2. Inline code spans close on the line that opens them. A stray backtick is
   invisible in a diff and turns everything after it into code until the next
   one, so a heading that lands inside the span stops being a heading. That is
   not hypothetical: this checker's own pull request shipped a `TASKS.md` where a
   splice landed inside `` `## DONE` ``, truncating one task's body and
   re-parenting its entire field list onto the next heading. The uniqueness check
   passed cleanly, because every heading was still unique.

3. Task IDs in TASKS.md are unique. A duplicate ID means two tasks answer to one
   name and an agent picking work up cannot tell which it was handed. Headings
   inside a `<details>` block are deliberately excluded: TASKS.md keeps a
   rejected task's original scope in one, and that is a record rather than a
   second live task.

4. A live task has a body, and a finished one is filed under DONE. Check 2
   catches the splice at its cause; this catches it at its effect, and catches
   the effect however it got there. The rule is TASKS.md's own, stated two
   paragraphs into it: every live task carries priority, dependencies, goal,
   acceptance, status and tests. It found four records left in live sections the
   first time it ran -- drift that predates the splice by weeks and that no
   syntactic check would ever see.

5. One OD number names one decision. Four open pull requests each inserted
   `## OD-16` into OWNER_DECISIONS.md at the same line, for four different
   decisions. They share no file, so git merges them clean and nothing forces a
   choice; "keep both" leaves two OD-16 headings and two ambiguous anchors with
   CI green. Check 3 is TASKS.md-only and check 1 captures a link's `#anchor`
   and then never looks at it, so nothing saw it. See T-127 for the anchor half,
   which is a bigger job and is not this check.

6. Nothing unexpected is tracked at the repository root. `git add -A` run from
   the root has twice swept in a file that was only ever meant to be read: an
   archive waiting to be unpacked, and later a vendor documentation page saved
   while researching a part. Both are somebody else's copyrighted material and
   the second one reached `main` before anyone noticed. .gitignore now covers
   the two shapes that have occurred; the allow-list here covers the shape that
   has not occurred yet, because the failure is the sweep and not the extension.

7. A `file.md:123` citation lands on a line that exists and is not blank. These
   documents cite each other by line number constantly, and a line number is a
   fact about a file at one moment. A branch that inserted seven lines into
   HARDWARE_MATRIX.md moved two PMU-rail rows from :144 and :145 to :151 and
   :152, and left two citations behind -- one of them pointing at a blank line,
   inside a `BLOCKED:` block whose whole subject is that guessing the GNSS rail
   means GNSS silently never starts. A citation with nothing at the end of it
   reads as a claim with no source.

   This cannot check that the line still *says* the right thing; it checks that
   there is something there. That is the difference between the two failures it
   has seen, and it is the cheap half. Citations to paths outside the repository
   -- upstream MeshCore sources and the like -- are skipped, because their line
   numbers are facts about somebody else's tree.

Run: python3 tools/docs/check_docs.py [root]
Exits non-zero on the first category that has findings, after printing all of
them. Invoke through `python3`, never as `./check_docs.py` -- the working copies
this repository is edited from report core.filemode=false, so an executable bit
never reaches a commit.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

# ](target) and ](target#anchor). Excludes targets containing whitespace, which
# in Markdown would carry a title string we do not want to parse.
LINK = re.compile(r"\]\(\s*([^)\s#]+)(#[^)\s]*)?\s*\)")
FENCE = re.compile(r"^\s*(```|~~~)")
TASK_HEADING = re.compile(r"^###\s+(T-\d+[a-z]?)\b")
# Runs of backticks delimit an inline code span, and a span does not survive a
# blank line. Counting runs rather than characters is what makes ``a `b` c``
# work.
TICK_RUN = re.compile(r"`+")
DETAILS_OPEN = re.compile(r"<details\b", re.I)
DETAILS_CLOSE = re.compile(r"</details\s*>", re.I)

SKIP_DIRS = {".git", "build", "node_modules", "external", ".venv", "__pycache__"}
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


def markdown_files(root: str) -> list[str]:
    found = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            if name.endswith(".md"):
                found.append(os.path.join(dirpath, name))
    return sorted(found)


def check_links(root: str) -> list[str]:
    problems = []
    for path in markdown_files(root):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        here = os.path.dirname(path)
        for lineno, line in strip_fences(text):
            for match in LINK.finditer(line):
                target = match.group(1)
                if target.startswith(EXTERNAL) or target.startswith("<"):
                    continue
                # A repository-root-absolute link is written /like/this; treat it
                # as relative to the root rather than to the filesystem.
                base = root if target.startswith("/") else here
                resolved = os.path.normpath(os.path.join(base, target.lstrip("/")))
                if not os.path.exists(resolved):
                    rel = os.path.relpath(path, root)
                    problems.append(f"{rel}:{lineno}: link target does not exist: {target}")
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
CITATION = re.compile(
    r"(?<![A-Za-z0-9_./-])((?:\.{1,2}/)*[A-Za-z0-9_][A-Za-z0-9_./-]*"
    r"\.(?:md|cpp|h|hpp|py|sh|yml|yaml|json|jq|txt))"
    # The `)` is a Markdown link closing before the line number:
    # `[ADR-0003](../adr/0003-radio-not-lora.md):109-111`. Not captured.
    r"\)?:(\d+)(?:\s*[-\u2013]\s*(\d+))?\b"
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
FINGERPRINT = re.compile(
    r'\A`?(?:\]\([^)]*\))?`?\s*[\u2014-]?\s*"([^"]{3,80})"'
)

# These documents also cite a sibling by its bare SHOUTING name --
# `HARDWARE_MATRIX:144`, no extension -- and that spelling is where the defect
# this check was written for actually lived. Resolved against the tree rather
# than a hardcoded list, and only when exactly one file answers to the name.
BARE_CITATION = re.compile(r"\b([A-Z][A-Z0-9_]{3,}):(\d+)(?:\s*[-\u2013]\s*(\d+))?\b")


def bare_document_index(root: str) -> dict[str, str]:
    index: dict[str, list[str]] = {}
    for path in markdown_files(root):
        stem = os.path.basename(path)[: -len(".md")]
        index.setdefault(stem, []).append(path)
    return {name: paths[0] for name, paths in index.items() if len(paths) == 1}


def check_citation_lines(root: str) -> list[str]:
    """A `file:line` citation points at a line that exists, is not blank, and --
    where the citation carries a fingerprint -- still says what it was cited
    for."""
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
    for path in markdown_files(root):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        here = os.path.dirname(path)
        rel_self = os.path.relpath(path, root)
        # Fences are NOT stripped here, unlike every other check in this file.
        # TASKS.md keeps its `BLOCKED:` records in fenced blocks, and the
        # citation that sent a reader to a blank line -- on the GNSS rail, the
        # fact CLAUDE.md holds up as the cost of guessing -- was inside one.
        for lineno, line, _fenced in scan_lines(text):
            for match in list(CITATION.finditer(line)) + list(
                BARE_CITATION.finditer(line)
            ):
                cited, first, last = match.group(1), int(match.group(2)), match.group(3)
                if cited in bare_index:
                    resolved = bare_index[cited]
                    body = lines_of(resolved)
                    if body is not None:
                        _report(
                            problems, rel_self, lineno, cited, match, body, line
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
                    # Not a file in this repository: an upstream source, a
                    # renamed path, or a false positive. check_links covers the
                    # ones written as links; a line number in somebody else's
                    # tree is not ours to verify.
                    continue
                body = lines_of(resolved)
                if body is None:
                    continue
                _report(problems, rel_self, lineno, cited, match, body, line)
    return problems


def _report(problems, rel_self, lineno, cited, match, body, line="") -> None:
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
    if not stamp:
        return
    snippet = stamp.group(1)
    if any(snippet in body[n - 1] for n in wanted):
        return
    elsewhere = [n for n, text in enumerate(body, 1) if snippet in text]
    if elsewhere:
        problems.append(
            '%s:%d: cites %s:%s for "%s", which is now at %s'
            % (
                rel_self,
                lineno,
                cited,
                span,
                snippet,
                ", ".join(":%d" % n for n in elsewhere[:3]),
            )
        )
    else:
        problems.append(
            '%s:%d: cites %s:%s for "%s", which is not on those lines and is '
            "not anywhere in that file"
            % (rel_self, lineno, cited, span, snippet)
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


def check_task_ids(root: str) -> list[str]:
    path = os.path.join(root, "TASKS.md")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    seen: dict[str, int] = {}
    problems = []
    depth = 0
    for lineno, line in strip_fences(text):
        # Count first so that a heading on the same line as <details> is excluded.
        opens = len(DETAILS_OPEN.findall(line))
        closes = len(DETAILS_CLOSE.findall(line))
        inside = depth > 0 or opens > 0
        depth = max(0, depth + opens - closes)
        if inside:
            continue
        match = TASK_HEADING.match(line)
        if not match:
            continue
        task = match.group(1)
        if task in seen:
            problems.append(
                f"TASKS.md:{lineno}: duplicate task ID {task}, first seen at line {seen[task]}"
            )
        else:
            seen[task] = lineno
    return problems


# A section heading, so a task can be told from the section it sits in.
SECTION = re.compile(r"^##\s+(.+?)\s*$")
PRIORITY = re.compile(r"^\s*[-*]\s+\*\*Priority:\*\*")
# A blocked task carries a blocker instead of a field list -- CLAUDE.md's own
# format, and TASKS.md writes it inside a fence, where nothing else here looks.
BLOCKER = re.compile(r"^\s*BLOCKED:")
# "### T-102 · Documentation consistency in CI — **DONE** 2026-08-22"
DONE_MARK = re.compile(r"\*\*DONE\*\*|\*\*REJECTED\*\*")

# Sections whose tasks are records rather than work to pick up. A record does
# not carry a field list and is not expected to.
RECORD_SECTIONS = {"DONE"}
# Sections whose tasks carry a blocker rather than a priority.
BLOCKED_SECTIONS = {"BLOCKED"}


def check_task_bodies(root: str) -> list[str]:
    """Every task in a live section has a body, and every task marked DONE or
    REJECTED is filed in a section for records.

    Both halves are one defect seen from two sides. When a heading is spliced
    into another task's text, the task above it loses its fields and the task
    below it inherits them -- so the first half catches the victim and the
    second catches the intruder, and a splice trips at least one of them
    wherever it lands. Unlike check_code_spans this does not care how the file
    got that way, which is why it also finds drift nobody typed in one edit.

    What counts as a body depends on the section. Under ## BLOCKED a task
    carries the blocker block CLAUDE.md specifies and no priority, which is
    correct rather than missing; everywhere else it is the field list, of which
    **Priority:** is the first line.
    """
    path = os.path.join(root, "TASKS.md")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as handle:
        text = handle.read()

    problems = []
    section = ""
    depth = 0
    pending: tuple[str, int, str, str] | None = None  # task, line, heading, section

    def close(pending, saw_body: bool) -> None:
        if pending is None:
            return
        task, lineno, heading, where = pending
        if where in RECORD_SECTIONS:
            return
        if DONE_MARK.search(heading):
            problems.append(
                f"TASKS.md:{lineno}: {task} is marked DONE or REJECTED but sits under "
                f"## {where}; finished work is filed under ## DONE"
            )
        elif not saw_body:
            wanted = (
                "BLOCKED: block" if where in BLOCKED_SECTIONS else "**Priority:** field"
            )
            problems.append(
                f"TASKS.md:{lineno}: {task} under ## {where} has no {wanted} "
                f"-- a live task with no body cannot be picked up"
            )

    saw_body = False
    for lineno, line, fenced in scan_lines(text):
        if not fenced:
            opens = len(DETAILS_OPEN.findall(line))
            closes = len(DETAILS_CLOSE.findall(line))
            inside = depth > 0 or opens > 0
            depth = max(0, depth + opens - closes)
            if inside:
                continue

            heading = SECTION.match(line)
            if heading:
                close(pending, saw_body)
                pending, saw_body = None, False
                section = heading.group(1)
                continue

            task_match = TASK_HEADING.match(line)
            if task_match:
                close(pending, saw_body)
                pending = (task_match.group(1), lineno, line, section)
                saw_body = False
                continue

        if pending is None:
            continue
        if pending[3] in BLOCKED_SECTIONS:
            # The blocker lives inside the fence, so this is the one thing here
            # that reads fenced lines.
            if BLOCKER.match(line):
                saw_body = True
        elif not fenced and PRIORITY.match(line):
            saw_body = True

    close(pending, saw_body)
    return problems


# Everything git tracks at the repository root, and nothing else belongs there.
# Kept as a literal list rather than a pattern because the point is that adding
# a root-level file should be a deliberate act that edits this line.
ROOT_ALLOWED = {
    ".gitignore",
    "ATTADIPA_RENAME_PLAN.md",
    "CLAUDE.md",
    "CMakeLists.txt",
    "CONTRIBUTING.md",
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


# The checks this file runs, as data. How many there are is quoted in STATUS.md,
# TASKS.md and the CI comment, and the copy in STATUS.md said Six on the very
# commit that added the seventh -- so the number now has one source, and
# test_check_docs.py holds the quotes to it. Found in review.
CHECKS = (
    ("Broken relative links", "check_links"),
    ("Unclosed inline code spans", "check_code_spans"),
    ("Duplicate task IDs", "check_task_ids"),
    ("Duplicate owner-decision numbers", "check_decision_ids"),
    ("Tasks with no body, or finished work outside DONE", "check_task_bodies"),
    ("Unexpected files tracked at the repository root", "check_root_files"),
    (
        "Citations pointing at a blank line or past the end of a file",
        "check_citation_lines",
    ),
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
