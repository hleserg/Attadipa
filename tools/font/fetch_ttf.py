#!/usr/bin/env python3
"""Fetch Montserrat-Medium.ttf from the pinned LVGL commit, and verify it.

`generate_ui_fonts.py` takes the TTF from whatever LVGL tree a simulator build
fetched. That is the right default for a developer who already has one, and a
bad deal for a CI job that wants nothing else from LVGL: the clone
`cmake/AttadipaLvgl.cmake` describes is MEASURED at 350 MiB and 22.8 s on a cold
runner, for one 243 kB file.

So this downloads the single file at the commit that file is pinned to. Two
properties make that safe rather than a shortcut:

* **The commit comes out of `cmake/AttadipaLvgl.cmake`,** parsed rather than
  copied. One pin, one place. A URL with the SHA typed into a workflow is a
  second pin that drifts silently the first time the real one moves.
* **The bytes are checked against `TTF_SHA256`,** the same constant the
  generator refuses to run without. A downloaded file that hashes to anything
  else is not written to disk at all — this exits non-zero instead, because a
  half-trusted font is how a subset ends up containing glyphs nobody reviewed.

  python3 tools/font/fetch_ttf.py --out /tmp/Montserrat-Medium.ttf
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_ui_fonts as fonts  # noqa: E402

PIN_FILE = ROOT / "cmake" / "AttadipaLvgl.cmake"
PIN = re.compile(r'set\(ATTADIPA_LVGL_COMMIT\s+"([0-9a-f]{40})"')
RAW = "https://raw.githubusercontent.com/lvgl/lvgl/{commit}/{path}"

TIMEOUT_SECONDS = 60


def pinned_commit() -> str:
    text = PIN_FILE.read_text(encoding="utf-8")
    match = PIN.search(text)
    if not match:
        raise SystemExit(
            f"no ATTADIPA_LVGL_COMMIT found in {PIN_FILE.relative_to(ROOT)}. The pin "
            f"has moved or changed shape; this script reads it rather than keeping a "
            f"second copy, so it has to be taught the new one.")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="where to write the TTF")
    arguments = parser.parse_args()

    commit = pinned_commit()
    url = RAW.format(commit=commit, path=fonts.TTF_IN_LVGL.as_posix())
    print(f"fetching {url}")
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response:
            payload = response.read()
    except (urllib.error.URLError, TimeoutError) as exc:
        raise SystemExit(f"could not fetch {url}: {exc}")

    digest = hashlib.sha256(payload).hexdigest()
    if digest != fonts.TTF_SHA256:
        raise SystemExit(
            f"{url}\nhashes {digest}\nexpected {fonts.TTF_SHA256}\n"
            f"Nothing was written. Either the commit pin no longer contains the font "
            f"this pipeline was built from, or the download is not what it claims.")

    destination = Path(arguments.out)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)
    print(f"{destination}  {len(payload)} bytes  sha256 {digest}  (LVGL {commit[:9]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
