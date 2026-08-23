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
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_docs  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def write(root: str, name: str, text: str) -> None:
    path = os.path.join(root, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


FAILURES: list[str] = []
RAN: list[str] = []

# EVERY CHECK MUST HAVE AT LEAST ONE CASE, and until this existed the count
# said nothing about that: `check_root_files` had zero, so six of seven checks
# were covered while three documents quoted the suite size as the evidence that
# "a checker that passes everything is worse than no checker". The number was
# load-bearing and the coverage behind it was not. Found in review.
#
# Recorded by wrapping the check functions rather than by annotating each case,
# because an annotation is a second thing to keep in step and this file exists
# because those go stale. A check nothing calls cannot hide.
CALLED: set[str] = set()


def instrument() -> None:
    for _title, function in check_docs.CHECKS:
        original = getattr(check_docs, function)

        def wrapper(root, _original=original, _name=function):
            CALLED.add(_name)
            return _original(root)

        setattr(check_docs, function, wrapper)


def checks_with_no_case() -> list[str]:
    return [
        "%s (%s) is in CHECKS and no case in this file calls it, so nothing "
        "here says it works" % (function, title)
        for title, function in check_docs.CHECKS
        if function not in CALLED
    ]


# Where the size of this suite, and the number of checks it covers, are quoted.
# Both were wrong at the same time: STATUS.md said "Six checks" on the commit
# that added the seventh, and "38 cases" stood in three documents whose own
# paragraph observed that "the three documents quoting it were all stale within
# a day of the last time cases were added". An instruction to fix them by hand
# is what had just failed, so they are read back instead. Found in review.
CLAIM_FILES = ("STATUS.md", "TASKS.md", os.path.join(".github", "workflows", "ci.yml"))
ANCHORS = ("check_docs.py", "test_check_docs.py")
WORDS = {
    "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7,
    "eight": 8, "nine": 9, "ten": 10, "eleven": 11, "twelve": 12,
}
# A CUE IS A CLAIM ABOUT THE SUITE'S SIZE, NOT ANY NUMBER NEAR THE WORD.
# `\**N\**\s+cases` matched "Four cases for that check followed" -- a true
# sentence about one round's additions, in a STATUS.md paragraph entirely about
# this checker. It passed only because that block happens not to contain the
# literal filename, so adding the filename to a paragraph about the file would
# have reddened CI on a true sentence. That is the third instance of the
# failure mode this suite has already fixed twice, so the fix is the same one:
# the size is written in a form nothing else uses -- `holds N cases`, or bold
# `**N cases**` -- and a bare count in prose is prose. Found in review.
CUES = (
    ("cases", re.compile(
        r"holds\s+\**([A-Za-z0-9]+)\**\s+(?:cases|mutation tests)\b"
        r"|\*\*([A-Za-z0-9]+)\s+(?:cases|mutation tests)\*\*"
    )),
    # The colon is required, and it is the convention rather than an accident
    # of parsing: the number of checks is claimed where the list of them is
    # introduced. Prose says "the two checks this pull request added" and means
    # something else entirely; a claim about the whole checker enumerates.
    ("checks", re.compile(r"\**([A-Za-z0-9]+)\**\s+checks:")),
)


def spelled(token: str):
    """A number written either way, or None for a word that is not one."""
    if token.isdigit():
        return int(token)
    return WORDS.get(token.lower())


def paragraphs(text: str):
    """Blocks separated by blank lines, with `#` comment markers stripped.

    One implementation for Markdown and for the workflow file: a CI comment
    block is a paragraph whose lines happen to start with `#`, and the numbers
    inside it go stale exactly like prose.
    """
    out, current, start = [], [], 1
    for lineno, line in enumerate(text.split("\n") + [""], 1):
        bare = re.sub(r"^#\s?", "", line.strip())
        if not bare:
            if current:
                out.append((start, " ".join(current)))
                current = []
            continue
        if not current:
            start = lineno
        current.append(bare)
    return out


def quoted_counts_that_disagree(cases: int, checks: int) -> list[str]:
    expected = {"cases": cases, "checks": checks}
    problems = []
    for relative in CLAIM_FILES:
        path = os.path.join(REPO, relative)
        if not os.path.isfile(path):
            problems.append(
                "%s is missing, and it is one of the files that quotes these "
                "numbers" % relative
            )
            continue
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        anchored = quoted = 0
        for lineno, block in paragraphs(text):
            if not any(name in block for name in ANCHORS):
                continue
            anchored += 1
            for kind, cue in CUES:
                for match in cue.finditer(block):
                    token = next(g for g in match.groups() if g is not None)
                    stated = spelled(token)
                    if stated is None:
                        continue
                    quoted += 1
                    if stated != expected[kind]:
                        problems.append(
                            "%s:%d states %s %s, and this tree has %d"
                            % (relative, lineno, token, kind, expected[kind])
                        )
        if anchored and not quoted:
            problems.append(
                "%s names the checker and quotes no number this reads. Either "
                "the claim went away -- say so -- or it is phrased in a way "
                "nothing checks, which is how all three went stale before."
                % relative
            )
        if not anchored:
            problems.append(
                "%s no longer mentions the checker at all; drop it from "
                "CLAIM_FILES deliberately rather than passing by default"
                % relative
            )
    return problems


def case(name: str, condition: bool) -> None:
    print(("  ok   " if condition else "  FAIL ") + name)
    RAN.append(name)
    if not condition:
        FAILURES.append(name)


def counted(text: str) -> list[str]:
    """Every size token the `cases` cue reads out of one block."""
    cue = dict(CUES)["cases"]
    return [next(g for g in m.groups() if g is not None) for m in cue.finditer(text)]


def main() -> int:
    instrument()

    # THE CUE READS A SIZE CLAIM, NOT ANY NUMBER NEAR THE WORD "cases". The
    # first version matched "Four cases for that check followed" -- a true
    # sentence about one round's additions -- and passed only because that
    # STATUS.md paragraph happens not to name the file it is entirely about.
    # Adding the filename would have reddened CI on a true sentence, which is
    # the third instance of the failure mode this suite has fixed twice.
    case(
        "a bare count in prose is prose, not a claim about the suite's size",
        counted("Four cases for that check followed.") == [],
    )
    case(
        "both forms the size is actually written in are read",
        counted("test_check_docs.py holds 63 cases; several assert") == ["63"]
        and counted("**63 cases** in `tools/docs/test_check_docs.py`") == ["63"]
        and counted("**52 mutation tests**, several of which") == ["52"],
    )
    with tempfile.TemporaryDirectory() as root:
        write(root, "docs/real.md", "# Real\n")

        write(root, "a.md", "See [it](docs/real.md) and [again](docs/real.md#real).\n")
        case("a link that resolves is not reported", not check_docs.check_links(root))

        # THE ANCHOR HALF, which check 1 captured and never looked at. Recorded
        # as T-127 and closed here, because it is what makes an OD-number
        # collision survive a merge: renumber one of two `## OD-16` headings and
        # every `#od-16` link lands at the top of the file with CI green.
        write(root, "a.md", "See [it](docs/real.md#imaginary).\n")
        problems = check_docs.check_links(root)
        case(
            "an anchor no heading answers to is reported",
            len(problems) == 1 and "#imaginary" in problems[0],
        )

        write(root, "a.md", "# Top\n\nSee [up](#top) and [nowhere](#bottom).\n")
        problems = check_docs.check_links(root)
        case(
            "a same-document anchor is resolved against this document",
            len(problems) == 1 and "#bottom" in problems[0]
            and "this document" in problems[0],
        )

        write(root, "docs/twice.md", "## Same\n\n## Same\n")
        write(root, "a.md", "[1](docs/twice.md#same) [2](docs/twice.md#same-1) [3](docs/twice.md#same-2)\n")
        problems = check_docs.check_links(root)
        case(
            "a repeated heading answers to #slug and #slug-1, not #slug-2",
            len(problems) == 1 and "#same-2" in problems[0],
        )

        # The em dash and the punctuation go; the spaces around them do not,
        # which is the whole reason `#od-16--a1-a2-and-a3` has two hyphens.
        write(root, "docs/dashed.md", "## OD-16 \u2014 A1, A2 and A3\n")
        write(root, "a.md", "[od](docs/dashed.md#od-16--a1-a2-and-a3)\n")
        case(
            "GitHub's slug rules are followed, punctuation dropped and its spaces kept",
            not check_docs.check_links(root),
        )

        write(root, "docs/fenced.md", "```\n## Not a heading\n```\n")
        write(root, "a.md", "[x](docs/fenced.md#not-a-heading)\n")
        problems = check_docs.check_links(root)
        case(
            "a heading inside a fence is not a heading, so its anchor is reported",
            len(problems) == 1 and "#not-a-heading" in problems[0],
        )

        # A non-Markdown target has no headings to check against, and GitHub
        # anchors a code file by line (`#L12`). Reporting those would be noise.
        write(root, "docs/code.py", "print(1)\n")
        write(root, "a.md", "[line](docs/code.py#L1)\n")
        case(
            "an anchor on a non-Markdown target is left alone",
            not check_docs.check_links(root),
        )

        # AN ILLUSTRATION IS NOT A LINK, and this one found itself: the record
        # closing T-127 describes the syntax as `](#same-document)`, in
        # backticks, and the new anchor check read it as a link into a heading
        # that does not exist. GitHub renders a code span as characters. Same
        # defect class as the `EXAMPLE.md` reservation, one check over.
        write(root, "a.md", "Write it `[x](docs/nope.md#imaginary)` like so.\n")
        case(
            "a link inside an inline code span is text, not a link",
            not check_docs.check_links(root),
        )

        write(root, "a.md", "A `span` then [real](docs/nope.md).\n")
        problems = check_docs.check_links(root)
        case(
            "and a real link after a code span is still read",
            len(problems) == 1 and "docs/nope.md" in problems[0],
        )
        os.remove(os.path.join(root, "docs", "twice.md"))
        os.remove(os.path.join(root, "docs", "dashed.md"))
        os.remove(os.path.join(root, "docs", "fenced.md"))
        os.remove(os.path.join(root, "docs", "code.py"))

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

        # One OD number, one decision. The real failure was four open pull
        # requests each inserting `## OD-16` at the same line for four different
        # decisions, sharing no other file, so git merged them clean.
        write(
            root,
            "docs/research/OWNER_DECISIONS.md",
            "## OD-15 — one thing\n\ntext\n\n## OD-16 — another\n\ntext\n",
        )
        case(
            "distinct decision numbers are not reported",
            not check_docs.check_decision_ids(root),
        )

        write(
            root,
            "docs/research/OWNER_DECISIONS.md",
            "## OD-16 — the display wakes on raise\n\ntext\n\n"
            "## OD-16 — which radio is fitted\n\ntext\n",
        )
        problems = check_docs.check_decision_ids(root)
        case(
            "a duplicate decision number is reported",
            len(problems) == 1 and "OD-16" in problems[0],
        )

        # The `keep both` resolution is exactly what a reader sees as two
        # headings that differ only in their prose, so the check must not be
        # fooled by the title differing.
        write(
            root,
            "docs/research/OWNER_DECISIONS.md",
            "## OD-16 — a\n\n## OD-17 — b\n\n## OD-16 — c\n\n## OD-17 — d\n",
        )
        case(
            "two separate collisions are both reported",
            len(check_docs.check_decision_ids(root)) == 2,
        )

        # A `### OD-16` sub-heading under a decision is not a second decision,
        # and neither is one inside a fenced example.
        write(
            root,
            "docs/research/OWNER_DECISIONS.md",
            "## OD-16 — a\n\n### OD-16 — a sub-heading\n\n"
            "```\n## OD-16 — inside a fence\n```\n",
        )
        case(
            "a sub-heading and a fenced example are not decisions",
            not check_docs.check_decision_ids(root),
        )

        os.remove(os.path.join(root, "docs/research/OWNER_DECISIONS.md"))
        case(
            "no register is not a finding",
            not check_docs.check_citation_lines(root)
            and not check_docs.check_decision_ids(root),
        )

        # Check 7. A branch inserted seven lines into HARDWARE_MATRIX.md, moved
        # two PMU-rail rows past the lines two other documents cited, and left
        # one citation pointing at a blank line -- inside the `BLOCKED:` block
        # about the GNSS rail, the fact CLAUDE.md holds up as the cost of
        # guessing.
        write(root, "docs/research/TARGET.md", "one\ntwo\n\nfour\n")
        write(root, "docs/research/CITER.md", "See `TARGET.md:1` for it.\n")
        case(
            "a citation landing on a real line is not reported",
            not check_docs.check_citation_lines(root),
        )

        write(root, "docs/research/CITER.md", "See `TARGET.md:3` for it.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a citation landing on a blank line is reported",
            len(problems) == 1 and "blank" in problems[0],
        )

        write(root, "docs/research/CITER.md", "See `TARGET.md:99` for it.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a citation past the end of the file is reported",
            len(problems) == 1 and "4 lines" in problems[0],
        )

        # The spelling the defect actually used: a bare SHOUTING basename with
        # no extension, which is how these documents cite a sibling.
        write(root, "docs/research/CITER.md", "See TARGET:3 and nothing else.\n")
        case(
            "a bare NAME:line citation is resolved and reported",
            len(check_docs.check_citation_lines(root)) == 1,
        )

        # A range is a finding only when the whole of it is blank; a range that
        # merely straddles a blank line is how a table is cited.
        write(root, "docs/research/CITER.md", "See `TARGET.md:1-4` for it.\n")
        case(
            "a range straddling a blank line is not reported",
            not check_docs.check_citation_lines(root),
        )

        write(root, "docs/research/TARGET.md", "one\n\n\nfour\n")
        write(root, "docs/research/CITER.md", "See `TARGET.md:2-3` for it.\n")
        case(
            "a range that is entirely blank is reported",
            len(check_docs.check_citation_lines(root)) == 1,
        )

        # Fences are deliberately NOT stripped for this check: TASKS.md keeps
        # its BLOCKED records in one, and that is where the defect lived.
        write(root, "docs/research/TARGET.md", "one\ntwo\n\nfour\n")
        write(root, "docs/research/CITER.md", "```\nBLOCKED: see TARGET.md:3\n```\n")
        case(
            "a citation inside a fenced block is still checked",
            len(check_docs.check_citation_lines(root)) == 1,
        )

        # A line number in somebody else's tree is not ours to verify.
        write(root, "docs/research/CITER.md", "Upstream `src/Utils.cpp:127-145`.\n")
        case(
            "a citation to a path outside the repository is skipped",
            not check_docs.check_citation_lines(root),
        )

        # A RELATIVE path is inside the repository, and the first version of
        # the pattern could not open with a `.`, so `../adr/0003.md:109-111`
        # was captured from the `a` of `adr/`, resolved from nowhere, and
        # skipped as somebody else's tree -- while three documents said
        # citations were checked. Found in review.
        write(root, "docs/adr/0003.md", "one\ntwo\n\nfour\n")
        write(root, "docs/research/CITER.md", "See [ADR](../adr/0003.md):3.\n")
        case(
            "a citation by relative path is resolved, not skipped as external",
            len(check_docs.check_citation_lines(root)) == 1,
        )

        # A DESCENDING range used to raise ValueError out of max([]), killing
        # the job with a traceback instead of naming the document.
        write(root, "docs/research/TARGET.md", "one\ntwo\nthree\nfour\n")
        write(root, "docs/research/CITER.md", "See `TARGET.md:4-2`.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a descending range is reported, not raised",
            len(problems) == 1 and "not a line range" in problems[0],
        )

        # And the sentence that made that rule fire on ordinary English: a
        # correct citation, a dash, and a correct number. It was read as the
        # range 843-26 and reported, so a true statement reddened CI. The
        # separator no longer takes whitespace. Found in review.
        write(
            root,
            "docs/research/CITER.md",
            "See `TARGET.md:4` - 2 lines up from the end.\n",
        )
        case(
            "a citation followed by prose and a number is not a range",
            not check_docs.check_citation_lines(root),
        )

        write(root, "docs/research/CITER.md", "See `TARGET.md:0`.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "line zero is reported, not resolved to the last line",
            len(problems) == 1 and "not a line range" in problems[0],
        )

        # The FINGERPRINT: the half a blank-line test cannot see. Two citations
        # in this repository were thirteen lines out and landed on a real,
        # wrong row -- non-blank, inside the file, invisible to every check.
        write(root, "docs/research/CITER.md", 'A row at `TARGET.md:2` "three".\n')
        problems = check_docs.check_citation_lines(root)
        case(
            "a fingerprint that has moved is reported, with where it moved to",
            len(problems) == 1 and 'which is now at :3' in problems[0],
        )

        write(root, "docs/research/CITER.md", 'A row at `TARGET.md:2` "nine".\n')
        problems = check_docs.check_citation_lines(root)
        case(
            "a fingerprint that is nowhere in the file is reported",
            len(problems) == 1 and "not anywhere in that file" in problems[0],
        )

        write(root, "docs/research/CITER.md", 'A row at `TARGET.md:3` "three".\n')
        case(
            "a fingerprint that still matches is not reported",
            not check_docs.check_citation_lines(root),
        )

        # A DOT-DIRECTORY IS A PATH. `(?:\.{1,2}/)*` admits `./` and `../` and
        # nothing else, so `.github/workflows/ci.yml:281` matched at no
        # position: the pattern could not start at the `.`, and starting at the
        # `g` is exactly what the look-behind refuses. Two citations to that
        # file were 211 lines out while three documents billed this check as
        # the answer to citation drift. Found in review.
        write(root, ".github/workflows/ci.yml", "one\ntwo\n\nfour\n")
        write(root, "docs/research/CITER.md", "See `.github/workflows/ci.yml:3`.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a citation into a dot-directory is seen, not skipped by the boundary",
            len(problems) == 1 and "ci.yml:3" in problems[0],
        )

        write(root, "docs/research/CITER.md", "See `.github/workflows/ci.yml:2`.\n")
        case(
            "and a good one in a dot-directory stays quiet",
            not check_docs.check_citation_lines(root),
        )

        # A basename with no path in front of it, cited from a document that is
        # neither beside it nor at the root. Four of these were being skipped
        # as "somebody else's tree", and one of them was wrong.
        write(root, "docs/deep/DEEP.md", "one\ntwo\n\nfour\n")
        write(root, "docs/research/CITER.md", "See `DEEP.md:3`.\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a bare basename resolves anywhere in the tree, not only beside the citer",
            len(problems) == 1 and "DEEP.md:3" in problems[0],
        )

        # AMBIGUITY IS STILL A SKIP. Two files answering to one basename cannot
        # be told apart from the citation, and guessing between them would
        # report a line number out of the wrong file.
        write(root, "docs/other/DEEP.md", "only one line\n")
        case(
            "two files with one basename are not guessed between",
            not check_docs.check_citation_lines(root),
        )
        os.remove(os.path.join(root, "docs", "other", "DEEP.md"))

        # A citation that names a DIRECTORY means that path, not a basename
        # match: `upstream/vendor/DEEP.md:3` is a fact about their tree.
        write(root, "docs/research/CITER.md", "See `upstream/vendor/DEEP.md:3`.\n")
        case(
            "a pathed citation that resolves to nothing is not rescued by basename",
            not check_docs.check_citation_lines(root),
        )
        os.remove(os.path.join(root, "docs", "deep", "DEEP.md"))
        os.remove(os.path.join(root, ".github", "workflows", "ci.yml"))

        # Through a Markdown link, which is how these documents write most
        # citations -- the citation match ends inside `](...)`, so a
        # fingerprint after the link had to be reachable or it would be a
        # promise nothing kept.
        write(
            root,
            "docs/research/CITER.md",
            'A row at [TARGET.md:2](TARGET.md) "three".\n',
        )
        case(
            "a fingerprint after a Markdown link tail is read",
            len(check_docs.check_citation_lines(root)) == 1,
        )

        # THE ESCAPE HATCH, and the reservation that keeps it one. STATUS.md
        # and TASKS.md illustrate this syntax, and an illustration written with
        # a real path is an assertion about a document neither of them is
        # about: inserting a line above the cited row reddens CI naming the two
        # files the next agent is told to read first. Found in review.
        write(
            root,
            "docs/research/CITER.md",
            'Write it as `EXAMPLE.md:357 "Display FPC"` — a live one names a '
            "real file.\n",
        )
        case(
            "an illustration written with the placeholder path asserts nothing",
            not check_docs.check_citation_lines(root),
        )

        write(root, "docs/research/EXAMPLE.md", "one\ntwo\n")
        problems = check_docs.check_citation_lines(root)
        case(
            "a real EXAMPLE.md is reported, because it would make every "
            "illustration live",
            any("reserved placeholder" in problem for problem in problems),
        )
        os.remove(os.path.join(root, "docs/research/EXAMPLE.md"))

        os.remove(os.path.join(root, "docs/adr/0003.md"))
        os.remove(os.path.join(root, "docs/research/TARGET.md"))
        os.remove(os.path.join(root, "docs/research/CITER.md"))

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

    # check_root_files needs a git checkout rather than a directory of
    # Markdown, so it gets its own fixture. It had no case at all until the
    # coverage guard above went looking.
    with tempfile.TemporaryDirectory() as root:
        case(
            "a tree that is not a git checkout is not judged",
            not check_docs.check_root_files(root),
        )
        subprocess.run(["git", "init", "-q", root], check=True)
        write(root, "README.md", "# Attadipa\n")
        write(root, "docs/research/NOTES.md", "notes\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        case(
            "an allow-listed root file and a file in a subdirectory are fine",
            not check_docs.check_root_files(root),
        )

        write(root, "vendor-datasheet.pdf.md", "swept in by git add -A\n")
        subprocess.run(["git", "add", "-A"], cwd=root, check=True)
        problems = check_docs.check_root_files(root)
        case(
            "a stray tracked at the root is reported",
            len(problems) == 1 and "vendor-datasheet.pdf.md" in problems[0],
        )

        write(root, "untracked.md", "never added\n")
        problems = check_docs.check_root_files(root)
        case(
            "an untracked stray is not reported -- the rule is about the index",
            len(problems) == 1 and "untracked.md" not in problems[0],
        )

    if FAILURES:
        print(f"\n{len(FAILURES)} failed", file=sys.stderr)
        return 1
    # The count is printed rather than left to be counted by hand. Three
    # documents quote it, and all three were stale within a day of the last
    # time cases were added -- the drift this file exists to catch, in the
    # file that catches it.
    print("\nall %d cases passed" % len(RAN))

    uncovered = checks_with_no_case()
    if uncovered:
        print()
        print("but %d check(s) have no case here:" % len(uncovered))
        for problem in uncovered:
            print("  " + problem)
        return 1

    stale = quoted_counts_that_disagree(len(RAN), len(check_docs.CHECKS))
    if stale:
        print()
        print("but %d quoted number(s) disagree with this tree:" % len(stale))
        for problem in stale:
            print("  " + problem)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
