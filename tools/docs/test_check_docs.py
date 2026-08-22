#!/usr/bin/env python3
"""Mutation tests for check_docs.py.

A checker that passes everything is worse than no checker, because it reads as
evidence. Each case below breaks exactly one thing and asserts the checker
notices — and the cases that assert it does *not* fire are the half that would
otherwise land a job failing on `main`.

Run: python3 tools/docs/test_check_docs.py
"""

from __future__ import annotations

import os
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_docs  # noqa: E402


def write(root: str, name: str, text: str) -> None:
    path = os.path.join(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


FAILURES: list[str] = []


def case(name: str, condition: bool) -> None:
    print(("  ok   " if condition else "  FAIL ") + name)
    if not condition:
        FAILURES.append(name)


def main() -> int:
    with tempfile.TemporaryDirectory() as root:
        write(root, "docs/real.md", "# Real\n")

        write(root, "a.md", "See [it](docs/real.md) and [again](docs/real.md#anchor).\n")
        case("a link that resolves is not reported", not check_docs.check_links(root))

        write(root, "a.md", "See [it](docs/gone.md).\n")
        problems = check_docs.check_links(root)
        case("a broken link is reported", len(problems) == 1 and "docs/gone.md" in problems[0])

        write(root, "a.md", "[up](https://example.invalid/x.md) [mail](mailto:a@b.c)\n")
        case("external links are not resolved", not check_docs.check_links(root))

        write(root, "a.md", "Prose.\n\n```\n[fake](docs/gone.md)\n```\n")
        case("links inside a fence are ignored", not check_docs.check_links(root))

        write(root, "a.md", "[root](/docs/real.md)\n")
        case("a root-relative link resolves against the repo root", not check_docs.check_links(root))

        write(root, "a.md", "A `span` on one line, and `another` one.\n")
        case("a balanced span is not reported", not check_docs.check_code_spans(root))

        # The per-line version of this check produced 61 false positives on this
        # repository, all of them this shape. CommonMark lets a span wrap a soft
        # line break and the prose here does it constantly.
        write(root, "a.md", "A span that wraps `the line\nbreak` and closes.\n")
        case("a span across a soft break is not reported", not check_docs.check_code_spans(root))

        write(root, "a.md", "Prose with `an unclosed span.\n\nA new paragraph.\n")
        problems = check_docs.check_code_spans(root)
        case("an unclosed span is reported", len(problems) == 1 and ":1:" in problems[0])

        # The real defect this check was added for: a splice landed inside
        # `` `## DONE` ``, leaving one backtick behind and re-parenting a task's
        # entire field list onto the next heading. Every heading stayed unique,
        # so the uniqueness check passed.
        write(root, "a.md", "### T-100 \u00b7 One\n- the tests in `## DONE\n\n### T-102 \u00b7 Two\n- **Priority:** P1\n")
        case(
            "the splice defect is caught",
            len(check_docs.check_code_spans(root)) == 1
            and not check_docs.check_task_ids(root),
        )

        write(root, "a.md", "Prose.\n\n```\nlone ` backtick in a fence\n```\n")
        case("backticks inside a fence are ignored", not check_docs.check_code_spans(root))

        write(root, "a.md", "A ``span with a ` inside`` it.\n")
        case("a double-backtick span is counted by runs", not check_docs.check_code_spans(root))

        os.remove(os.path.join(root, "a.md"))

        write(root, "TASKS.md", "### T-001 · One\n\n### T-002 · Two\n")
        case("distinct task IDs are not reported", not check_docs.check_task_ids(root))

        write(root, "TASKS.md", "### T-001 · One\n\n### T-001 · One again\n")
        problems = check_docs.check_task_ids(root)
        case("a duplicate task ID is reported", len(problems) == 1 and "T-001" in problems[0])

        write(
            root,
            "TASKS.md",
            "### T-001 · One\n\n<details>\n<summary>Original scope</summary>\n\n"
            "### T-001 · the scope it was filed with\n\n</details>\n",
        )
        case(
            "a heading inside <details> is not a duplicate",
            not check_docs.check_task_ids(root),
        )

        write(
            root,
            "TASKS.md",
            "### T-001 · One\n\n<details>\n\n### T-002 · inside\n\n</details>\n\n"
            "### T-001 · after the block closes\n",
        )
        problems = check_docs.check_task_ids(root)
        case(
            "the <details> exclusion ends with the block",
            len(problems) == 1 and "T-001" in problems[0],
        )

        write(root, "TASKS.md", "### T-001a · One\n\n### T-001a · Again\n")
        case(
            "a lettered task ID is matched",
            len(check_docs.check_task_ids(root)) == 1,
        )

        write(root, "TASKS.md", "## NEXT\n\n### T-001 · One\n- **Priority:** P1.\n- **Goal:** a thing.\n")
        case(
            "a live task with a field list is not reported",
            not check_docs.check_task_bodies(root),
        )

        write(root, "TASKS.md", "## NEXT\n\n### T-001 · One\n\n### T-002 · Two\n- **Priority:** P1.\n")
        problems = check_docs.check_task_bodies(root)
        case(
            "a live task with no field list is reported",
            len(problems) == 1 and "T-001" in problems[0] and "Priority" in problems[0],
        )

        write(
            root,
            "TASKS.md",
            "## NEXT\n\n### T-001 · One — **DONE** 2026-08-22\n- What came of it.\n",
        )
        problems = check_docs.check_task_bodies(root)
        case(
            "finished work outside ## DONE is reported",
            len(problems) == 1 and "T-001" in problems[0] and "## NEXT" in problems[0],
        )

        write(
            root,
            "TASKS.md",
            "## DONE\n\n### T-001 · One — **DONE** 2026-08-22\n- What came of it.\n",
        )
        case(
            "a record under ## DONE needs no field list",
            not check_docs.check_task_bodies(root),
        )

        write(
            root,
            "TASKS.md",
            "## BLOCKED\n\n### T-010 · Bring-up\n```\nBLOCKED:\nReason:  No board.\n```\n",
        )
        case(
            "a blocked task is bodied by its blocker, not a priority",
            not check_docs.check_task_bodies(root),
        )

        write(root, "TASKS.md", "## BLOCKED\n\n### T-010 · Bring-up\n- Some prose.\n")
        problems = check_docs.check_task_bodies(root)
        case(
            "a blocked task with no blocker is reported, and the message says so",
            len(problems) == 1 and "BLOCKED: block" in problems[0],
        )

        write(root, "TASKS.md", "## NEXT\n\n### T-001 · One\n```\n- **Priority:** P1.\n```\n")
        problems = check_docs.check_task_bodies(root)
        case(
            "a priority inside a fence is an example, not a body",
            len(problems) == 1 and "T-001" in problems[0],
        )

        # The defect this pull request shipped, seen from the other side: the
        # span check catches the cause, this catches the effect.
        write(
            root,
            "TASKS.md",
            "## NOW\n\n### T-100 · One\n- **Priority:** P1, and see `some\n"
            "### T-102 · Two — **DONE** 2026-08-22\n- **Goal:** a thing.\n",
        )
        problems = check_docs.check_task_bodies(root)
        case(
            "a spliced heading is caught from at least one side",
            len(problems) == 1 and "T-102" in problems[0],
        )

        # Drift with no splice behind it: a record left in a live section, which
        # is what this check actually found in TASKS.md and no syntactic check
        # would ever see.
        write(
            root,
            "TASKS.md",
            "## READY\n\n### T-084 · Research — **DONE** 2026-08-22\n- What came of it.\n"
            "\n## DONE\n\n### T-102 · Other — **DONE** 2026-08-22\n- And of this.\n",
        )
        problems = check_docs.check_task_bodies(root)
        case(
            "a record left in a live section is reported, the one under DONE is not",
            len(problems) == 1 and "T-084" in problems[0],
        )

    if FAILURES:
        print(f"\n{len(FAILURES)} failed", file=sys.stderr)
        return 1
    print("\nall passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
