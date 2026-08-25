#!/usr/bin/env python3
"""Prove the generated-tree checks actually catch a changed output.

They did not, for as long as both existed. `generate_ui_fonts.py --check` and
`generate_images.py --check` compared an inputs digest and then counted
filenames, so a hand-edited bitmap byte, a corrupted glyph descriptor or a
truncated header passed green and reached firmware — verified as a reproducer
before this file was written, not imagined.

A check nobody has seen reject anything is a comment. So every case here builds
one specific mistake, runs the real tool against it, and fails if the tool
accepted it — and asserts the *reason* the tool gives, because a check that
rejects for the wrong reason sends the next person to the wrong file. That is
the discipline `tools/l10n/selftest.py` and `tools/ui/selftest.py` already
apply; this is the same shape over the two committed asset trees.

Nothing here touches the repository's own trees. Both are copied into a
temporary directory, mutated there, and restored between cases, so a failing run
leaves no half-edited font behind.

**What this does not cover, deliberately:** that a fresh generation from two
different absolute checkout paths produces identical bytes. That needs Node and
the pinned `lv_font_conv`, which most machines and every host CI job here do not
have — `tools/integrity/reproducibility.py` does that, in a job of its own.
Requiring it here would mean either a Node dependency in every host build or a
check that quietly skips, and a skipped check reads as a passing one.

  python3 tools/integrity/selftest.py
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

# Everything either `--check` reads. Copied rather than monkey-patched, because
# what is under test is the real command line a CI job runs, including how each
# script finds its own root.
SANDBOX_TREE = (
    "tools/integrity",
    "tools/font",
    "tools/assets",
    "assets/fonts/generated",
    "ui/assets/generated",
    "ui/assets/source",
)

FONT_CHECK = ("tools/font/generate_ui_fonts.py", "--check")
IMAGE_CHECK = ("tools/assets/generate_images.py", "--check")

FONT_SIZES = (14, 16, 20, 28, 64, 96)
FONT_OUTPUTS = [f"assets/fonts/generated/attadipa_nunito_sans_{size}.c" for size in FONT_SIZES]
FONT_STAMP = "assets/fonts/generated/INPUTS.sha256"

ICONS = ("mesh", "position", "warning")
ICON_SIZES = (33, 39, 47)
IMAGE_OUTPUTS = [f"ui/assets/generated/attadipa_icon_{name}_{size}.c"
                 for name in ICONS for size in ICON_SIZES]
IMAGE_OUTPUTS.append("ui/assets/generated/attadipa_images.h")
IMAGE_STAMP = "ui/assets/generated/INPUTS.sha256"

FAILURES: list[str] = []
# Counted rather than asserted at a number. A case list that silently shrank —
# a loop over an empty tuple, a helper that stopped being called — would still
# report every remaining case green, and the summary is where that shows.
CASES = 0


def ok(name: str) -> None:
    global CASES
    CASES += 1
    print(f"  ok    {name}")


def fail(name: str, detail: str) -> None:
    global CASES
    CASES += 1
    print(f"  FAIL  {name}\n        {detail}")
    FAILURES.append(name)


def build_sandbox(destination: Path) -> None:
    for relative in SANDBOX_TREE:
        source = ROOT / relative
        if not source.is_dir():
            raise SystemExit(f"selftest: {relative} is not a directory in {ROOT}")
        shutil.copytree(source, destination / relative,
                        ignore=shutil.ignore_patterns("__pycache__"))


def run_check(sandbox: Path, command: tuple[str, ...]) -> subprocess.CompletedProcess:
    """Run a real check in the sandbox, with bytecode caching off.

    `-B` is not tidiness. CPython decides a cached `.pyc` is still valid from the
    source's mtime and size, both to one-second resolution — and this file edits
    a module, runs it, and restores the original bytes inside the same second,
    often at exactly the same length. The restored source is then byte-identical
    and the interpreter still imports the mutated bytecode, so a later case fails
    against a tree that is provably correct on disk. Found by the closing control
    case rather than reasoned about, which is why that case exists.
    """
    environment = dict(os.environ, PYTHONDONTWRITEBYTECODE="1")
    return subprocess.run([sys.executable, "-B", str(sandbox / command[0]), *command[1:]],
                          capture_output=True, text=True, cwd=str(sandbox),
                          env=environment)


def corrupt(path: Path) -> None:
    """Change exactly one byte, the way a bad merge or a stray keystroke would.

    A hex literal where the file has one — that is a bitmap byte or a descriptor
    field, the reproducer the finding was filed with — and the middle byte
    otherwise, which is how a generated header gets damaged.
    """
    raw = bytearray(path.read_bytes())
    marker = raw.rfind(b"0x")
    position = marker + 2 if marker != -1 else len(raw) // 2
    raw[position] = ord("1") if raw[position] != ord("1") else ord("2")
    path.write_bytes(bytes(raw))


def substitute(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise SystemExit(f"selftest: {path.name} does not contain {old!r} to replace")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def case(sandbox: Path, name: str, command: tuple[str, ...], touched: list[str],
         mutate, expect: list[str]) -> None:
    """Mutate, run the real check, restore. The check must fail, and say why."""
    saved = {relative: (sandbox / relative).read_bytes()
             for relative in touched if (sandbox / relative).exists()}
    try:
        mutate()
        result = run_check(sandbox, command)
        output = result.stdout + result.stderr
        if result.returncode == 0:
            fail(name, "the check ACCEPTED it — exit 0:\n        " + output.strip())
            return
        missing = [fragment for fragment in expect if fragment not in output]
        if missing:
            fail(name, f"rejected, but did not say {missing}:\n        {output.strip()}")
            return
        ok(name)
    finally:
        for relative in touched:
            path = sandbox / relative
            if relative in saved:
                path.write_bytes(saved[relative])
            elif path.exists():
                path.unlink()


def control(sandbox: Path) -> None:
    """An untouched copy passes both checks.

    First, and not a formality: every case below asserts a non-zero exit, and a
    harness that had broken the sandbox — a missed file, a wrong working
    directory — would satisfy all of them while testing nothing.
    """
    for label, command, count in (("fonts", FONT_CHECK, len(FONT_OUTPUTS)),
                                  ("images", IMAGE_CHECK, len(IMAGE_OUTPUTS))):
        result = run_check(sandbox, command)
        if result.returncode != 0:
            fail(f"an untouched {label} tree passes",
                 (result.stdout + result.stderr).strip())
        elif f"{count} generated file(s)" not in result.stdout:
            fail(f"an untouched {label} tree passes",
                 f"passed, but did not report {count} files: {result.stdout.strip()}")
        else:
            ok(f"an untouched {label} tree passes, all {count} of it")


def font_cases(sandbox: Path) -> None:
    print("\nfonts — every output, one at a time:")
    for relative in FONT_OUTPUTS:
        name = Path(relative).name
        case(sandbox, f"a changed byte in {name}", FONT_CHECK, [relative],
             lambda r=relative: corrupt(sandbox / r),
             [name, "its bytes changed"])

    print("\nfonts — the other ways a tree goes wrong:")
    banner_file = FONT_OUTPUTS[0]
    case(sandbox, "a changed comment in a generated font", FONT_CHECK, [banner_file],
         lambda: substitute(sandbox / banner_file, "do not edit", "do not edlt"),
         [Path(banner_file).name, "its bytes changed"])
    case(sandbox, "a deleted font", FONT_CHECK, [banner_file],
         lambda: (sandbox / banner_file).unlink(),
         [Path(banner_file).name, "missing from the tree"])
    case(sandbox, "an edited charset", FONT_CHECK, ["tools/font/charset.py"],
         lambda: substitute(sandbox / "tools/font/charset.py",
                            "(0x2116, 0x2116,", "(0x2117, 0x2117,"),
         ["the inputs changed"])
    case(sandbox, "an added size", FONT_CHECK, [FONT_CHECK[0]],
         lambda: substitute(sandbox / FONT_CHECK[0],
                            "SIZES = (14, 16, 20, 28, 64, 96)",
                            "SIZES = (14, 16, 20, 28, 32, 64, 96)"),
         ["the inputs changed"])
    case(sandbox, "a changed bit depth", FONT_CHECK, [FONT_CHECK[0]],
         lambda: substitute(sandbox / FONT_CHECK[0], "BPP = 4", "BPP = 2"),
         ["the inputs changed"])
    case(sandbox, "a different source TTF", FONT_CHECK, [FONT_CHECK[0]],
         lambda: substitute(sandbox / FONT_CHECK[0], "TTF_SHA256 = \"f934d714",
                            "TTF_SHA256 = \"f934d715"),
         ["the inputs changed"])
    case(sandbox, "a different converter version", FONT_CHECK, [FONT_CHECK[0]],
         lambda: substitute(sandbox / FONT_CHECK[0],
                            'CONVERTER_VERSION = "1.5.3"', 'CONVERTER_VERSION = "1.6.0"'),
         ["the inputs changed"])
    case(sandbox, "an edited banner, with the fonts left alone", FONT_CHECK, [FONT_CHECK[0]],
         lambda: substitute(sandbox / FONT_CHECK[0],
                            "Variable source pinned to",
                            "Variable source fixed to"),
         ["the inputs changed"])


def stamp_cases(sandbox: Path, label: str, stamp_file: str, command: tuple[str, ...],
                sample_output: str) -> None:
    print(f"\n{label} — the stamp itself:")
    case(sandbox, f"a {label} stamp with a doctored output hash", command, [stamp_file],
         lambda: substitute(sandbox / stamp_file, "output  ", "output  0"),
         ["is not a valid stamp"])
    case(sandbox, f"a {label} stamp missing one output line", command, [stamp_file],
         lambda: drop_output_line(sandbox / stamp_file, Path(sample_output).name),
         [Path(sample_output).name, "the stamp does not record it"])
    case(sandbox, f"a {label} stamp with no inputs line", command, [stamp_file],
         lambda: substitute(sandbox / stamp_file, "inputs  ", "# inputs  "),
         ["is not a valid stamp", "no 'inputs' line"])
    case(sandbox, f"a {label} stamp with a line nobody wrote", command, [stamp_file],
         lambda: (sandbox / stamp_file).write_text(
             (sandbox / stamp_file).read_text(encoding="utf-8") + "looks fine to me\n",
             encoding="utf-8", newline="\n"),
         ["is not a valid stamp"])
    case(sandbox, f"a {label} stamp that records a file nobody generates", command,
         [stamp_file],
         lambda: (sandbox / stamp_file).write_text(
             (sandbox / stamp_file).read_text(encoding="utf-8")
             + f"output  {'0' * 64}  attadipa_extra.c\n",
             encoding="utf-8", newline="\n"),
         ["attadipa_extra.c", "does not produce"])
    case(sandbox, f"a deleted {label} stamp", command, [stamp_file],
         lambda: (sandbox / stamp_file).unlink(),
         ["INPUTS.sha256 is missing"])
    # A digest that is a real digest of the wrong bytes. The doctored-hash case
    # above is caught as malformed; this one has to be caught by comparison,
    # which is a different code path and the one that matters.
    case(sandbox, f"a {label} stamp holding a plausible but wrong hash", command,
         [stamp_file],
         lambda: swap_recorded_hash(sandbox / stamp_file, Path(sample_output).name),
         [Path(sample_output).name, "its bytes changed"])


def drop_output_line(stamp_file: Path, name: str) -> None:
    kept = [line for line in stamp_file.read_text(encoding="utf-8").splitlines()
            if not line.endswith(f"  {name}")]
    stamp_file.write_text("\n".join(kept) + "\n", encoding="utf-8", newline="\n")


def swap_recorded_hash(stamp_file: Path, name: str) -> None:
    """Give one file another file's recorded digest — still 64 valid hex characters.

    The point of this case is that the stamp stays perfectly well-formed. The
    doctored-hash case is rejected by the parser before anything is compared;
    this one can only be caught by hashing the file and looking.
    """
    lines = stamp_file.read_text(encoding="utf-8").splitlines()
    outputs = [index for index, line in enumerate(lines) if line.startswith("output  ")]
    target = next(index for index in outputs if lines[index].endswith(f"  {name}"))
    donor = next(index for index in outputs if index != target)
    lines[target] = f"output  {lines[donor].split()[1]}  {name}"
    stamp_file.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")


def image_cases(sandbox: Path) -> None:
    print("\nimages — every output, one at a time:")
    for relative in IMAGE_OUTPUTS:
        name = Path(relative).name
        case(sandbox, f"a changed byte in {name}", IMAGE_CHECK, [relative],
             lambda r=relative: corrupt(sandbox / r),
             [name, "its bytes changed"])

    print("\nimages — the other ways a tree goes wrong:")
    first = IMAGE_OUTPUTS[0]
    case(sandbox, "a deleted mask", IMAGE_CHECK, [first],
         lambda: (sandbox / first).unlink(),
         [Path(first).name, "missing from the tree"])
    header = IMAGE_OUTPUTS[-1]
    case(sandbox, "a truncated header", IMAGE_CHECK, [header],
         lambda: (sandbox / header).write_text(
             (sandbox / header).read_text(encoding="utf-8")[:200],
             encoding="utf-8", newline="\n"),
         [Path(header).name, "its bytes changed"])
    case(sandbox, "edited source art", IMAGE_CHECK, ["ui/assets/source/icons/mesh_33.png"],
         lambda: corrupt(sandbox / "ui/assets/source/icons/mesh_33.png"),
         ["the inputs changed"])
    case(sandbox, "an edited manifest", IMAGE_CHECK, ["tools/assets/manifest.py"],
         lambda: substitute(sandbox / "tools/assets/manifest.py", "SIZES", "SIZES "),
         ["the inputs changed"])
    case(sandbox, "an edited converter", IMAGE_CHECK, ["tools/assets/vendor/LVGLImage.py"],
         lambda: corrupt(sandbox / "tools/assets/vendor/LVGLImage.py"),
         ["the inputs changed"])


def main() -> int:
    with tempfile.TemporaryDirectory() as directory:
        sandbox = Path(directory) / "checkout"
        build_sandbox(sandbox)

        print("a tree that is what it says it is:")
        control(sandbox)
        font_cases(sandbox)
        stamp_cases(sandbox, "fonts", FONT_STAMP, FONT_CHECK, FONT_OUTPUTS[0])
        image_cases(sandbox)
        stamp_cases(sandbox, "images", IMAGE_STAMP, IMAGE_CHECK, IMAGE_OUTPUTS[0])

        # The tree is intact again. Every case restores what it touched, and a
        # case that did not would leave the following ones testing a corrupted
        # baseline while still reporting failure for the right-looking reason.
        print("\nand afterwards:")
        control(sandbox)

    if FAILURES:
        print(f"\ngenerated-output selftest FAILED: {len(FAILURES)} of {CASES} case(s)",
              file=sys.stderr)
        for name in FAILURES:
            print(f"  * {name}", file=sys.stderr)
        return 1
    print(f"\ngenerated-output selftest: {CASES} cases, every deliberate mutation "
          f"rejected, each for its own reason.\nReproducibility across checkout paths "
          "is not "
          "covered here — tools/integrity/reproducibility.py, which needs the "
          "pinned converter.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
