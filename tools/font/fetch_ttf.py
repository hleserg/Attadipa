#!/usr/bin/env python3
"""Fetch Nunito Sans from the pinned Google Fonts commit, and verify it.

The commit, path and SHA-256 all come from `generate_ui_fonts.py`: one pin, one
place. A downloaded file that differs is never written.

  python3 tools/font/fetch_ttf.py --out /tmp/NunitoSans.ttf
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import generate_ui_fonts as fonts  # noqa: E402

RAW = "https://raw.githubusercontent.com/google/fonts/{commit}/{path}"

TIMEOUT_SECONDS = 60


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True, help="where to write the TTF")
    arguments = parser.parse_args()

    url = RAW.format(commit=fonts.TTF_COMMIT, path=fonts.TTF_PATH.as_posix())
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
    print(f"{destination}  {len(payload)} bytes  sha256 {digest}  "
          f"(google/fonts {fonts.TTF_COMMIT[:9]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
