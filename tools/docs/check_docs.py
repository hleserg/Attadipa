#!/usr/bin/env python3
"""Two checks on the documentation, both of which have already gone wrong here.

1. Relative links resolve. This repository's documents cite each other
   constantly, and a link that 404s reads exactly like one that works until
   somebody clicks it. A rename with a stale reference behind it is the normal
   way this happens.

2. Task IDs in TASKS.md are unique. A duplicate ID means two tasks answer to one
   name and an agent picking work up cannot tell which it was handed. Headings
   inside a `<details>` block are deliberately excluded: TASKS.md keeps a
   rejected task's original scope in one, and that is a record rather than a
   second live task.

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
DETAILS_OPEN = re.compile(r"<details\b", re.I)
DETAILS_CLOSE = re.compile(r"</details\s*>", re.I)

SKIP_DIRS = {".git", "build", "node_modules", "external", ".venv", "__pycache__"}
EXTERNAL = ("http://", "https://", "mailto:", "tel:", "ftp://", "//")


def strip_fences(text: str) -> list[tuple[int, str]]:
    """Lines outside fenced code blocks, as (1-based line number, text)."""
    out: list[tuple[int, str]] = []
    fenced = False
    for n, line in enumerate(text.splitlines(), 1):
        if FENCE.match(line):
            fenced = not fenced
            continue
        if not fenced:
            out.append((n, line))
    return out


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


def main() -> int:
    root = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ".")
    failed = False
    for title, problems in (
        ("Broken relative links", check_links(root)),
        ("Duplicate task IDs", check_task_ids(root)),
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
