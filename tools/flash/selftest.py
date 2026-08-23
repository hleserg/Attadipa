#!/usr/bin/env python3
"""Prove the SPIFFS extractor writes what it should and refuses what it must.

The reuse ledger's entry for `spiffs_extract.py` says, under *Tests required*:
*"none automated, and that is a real gap rather than a judgement. It has been
run against exactly one image — the Waveshare factory dump — which cannot be
committed."* This is that gap closed the way the same entry suggests: the images
are built here, from the on-disk layout the extractor documents, so nothing
copyrighted has to be committed to have something to parse.

`build()` is a **fixture, not a SPIFFS implementation.** It writes the parts the
extractor reads — the object index header at `span_ix == 0` with `u32 size`
before the NUL-terminated name, and data pages carrying `page - 5` bytes each —
and leaves the object lookup table erased, because the extractor skips those
pages by position and never reads them. Do not mistake a round trip through this
for a round trip through SPIFFS.

The half that matters is the refusals. A SPIFFS name is not a path: the device
has no directories, `/a/b` and `/a_b` are two unrelated names, and the first
version of this tool mapped both onto one file and reported both as extracted.
Every case below is a way two names could become one file, or one name could
become a file outside `outdir`, and each asserts that the tool says no *and*
that nothing was written when it did.

Run: python3 tools/flash/selftest.py
"""

from __future__ import annotations

import contextlib
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE / "spiffs_extract.py"
sys.path.insert(0, str(HERE))

import spiffs_extract  # noqa: E402

FAILURES: list[str] = []


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  ok    {name}")
    else:
        print(f"  FAIL  {name} {detail}")
        FAILURES.append(name)


def build(files, declared: dict[str, int] | None = None,
          page: int = 256, block: int = 4096, blocks: int = 8) -> bytes:
    """A SPIFFS image holding `files`, keyed by their on-device names.

    `files` is a name → bytes mapping, or a list of (name, bytes) pairs when the
    case needs **two object ids carrying one name** — which a real image taken
    off a used unit can hold, because a file deleted and recreated on the device
    keeps its name and gets a new id, and this parser reads the stale header too.
    A dict cannot express that, and a fixture that cannot express a case is why
    the case is missing.

    `declared` overrides the size written into an object index header, which is
    how the "declares more bytes than it has" case is built: no correct writer
    would produce it, and the extractor's refusal to write such a file short is
    one of the things worth keeping.
    """
    declared = declared or {}
    entries = list(files.items()) if isinstance(files, dict) else list(files)
    pages_per_block = block // page
    lookup_pages = -(-pages_per_block * 2 // page)  # ceil, as in the extractor
    payload_size = page - 5

    image = bytearray(b"\xff" * (blocks * block))
    # Every page of every block except the lookup pages at the head of each.
    free = [index for index in range(blocks * pages_per_block)
            if index % pages_per_block >= lookup_pages]

    def put(index: int, blob: bytes) -> None:
        at = index * page
        image[at:at + len(blob)] = blob

    for number, (name, data) in enumerate(entries, start=1):
        obj_id = number  # never 0x0000 or 0xFFFF, which the extractor skips
        encoded = name.encode("ascii") + b"\x00"

        # spiffs_page_object_ix_header, from spiffs_nucleus.h: the 5-byte page
        # header, three bytes of alignment (5 & 3 == 1, so the u32 is padded up
        # to 4), u32 size, u8 type, then the name. The extractor does not
        # hard-code these offsets — it finds the name and reads the u32 in front
        # of it — but the fixture has to put them somewhere, and the real layout
        # is the only defensible somewhere.
        header = bytearray(b"\xff" * page)
        header[0:5] = struct.pack("<HHB", obj_id | 0x8000, 0, 0xFF)
        header[8:12] = struct.pack("<I", declared.get(name, len(data)))
        header[12] = 0x01  # SPIFFS_OBJ_TYPE_FILE
        header[13:13 + len(encoded)] = encoded
        put(free.pop(0), bytes(header))

        for span, at in enumerate(range(0, len(data), payload_size)):
            chunk = data[at:at + payload_size]
            body = bytearray(b"\xff" * page)
            body[0:5] = struct.pack("<HHB", obj_id, span, 0xFF)
            body[5:5 + len(chunk)] = chunk
            put(free.pop(0), bytes(body))

    return bytes(image)


@contextlib.contextmanager
def workspace(files, declared: dict[str, int] | None = None):
    """A temporary image and an `out/` path the tool has not created yet."""
    with tempfile.TemporaryDirectory() as root:
        base = Path(root)
        image = base / "storage.spiffs"
        image.write_bytes(build(files, declared))
        yield image, base


def run(image: Path, out: Path, *extra: str) -> tuple[int, str]:
    result = subprocess.run(
        [sys.executable, str(TOOL), str(image), str(out), *extra],
        capture_output=True, text=True, check=False)
    return result.returncode, result.stdout + result.stderr


# The only portable way to make a write fail *after* the file exists, which is
# the case that matters: a full disk, a quota, a file-size limit. A read-only
# directory does not do it — that fails at open(), before there is anything to
# clean up, and it does nothing at all as root, which is what ctest inside a
# container runs as. SIGXFSZ is ignored so the limit arrives as EFBIG from
# write() instead of killing the process.
UNDER_A_SIZE_LIMIT = """
import resource, runpy, signal, sys
signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
resource.setrlimit(resource.RLIMIT_FSIZE, (int(sys.argv.pop(1)), resource.RLIM_INFINITY))
runpy.run_path(sys.argv.pop(1), run_name="__main__")
"""


def run_limited(limit: int, image: Path, out: Path, *extra: str) -> tuple[int, str]:
    result = subprocess.run(
        [sys.executable, "-c", UNDER_A_SIZE_LIMIT, str(limit), str(TOOL),
         str(image), str(out), *extra],
        capture_output=True, text=True, check=False)
    return result.returncode, result.stdout + result.stderr


def content(path: Path) -> bytes | None:
    """What is in the file, or None if the run did not make one.

    A missing file is a failed case with a message, not a traceback that stops
    the other cases from running — which is what the first version of this file
    did when it was pointed at the extractor this issue was filed against.
    """
    try:
        return path.read_bytes()
    except OSError:
        return None


def tree(out: Path) -> set[str]:
    """Every file the run left behind, relative to outdir, symlinks included."""
    if not out.exists():
        return set()
    found = set()
    for base, _dirs, names in os.walk(out):
        for name in names:
            found.add(os.path.relpath(os.path.join(base, name), out))
    return found


def main() -> int:  # noqa: C901 — a list of cases, not a branching function
    print("the finding: two names, one file")
    # The regression. On the version this issue was filed against, both of these
    # became `out/a_b`, the second silently replaced the first, and the summary
    # said two files had been extracted.
    with workspace({"/a/b": b"nested content", "/a_b": b"flat content"}) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("the run succeeds", code == 0, f"— exit {code}\n{output}")
        check("the nested name keeps its directory",
              content(out / "a" / "b") == b"nested content", f"— {sorted(tree(out))}")
        check("the flat name keeps its own file",
              content(out / "a_b") == b"flat content", f"— {sorted(tree(out))}")
        check("neither overwrote the other", tree(out) == {os.path.join("a", "b"), "a_b"},
              f"— {sorted(tree(out))}")
        check("the summary counts files that exist, not names that were seen",
              "\n2 extracted, 0 incomplete" in output and len(tree(out)) == 2, f"— {output}")

    print("\nordinary extraction")
    with workspace({"/image/image1.bin": b"\x19\x12\x00\x00pixels",
                    "/music/BGM_1.mp3": b"ID3\x04mp3 bytes"}) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("two nested names that do not collide are both written", code == 0,
              f"— exit {code}\n{output}")
        check("the hierarchy is the device's",
              tree(out) == {os.path.join("image", "image1.bin"),
                            os.path.join("music", "BGM_1.mp3")},
              f"— {sorted(tree(out))}")
        check("contents survive the round trip",
              content(out / "image" / "image1.bin") == b"\x19\x12\x00\x00pixels")

    # A file bigger than one page proves the span reassembly, which is the whole
    # reason this tool exists rather than `strings`.
    big = bytes(range(256)) * 8  # 2048 bytes, nine pages at 251 bytes of payload
    with workspace({"/image/big.bin": big}) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a multi-page file is reassembled in order",
              code == 0 and content(out / "image" / "big.bin") == big,
              f"— exit {code}\n{output}")

    print("\nnames that must be refused")
    cases: list[tuple[str, dict[str, bytes], str]] = [
        ("a name that climbs out of outdir", {"/../escape": b"x"}, "move the destination"),
        ("a name that climbs out further in",
         {"/a/../../escape": b"x"}, "move the destination"),
        ("repeated separators colliding with the plain name",
         {"/a//b": b"first", "/a/b": b"second"}, "same path"),
        ("a backslash, which is a separator on another host",
         {"/a\\b": b"x"}, "separator or a drive marker"),
        ("a drive letter", {"/C:/x": b"x"}, "separator or a drive marker"),
        ("a file where another name needs a directory",
         {"/a": b"file", "/a/b": b"under it"}, "directory"),
        ("a name that is nothing but separators", {"//": b"x"}, "separators"),
    ]
    for label, files, fragment in cases:
        with workspace(files) as (image, base):
            out = base / "out"
            code, output = run(image, out)
            check(label, code == 2, f"— exit {code}\n{output}")
            check(f"  and says why: {fragment!r}", fragment in output, f"— {output}")
            check("  and writes nothing", tree(out) == set(), f"— {sorted(tree(out))}")
            check("  and the summary claims nothing", "\n0 extracted" in output, f"— {output}")
            check("  and leaves nothing beside outdir",
                  set(os.listdir(base)) <= {"storage.spiffs", "out"},
                  f"— {sorted(os.listdir(base))}")

    print("\none bad name stops the whole run, before the first write")
    with workspace({"/good.bin": b"perfectly fine", "/../escape": b"x"}) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("the run is refused", code == 2, f"— exit {code}\n{output}")
        check("the good file is not written either", tree(out) == set(),
              f"— {sorted(tree(out))}")

    print("\nsomething is already at the destination")
    with workspace({"/x.bin": b"new bytes"}) as (image, base):
        out = base / "out"
        out.mkdir()
        (out / "x.bin").write_bytes(b"older bytes")
        code, output = run(image, out)
        check("an existing file is not overwritten", code == 2, f"— exit {code}\n{output}")
        check("  and still holds what it held", content(out / "x.bin") == b"older bytes")
        check("  and the message says how to mean it", "--force" in output, f"— {output}")
        code, output = run(image, out, "--force")
        check("--force replaces it", code == 0, f"— exit {code}\n{output}")
        check("  with the extracted bytes", content(out / "x.bin") == b"new bytes")
        check("  and says it replaced something", "replaced" in output, f"— {output}")

    with workspace({"/x.bin": b"new bytes"}) as (image, base):
        out = base / "out"
        out.mkdir()
        (base / "secret.txt").write_bytes(b"not ours")
        os.symlink(os.path.join("..", "secret.txt"), out / "x.bin")
        for extra in ((), ("--force",)):
            code, output = run(image, out, *extra)
            label = "--force " if extra else ""
            check(f"a symlink out of outdir is not written through ({label or 'plain'})",
                  code == 2, f"— exit {code}\n{output}")
            check("  and its target is untouched",
                  content(base / "secret.txt") == b"not ours")

    with workspace({"/x.bin": b"new bytes"}) as (image, base):
        out = base / "out"
        out.mkdir()
        (out / "inside.txt").write_bytes(b"also not ours")
        os.symlink("inside.txt", out / "x.bin")
        code, output = run(image, out, "--force")
        check("a symlink pointing inside outdir is refused as well", code == 2,
              f"— exit {code}\n{output}")
        check("  and its target is untouched",
              content(out / "inside.txt") == b"also not ours")

    with workspace({"/image/x.bin": b"new bytes"}) as (image, base):
        out = base / "out"
        out.mkdir()
        (base / "elsewhere").mkdir()
        os.symlink(os.path.join("..", "elsewhere"), out / "image")
        code, output = run(image, out)
        check("a symlinked directory in the way is refused", code == 2,
              f"— exit {code}\n{output}")
        check("  and nothing lands on the other side of it",
              list((base / "elsewhere").iterdir()) == [],
              f"— {list((base / 'elsewhere').iterdir())}")

    with workspace({"/x.bin": b"new bytes"}) as (image, base):
        out = base / "out"
        (out / "x.bin").mkdir(parents=True)
        code, output = run(image, out, "--force")
        check("a directory at the destination is refused even with --force", code == 2,
              f"— exit {code}\n{output}")

    print("\na write that fails after the file exists keeps nothing either")
    # The failure plan() cannot see coming, and the one this file got wrong on
    # its first pass: open() succeeds, so the destination exists, and then the
    # write fails. Nothing may be left behind and nothing may claim there was.
    with workspace([("/a.bin", b"a" * 100), ("/big.bin", b"b" * 4096)]) as (image, base):
        out = base / "out"
        code, output = run_limited(1024, image, out)
        check("the run fails", code == 2, f"— exit {code}\n{output}")
        check("  and says which file stopped it",
              "FAILED" in output and "/big.bin" in output, f"— {output}")
        check("  and the file already written is gone",
              not (out / "a.bin").exists(), f"— {sorted(tree(out))}")
        check("  and the half-written file is gone with it",
              not (out / "big.bin").exists(), f"— {sorted(tree(out))}")
        check("  and the summary claims nothing", "\n0 extracted" in output, f"— {output}")

    with workspace([("/big.bin", b"b" * 4096)]) as (image, base):
        out = base / "out"
        out.mkdir()
        (out / "big.bin").write_bytes(b"the file this replaced")
        code, output = run_limited(1024, image, out, "--force")
        check("a --force replacement that fails is reported, not papered over",
              code == 2, f"— exit {code}\n{output}")
        check("  and says the file is now neither one thing nor the other",
              "holds neither its old contents nor the new ones" in output, f"— {output}")
        check("  and does not delete the file it replaced",
              (out / "big.bin").exists(), f"— {sorted(tree(out))}")
        check("  which is no longer what it was, exactly as the message says",
              content(out / "big.bin") != b"the file this replaced")

    # And the case that made `0 extracted` a lie: a replacement that *succeeded*
    # before the failure. It is not rolled back — it was somebody else's file —
    # so it holds the extracted bytes, and the run has to name it.
    with workspace([("/a.bin", b"a" * 100), ("/big.bin", b"b" * 4096)]) as (image, base):
        out = base / "out"
        out.mkdir()
        (out / "a.bin").write_bytes(b"older a")
        (out / "big.bin").write_bytes(b"older big")
        code, output = run_limited(1024, image, out, "--force")
        check("a replacement completed before the failure is named", code == 2
              and "a.bin was replaced under --force" in output, f"— exit {code}\n{output}")
        check("  and it holds the extracted bytes, as the message says",
              content(out / "a.bin") == b"a" * 100)
        check("  and the summary counts it rather than claiming nothing happened",
              "\n0 extracted, 1 left replaced" in output, f"— {output}")

    # Two destinations that are one file underneath. A case-insensitive
    # filesystem is the reason this check exists and cannot be produced on this
    # one; a hard link is the same condition and can. Without --force O_EXCL
    # refuses the second open; with it there is no O_EXCL, which is exactly
    # where the tool is most destructive.
    with workspace({"/A.bin": b"first", "/a.bin": b"second"}) as (image, base):
        out = base / "out"
        out.mkdir()
        (out / "A.bin").write_bytes(b"older")
        os.link(out / "A.bin", out / "a.bin")
        code, output = run(image, out, "--force")
        check("two names that are one file underneath are caught, not merged",
              code == 2, f"— exit {code}\n{output}")
        check("  and it says so in those words",
              "same file on disk" in output, f"— {output}")
        check("  and the summary does not claim two files",
              "\n0 extracted" in output, f"— {output}")

    print("\ntwo object ids can carry one name, and a used image will")
    # A file deleted and recreated on the device keeps its name and gets a new
    # object id, and this parser reads the stale header as well as the live one.
    # Refusing the run is the right default; --allow-partial is the way through.
    with workspace([("/x.bin", b"first copy"), ("/x.bin", b"second copy")]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("one name from two ids is refused, not silently merged", code == 2,
              f"— exit {code}\n{output}")
        check("  and nothing is written", tree(out) == set(), f"— {sorted(tree(out))}")
        code, output = run(image, out, "--allow-partial")
        check("--allow-partial writes neither copy, because which is live is UNKNOWN",
              tree(out) == set(), f"— {sorted(tree(out))}")
        check("  and says that, rather than picking by object id",
              "which is live is UNKNOWN" in output, f"— {output}")
        check("  and still fails", code == 2, f"— exit {code}\n{output}")

    print("\n--allow-partial writes what was safe and nothing that was not")
    # The flag has to act on plan()'s writes, and plan() has to have taken every
    # refused name *and everything under it* out of that list. The first version
    # left them in, which was invisible while any refusal aborted the run and
    # became a write through a refused symlink the moment it did not.
    # The link points *inside* outdir on purpose: one pointing out is already
    # refused by inside(), so it would prove nothing about this rule.
    with workspace({"/good.bin": b"keep me",
                    "/image/x.bin": b"not through a link"}) as (image, base):
        out = base / "out"
        (out / "real").mkdir(parents=True)
        os.symlink("real", out / "image")
        code, output = run(image, out, "--allow-partial")
        check("the good file is written", content(out / "good.bin") == b"keep me",
              f"— {sorted(tree(out))}")
        check("  and nothing goes through the symlinked directory",
              list((out / "real").iterdir()) == [],
              f"— {list((out / 'real').iterdir())}")
        check("  and the run still fails", code == 2, f"— exit {code}\n{output}")
        check("  and the summary counts both sides",
              "\n1 extracted, 1 refused" in output, f"— {output}")

    with workspace({"/good.bin": b"keep me too",
                    "/image/x.bin": b"under a file"}) as (image, base):
        out = base / "out"
        out.mkdir()
        (out / "image").write_bytes(b"an ordinary file called image")
        code, output = run(image, out, "--allow-partial")
        check("a file where a directory is needed does not throw away the good ones",
              content(out / "good.bin") == b"keep me too", f"— {sorted(tree(out))}")
        check("  and the file in the way is untouched",
              content(out / "image") == b"an ordinary file called image")
        check("  and the reason is the one plan() diagnosed, not FileExistsError",
              "is already a file, not a directory" in output and "Errno 17" not in output,
              f"— {output}")

    print("\nan image this parser does not understand is not a success")
    with tempfile.TemporaryDirectory() as root:
        base = Path(root)
        image = base / "not-a-spiffs.bin"
        image.write_bytes(b"\x00" * 8192)
        code, output = run(image, base / "out")
        check("nothing recognised is reported and is not exit 0", code == 2,
              f"— exit {code}\n{output}")
        check("  and names what it could be",
              "geometry" in output and "empty partition" in output, f"— {output}")
        check("  and creates no output directory", not (base / "out").exists())

    print("\nwhat the image itself got wrong")
    with workspace({"/good.bin": b"all here", "/short.bin": b"truncated"},
                   declared={"/short.bin": 99999}) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a file declaring more bytes than it has is reported", "INCOMPLETE" in output,
              f"— {output}")
        check("  and is not written short", not (out / "short.bin").exists())
        check("  while the rest is extracted", content(out / "good.bin") == b"all here")
        check("  and the exit code says something was wrong", code == 1, f"— exit {code}")
        check("  and the summary counts one, not two", "\n1 extracted, 1 incomplete" in output,
              f"— {output}")

    print("\nthe rules themselves")
    check("a leading separator is dropped, not refused",
          spiffs_extract.components("/a/b") == ["a", "b"])
    check("repeated separators collapse",
          spiffs_extract.components("/a//b") == ["a", "b"])
    check("a name with no separators at all is one component",
          spiffs_extract.components("plain.bin") == ["plain.bin"])
    for refused in ("/..", "/a/../b", "/.", "//", "/", "/a\\b", "/c:x", "/a/\x00b"):
        try:
            spiffs_extract.components(refused)
            check(f"{refused!r} is refused", False, "— it was accepted")
        except spiffs_extract.UnsafeName:
            check(f"{refused!r} is refused", True)

    with tempfile.TemporaryDirectory() as root:
        canonical = os.path.realpath(root)
        check("a path under outdir is inside it",
              spiffs_extract.inside(canonical, os.path.join(root, "a", "b")))
        check("outdir itself is not inside itself",
              not spiffs_extract.inside(canonical, root))
        check("a sibling of outdir is not inside it",
              not spiffs_extract.inside(canonical, os.path.join(root, "..", "sibling")))

    print(f"\nselftest: {len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
