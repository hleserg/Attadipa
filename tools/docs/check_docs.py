#!/usr/bin/env python3
"""Four checks on the documentation, each of a failure that already happened here.

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

Run: python3 tools/docs/check_docs.py [root]
Exits non-zero on the first category that has findings, after printing all of
them. Invoke through `python3`, never as `./check_docs.py` -- the working copies
this repository is edited from report core.filemode=false, so an executable bit
never reaches a commit.
"""

from __future__ import annotations

import os
import re
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


def main() -> int:
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    failed = False
    for title, problems in (
        ("Broken relative links", check_links(root)),
        ("Unclosed inline code spans", check_code_spans(root)),
        ("Duplicate task IDs", check_task_ids(root)),
        ("Tasks with no body, or finished work outside DONE", check_task_bodies(root)),
    ):
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
