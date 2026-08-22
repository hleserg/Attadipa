#!/usr/bin/env python3
"""Mutation tests for check_docs.py.

A checker that passes everything is worse than no checker, because it reads as
evidence. Each case below breaks exactly one thing and asserts the checker
notices — and the last two assert it does *not* fire where firing would be wrong,
which is the half that would otherwise land a job that fails on `main`.

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

    if FAILURES:
        print(f"\n{len(FAILURES)} failed", file=sys.stderr)
        return 1
    print("\nall passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
