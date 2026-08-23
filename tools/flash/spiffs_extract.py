#!/usr/bin/env python3
"""Extract files from a SPIFFS image, without mkspiffs and without an ESP-IDF build.

Written because the alternative — `mkspiffs -u` — needs a toolchain nobody had
to hand, and because `strings` on a SPIFFS image recovers file *names* and no
file *bodies*: SPIFFS scatters a file's data across pages that are not
contiguous and not in order.

Geometry defaults match ESP-IDF's: 4096-byte erase blocks, 256-byte pages. Pass
them explicitly for an image built with anything else.

    python3 tools/flash/spiffs_extract.py storage.spiffs out/

Layout, from the SPIFFS sources — `pellepl/spiffs` at `ad902ca`, which is the
commit ESP-IDF's `components/spiffs/spiffs` submodule points at, so it is the
revision that wrote any image this repository will ever be handed:

- A block holds `block // page` pages. The first pages of each block are the
  **object lookup table**: one `u16` object id per page in the block, laid out
  across those pages back to back. `SPIFFS_OBJ_LOOKUP_PAGES` is
  `MAX(1, pages_per_block * 2 / page_size)`, floored.
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

**A page that exists is not a page that counts.** SPIFFS is log-structured:
nothing is overwritten in place, so a file that was edited or deleted leaves its
old pages sitting in flash with their `obj_id` and `span_ix` intact until the
block is erased. `spiffs_page_delete()` writes the lookup entry to
`SPIFFS_OBJ_ID_DELETED` and *then* clears two bits of the page header; it never
touches the id or the span. The first version of this script read the page
header alone, kept the last physical page it saw for each `obj_id/span_ix`, and
so could hand back a file assembled out of two different generations that passed
the length check — silent corruption, in a tool whose whole output is evidence.

So a page is used only when it is live by SPIFFS's own test, which is
`spiffs_obj_lu_find_id_and_span_v()` in `spiffs_nucleus.c` and is reproduced in
`is_live()` below: the block's **object lookup entry** must still name the page,
the page header's `obj_id` must agree with it, and the `flags` byte — every bit
of which is active *low*, because erased flash is `1` — must say used, not
deleted and finalised. An object index header carries one more condition,
`SPIFFS_PH_FLAG_IXDELE`, which the device clears before it starts unlinking the
object's pages.

Everything that could still make the answer ambiguous is refused rather than
guessed:

- two live pages claiming one `obj_id/span_ix`, or two live index headers for
  one object — the run is refused rather than resolved by physical order, which
  is not a recency;
- a gap in the spans, which the old `b"".join(...)` would have closed by
  sliding the later spans forward;
- a live index header whose name this parser cannot find, which means the
  layout is not the one documented above and a silent skip would be a partial
  answer dressed as a complete one.

The geometry is checked before any of that, against the per-block magic
(`SPIFFS_MAGIC`) that ESP-IDF writes by default. Wrong `--page`/`--block`, a
truncated dump or an image that is not SPIFFS stops the run with the reason
instead of producing a plausible-looking subset.

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
incomplete in the image and the rest was written; `2` something stopped it — the
geometry did not check out, an object in the image was ambiguous, a destination
was refused, a write failed, or the image holds nothing this parser recognises.
**`2` does not on its own mean the output directory is untouched**:
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

# --------------------------------------------------------------------------
# The on-flash constants, from `pellepl/spiffs` at `ad902ca` — the revision
# ESP-IDF's submodule pins. ESP-IDF's `spiffs_config.h` makes `spiffs_obj_id`,
# `spiffs_span_ix` and `spiffs_page_ix` all `u16_t`, which is where the 5-byte
# page header and the 2-byte lookup entry come from.
# --------------------------------------------------------------------------
OBJ_ID_SIZE = 2       # sizeof(spiffs_obj_id)
PAGE_HEADER = 5       # u16 obj_id, u16 span_ix, u8 flags

OBJ_ID_DELETED = 0x0000   # SPIFFS_OBJ_ID_DELETED
OBJ_ID_FREE = 0xFFFF      # SPIFFS_OBJ_ID_FREE
OBJ_ID_IX_FLAG = 0x8000   # SPIFFS_OBJ_ID_IX_FLAG

# Every one of these is active *low*: erased flash reads as 1, and SPIFFS marks
# a property by clearing its bit, which is the only direction a NOR flash write
# can go without an erase. So `USED` clear means the page is in use, and `DELET`
# clear means it is deleted. Reading them the other way round is the mistake
# this file exists to stop making.
PH_FLAG_USED = 1 << 0     # 0: written to.  1: clean
PH_FLAG_FINAL = 1 << 1    # 0: finalised.   1: under modification
PH_FLAG_INDEX = 1 << 2    # 0: index page.  1: data page
PH_FLAG_IXDELE = 1 << 6   # 0: this index header is being deleted
PH_FLAG_DELET = 1 << 7    # 0: deleted.     1: valid

# SPIFFS_MAGIC. The `^ page_size` is deliberate upstream: it makes an image
# built for one page size fail to mount under another, which is exactly the
# question `--page` asks and the reason this is worth reading.
MAGIC_BASE = 0x20140529

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


class UnsupportedImage(ValueError):
    """This image cannot be read under this geometry, so none of it is read.

    Raised before a single page is interpreted. The alternative — parsing on
    and returning whatever happened to look like a file — is the failure this
    tool must not have: a partial answer that reads exactly like a complete one.
    """


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


class Geometry(NamedTuple):
    """Everything about the image that has to be true before a page is read."""

    page: int
    block: int
    blocks: int
    pages_per_block: int
    lookup_pages: int
    max_entries: int          # SPIFFS_OBJ_LOOKUP_MAX_ENTRIES: data pages per block
    entries_per_page: int     # lookup entries in one lookup page


class Read(NamedTuple):
    """What one pass over an image found.

    Three lists rather than two, because the three mean different things to the
    caller. `problems` is the image being honestly incomplete — a file that
    declares more bytes than it holds — and the rest of the run still stands.
    `refusals` is this tool declining to guess: a page set that has more than one
    live answer, or none it can read. A refusal is not an incompleteness, and
    collapsing the two would let an ambiguous object leave under exit 1, which
    reads as "everything else is fine" and, for an object nobody can resolve,
    is not a thing anyone should be told.
    """

    files: list[dict]
    problems: list[str]
    refusals: list[str]
    census: list[str]         # the geometry and the page counts, for the summary


def geometry(size: int, page: int, block: int) -> Geometry:
    """Work out the layout, or refuse the image outright.

    Every arithmetic identity here is from `spiffs_nucleus.h`. They are checked
    rather than assumed because `--page` and `--block` are guesses until
    something confirms them, and a wrong guess does not fail loudly on its own:
    it silently reads the wrong five bytes out of every page and reports what it
    finds as a result.
    """
    if page < PAGE_HEADER + 1:
        raise UnsupportedImage(f"--page {page} cannot hold a page header and any data")
    if block <= 0 or block % page:
        raise UnsupportedImage(
            f"--block {block} is not a whole number of {page}-byte pages")
    pages_per_block = block // page
    # SPIFFS_OBJ_LOOKUP_PAGES: MAX(1, (pages_per_block * sizeof(obj_id)) / page),
    # C integer division, so floored. ESP-IDF's `spiffsgen.py` writes the same
    # image using ceil instead. For every power-of-two geometry — which is every
    # geometry either of them supports — the two agree, because the quotient is
    # then itself a power of two and is either exact or below one. Where they do
    # not agree the writer and the reader disagree about where the data starts,
    # and this tool has no business picking a winner.
    floored = max(1, (pages_per_block * OBJ_ID_SIZE) // page)
    ceiled = max(1, -(-pages_per_block * OBJ_ID_SIZE // page))
    if floored != ceiled:
        raise UnsupportedImage(
            f"page {page} / block {block} puts the object lookup table at "
            f"{floored} pages by the SPIFFS sources and {ceiled} by spiffsgen.py; "
            f"the two disagree, so the start of the data is unknown")
    if floored >= pages_per_block:
        raise UnsupportedImage(
            f"page {page} / block {block} leaves no room for data: the object "
            f"lookup table alone would take all {pages_per_block} pages")
    if size == 0:
        raise UnsupportedImage("the image is empty")
    if size % block:
        raise UnsupportedImage(
            f"{size} bytes is not a whole number of {block}-byte blocks — this "
            f"is a truncated dump, or --page/--block is wrong")
    return Geometry(page=page, block=block, blocks=size // block,
                    pages_per_block=pages_per_block, lookup_pages=floored,
                    max_entries=pages_per_block - floored,
                    entries_per_page=page // OBJ_ID_SIZE)


def confirm_geometry(image: bytes, geo: Geometry) -> str:
    """Check the per-block magic, and say what it settled.

    `SPIFFS_MAGIC` sits in the second-to-last object-id slot of a block's lookup
    area and is written when the block is formatted, so an ESP-IDF image has one
    in every block including the empty ones. It is a function of the page size
    and — under `SPIFFS_USE_MAGIC_LENGTH`, ESP-IDF's default — of the number of
    blocks, which makes it exactly a check on the two numbers this tool is
    guessing at.

    Absent is not wrong: `CONFIG_SPIFFS_USE_MAGIC` can be off, and then every
    slot is erased. Present and disagreeing *is* wrong, and stops the run.
    """
    # SPIFFS_CHECK_MAGIC_POSSIBLE: with enough entries the last lookup page
    # reaches into the magic's slot, and SPIFFS then does not write one.
    reach = (geo.max_entries % geo.entries_per_page) * OBJ_ID_SIZE
    if reach > geo.page - 2 * OBJ_ID_SIZE:
        return "not corroborated: this geometry leaves the magic no slot"

    found = [
        struct.unpack_from(
            "<H", image, bix * geo.block + geo.lookup_pages * geo.page - 2 * OBJ_ID_SIZE)[0]
        for bix in range(geo.blocks)
    ]
    variants = {
        "with SPIFFS_USE_MAGIC_LENGTH":
            lambda bix: (MAGIC_BASE ^ geo.page ^ (geo.blocks - bix)) & 0xFFFF,
        "without SPIFFS_USE_MAGIC_LENGTH":
            lambda bix: (MAGIC_BASE ^ geo.page) & 0xFFFF,
    }
    for label, expected in variants.items():
        # An erased slot abstains rather than disagreeing, so one unformatted
        # block at the end of a partition does not overturn the other 1535.
        against = [bix for bix, value in enumerate(found)
                   if value != OBJ_ID_FREE and value != expected(bix)]
        for_it = [bix for bix, value in enumerate(found) if value == expected(bix)]
        if for_it and not against:
            return f"confirmed by the block magic, {label}"

    if all(value == OBJ_ID_FREE for value in found):
        return "not corroborated: no block carries a magic value"

    bix = next(i for i, value in enumerate(found) if value != OBJ_ID_FREE)
    raise UnsupportedImage(
        f"the geometry does not check out: block {bix} carries 0x{found[bix]:04X} "
        f"where the SPIFFS magic belongs, and no supported setting produces that "
        f"for {geo.page}-byte pages over {geo.blocks} blocks "
        f"(0x{variants['with SPIFFS_USE_MAGIC_LENGTH'](bix):04X} or "
        f"0x{variants['without SPIFFS_USE_MAGIC_LENGTH'](bix):04X}). Either "
        f"--page/--block is wrong, or this is part of a partition rather than the "
        f"whole of one, or it is not a SPIFFS image at all")


def is_live(lookup_id: int, obj_id: int, span: int, flags: int) -> bool:
    """SPIFFS's own answer to "does this page still count?".

    `spiffs_obj_lu_find_id_and_span_v()` in `spiffs_nucleus.c`, which is the
    visitor every lookup-driven search in SPIFFS runs, and therefore the
    definition of live rather than an interpretation of it:

        if (ph.obj_id == obj_id &&
            ph.span_ix == *((spiffs_span_ix*)user_var_p) &&
            (ph.flags & (FINAL | DELET | USED)) == DELET &&
            !((obj_id & IX_FLAG) && (ph.flags & IXDELE) == 0 && ph.span_ix == 0))

    where `obj_id` is the **object lookup entry's** value and `ph` is the page's
    own header. The two agreeing is the whole check the first version of this
    tool did not do: a deleted page keeps its header id and loses its lookup
    entry, so the header alone cannot tell a live page from a discarded one.
    """
    if lookup_id in (OBJ_ID_FREE, OBJ_ID_DELETED):
        return False
    if obj_id != lookup_id:
        return False
    if flags & (PH_FLAG_FINAL | PH_FLAG_DELET | PH_FLAG_USED) != PH_FLAG_DELET:
        return False
    if lookup_id & OBJ_ID_IX_FLAG and span == 0 and not flags & PH_FLAG_IXDELE:
        return False
    return True


def _read_name(blob: bytes) -> tuple[str, int] | None:
    """Name and declared size out of an object index header page, or None.

    The `u32` size is five bytes before the name rather than four: the `u8`
    object type sits between them.
    """
    found = NAME_IN_PAGE.search(blob[PAGE_HEADER:80])
    if not found:
        return None
    at = PAGE_HEADER + found.start()
    return (found.group(0).rstrip(b"\x00").decode("ascii"),
            struct.unpack_from("<I", blob, at - 5)[0])


def extract(image: bytes, page: int, block: int) -> Read:
    """Read every live page of the image and reassemble the files they carry.

    Driven by the object lookup tables rather than by a walk over the pages,
    because the lookup table is what SPIFFS itself searches and a page it has
    released is not part of the filesystem however intact its header looks.
    """
    geo = geometry(len(image), page, block)
    census = [
        f"{geo.page} B pages, {geo.block} B blocks, {geo.blocks} blocks — "
        f"{confirm_geometry(image, geo)}"
    ]

    headers: dict[int, dict] = {}
    payload: dict[int, dict[int, bytes]] = {}
    refusals: list[str] = []
    ambiguous: set[int] = set()
    # `stale` is the count this whole exercise is about: pages the lookup table
    # has released that are still wearing an object id, which is what the
    # previous version of this tool read as data. `unusable` is everything else
    # that is labelled and not live — a write or a delete interrupted part-way,
    # or a lookup entry naming a different object than the page header does.
    counts = {"live": 0, "stale": 0, "unusable": 0, "free": 0}

    for bix in range(geo.blocks):
        for entry in range(geo.max_entries):
            lookup_at = (bix * geo.block
                         + (entry // geo.entries_per_page) * geo.page
                         + (entry % geo.entries_per_page) * OBJ_ID_SIZE)
            lookup_id = struct.unpack_from("<H", image, lookup_at)[0]
            at = (bix * geo.pages_per_block + geo.lookup_pages + entry) * geo.page
            obj_id, span, flags = struct.unpack_from("<HHB", image, at)

            if not is_live(lookup_id, obj_id, span, flags):
                if lookup_id == OBJ_ID_FREE:
                    counts["free"] += 1        # never allocated
                elif lookup_id == OBJ_ID_DELETED:
                    # Released. Only interesting while the page header still
                    # names an object, because that is what a header-only parser
                    # reads as data.
                    counts["stale" if obj_id not in (OBJ_ID_DELETED, OBJ_ID_FREE)
                           else "free"] += 1
                else:
                    # The lookup table claims this page and the page does not
                    # deliver: an allocation or a delete stopped part-way, or the
                    # two disagree about which object it belongs to.
                    counts["unusable"] += 1
                continue

            counts["live"] += 1
            bare = obj_id & ~OBJ_ID_IX_FLAG
            if obj_id & OBJ_ID_IX_FLAG:
                if span != 0:
                    continue  # an object index page; the data pages carry the bytes
                read = _read_name(image[at:at + geo.page])
                if read is None:
                    refusals.append(
                        f"object {bare:#06x}: a live object index header at page "
                        f"{at // geo.page} carries no name this parser can find, so "
                        f"the layout is not the one it knows")
                    ambiguous.add(bare)
                    continue
                # Two live headers that say the *same* thing are an interrupted
                # update and not an ambiguity: the reconstruction below reads
                # the data pages, not the page-index table, so either header
                # gives the same file. Only a disagreement has no answer.
                if bare in headers and headers[bare] != {"name": read[0], "size": read[1]}:
                    refusals.append(
                        f"object {bare:#06x}: two live object index headers, "
                        f"{headers[bare]['name']!r} and {read[0]!r} — which one the "
                        f"device would have opened is UNKNOWN")
                    ambiguous.add(bare)
                    continue
                headers[bare] = {"name": read[0], "size": read[1]}
            else:
                spans = payload.setdefault(bare, {})
                if span in spans:
                    refusals.append(
                        f"object {bare:#06x}: two live pages both claim span {span}, "
                        f"and physical order is not a recency — neither is used")
                    ambiguous.add(bare)
                    continue
                spans[span] = image[at + PAGE_HEADER:at + geo.page]

    census.append(
        f"{counts['live']} live, {counts['stale']} stale, "
        f"{counts['unusable']} unusable, {counts['free']} free")
    # Said out loud rather than left in a count, because it is the one line that
    # tells whoever is holding an earlier measurement whether it was taken over
    # a clean image or not.
    said = []
    if counts["stale"]:
        said.append(f"{counts['stale']} page(s) the object lookup table has released "
                    f"still carry an object id")
    if counts["unusable"]:
        said.append(f"{counts['unusable']} page(s) are labelled without being live")
    if said:
        census.append(" and ".join(said) + ". A parser that read the page headers "
                      "alone would have taken them for data")

    files, problems = [], []
    for obj_id, meta in sorted(headers.items()):
        if obj_id in ambiguous:
            continue  # already refused above, with the reason
        spans = payload.get(obj_id, {})
        if sorted(spans) != list(range(len(spans))):
            missing = sorted(set(range(max(spans) + 1)) - set(spans))
            refusals.append(
                f"{meta['name']}: span{'s' if len(missing) > 1 else ''} "
                f"{', '.join(str(s) for s in missing)} of {max(spans) + 1} "
                f"{'are' if len(missing) > 1 else 'is'} not in the image, and "
                f"closing the gap would move every later byte forward")
            continue
        body = b"".join(spans[s] for s in sorted(spans))
        if meta["size"] > len(body):
            problems.append(
                f"{meta['name']}: declares {meta['size']} bytes, only {len(body)} recovered"
            )
            continue
        files.append({"name": meta["name"], "size": meta["size"],
                      "pages": len(spans), "data": body[:meta["size"]]})

    orphans = sorted(set(payload) - set(headers) - ambiguous)
    for obj_id in orphans:
        problems.append(
            f"object {obj_id:#06x}: {len(payload[obj_id])} live data page(s) and no "
            f"live object index header, so the image holds bytes this parser cannot "
            f"name")

    return Read(files, problems, refusals, census)


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
    try:
        read = extract(image, args.page, args.block)
    except UnsupportedImage as why:
        # Before any page was interpreted, so there is nothing to report but the
        # reason, and no output directory has been created to report it into.
        print(f"UNSUPPORTED  {why}", file=sys.stderr)
        print("\n0 extracted, image not read")
        return 2
    files, problems, refusals = read.files, read.problems, list(read.refusals)

    for problem in problems:
        print(f"INCOMPLETE  {problem}", file=sys.stderr)
    for refusal in refusals:
        print(f"REFUSED  {refusal}", file=sys.stderr)

    def census() -> None:
        print(f"\ngeometry  {read.census[0]}")
        print(f"pages     {read.census[1]}")
        for line in read.census[2:]:
            print(f"note      {line}")

    def summary(extracted: int) -> None:
        census()
        print(f"{extracted} extracted, "
              f"{f'{len(refusals)} refused, ' if refusals else ''}"
              f"{len(problems)} incomplete")

    # Nothing recognised is not the same as nothing there, and neither is a
    # success. Reporting `0 extracted` and exit 0 for an image this parser did
    # not understand at all — the wrong partition, a littlefs image, a geometry
    # that does not match --page/--block — is the failure mode this whole file
    # is being rewritten to avoid: an output that reads like a result.
    if not files and not problems and not refusals:
        print("NOTHING RECOGNISED  no live SPIFFS object index header was found: an "
              "empty partition, the wrong --page/--block geometry, or not a "
              "SPIFFS image at all", file=sys.stderr)
        summary(0)
        return 2

    planned, refused_here = plan(files, args.outdir, args.force)
    for refusal in refused_here:
        print(f"REFUSED  {refusal}", file=sys.stderr)
    refusals += refused_here
    if refusals and not args.allow_partial:
        summary(0)
        return 2
    writes = planned

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
        census()
        print(f"0 extracted, {len(failure.kept)} left replaced, "
              f"{len(refusals)} refused, {len(problems)} incomplete")
        return 2

    summary(len(written))
    if refusals:
        return 2
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
