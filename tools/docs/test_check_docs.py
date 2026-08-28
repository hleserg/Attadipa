#!/usr/bin/env python3
"""Small mutation suite for the documentation checker."""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_docs  # noqa: E402


def write(root: str, name: str, text: str) -> None:
    path = os.path.join(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def main() -> int:
    failures: list[str] = []
    called: set[str] = set()
    ran = 0

    def case(name: str, function: str, condition: bool) -> None:
        nonlocal ran
        ran += 1
        called.add(function)
        print(("  ok   " if condition else "  FAIL ") + name)
        if not condition:
            failures.append(name)

    with tempfile.TemporaryDirectory() as root:
        write(root, "docs/real.md", "# Real\n")
        write(root, "a.md", "See [it](docs/real.md).\n")
        case("a relative link resolves", "check_links", not check_docs.check_links(root))
        write(root, "a.md", "See [it](docs/missing.md).\n")
        case("a broken relative link is reported", "check_links", bool(check_docs.check_links(root)))

        write(root, "a.md", "A `closed` span.\n")
        case("a closed code span passes", "check_code_spans", not check_docs.check_code_spans(root))
        write(root, "a.md", "An `open span.\n\nNext paragraph.\n")
        case("an unclosed code span is reported", "check_code_spans", bool(check_docs.check_code_spans(root)))
        os.remove(os.path.join(root, "a.md"))

        write(
            root,
            "docs/research/OWNER_DECISIONS.md",
            "## OD-1 — first\n\n## OD-2 — second\n",
        )
        case(
            "distinct owner decisions pass",
            "check_decision_ids",
            not check_docs.check_decision_ids(root),
        )
        write(
            root,
            "docs/research/OWNER_DECISIONS.md",
            "## OD-1 — first\n\n## OD-1 — collision\n",
        )
        case(
            "a duplicate owner decision is reported",
            "check_decision_ids",
            bool(check_docs.check_decision_ids(root)),
        )

        write(
            root,
            "docs/research/OPEN_QUESTIONS.md",
            "| D1 | first | UNKNOWN |\n| D2 | second | UNKNOWN |\n",
        )
        case(
            "distinct open questions pass",
            "check_question_ids",
            not check_docs.check_question_ids(root),
        )
        write(
            root,
            "docs/research/OPEN_QUESTIONS.md",
            "| D1 | first | UNKNOWN |\n| D1 | collision | UNKNOWN |\n",
        )
        case(
            "a duplicate open question is reported",
            "check_question_ids",
            bool(check_docs.check_question_ids(root)),
        )

        write(root, "docs/research/TARGET.md", "one\ntwo\n\nfour\n")
        write(root, "docs/research/CITER.md", "See `TARGET.md:2`.\n")
        case(
            "a citation to content passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(root, "docs/research/CITER.md", "See `TARGET.md:3`.\n")
        case(
            "a citation to a blank line is reported",
            "check_citation_lines",
            bool(check_docs.check_citation_lines(root)),
        )

        # A fingerprint that WRAPPED onto the next line. This is the shape that
        # hid a real drift: WAVESHARE_ARRIVAL cited HARDWARE_MATRIX for
        # "8 MB **octal**" with the quote on the following line, the citation
        # drifted ten lines, and the checker read end-of-line as "no
        # fingerprint given" and said nothing.
        write(root, "docs/research/CITER.md", 'See [`TARGET.md:2`](TARGET.md)\n"two" is the claim.\n')
        case(
            "a wrapped fingerprint that still holds passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(root, "docs/research/CITER.md", 'See [`TARGET.md:1`](TARGET.md)\n"two" is the claim.\n')
        problems = check_docs.check_citation_lines(root)
        case(
            "a wrapped fingerprint that drifted is reported, and says where the text went",
            "check_citation_lines",
            any("which is now at :2" in problem for problem in problems),
        )
        # Prose after the citation is prose, not a fingerprint: a quotation
        # further down it belongs to the sentence, not to this citation.
        write(root, "docs/research/CITER.md", 'See [`TARGET.md:1`](TARGET.md) and note\n"two" elsewhere.\n')
        case(
            "a quote below unrelated prose is not read as a fingerprint",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )

        # A SHORT LABEL resolved through its own href. `core/thing.h` is not a
        # path this tree has; the link the reader clicks is. Before the href
        # was consulted a fingerprint here ran against nothing at all and was
        # green while asserting nothing.
        write(root, "core/include/thing.h", "alpha\nbeta\n")
        write(
            root,
            "docs/research/CITER.md",
            'See [`core/thing.h:2`](../../core/include/thing.h) — "beta".\n',
        )
        case(
            "a short label resolved through its href passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(
            root,
            "docs/research/CITER.md",
            'See [`core/thing.h:1`](../../core/include/thing.h) — "beta".\n',
        )
        problems = check_docs.check_citation_lines(root)
        case(
            "a short label with a drifted fingerprint is reported",
            "check_citation_lines",
            any("which is now at :2" in problem for problem in problems),
        )
        # No href, so nothing proves which line to read -- but the basename is
        # unique in this tree, which proves the FILE is ours and moved.
        write(root, "docs/research/CITER.md", "See `core/thing.h:1`.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a path this repository moved is reported as a rename",
            "check_citation_lines",
            any("no longer has at that path" in problem for problem in problems),
        )
        # An upstream path stays silent: the basename is not ours, so whether
        # the citation is right is not decidable from here.
        write(root, "docs/research/CITER.md", "See `upstream/other/absent.h:1`.\n")
        case(
            "an upstream path this tree does not carry stays silent",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        os.remove(os.path.join(root, "core/include/thing.h"))
        write(root, "docs/research/CITER.md", "See `TARGET.md:2`.\n")

    with tempfile.TemporaryDirectory() as root:
        subprocess.run(["git", "init", "-q", root], check=True)
        write(root, "README.md", "# Attadipa\n")
        subprocess.run(["git", "add", "README.md"], cwd=root, check=True)
        case(
            "an allowed root file passes",
            "check_root_files",
            not check_docs.check_root_files(root),
        )
        write(root, "vendor-datasheet.pdf.md", "accidental root file\n")
        subprocess.run(["git", "add", "vendor-datasheet.pdf.md"], cwd=root, check=True)
        case(
            "an unexpected tracked root file is reported",
            "check_root_files",
            bool(check_docs.check_root_files(root)),
        )

    missing = {function for _title, function in check_docs.CHECKS} - called
    if missing:
        failures.append("checks without a mutation case: " + ", ".join(sorted(missing)))

    if failures:
        print("\n%d failure(s):" % len(failures), file=sys.stderr)
        for failure in failures:
            print("  - " + failure, file=sys.stderr)
        return 1
    print("\nall %d cases passed" % ran)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
