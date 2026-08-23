#!/usr/bin/env python3
"""Extract files from a SPIFFS image, without mkspiffs and without an ESP-IDF build.

Written because the alternative — `mkspiffs -u` — needs a toolchain nobody had
to hand, and because `strings` on a SPIFFS image recovers file *names* and no
file *bodies*: SPIFFS scatters a file's data across pages that are not
contiguous and not in order.

Geometry defaults match ESP-IDF's: 4096-byte erase blocks, 256-byte pages. Pass
them explicitly for an image built with anything else.

    python3 tools/flash/spiffs_extract.py storage.spiffs out/

Layout, from the SPIFFS sources:

- A block holds `block // page` pages. The first pages of each block are the
  object lookup table: one `u16` object id per page in the block, so the number
  of lookup pages is `ceil(pages_per_block * 2 / page_size)`.
- Every page begins with a 5-byte header — `obj_id` u16, `span_ix` u16,
  `flags` u8. An id with bit 15 set marks an *index* page; the rest are data.
- The index page with `span_ix == 0` is the object index header, and carries the
  file's size and name.

The offsets of that size and name differ between SPIFFS versions and between
`SPIFFS_OBJ_META_LEN` settings, so this does not hard-code them. It finds the
name as the first NUL-terminated printable run beginning with `/`, and reads the
size from the `u32` immediately preceding it — then checks the result against
the number of data-page bytes the object actually has. A file whose declared
size exceeds its recovered bytes is reported and not written, rather than
written short.

**A name off the device is not a path.** SPIFFS has no directories:
`/image/image1.bin` is one 17-character name that happens to contain slashes,
and nothing on the device stops a second name from being `/image_image1.bin`.
The first version of this script flattened slashes to underscores and opened the
result `"wb"`, so those two names were one file on disk, the second silently
replaced the first, and the summary reported both as extracted — data loss in a
tool whose entire output is evidence. So destinations are now worked out and
checked *as a set* before a single byte is written:

- the on-device hierarchy is kept rather than flattened, which is also the
  closest the output can get to what the device had;
- two names may never land on one path, and `/a` may not be a file when `/a/b`
  needs `a` to be a directory;
- nothing is written outside the canonical `outdir` — checked through
  `realpath`, so a symlinked directory in the way is caught rather than followed;
- nothing already there is overwritten. `--force` allows replacing a regular
  file and still refuses a symlink;
- a run that cannot give every name a safe destination of its own writes nothing
  at all, and a write that fails part-way removes what it created. `--allow-partial`
  writes the names that *were* safe instead — it still lists every refusal and
  still exits non-zero, because an image with one unusable name in it should not
  have to be all or nothing, and must never read as a clean run either.

Exit codes: `0` everything the image held was written; `1` at least one file was
incomplete in the image and the rest was written; `2` something stopped it — a
destination was refused, a write failed, or the image holds nothing this parser
recognises. **`2` does not on its own mean the output directory is untouched**:
a `--force` replacement that fails has already emptied the file it replaced, and
the `FAILED` line says so when it happens.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys
from typing import Callable, NamedTuple

NAME_IN_PAGE = re.compile(rb"/[\x20-\x7e]{1,63}\x00")

# A component has to mean "one directory entry with this name" on every host we
# might run on, not just on this one. `..` climbs out of outdir; a backslash is
# a separator on Windows and an ordinary character here; a colon there is a
# drive marker and an alternate-data-stream separator. Refusing all of them
# everywhere keeps one image extracting to one tree on every machine, which
# matters because this output is evidence rather than convenience.
FORBIDDEN_IN_COMPONENT = ("\\", ":", "\x00")

# POSIX refuses to open a symlink with this; Windows has no equivalent, so there
# the islink() checks in plan() stand alone. Neither guards an *intermediate*
# directory swapped for a symlink between plan() and the write — O_NOFOLLOW is
# about the final component, and _ensure_directory follows a link like anything
# else. This reads a vendor image on a workstation; it does not defend against
# somebody editing its output directory while it runs.
NOFOLLOW = getattr(os, "O_NOFOLLOW", 0)
# Windows only, and a no-op everywhere else.
BINARY = getattr(os, "O_BINARY", 0)


class UnsafeName(ValueError):
    """A SPIFFS name that cannot become a path inside outdir at all."""


class Write(NamedTuple):
    """One planned write: the file, where it goes, and what is already there."""

    entry: dict
    dest: str
    replaces: bool  # a regular file --force is allowed to overwrite


class Failure(NamedTuple):
    """Why the writing stopped, and what it left behind that cannot be undone.

    `kept` and `emptied` are the whole reason this is a record rather than a
    string. Everything this run *created* is removed on the way out, but a file
    `--force` replaced was somebody else's: the ones already written hold the
    extracted bytes, and the one that was emptied and then failed holds neither
    those nor what it used to. Reporting `0 extracted` over that, with no names,
    is the shape of defect this file exists to stop.
    """

    item: Write
    why: str
    kept: list[str]
    emptied: str | None


def extract(image: bytes, page: int, block: int) -> tuple[list[dict], list[str]]:
    pages_per_block = block // page
    lookup_pages = -(-pages_per_block * 2 // page)  # ceil

    headers: dict[int, dict] = {}
    payload: dict[int, dict[int, bytes]] = {}

    for index in range(len(image) // page):
        at = index * page
        obj_id, span, _flags = struct.unpack("<HHB", image[at:at + 5])
        if obj_id in (0x0000, 0xFFFF):
            continue
        if index % pages_per_block < lookup_pages:
            continue
        if obj_id & 0x8000:
            if span != 0:
                continue
            blob = image[at:at + page]
            found = NAME_IN_PAGE.search(blob[5:80])
            if not found:
                continue
            name_at = 5 + found.start()
            headers[obj_id & 0x7FFF] = {
                "name": found.group(0).rstrip(b"\x00").decode("ascii"),
                "size": struct.unpack("<I", blob[name_at - 5:name_at - 1])[0],
            }
        else:
            payload.setdefault(obj_id, {})[span] = image[at + 5:at + page]

    files, problems = [], []
    for obj_id, meta in sorted(headers.items()):
        spans = payload.get(obj_id, {})
        body = b"".join(spans[s] for s in sorted(spans))
        if meta["size"] > len(body):
            problems.append(
                f"{meta['name']}: declares {meta['size']} bytes, only {len(body)} recovered"
            )
            continue
        files.append({"name": meta["name"], "size": meta["size"],
                      "pages": len(spans), "data": body[:meta["size"]]})
    return files, problems


def components(name: str) -> list[str]:
    """Split an on-device name into path components, or refuse it.

    A leading slash and repeated slashes are dropped rather than refused —
    `/a//b` is the same file on the device as `/a/b` and the two collapsing onto
    one destination is caught by plan(), which is where a collision belongs.
    """
    parts = [part for part in name.split("/") if part]
    if not parts:
        raise UnsafeName("the name is nothing but separators")
    for part in parts:
        if part in (".", ".."):
            raise UnsafeName(f"component {part!r} would move the destination")
        for character in FORBIDDEN_IN_COMPONENT:
            if character in part:
                raise UnsafeName(
                    f"component {part!r} contains {character!r}, which is a "
                    f"separator or a drive marker on some host")
    return parts


def inside(root: str, path: str) -> bool:
    """Is `path` strictly inside the canonical `root`, symlinks resolved?

    realpath resolves the symlinks in the part of `path` that exists, which is
    the whole point: `outdir/image` being a link to `/etc` has to be caught
    here, because the open() that followed it would look perfectly ordinary.
    """
    resolved = os.path.realpath(path)
    try:
        return resolved != root and os.path.commonpath([root, resolved]) == root
    except ValueError:  # different drives on Windows; not inside by definition
        return False


def _under(relative: str, directories: set[str]) -> bool:
    """Is this path inside one of those directories?"""
    parent = os.path.dirname(relative)
    while parent:
        if parent in directories:
            return True
        parent = os.path.dirname(parent)
    return False


def plan(files: list[dict], outdir: str, force: bool = False) -> tuple[list[Write], list[str]]:
    """Give every file a destination inside outdir, or say why it cannot have one.

    Changes nothing on disk. Every refusal below is a way two names could have
    become one file, or one name could have become a file somewhere it was never
    meant to be, and all of them are found before the first byte is written.

    **A name that appears in the refusals never appears in the writes**, and that
    is the invariant rather than a nicety: the default aborts on any refusal, so
    a name in both lists cost nothing, and `--allow-partial` then acted on it —
    writing through a directory the same run had just refused as a symlink.
    Caught in review; hence `ambiguous` and `refused_directories` below, which
    drop everything under a refusal rather than only the entry that raised it.
    """
    root = os.path.realpath(outdir)
    resolved: list[tuple[dict, str]] = []
    refusals: list[str] = []
    claimed: dict[str, str] = {}
    ambiguous: set[str] = set()

    for entry in files:
        name = entry["name"]
        try:
            parts = components(name)
        except UnsafeName as why:
            refusals.append(f"{name}: {why}")
            continue
        relative = os.path.join(*parts)
        if relative in claimed:
            # Two objects under one name is not a corruption: a file deleted and
            # recreated on the device keeps its name and gets a new object id,
            # and this parser reads the stale object index header as well as the
            # live one. Which of the two is live is UNKNOWN here — nothing reads
            # the delete flag — so *neither* is written. Picking by object id
            # would be picking by an ordering that is not a recency, and writing
            # the deleted copy into evidence is worse than writing nothing.
            first = claimed[relative]
            if first == name:
                refusals.append(
                    f"{name}: two objects in this image carry this name, and which "
                    f"is live is UNKNOWN — neither is written")
            else:
                refusals.append(
                    f"{name}: would be written to the same path as {first} "
                    f"({relative})")
            ambiguous.add(relative)
            continue
        if not inside(root, os.path.join(outdir, relative)):
            refusals.append(f"{name}: {relative} resolves outside {root}")
            continue
        claimed[relative] = name
        resolved.append((entry, relative))

    # `/a` and `/a/b` are both ordinary SPIFFS names and only one of them can be
    # a file called `a`. No ordering makes that work, so it is a property of the
    # set rather than of either name, and is checked once the set is known.
    directories: dict[str, str] = {}
    for entry, relative in resolved:
        parent = os.path.dirname(relative)
        while parent:
            directories.setdefault(parent, entry["name"])
            parent = os.path.dirname(parent)

    refused_directories: set[str] = set()
    for relative, wanted_by in sorted(directories.items()):
        path = os.path.join(outdir, relative)
        if os.path.islink(path):
            refusals.append(
                f"{wanted_by}: {relative} is a symlink, and nothing is written "
                f"through one — where it points is not this tool's decision")
            refused_directories.add(relative)
        elif os.path.exists(path) and not os.path.isdir(path):
            refusals.append(f"{wanted_by}: {relative} is already a file, not a directory")
            refused_directories.add(relative)

    writes: list[Write] = []
    for entry, relative in resolved:
        name = entry["name"]
        if relative in ambiguous or _under(relative, refused_directories):
            # Already refused above, as the path itself or as something it is
            # inside. Saying it twice would be noise; writing it would be the
            # defect.
            continue
        if relative in directories:
            refusals.append(
                f"{name}: {directories[relative]} needs {relative} to be a directory")
            continue
        dest = os.path.join(outdir, relative)
        # islink first, and not exists: a dangling symlink is a link and is not
        # something that exists, and following it would create the file it
        # points at — outside outdir, if that is where it points.
        if os.path.islink(dest):
            refusals.append(f"{name}: {relative} is a symlink; it will not be written through")
            continue
        replaces = False
        if os.path.exists(dest):
            if os.path.isdir(dest):
                refusals.append(f"{name}: {relative} is a directory")
                continue
            if not force:
                refusals.append(
                    f"{name}: {relative} already exists — pass --force to replace it")
                continue
            replaces = True
        writes.append(Write(entry, dest, replaces))

    return writes, refusals


def _ensure_directory(path: str, made: list[str]) -> None:
    """makedirs, remembering what it actually created so a failure can undo it."""
    if not path or os.path.isdir(path):
        return
    parent = os.path.dirname(path)
    if parent and parent != path:
        _ensure_directory(parent, made)
    os.mkdir(path)
    made.append(path)


def _undo(action: Callable[[str], None], path: str) -> None:
    """Best effort. A rollback that raises on the way out reports the wrong fault."""
    try:
        action(path)
    except OSError:
        pass


def _shut(descriptor: int) -> None:
    """Close a descriptor os.fdopen never took ownership of."""
    try:
        os.close(descriptor)
    except OSError:
        pass


def write_all(writes: list[Write]) -> tuple[list[Write], Failure | None]:
    """Write every planned file, or undo what this run created and say why not.

    plan() has already refused everything that can be refused by looking. What
    is left is the filesystem saying no while the writing is under way — a full
    disk, a quota, a file-size limit — and one thing no amount of looking at
    paths can see: two destinations that are **one file** on the filesystem
    underneath, because it folds case or because somebody hard-linked them. That
    is what the `st_dev`/`st_ino` check below is for; `O_EXCL` catches it when
    nothing was there to begin with, and `--force` is exactly where `O_EXCL` is
    not available, which is also where the tool is at its most destructive.
    """
    made_files: list[str] = []
    made_dirs: list[str] = []
    done: list[Write] = []
    kept: list[str] = []  # replacements that completed: these hold the new bytes
    written_files: set[tuple[int, int]] = set()

    def rolled_back(item: Write, why: str, emptied: str | None) -> Failure:
        # Undo only what this run made. A file --force replaced belonged to
        # somebody else before this run and its old bytes are gone either way;
        # deleting it as well would turn a failed extraction into a deletion.
        # rmdir removes empty directories only, by definition.
        for path in reversed(made_files):
            _undo(os.unlink, path)
        for path in reversed(made_dirs):
            _undo(os.rmdir, path)
        return Failure(item, why, list(kept), emptied)

    for item in writes:
        try:
            _ensure_directory(os.path.dirname(item.dest), made_dirs)
            # No O_TRUNC even under --force: the file has to survive as far as
            # the identity check below, or a destination that turns out to be a
            # file this run already wrote would be emptied before anything
            # noticed. ftruncate does the emptying afterwards.
            flags = os.O_WRONLY | os.O_CREAT | NOFOLLOW | BINARY
            if not item.replaces:
                flags |= os.O_EXCL
            descriptor = os.open(item.dest, flags, 0o644)
        except OSError as why:
            return [], rolled_back(item, str(why), None)

        # The destination exists from this line on — O_CREAT made it — so it is
        # recorded *before* anything is written to it. A write that fails at the
        # flush inside close() would otherwise leave a truncated file behind
        # while the message said every file this run created had been removed:
        # the same shape as the defect this whole file was rewritten for, an
        # output that asserts the opposite of what is on disk. Caught in review.
        if not item.replaces:
            made_files.append(item.dest)

        emptied: str | None = None
        try:
            here = os.fstat(descriptor)
            if (here.st_dev, here.st_ino) in written_files:
                _shut(descriptor)
                return [], rolled_back(
                    item,
                    "this is the same file on disk as one already written in this "
                    "run — two names this filesystem cannot tell apart", None)
            written_files.add((here.st_dev, here.st_ino))
            if item.replaces:
                os.ftruncate(descriptor, 0)
                emptied = item.dest
        except OSError as why:
            _shut(descriptor)
            return [], rolled_back(item, str(why), emptied)

        try:
            with os.fdopen(descriptor, "wb") as handle:
                handle.write(item.entry["data"])
        except OSError as why:
            return [], rolled_back(item, str(why), emptied)
        if item.replaces:
            kept.append(item.dest)
        done.append(item)

    return done, None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("image")
    parser.add_argument("outdir")
    parser.add_argument("--page", type=int, default=256)
    parser.add_argument("--block", type=int, default=4096)
    parser.add_argument("--force", action="store_true",
                        help="replace a regular file already at a destination; a "
                             "symlink or a directory is still refused")
    parser.add_argument("--allow-partial", action="store_true",
                        help="write the names that were not refused instead of "
                             "nothing. The refusals are still listed and the exit "
                             "code stays non-zero, so this can never read as a "
                             "clean run — it is for an image that holds one "
                             "unusable name and five good ones")
    args = parser.parse_args()

    with open(args.image, "rb") as handle:
        image = handle.read()
    files, problems = extract(image, args.page, args.block)

    for problem in problems:
        print(f"INCOMPLETE  {problem}", file=sys.stderr)

    # Nothing recognised is not the same as nothing there, and neither is a
    # success. Reporting `0 extracted` and exit 0 for an image this parser did
    # not understand at all — the wrong partition, a littlefs image, a geometry
    # that does not match --page/--block — is the failure mode this whole file
    # is being rewritten to avoid: an output that reads like a result.
    if not files and not problems:
        print("NOTHING RECOGNISED  no SPIFFS object index header was found: an "
              "empty partition, the wrong --page/--block geometry, or not a "
              "SPIFFS image at all", file=sys.stderr)
        print("\n0 extracted, 0 incomplete")
        return 2

    writes, refusals = plan(files, args.outdir, args.force)
    for refusal in refusals:
        print(f"REFUSED  {refusal}", file=sys.stderr)
    if refusals and not args.allow_partial:
        print(f"\n0 extracted, {len(refusals)} refused, {len(problems)} incomplete")
        return 2

    written, failure = write_all(writes)
    for item in written:
        entry = item.entry
        print(f"{entry['name']:<28} {entry['size']:>9} bytes  "
              f"{entry['pages']:>5} pages  -> {item.dest}"
              f"{'  (replaced)' if item.replaces else ''}")
    if failure is not None:
        print(f"FAILED  {failure.item.entry['name']} -> {failure.item.dest}: "
              f"{failure.why}", file=sys.stderr)
        print("        every file this run created has been removed", file=sys.stderr)
        for path in failure.kept:
            print(f"        {path} was replaced under --force and now holds the "
                  f"extracted bytes — its old contents cannot be put back",
                  file=sys.stderr)
        if failure.emptied is not None:
            print(f"        {failure.emptied} was emptied under --force and holds "
                  f"neither its old contents nor the new ones", file=sys.stderr)
        print(f"\n0 extracted, {len(failure.kept)} left replaced, "
              f"{len(refusals)} refused, {len(problems)} incomplete")
        return 2

    refused = f", {len(refusals)} refused" if refusals else ""
    print(f"\n{len(written)} extracted{refused}, {len(problems)} incomplete")
    if refusals:
        return 2
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
