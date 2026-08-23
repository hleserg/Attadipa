"""The output-integrity contract every generated tree in this repository shares.

Two trees are committed rather than built: `assets/fonts/generated/` and
`ui/assets/generated/`. Both are shipping firmware assets, and both used to be
guarded by a stamp that recorded only what they were *made from*. That is a
promise, not a binding: it says the outputs were once produced from these
inputs, and it says nothing about the bytes actually sitting in the tree. A
hand-edited bitmap, a corrupted glyph descriptor or a truncated header passed
that check green, because none of them touches an input.

So a stamp here records both halves, and a check verifies both:

    # <why this tree exists, written by its generator>
    inputs  <sha256 of everything that determines the output>
    output  <sha256 of the file>  <name>
    output  <sha256 of the file>  <name>
    ...

What that buys, case by case:

* an edited output byte fails, because its hash no longer matches;
* a deleted output fails, because the stamp still lists it;
* a *new* output nobody stamped fails, because the generator's list and the
  stamp's list must agree in both directions — otherwise deleting an asset and
  leaving a stale stamp would read as success;
* a changed input still fails, exactly as before;
* a corrupted or hand-edited stamp fails as a *malformed stamp* rather than as a
  content mismatch, because the two need different fixes and a check that
  cannot tell them apart sends people to the wrong one.

**Only a generator writes a stamp.** There is deliberately no command line here
and no "re-stamp what is on disk" mode: a tool that blesses whatever bytes it
finds would reintroduce the hole this module exists to close, and it would do it
while looking like maintenance. The one way to update a stamp is to regenerate
the tree, which needs the real converter and the real inputs.

The write is atomic — a temporary file in the same directory, then `os.replace`.
An interrupted generator must not be able to leave a stamp that binds half a
tree, because half a binding is indistinguishable from a passing check for the
files it still covers.
"""

from __future__ import annotations

import hashlib
import os
import re
import tempfile
from pathlib import Path
from typing import Iterable, Sequence

# Lower-case hex, because that is what hashlib produces. An upper-case digest in
# a stamp means somebody typed it, and a typed digest is a guess.
DIGEST = re.compile(r"\A[0-9a-f]{64}\Z")

READ_SIZE = 1024 * 1024


def sha256_of(path: Path) -> str:
    """The hash of a file's bytes, read in chunks.

    Chunked because the largest generated font here is already 195 kB and the
    only reason this stays small is that nobody has asked for a 40 px size yet.
    """
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(READ_SIZE)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def render(explanation: str, inputs_digest: str, outputs: Sequence[Path]) -> str:
    """The stamp's exact text, so a caller can compare without writing a file."""
    if not DIGEST.match(inputs_digest):
        raise ValueError(f"not a sha256 digest: {inputs_digest!r}")
    lines = [f"# {line}".rstrip() for line in explanation.strip().splitlines()]
    lines.append(f"inputs  {inputs_digest}")
    for path in outputs:
        lines.append(f"output  {sha256_of(path)}  {path.name}")
    return "\n".join(lines) + "\n"


def write(stamp_path: Path, explanation: str, inputs_digest: str,
          outputs: Sequence[Path]) -> None:
    """Bind a tree to its inputs and its own bytes, in one replace."""
    text = render(explanation, inputs_digest, outputs)
    handle = tempfile.NamedTemporaryFile(
        "w", encoding="utf-8", newline="\n", delete=False,
        dir=str(stamp_path.parent), prefix=stamp_path.name + ".", suffix=".tmp")
    try:
        with handle:
            handle.write(text)
        # tempfile creates at 0600, and os.replace keeps the source's mode, so
        # without this the stamp lands more restrictive than the generated files
        # beside it. Git records 100644 either way; this is about the working
        # copy not growing an odd file nobody chose.
        os.chmod(handle.name, 0o644)
        os.replace(handle.name, stamp_path)
    except BaseException:
        Path(handle.name).unlink(missing_ok=True)
        raise


class Malformed(Exception):
    """The stamp itself is not a stamp. Different fault, different fix."""


def parse(text: str) -> tuple[str, dict[str, str]]:
    """(inputs digest, {name: digest}) — or `Malformed` saying which line.

    Strict on purpose. A stamp is machine-written and machine-read, so anything
    that is not exactly the format is a corruption rather than a dialect, and
    guessing at it would mean a mangled stamp could still pass.
    """
    inputs_digest = None
    outputs: dict[str, str] = {}
    for number, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        fields = stripped.split()
        if fields[0] == "inputs":
            if len(fields) != 2 or not DIGEST.match(fields[1]):
                raise Malformed(f"line {number}: 'inputs' needs one sha256, got {stripped!r}")
            if inputs_digest is not None:
                raise Malformed(f"line {number}: a second 'inputs' line")
            inputs_digest = fields[1]
        elif fields[0] == "output":
            if len(fields) != 3 or not DIGEST.match(fields[1]):
                raise Malformed(
                    f"line {number}: 'output' needs a sha256 and a name, got {stripped!r}")
            if fields[2] in outputs:
                raise Malformed(f"line {number}: {fields[2]} is listed twice")
            outputs[fields[2]] = fields[1]
        else:
            raise Malformed(f"line {number}: {fields[0]!r} is not 'inputs' or 'output'")
    if inputs_digest is None:
        raise Malformed("no 'inputs' line — this file records nothing about the inputs")
    if not outputs:
        raise Malformed("no 'output' lines — this file binds no bytes at all")
    return inputs_digest, outputs


def verify(stamp_path: Path, inputs_digest: str, outputs: Iterable[Path]) -> list[str]:
    """Everything wrong with a generated tree, in the order a reader wants it.

    A list rather than the first fault, because "four fonts differ" and "one
    font differs" are different situations and a check that stops at the first
    one makes them look the same.
    """
    expected = list(outputs)
    if not stamp_path.exists():
        return [f"{stamp_path.name} is missing — this tree has never been generated, "
                f"or the stamp was deleted"]

    try:
        recorded_inputs, recorded_outputs = parse(
            stamp_path.read_text(encoding="utf-8"))
    except Malformed as exc:
        return [f"{stamp_path.name} is not a valid stamp — {exc}"]
    except UnicodeDecodeError as exc:
        return [f"{stamp_path.name} is not a valid stamp — it is not UTF-8 text ({exc})"]

    problems = []
    if recorded_inputs != inputs_digest:
        problems.append(
            "the inputs changed since this tree was generated\n"
            f"    recorded {recorded_inputs}\n"
            f"    current  {inputs_digest}")

    by_name = {path.name: path for path in expected}
    for name in sorted(set(recorded_outputs) - set(by_name)):
        problems.append(
            f"the stamp records {name}, which this generator does not produce — "
            f"an asset was removed or renamed and the stamp was not rewritten")
    for name in sorted(set(by_name) - set(recorded_outputs)):
        problems.append(
            f"{name} is generated but the stamp does not record it — "
            f"nothing is checking its bytes")

    for name in sorted(set(by_name) & set(recorded_outputs)):
        path = by_name[name]
        if not path.exists():
            problems.append(f"{name} is recorded in the stamp and missing from the tree")
            continue
        actual = sha256_of(path)
        if actual != recorded_outputs[name]:
            problems.append(
                f"{name} is not the file that was generated — its bytes changed\n"
                f"    recorded {recorded_outputs[name]}\n"
                f"    current  {actual}")
    return problems
