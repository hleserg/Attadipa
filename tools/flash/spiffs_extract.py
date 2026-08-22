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
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys

NAME_IN_PAGE = re.compile(rb"/[\x20-\x7e]{1,63}\x00")


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("image")
    parser.add_argument("outdir")
    parser.add_argument("--page", type=int, default=256)
    parser.add_argument("--block", type=int, default=4096)
    args = parser.parse_args()

    with open(args.image, "rb") as handle:
        image = handle.read()
    files, problems = extract(image, args.page, args.block)

    os.makedirs(args.outdir, exist_ok=True)
    for entry in files:
        # Flatten: SPIFFS has no directories, only names that contain slashes.
        path = os.path.join(args.outdir, entry["name"].lstrip("/").replace("/", "_"))
        with open(path, "wb") as handle:
            handle.write(entry["data"])
        print(f"{entry['name']:<28} {entry['size']:>9} bytes  "
              f"{entry['pages']:>5} pages  -> {path}")

    for problem in problems:
        print(f"INCOMPLETE  {problem}", file=sys.stderr)
    print(f"\n{len(files)} extracted, {len(problems)} incomplete")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
