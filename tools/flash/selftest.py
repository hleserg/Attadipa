#!/usr/bin/env python3
"""Prove the SPIFFS extractor writes what it should and refuses what it must.

The reuse ledger's entry for `spiffs_extract.py` says, under *Tests required*:
*"none automated, and that is a real gap rather than a judgement. It has been
run against exactly one image — the Waveshare factory dump — which cannot be
committed."* This is that gap closed the way the same entry suggests: the images
are built here, from the on-disk layout the extractor documents, so nothing
copyrighted has to be committed to have something to parse.

`build()` is a **fixture, not a SPIFFS implementation.** It writes the parts the
extractor reads — the object lookup table at the head of each block, the page
header with the flag byte ESP-IDF's own writers produce, the object index header
at `span_ix == 0` with `u32 size` before the NUL-terminated name, data pages
carrying `page - 5` bytes each, and the per-block magic. Do not mistake a round
trip through this for a round trip through SPIFFS.

The half that matters is the refusals, and there are two families of them.

**A name off the device is not a path.** SPIFFS has no directories, `/a/b` and
`/a_b` are two unrelated names, and the first version of this tool mapped both
onto one file and reported both as extracted.

**A page that exists is not a page that counts.** SPIFFS never overwrites in
place, so an edited or deleted file leaves its old pages in flash with their
`obj_id` and `span_ix` untouched — `spiffs_page_delete()` clears the *lookup*
entry and two bits of the page header and nothing else. The first version read
the page header alone and kept whichever copy came last physically, which is not
a recency, so it could hand back a file assembled out of two generations that
still passed the length check.

Every case below is a way two names could become one file, a way stale bytes
could get into a live file, or a way one name could become a file outside
`outdir`; each asserts that the tool says no *and* that nothing was written when
it did.

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
from typing import NamedTuple

HERE = Path(__file__).resolve().parent
TOOL = HERE / "spiffs_extract.py"
sys.path.insert(0, str(HERE))

import spiffs_extract  # noqa: E402

FAILURES: list[str] = []


def rule(name: str, fallback=None):
    """A name the extractor is expected to expose, or a recorded failure.

    Pointing this file at an older extractor is how a case is shown to bite, and
    that only works if the run gets to the end: an `AttributeError` halfway down
    takes every case after it with it, and the count that comes out is a
    property of where the traceback landed rather than of the tool. Same reason
    `content()` returns None instead of raising.
    """
    found = getattr(spiffs_extract, name, None)
    if found is None:
        check(f"the extractor exposes {name}", False, "— it does not")
        return fallback if fallback is not None else (lambda *a, **k: None)
    return found


def check(name: str, condition: bool, detail: str = "") -> None:
    if condition:
        print(f"  ok    {name}")
    else:
        print(f"  FAIL  {name} {detail}")
        FAILURES.append(name)


# The flag bytes ESP-IDF's own writers put in a page header, named as
# `spiffsgen.py` names them: SPIFFS_PH_FLAG_USED_FINAL_INDEX and
# SPIFFS_PH_FLAG_USED_FINAL. Every bit is active low, so 0xF8 is "used,
# finalised, an index page" and 0xFC is the same for a data page.
LIVE_INDEX = 0xF8
LIVE_DATA = 0xFC
# What `spiffs_page_delete()` leaves behind: DELET and USED cleared in the page
# header, the object id and the span index untouched, and the *lookup* entry set
# to SPIFFS_OBJ_ID_DELETED. That asymmetry is the whole finding — the header
# still reads as a perfectly good page.
DELETED_LOOKUP = 0x0000
FREE_LOOKUP = 0xFFFF


def released(flags: int) -> int:
    """The flag byte a delete leaves: `flags &= ~(DELET | USED)`."""
    return flags & ~(0x80 | 0x01)


class Page(NamedTuple):
    """One physical page, and what its block's lookup table says about it.

    `lookup` is separate from `obj_id` on purpose: in flash they are two
    different writes to two different places, they disagree for a whole page's
    lifetime after a delete, and a fixture that cannot make them disagree cannot
    build the image this issue is about.
    """

    lookup: int
    obj_id: int
    span: int
    flags: int
    body: bytes


def index_page(obj_id: int, name: str, size: int, page: int,
               flags: int = LIVE_INDEX, lookup: int | None = None) -> Page:
    """An object index header — `spiffs_page_object_ix_header` with span 0.

    Three bytes of alignment after the 5-byte page header (5 & 3 == 1, so the
    u32 is padded up to 4), u32 size, u8 type, then the NUL-terminated name. The
    extractor does not hard-code those offsets — it finds the name and reads the
    u32 in front of it — but the fixture has to put them somewhere, and the real
    layout is the only defensible somewhere.
    """
    body = bytearray(b"\xff" * (page - 5))
    body[3:7] = struct.pack("<I", size)
    body[7] = 0x01  # SPIFFS_OBJ_TYPE_FILE
    encoded = name.encode("ascii") + b"\x00"
    body[8:8 + len(encoded)] = encoded
    identifier = obj_id | 0x8000
    return Page(identifier if lookup is None else lookup,
                identifier, 0, flags, bytes(body))


def data_page(obj_id: int, span: int, chunk: bytes, page: int,
              flags: int = LIVE_DATA, lookup: int | None = None) -> Page:
    body = bytearray(b"\xff" * (page - 5))
    body[:len(chunk)] = chunk
    return Page(obj_id if lookup is None else lookup,
                obj_id, span, flags, bytes(body))


def object_pages(obj_id: int, name: str, data: bytes, page: int,
                 size: int | None = None, live: bool = True) -> list[Page]:
    """Every page one file occupies, live or deleted as a whole."""
    flag_index = LIVE_INDEX if live else released(LIVE_INDEX)
    flag_data = LIVE_DATA if live else released(LIVE_DATA)
    lookup = None if live else DELETED_LOOKUP
    pages = [index_page(obj_id, name, len(data) if size is None else size, page,
                        flag_index, lookup)]
    payload_size = page - 5
    for span, at in enumerate(range(0, len(data), payload_size)):
        pages.append(data_page(obj_id, span, data[at:at + payload_size], page,
                               flag_data, lookup))
    return pages


def build(files, declared: dict[str, int] | None = None,
          page: int = 256, block: int = 4096, blocks: int = 8,
          extra: list[Page] = (), magic: bool = True) -> bytes:
    """A SPIFFS image holding `files`, keyed by their on-device names.

    `files` is a name → bytes mapping, or a list of (name, bytes) pairs when the
    case needs **two object ids carrying one name** — which a real image taken
    off a used unit can hold, because a file deleted and recreated on the device
    keeps its name and gets a new id. A dict cannot express that, and a fixture
    that cannot express a case is why the case is missing.

    `declared` overrides the size written into an object index header, which is
    how the "declares more bytes than it has" case is built: no correct writer
    would produce it, and the extractor's refusal to write such a file short is
    one of the things worth keeping.

    `extra` appends hand-built pages after the files — a released page still
    wearing its old label, a second live page for one span, a header whose
    lookup entry names a different object. Those are what a *used* image holds
    and a freshly generated one does not, and they are the reason this fixture
    writes a lookup table at all.
    """
    declared = declared or {}
    entries = list(files.items()) if isinstance(files, dict) else list(files)
    pages_per_block = block // page
    # SPIFFS_OBJ_LOOKUP_PAGES, as the extractor computes it.
    lookup_pages = max(1, (pages_per_block * 2) // page)
    max_entries = pages_per_block - lookup_pages
    entries_per_page = page // 2

    laid_out: list[Page] = []
    for number, (name, data) in enumerate(entries, start=1):
        # Never 0x0000 or 0xFFFF, which are SPIFFS's deleted and free markers.
        laid_out += object_pages(number, name, data, page, declared.get(name))
    laid_out += list(extra)
    if len(laid_out) > blocks * max_entries:
        raise ValueError(f"{len(laid_out)} pages will not fit in {blocks} blocks")

    image = bytearray(b"\xff" * (blocks * block))
    for slot, item in enumerate(laid_out):
        bix, entry = divmod(slot, max_entries)
        # The lookup entries of a block run back to back across its lookup
        # pages, which is how spiffs_obj_lu_find_entry_visitor() walks them.
        lookup_at = (bix * block + (entry // entries_per_page) * page
                     + (entry % entries_per_page) * 2)
        image[lookup_at:lookup_at + 2] = struct.pack("<H", item.lookup)
        at = (bix * pages_per_block + lookup_pages + entry) * page
        image[at:at + 5] = struct.pack("<HHB", item.obj_id, item.span, item.flags)
        image[at + 5:at + page] = item.body

    if magic:
        # SPIFFS_MAGIC with SPIFFS_USE_MAGIC_LENGTH, ESP-IDF's default: the
        # second-to-last object-id slot of the block's lookup area, in every
        # block including the ones holding nothing.
        for bix in range(blocks):
            at = bix * block + lookup_pages * page - 4
            image[at:at + 2] = struct.pack(
                "<H", (0x20140529 ^ page ^ (blocks - bix)) & 0xFFFF)

    return bytes(image)


@contextlib.contextmanager
def workspace(files, declared: dict[str, int] | None = None, **rest):
    """A temporary image and an `out/` path the tool has not created yet."""
    with tempfile.TemporaryDirectory() as root:
        base = Path(root)
        image = base / "storage.spiffs"
        image.write_bytes(build(files, declared, **rest))
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

    print("\nthe second finding: a page that exists is not a page that counts")
    # #108. The old parser read the page header alone and kept whichever copy of
    # an `obj_id/span_ix` came last physically. A rewritten file leaves the old
    # page in flash with its header intact, so "last physically" is not
    # "current" — and the mixed result still passed the length check.
    with workspace(
        {"/x.bin": b"L" * 251 + b"IVE"},
        extra=[data_page(1, 1, b"stale, and 3 bytes long", 256,
                         flags=released(LIVE_DATA), lookup=DELETED_LOOKUP)],
    ) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a released page of the same span is not read as data", code == 0,
              f"— exit {code}\n{output}")
        check("  and the live bytes are the ones written",
              content(out / "x.bin") == b"L" * 251 + b"IVE", f"— {output}")
        check("  and the run says how many pages it stepped over",
              "1 stale" in output, f"— {output}")
        check("  and says what that means for anyone holding an old measurement",
              "would have taken them for data" in output, f"— {output}")

    # A whole generation of a rewritten multi-page file, still in flash. Every
    # span has a released twin, so a parser that took the last physical page for
    # each span would return the *entire* previous version and report success.
    older = b"O" * 251 + b"L" * 251 + b"D" * 100
    newer = b"N" * 251 + b"E" * 251 + b"W" * 100
    with workspace(
        {"/rewritten.bin": newer},
        extra=[data_page(1, span, older[at:at + 251], 256,
                         flags=released(LIVE_DATA), lookup=DELETED_LOOKUP)
               for span, at in enumerate(range(0, len(older), 251))],
    ) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a multi-page file whose previous generation is still in flash",
              code == 0, f"— exit {code}\n{output}")
        check("  comes out as the generation the lookup table still points at",
              content(out / "rewritten.bin") == newer, f"— {output}")
        check("  with none of the older one mixed in",
              b"OLD" not in (content(out / "rewritten.bin") or b"OLD"))
        check("  and three released pages are counted", "3 stale" in output,
              f"— {output}")

    print("\na deleted file is not a file")
    # Deleting on the device clears the lookup entries and two bits per page. It
    # does not touch the name, the object id or the bytes, so the whole file is
    # still legible to anything that reads page headers alone — which is how a
    # deleted file gets into evidence as a live one.
    with workspace({"/kept.bin": b"still here"},
                   extra=object_pages(2, "/gone.bin", b"deleted content", 256,
                                      live=False)) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("the live file is written", content(out / "kept.bin") == b"still here",
              f"— exit {code}\n{output}")
        check("  and the deleted one is not written at all",
              tree(out) == {"kept.bin"}, f"— {sorted(tree(out))}")
        check("  nor named anywhere in the output", "/gone.bin" not in output,
              f"— {output}")
        check("  and its pages are counted as stale", "2 stale" in output, f"— {output}")

    # The device marks an object index header IXDELE *before* it starts
    # unlinking pages, so an interrupted delete leaves a header that still has
    # its lookup entry and still passes every other test.
    with workspace({"/kept.bin": b"still here"},
                   extra=[index_page(2, "/half-deleted.bin", 5, 256,
                                     flags=LIVE_INDEX & ~0x40),
                          data_page(2, 0, b"bytes", 256)]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("an object index header marked for deletion is not a file",
              "/half-deleted.bin" not in output, f"— {output}")
        check("  and its data pages are reported rather than silently dropped",
              "no live object index header" in output, f"— {output}")
        check("  which is an incompleteness, not a refusal", code == 1,
              f"— exit {code}\n{output}")
        check("  and the live file is still written",
              content(out / "kept.bin") == b"still here", f"— {sorted(tree(out))}")

    print("\nambiguity is refused, not resolved by physical order")
    with workspace({"/x.bin": b"first"},
                   extra=[data_page(1, 0, b"second", 256)]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("two live pages claiming one span stop the run", code == 2,
              f"— exit {code}\n{output}")
        check("  and it says physical order is not a recency",
              "not a recency" in output, f"— {output}")
        check("  and neither is written", tree(out) == set(), f"— {sorted(tree(out))}")

    with workspace({"/x.bin": b"body"},
                   extra=[index_page(1, "/other-name.bin", 4, 256)]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("two live object index headers for one object stop the run", code == 2,
              f"— exit {code}\n{output}")
        check("  and say which two names they are",
              "/x.bin" in output and "/other-name.bin" in output, f"— {output}")
        check("  and that which one the device would open is UNKNOWN",
              "is UNKNOWN" in output, f"— {output}")
        check("  and neither is written", tree(out) == set(), f"— {sorted(tree(out))}")

    # A gap is the quiet one. `b"".join(sorted(spans))` closes it by sliding
    # every later span forward, so the file is the right length, wrong from the
    # gap onwards, and reported as a clean extraction.
    with workspace({}, extra=[index_page(1, "/gappy.bin", 502, 256),
                              data_page(1, 0, b"A" * 251, 256),
                              data_page(1, 2, b"C" * 251, 256)]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a missing span is refused rather than closed up", code == 2,
              f"— exit {code}\n{output}")
        check("  and names the span that is absent",
              "span 1 of 3" in output, f"— {output}")
        check("  and says what closing it would have done",
              "move every later byte forward" in output, f"— {output}")
        check("  and writes nothing", tree(out) == set(), f"— {sorted(tree(out))}")

    print("\nthe lookup table is the authority, and the header alone is not")
    # The two are written at different times to different places. When they
    # disagree the page is not part of the filesystem, whatever its header says
    # — and the file that needed it is short rather than quietly complete.
    with workspace({}, extra=[index_page(1, "/x.bin", 300, 256),
                              data_page(1, 0, b"A" * 251, 256),
                              data_page(1, 1, b"B" * 49, 256, lookup=7)]
                   ) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a page whose lookup entry names another object is not used",
              "declares 300 bytes, only 251 recovered" in output, f"— {output}")
        check("  and the file is not written short", tree(out) == set(),
              f"— {sorted(tree(out))}")
        check("  and it is counted as unusable rather than stale",
              "1 unusable" in output, f"— {output}")
        check("  and the run reports it", code == 1, f"— exit {code}\n{output}")

    with workspace({}, extra=[index_page(1, "/x.bin", 5, 256),
                              data_page(1, 0, b"bytes", 256,
                                        flags=LIVE_DATA | 0x02)]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a page whose write never finalised is not used",
              "declares 5 bytes, only 0 recovered" in output, f"— {output}")
        check("  and nothing is written", tree(out) == set(), f"— {sorted(tree(out))}")

    # A live object index header this parser cannot read a name out of means the
    # layout is not the documented one. Skipping it would answer a question
    # nobody asked: "here is some of the partition".
    with workspace({"/kept.bin": b"fine"},
                   extra=[Page(2 | 0x8000, 2 | 0x8000, 0, LIVE_INDEX,
                               b"\xff" * 251)]) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("a live index header with no readable name stops the run", code == 2,
              f"— exit {code}\n{output}")
        check("  and says the layout is not the one it knows",
              "not the one it knows" in output, f"— {output}")
        check("  and does not quietly extract the rest", tree(out) == set(),
              f"— {sorted(tree(out))}")

    print("\nthe liveness rule itself, against the SPIFFS sources")
    live_data, live_index = 0xFC, 0xF8
    is_live = rule("is_live")
    check("a finalised, undeleted data page whose lookup entry agrees is live",
          is_live(3, 3, 0, live_data))
    check("an object index header with IXDELE still set is live",
          is_live(3 | 0x8000, 3 | 0x8000, 0, live_index))
    check("a released lookup entry is not, however good the header looks",
          not is_live(0x0000, 3, 0, live_data))
    check("a free lookup entry is not either",
          not is_live(0xFFFF, 3, 0, live_data))
    check("a lookup entry naming a different object is not",
          not is_live(4, 3, 0, live_data))
    check("a page whose DELET bit is cleared is not",
          not is_live(3, 3, 0, released(live_data)))
    check("a page still marked under modification is not",
          not is_live(3, 3, 0, live_data | 0x02))
    check("a page whose USED bit is still set — never written — is not",
          not is_live(3, 3, 0, 0xFF))
    check("an object index header with IXDELE cleared is not",
          not is_live(3 | 0x8000, 3 | 0x8000, 0, live_index & ~0x40))
    check("but IXDELE says nothing about an index page that is not span 0",
          is_live(3 | 0x8000, 3 | 0x8000, 1, live_index & ~0x40))
    check("and nothing about a data page",
          is_live(3, 3, 0, live_data & ~0x40))

    print("\nan image this parser does not understand is not a success")
    with tempfile.TemporaryDirectory() as root:
        base = Path(root)
        image = base / "not-a-spiffs.bin"
        image.write_bytes(b"\x00" * 8192)
        code, output = run(image, base / "out")
        check("an image that is not SPIFFS is not exit 0", code == 2,
              f"— exit {code}\n{output}")
        check("  and the magic is what says so",
              "the geometry does not check out" in output, f"— {output}")
        check("  and it names the three things it could be",
              "--page/--block is wrong" in output and "part of a partition" in output
              and "not a SPIFFS image" in output, f"— {output}")
        check("  and creates no output directory", not (base / "out").exists())

    # An image with a lookup table full of erased entries: every page free, so
    # nothing is live and nothing is claimed. Distinct from the case above,
    # because here the geometry checks out and the partition really is empty.
    with workspace({}) as (image, base):
        code, output = run(image, base / "out")
        check("an empty but well-formed partition is reported, not called a success",
              code == 2, f"— exit {code}\n{output}")
        check("  and says nothing was recognised",
              "NOTHING RECOGNISED" in output, f"— {output}")
        check("  and the geometry line still says what it read",
              "confirmed by the block magic" in output, f"— {output}")

    for label, page, block in (("--page below a page header", 4, 4096),
                               ("--block that is not whole pages", 256, 4000),
                               ("--block smaller than a page", 512, 256)):
        with workspace({"/x.bin": b"x"}) as (image, base):
            code, output = run(image, base / "out", "--page", str(page),
                               "--block", str(block))
            check(f"{label} is refused before a page is read", code == 2,
                  f"— exit {code}\n{output}")
            check("  and nothing is read", "image not read" in output, f"— {output}")

    with workspace({"/x.bin": b"x"}) as (image, base):
        # Half a block off the end: a dump that was cut short, or the wrong
        # --block. Either way the block count is wrong, and the block count is
        # what the magic is a function of.
        image.write_bytes(image.read_bytes() + b"\xff" * 2048)
        code, output = run(image, base / "out")
        check("a dump that is not a whole number of blocks is refused", code == 2,
              f"— exit {code}\n{output}")
        check("  and says which two numbers disagree",
              "not a whole number of 4096-byte blocks" in output, f"— {output}")

    with workspace({"/x.bin": b"x"}) as (image, base):
        # The magic is a function of the block count, so doubling --block moves
        # every expected value and the ones in the image contradict it.
        code, output = run(image, base / "out", "--block", "8192")
        check("a block size the image was not built with is caught by the magic",
              code == 2, f"— exit {code}\n{output}")
        check("  and named as a geometry failure",
              "the geometry does not check out" in output, f"— {output}")
        check("  before any page is read", "image not read" in output, f"— {output}")

    with workspace({"/x.bin": b"x"}) as (image, base):
        # The magic cannot catch every wrong geometry, and this is one it does
        # not: at 512-byte pages its slot lands on erased padding, which abstains
        # rather than contradicting. What stops the run is that nothing is live
        # — and the geometry line says, in the same output, that nothing ever
        # confirmed the numbers. Recorded as a case because the limit of the
        # check is worth knowing rather than discovering.
        code, output = run(image, base / "out", "--page", "512")
        check("a page size the image was not built with finds nothing live",
              code == 2, f"— exit {code}\n{output}")
        check("  and says so rather than reporting a subset",
              "NOTHING RECOGNISED" in output, f"— {output}")
        check("  with the geometry line admitting it was never corroborated",
              "not corroborated" in output, f"— {output}")

    # Magic off is a supported configuration — CONFIG_SPIFFS_USE_MAGIC can be
    # cleared — so an image without one has to still read. It is the difference
    # between "not corroborated" and "contradicted", and only the second stops
    # the run.
    with workspace({"/x.bin": b"unmagicked"}, magic=False) as (image, base):
        out = base / "out"
        code, output = run(image, out)
        check("an image with no magic at all is still read", code == 0,
              f"— exit {code}\n{output}")
        check("  and the geometry line says it was not corroborated",
              "no block carries a magic value" in output, f"— {output}")
        check("  and the file comes out", content(out / "x.bin") == b"unmagicked")

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
    components = rule("components")
    inside = rule("inside")
    unsafe_name = rule("UnsafeName", ValueError)
    check("a leading separator is dropped, not refused",
          components("/a/b") == ["a", "b"])
    check("repeated separators collapse",
          components("/a//b") == ["a", "b"])
    check("a name with no separators at all is one component",
          components("plain.bin") == ["plain.bin"])
    for refused in ("/..", "/a/../b", "/.", "//", "/", "/a\\b", "/c:x", "/a/\x00b"):
        try:
            components(refused)
            check(f"{refused!r} is refused", False, "— it was accepted")
        except unsafe_name:
            check(f"{refused!r} is refused", True)

    with tempfile.TemporaryDirectory() as root:
        canonical = os.path.realpath(root)
        check("a path under outdir is inside it",
              inside(canonical, os.path.join(root, "a", "b")))
        check("outdir itself is not inside itself",
              not inside(canonical, root))
        check("a sibling of outdir is not inside it",
              not inside(canonical, os.path.join(root, "..", "sibling")))

    print(f"\nselftest: {len(FAILURES)} failure(s)")
    return 1 if FAILURES else 0


if __name__ == "__main__":
    raise SystemExit(main())
