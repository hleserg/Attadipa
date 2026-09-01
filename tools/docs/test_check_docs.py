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
        # The same href written the two other ways `check_links` accepts.
        # Both used to be joined onto the citing file's directory, which for a
        # URL cannot resolve and for `/path` resolves to the wrong place, and
        # the miss then reached the rename heuristic and reported a file that
        # had not moved.
        write(
            root,
            "docs/research/CITER.md",
            'See [`core/thing.h:2`](/core/include/thing.h) — "beta".\n',
        )
        case(
            "a root-relative href resolves from the repository root",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(
            root,
            "docs/research/CITER.md",
            'See [`core/thing.h:2`](https://example.com/core/thing.h) — "beta".\n',
        )
        case(
            "an external href is not read as a local path",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
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
        # An upstream path whose basename we happen to carry ONCE stays silent
        # too. `upstream/other/thing.h` is not our `core/include/thing.h`, and
        # a unique basename alone would have announced a rename between two
        # unrelated files in two different projects.
        write(root, "docs/research/CITER.md", "See `upstream/other/thing.h:1`.\n")
        case(
            "an upstream path with a basename unique here is not called a rename",
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

    # A FINGERPRINT IS MANDATORY into a file this repository edits, and the
    # rule is scoped by what git tracks -- so this block needs a real checkout,
    # unlike the citation cases above.
    with tempfile.TemporaryDirectory() as root:
        subprocess.run(["git", "init", "-q", root], check=True)
        write(root, "core/thing.h", "alpha\nbeta\ngamma\n")
        write(root, "docs/TARGET.md", "one\ntwo\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)

        write(root, "docs/CITER.md", "See `core/thing.h:2`.\n")
        case(
            "a source citation with no fingerprint is reported",
            "check_citation_lines",
            any(
                "with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        write(root, "docs/CITER.md", 'See `core/thing.h:2` — "beta".\n')
        case(
            "a source citation that carries one passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # The exemption #331 settled: documentation lines are stable and are
        # themselves checked, so a citation into one stays opt-in.
        write(root, "docs/CITER.md", "See `docs/TARGET.md:2`.\n")
        case(
            "a documentation citation still needs no fingerprint",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # Scoped to what we EDIT. A build directory or a vendored tree is not
        # ours to keep, and is not in CI's checkout at all.
        write(root, "build/generated.h", "alpha\nbeta\n")
        write(root, "docs/CITER.md", "See `build/generated.h:2`.\n")
        case(
            "an untracked file is not required to carry one",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # THE FORM MOST SOURCE CITATIONS IN THIS REPOSITORY ARE WRITTEN IN:
        # a short label for the reader, the real path in the href. The rule has
        # to reach the target THROUGH the href, or it is enforced on the minority
        # of citations that spell the path out and silently skipped on the rest.
        write(
            root,
            "docs/CITER.md",
            "See [`thing.h:2`](../core/thing.h).\n",
        )
        case(
            "a source cited through an href still needs a fingerprint",
            "check_citation_lines",
            any(
                "with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        write(
            root,
            "docs/CITER.md",
            'See [`thing.h:2`](../core/thing.h) \u2014 "beta".\n',
        )
        case(
            "a source cited through an href that carries one passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # A CONTINUATION -- `:2` with no path -- continues the citation before
        # it ON THE SAME LINE. Both the bind and the four ways it must NOT bind
        # are below, because binding by proximity instead is what makes this
        # rule dangerous: 132 of the 138 bare forms in these documents either
        # continue a path into a tree we do not have or follow no citation at
        # all, and a rule that reaches backwards for a path verifies our lines
        # against claims about somebody else's file.
        write(root, "docs/CITER.md", 'See `docs/TARGET.md:1` and `:9`.\n')
        case(
            "a continuation is checked against the file cited before it",
            "check_citation_lines",
            any(
                "cites docs/TARGET.md:9" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        write(root, "docs/CITER.md", 'See `docs/TARGET.md:1` and `:2`.\n')
        case(
            "a continuation naming a line that exists passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # THE 77. No citation before it on the line, so there is no file to
        # check it against and nothing to say.
        write(root, "docs/CITER.md", "See `:9` in the table above.\n")
        case(
            "a continuation with no citation before it is not guessed at",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # THE 55. The anchor is a path this repository does not contain, so it
        # resolves to nothing -- and a continuation of nothing is nothing. The
        # bind is to the anchor's RESOLVED TARGET, never to its path text.
        write(
            root,
            "docs/CITER.md",
            "InfiniTime `upstream/infinitime/clock.py:97`, and `:9999`.\n",
        )
        case(
            "a continuation of an upstream citation is not checked here",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # A second citation MOVES the anchor: `:9` continues the file it
        # follows, not the one before that.
        write(
            root,
            "docs/CITER.md",
            'Both `docs/TARGET.md:1` and `core/thing.h:1` — "alpha", so `:9`.\n',
        )
        case(
            "a second citation on the line takes over as the anchor",
            "check_citation_lines",
            any(
                "cites core/thing.h:9" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        # ...and an UNRESOLVED citation takes the anchor away rather than
        # leaving the last resolved one standing. This is the shape that makes
        # a document's own prose dangerous: a line naming a file of ours and
        # then an upstream one, where `:9` belongs to the upstream file and
        # only a cleared anchor keeps it from being read against ours.
        write(
            root,
            "docs/CITER.md",
            "Ours `docs/TARGET.md:1`, theirs "
            "`upstream/infinitime/clock.py:97` and `:9`.\n",
        )
        case(
            "an unresolved citation clears the anchor rather than passing it on",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # Backticks are what separate a citation from prose, and `:9` is far
        # too small a shape to read outside them.
        write(root, "docs/CITER.md", "See `docs/TARGET.md:1` at :9 today.\n")
        case(
            "an unbackticked :NN in prose is not a continuation",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # AND IT INHERITS #337: a continuation into a file this repository
        # edits carries its own fingerprint. The anchor's quote is about the
        # anchor's line and says nothing about this one.
        write(
            root,
            "docs/CITER.md",
            'See `core/thing.h:1` — "alpha", and `:2`.\n',
        )
        case(
            "a continuation into a source file still needs a fingerprint",
            "check_citation_lines",
            any(
                "with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        write(
            root,
            "docs/CITER.md",
            'See `core/thing.h:1` — "alpha", and `:2` — "beta".\n',
        )
        case(
            "a continuation into a source file that carries one passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # `file:line:column:` is a compiler transcript, quoted verbatim and not
        # editable. It is not this repository's citation syntax and must not be
        # asked for a promise.
        write(
            root,
            "docs/CITER.md",
            "```\ncore/thing.h:2:10: fatal error: nope\n```\n",
        )
        case(
            "a compiler diagnostic is not read as a citation",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
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
