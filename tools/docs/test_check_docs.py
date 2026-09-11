#!/usr/bin/env python3
"""Small mutation suite for the documentation checker."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
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
        for name in sorted(check_docs.ROOT_REQUIRED - {"SECURITY.md"}):
            write(root, name, "# Attadipa\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        # Named rather than covered by the generic cases around it, and checked
        # from both sides, because this file is load-bearing outside the
        # repository and silent inside it. Dropping it from the allow-list
        # turns a required job red on every open pull request until somebody
        # notices -- that is what #524 was. Moving the file itself under
        # `docs/` turns nothing red at all: GitHub's "Report a vulnerability"
        # link resolves this path and no other, and simply stops working.
        case(
            "the security policy missing from the root is reported",
            "check_root_files",
            any(
                "SECURITY.md" in problem
                for problem in check_docs.check_root_files(root)
            ),
        )
        write(root, "SECURITY.md", "# Security policy\n")
        subprocess.run(["git", "add", "SECURITY.md"], cwd=root, check=True)
        case(
            "the allowed root files, security policy included, pass",
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
        # #331 exempted documentation on the reasoning that its lines are
        # stable and themselves checked. #386 found the check they face is
        # "exists and is not blank", which a table rule passes, and that a
        # majority of the tree's `.md` citations had already drifted onto one.
        # So the rule is the same for every file this repository edits, and
        # the report names the file so the author knows which rule fired.
        write(root, "docs/CITER.md", "See `docs/TARGET.md:2`.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a documentation citation with no fingerprint is reported",
            "check_citation_lines",
            any("with no fingerprint" in problem and "docs/TARGET.md" in problem
                for problem in problems),
        )
        write(root, "docs/CITER.md", 'See `docs/TARGET.md:2` — "two".\n')
        case(
            "a documentation citation that carries one passes",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # `docs/upstream/` is not exempt. #399 first kept it opt-in as
        # "somebody else's text, copied", and review showed the premise false:
        # both files there are written and edited here, one gaining 54 lines
        # in a single commit, and this repository cites into them. Tracked is
        # the rule, and the directory name buys nothing.
        write(root, "docs/upstream/THEIRS.md", "their one\ntheir two\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        write(root, "docs/CITER.md", "See `docs/upstream/THEIRS.md:2`.\n")
        case(
            "a tracked upstream document is required to carry one like any other",
            "check_citation_lines",
            any("with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        write(root, "docs/CITER.md", 'See `docs/upstream/THEIRS.md:2` — "their one".\n')
        case(
            "a fingerprint into an upstream document is checked",
            "check_citation_lines",
            any("which is now at :1" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        # A bare `:NN` with no citation on its own line continues nothing
        # (#336), so there is nothing to demand a fingerprint for: it stays
        # silent under the mandatory rule exactly as it did before it.
        write(root, "docs/CITER.md",
              'See `docs/TARGET.md:1` — "one".\nAnd `:2` as well.\n')
        case(
            "a bare continuation on its own line is still checked by nothing",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # ... while one on the SAME line continues a tracked citation and is
        # held to the same rule as its anchor.
        write(root, "docs/CITER.md",
              'See `docs/TARGET.md:1` — "one" and `:2`.\n')
        case(
            "a continuation on the anchor's line needs its own fingerprint",
            "check_citation_lines",
            any("with no fingerprint" in problem and ":2" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        # THE DECORATION A FINGERPRINT MAY WEAR. Three live citations were
        # rejected for punctuation alone, and the rejection read as "no
        # fingerprint given" -- silence about a quote the author can see on
        # the line. The class has no letters in it, so widening it cannot let
        # a quotation further down the sentence be read as this citation's
        # fingerprint; the guard case below is what holds that.
        write(root, "docs/CITER.md", 'See `core/thing.h:2`: *"beta"*.\n')
        case(
            "an italic fingerprint after a colon is one",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(root, "docs/CITER.md", 'See `core/thing.h:2`, "beta".\n')
        case(
            "a fingerprint after a comma is one",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # THE GUARD. Prose between the citation and the quote means the author
        # wrote a sentence, not a fingerprint, and the widening above must not
        # reach across it.
        write(
            root,
            "docs/CITER.md",
            'See `core/thing.h:2` which the report calls "beta".\n',
        )
        case(
            "a quote separated by prose is not a fingerprint",
            "check_citation_lines",
            any(
                "with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        # THE SAME DECORATION WHEN THE QUOTE WRAPS. Reflowed prose puts the
        # quote at the head of the next line; the line before it may end in
        # the same colon or comma the same-line rule accepts, or the two paths
        # disagree and the wrapped one says "no fingerprint" about a quote one
        # line down. Found in review of #399.
        write(root, "docs/CITER.md", 'See `core/thing.h:2`:\n"beta" and so on.\n')
        case(
            "a wrapped fingerprint after a colon is one",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # A LENGTH LIMIT THAT SAYS SO. The cap used to live inside the match,
        # so a quote one character too long fell out of it and was reported as
        # no quote at all. Two citations in this repository were in that state
        # and the check said nothing a reader could act on.
        long_quote = "x" * (check_docs.FINGERPRINT_MAX + 5)
        write(root, "core/long.h", "alpha\n%s\n" % long_quote)
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        write(
            root,
            "docs/CITER.md",
            'See `core/long.h:2` — "%s".\n' % long_quote,
        )
        problems = check_docs.check_citation_lines(root)
        case(
            "an over-long fingerprint is named as over-long, not as missing",
            "check_citation_lines",
            any("%d-character fingerprint" % len(long_quote) in problem
                for problem in problems)
            and not any("with no fingerprint" in problem for problem in problems),
        )
        # ... and the limit is not gated on the file being one this repository
        # edits: a target that needs no quote is still held to the one it
        # carries. Found in review of #399, where the only length case cited a
        # source file and `if tracked and len(...)` survived the suite. The
        # untracked file is the one target left that needs no quote.
        write(root, "docs/LONG.md", "alpha\n%s\n" % long_quote)
        write(root, "docs/CITER.md",
              'See `docs/LONG.md:2` — "%s".\n' % long_quote)
        case(
            "an over-long fingerprint into an untracked file is still over-long",
            "check_citation_lines",
            any("%d-character fingerprint" % len(long_quote) in problem
                for problem in check_docs.check_citation_lines(root)),
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
        # THE BACKTICK CLOSING BEFORE THE COLON: `` `docs/TARGET.md`:9 `` and
        # `` `TARGET`:9 ``. Ninety-eight citations in one document were written
        # that way, and to a grammar that admits only a paren there every one
        # was prose -- not asked for a fingerprint, and not seen pointing at a
        # blank line while it held a retrofit blocker open. Found in review of
        # #399, the third review to find a spelling the grammar did not admit.
        write(root, "docs/CITER.md", "See `docs/TARGET.md`:9.\n")
        case(
            "a backtick closing before the colon is still a citation",
            "check_citation_lines",
            any(
                "cites docs/TARGET.md:9" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
        )
        write(root, "docs/CITER.md", "See `TARGET`:9.\n")
        case(
            "a bare name with the backtick before the colon is one too",
            "check_citation_lines",
            any(
                "cites TARGET:9" in problem
                for problem in check_docs.check_citation_lines(root)
            ),
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
        write(root, "docs/CITER.md",
              'See `docs/TARGET.md:1` — "one" and `:2` — "two".\n')
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
            'Ours `docs/TARGET.md:1` — "one", theirs '
            "`upstream/infinitime/clock.py:97` and `:9`.\n",
        )
        case(
            "an unresolved citation clears the anchor rather than passing it on",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # Backticks are what separate a citation from prose, and `:9` is far
        # too small a shape to read outside them.
        write(root, "docs/CITER.md", 'See `docs/TARGET.md:1` — "one" at :9 today.\n')
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

    # Exercise the shipping CLI with the real bundled bytes, then mutate each
    # copy/source. Loading an image in the browser cannot detect this drift.
    repository = Path(__file__).resolve().parents[2]
    bundled = (
        "docs/ui/prototype/NunitoSans.ttf",
        "docs/ui/prototype/OFL.txt",
        "docs/ui/prototype/clock_meadow_night_410x502.png",
    )
    sources = (
        "tools/font/generate_ui_fonts.py",
        "assets/fonts/OFL.txt",
        "ui/assets/source/backgrounds/clock_meadow_night_410x502.png",
    )
    with tempfile.TemporaryDirectory() as root:
        for name in bundled + sources:
            target = Path(root, name)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repository / name, target)

        def run_checker():
            return subprocess.run(
                [sys.executable, check_docs.__file__, root],
                capture_output=True, text=True, check=False,
            )

        case("bundled assets pass the shipping CLI", "check_prototype_assets",
             run_checker().returncode == 0)
        for name in bundled + sources:
            target = Path(root, name)
            original = target.read_bytes()
            changed = (re.sub(rb'TTF_SHA256 = "[0-9a-f]{64}"',
                              b'TTF_SHA256 = "' + b"0" * 64 + b'"', original)
                       if name == sources[0] else original + b"drift")
            target.write_bytes(changed)
            result = run_checker()
            case(f"CLI rejects drift in {name}", "check_prototype_assets",
                 result.returncode == 1 and name in result.stderr)
            target.unlink()
            result = run_checker()
            case(f"CLI rejects missing {name}", "check_prototype_assets",
                 result.returncode == 1 and name in result.stderr)
            target.write_bytes(original)

    # SOURCE COMMENTS GO THROUGH THE SAME LOOP AND THE SAME RULES -- #462.
    # A citation in a `//` comment rots the way one in a paragraph does, and
    # rots sooner, because source moves more than prose. The tracked half of
    # the rule is what needs a real checkout: `git ls-files` is empty outside
    # one, so a suite run there greens every mandatory-fingerprint case
    # without testing anything.
    with tempfile.TemporaryDirectory() as root:
        subprocess.run(["git", "init", "-q", root], check=True)
        write(root, "core/thing.h", "alpha\nbeta\ngamma\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)

        write(root, "src/citer.cpp",
              '// See `core/thing.h:2` -- "gamma".\nint main() { return 0; }\n')
        case(
            "a stale fingerprint in a `//` comment is reported",
            "check_citation_lines",
            any("src/citer.cpp" in problem and "which is now at :3" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        write(root, "src/citer.cpp",
              "// See `core/thing.h:2`.\nint main() { return 0; }\n")
        case(
            "a `//` comment citing a tracked file must carry a fingerprint",
            "check_citation_lines",
            any("src/citer.cpp" in problem and "with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        # A CITATION IN A STRING LITERAL IS NOT A CITATION, and does not become
        # one by looking like prose. `comment_lines` empties every other line
        # rather than dropping it -- so a reported line number still opens in
        # the file, and code is excluded by construction rather than by a
        # pattern. A fixture that builds a fake citation to test this very
        # checker is the case that needs it.
        write(root, "src/citer.cpp",
              'const char *fixture = "See `core/thing.h:2`.";\n')
        case(
            "a citation inside a string literal is not checked",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # A COMMENT REFLOWS AT 80 COLUMNS, and a fingerprint long enough to be
        # a handle rarely fits after the path: the quote OPENS on the citation
        # line and CLOSES on the next. Read as two lines such a citation
        # carries an unterminated quote, which is no fingerprint at all -- and
        # the check would then demand the quote already in front of the author.
        write(root, "core/thing.h", "alpha\nbeta gamma delta\ngamma\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        write(root, "src/citer.cpp",
              '// See `core/thing.h:2` -- "beta\n// gamma delta".\n')
        case(
            "a fingerprint wrapped onto the next comment line is read whole",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(root, "src/citer.cpp",
              '// See `core/thing.h:3` -- "beta\n// gamma delta".\n')
        case(
            "a wrapped fingerprint is still checked against its own line",
            "check_citation_lines",
            any("which is now at :2" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        # A LINE COMMENT OPENS ANYWHERE ON ITS LINE. `startswith` was the whole
        # test, so a trailing comment was emptied along with the code in front
        # of it and the mandatory-fingerprint rule never reached one. None
        # existed in the tree when that was found, which is exactly when it is
        # cheap to fix and the moment nothing is asserting it either way.
        write(root, "src/citer.cpp",
              'int x = 0;  // See `core/thing.h:3` -- "beta gamma delta".\n')
        case(
            "a trailing `//` comment is read",
            "check_citation_lines",
            any("which is now at :2" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        # ...and a BLOCK continuation does not. `*` is a marker only at the
        # start of a line; mid-line it is a dereference or a multiplication,
        # and reading the rest of such a line as prose is how code becomes
        # comment text.
        write(root, "src/citer.cpp",
              'int y = a * b; /* and `core/thing.h:3` */\n')
        case(
            "a `/* ... */` body on one line is read from the opener",
            "check_citation_lines",
            any("with no fingerprint" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        write(root, "src/citer.cpp",
              'int y = a * `core/thing.h:3`;\n')
        case(
            "a `*` that does not open the line is not a comment marker",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        # The `#` half had no case at all: every source-comment case above is
        # `//` in a `.cpp`, so Python, shell, YAML and CMake were widened by a
        # table entry that nothing exercised.
        for name in ("tools/citer.py", "tools/citer.sh", ".github/workflows/citer.yml"):
            write(root, name, 'run  # See `core/thing.h:3` -- "beta gamma delta".\n')
            case(
                "a `#` comment is read in %s" % name.rsplit(".", 1)[1],
                "check_citation_lines",
                any(name in problem and "which is now at :2" in problem
                    for problem in check_docs.check_citation_lines(root)),
            )
            write(root, name, "")
        # CMAKE IS SELECTED BY NAME. `.cmake` admits a suffix no file in this
        # repository has -- all seventeen are `CMakeLists.txt` -- so the entry
        # added for CMake selected none of them, and one of the fifteen the
        # supporting grep missed was carrying a stale citation.
        write(root, "gnss/CMakeLists.txt",
              '# See `core/thing.h:3` -- "beta gamma delta".\n')
        case(
            "CMakeLists.txt is walked, though nothing here ends in .cmake",
            "check_citation_lines",
            any("gnss/CMakeLists.txt" in problem and "which is now at :2" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        write(root, "gnss/CMakeLists.txt", "")
        # A PYTHON DOCSTRING IS A COMMENT THAT HAPPENS TO BE A STRING, and this
        # repository writes its `tools/` prose in one. Keeping only `#` lines
        # left a real citation in `tools/flash/selftest.py` five lines out of
        # date and reported the tree green.
        write(root, "tools/citer.py",
              'def f():\n    """See `core/thing.h:3` -- "beta gamma delta".\n    """\n')
        case(
            "a citation in a Python docstring is checked",
            "check_citation_lines",
            any("which is now at :2" in problem
                for problem in check_docs.check_citation_lines(root)),
        )
        # ...but only where a docstring actually opens. A triple quote inside
        # an expression would otherwise swallow every line after it as prose.
        write(root, "tools/citer.py",
              'sep = \'\'\'x\'\'\'\nfixture = "See `core/thing.h:3`."\n')
        case(
            "a triple quote that does not open the line opens no docstring",
            "check_citation_lines",
            not check_docs.check_citation_lines(root),
        )
        write(root, "tools/citer.py", "")

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
