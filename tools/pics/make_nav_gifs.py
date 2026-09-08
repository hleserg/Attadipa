#!/usr/bin/env python3
"""Rebuild the two navigation animations the README shows.

    cmake -S . -B build-sim -DATTADIPA_BUILD_SIMULATOR=ON && cmake --build build-sim -j
    python3 tools/pics/make_nav_gifs.py

Writes `pics/nav-honest-states.gif` and `pics/two-watches-one-codebase.gif`,
then prints the SHA-256 of each so `pics/README.md` can be brought up to date.

This exists because one step of it is not guessable. The screen is a painted
meadow, median-cut allocates its palette by pixel count, and the amber status
line is a few hundred pixels out of a hundred thousand -- so a plain adaptive
palette folds it into the foliage, and the animation loses the one line that
says what the watch does not know. Reserving the readout's colours is the whole
reason this is a script and not a line of ffmpeg. The assertion at the end of
`save()` fails the run rather than shipping a picture with the alarm colour
quantised away.
"""

import hashlib
import os
import pathlib
import subprocess
import sys
import tempfile

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
SIM = ROOT / "build-sim" / "sim" / "attadipa_sim"
PICS = ROOT / "pics"

# The six the README has always cycled through: one answer, then the five ways
# of not having one.
STATES = ["ready", "node-stale", "node-unavailable",
          "node-unknown", "no-fix", "waiting"]
# Frames 2..5 are the ones whose status line is drawn in the alarm colour. It
# is the STATUS row that turns warm and never the caveat under it: `caveat_` is
# `TextMuted` from build, and `NavFace::update()` recolours `status_` alone.
# Naming the wrong row here would send the next person looking for amber in a
# line that is sage green in all six frames.
FIRST_AMBER_FRAME = 2
HOLD_MS = 1600

# What the readout is made of: ivory numerals, the honey trail, the muted
# caveat line, the navigation teal, and the amber status row. Sampled off the
# renders, not off the token table -- these are the composited values.
UI = [(255, 246, 232), (255, 200, 87), (167, 180, 156),
      (111, 183, 181), (255, 138, 64)]
AMBER = (255, 138, 64)
# Roughly the meadow inside the ring, which is what every glyph is composited
# over; the ramp keeps antialiased edges from snapping to the nearest leaf.
GROUND = (10, 14, 10)
RESERVED = [tuple(int(round(GROUND[i] + (c[i] - GROUND[i]) * t)) for i in range(3))
            for c in UI for t in (1.0, 0.72, 0.45, 0.22)]


def render(board: str, state: str, into: pathlib.Path) -> pathlib.Path:
    into.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(SIM), "--board", board, "--nav", "--nav-state", state,
         "--theme", "night", "--locale", "en", "--frames", "4",
         "--screenshot", str(into)],
        check=True, capture_output=True,
        env={**os.environ, "SDL_VIDEODRIVER": "dummy"})
    return into


def save(frames, path: pathlib.Path) -> None:
    strip = Image.new("RGB", (frames[0].width * len(frames), frames[0].height))
    for i, f in enumerate(frames):
        strip.paste(f, (i * f.width, 0))
    adaptive = strip.quantize(colors=256 - len(RESERVED))
    entries = RESERVED + [tuple(adaptive.getpalette()[i * 3:i * 3 + 3])
                          for i in range(256 - len(RESERVED))]
    palette = Image.new("P", (1, 1))
    flat = [v for c in entries for v in c]
    palette.putpalette(flat + [0] * (768 - len(flat)))

    quantised = [f.quantize(palette=palette, dither=Image.NONE) for f in frames]
    quantised[0].save(path, save_all=True, append_images=quantised[1:],
                      duration=HOLD_MS, loop=0, optimize=True, disposal=1)

    written = Image.open(path)
    lost = []
    for i in range(written.n_frames):
        written.seek(i)
        # `getcolors` over a palette image is exact and cheap -- a GIF frame has
        # at most 256 distinct colours, so this is a scan of the palette rather
        # than of a third of a million pixels. It also keeps the only dependency
        # here to Pillow, which the simulator's own tooling already needs.
        present = written.convert("RGB").getcolors(1 << 16) or []
        near = any(sum(abs(c[j] - AMBER[j]) for j in range(3)) < 20
                   for _, c in present)
        if i >= FIRST_AMBER_FRAME and not near:
            lost.append(i)
    assert not lost, f"{path.name}: the alarm colour was quantised away in frames {lost}"
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    print(f"{path.relative_to(ROOT)}  {written.n_frames} frames  {digest}")


def main() -> int:
    if not SIM.exists():
        print(f"no simulator at {SIM}; build it first", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        ws = {s: render("waveshare-amoled-206", s, work / "ws" / f"{s}.png") for s in STATES}
        tw = {s: render("t-watch-s3-plus", s, work / "tw" / f"{s}.png") for s in STATES}

        # One panel, scaled to the width the README shows it at.
        save([Image.open(ws[s]).convert("RGB").resize((330, 404), Image.LANCZOS)
              for s in STATES], PICS / "nav-honest-states.gif")

        # Both panels on a plain ground, at one scale so the size difference is
        # the real one. 20 px margins, the small panel centred on the tall one.
        both = []
        for s in STATES:
            canvas = Image.new("RGB", (660, 501), (24, 27, 23))
            canvas.paste(Image.open(ws[s]).convert("RGB").resize((376, 461), Image.LANCZOS), (20, 20))
            canvas.paste(Image.open(tw[s]).convert("RGB").resize((220, 220), Image.LANCZOS), (420, 140))
            both.append(canvas)
        save(both, PICS / "two-watches-one-codebase.gif")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
