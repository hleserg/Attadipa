# Brand assets

Supplied by the project owner on 2026-08-21, for publication. Unlike
[`../docs/ui/reference/`](../docs/ui/reference/README.md) — which holds design
*inputs* that the UI answers to — these are finished marks meant to be shown as
they are.

| File | Format | SHA-256 | Intended use |
|---|---|---|---|
| `atta-dipa-banner.png` | PNG, 1774 × 887, RGB (no alpha) | `e905896b19a15dd2b24774840767e912261c6fd1f91f330f4063c8016cf9d05d` | repository banner — used by both [`../README.md`](../README.md) and [`../README.ru.md`](../README.ru.md) |
| `Ikon.png` | PNG, 1254 × 1254, **RGBA** | `bdb9cf99275711c24039e836737ec95243bfd91791e3058f8cc0f9d9efec966b` | application / launcher icon — **not yet used** |
| `Favicon.png` | PNG, 1254 × 1254, **RGBA** | `bdb9cf99275711c24039e836737ec95243bfd91791e3058f8cc0f9d9efec966b` | site favicon — **not yet used** |

## What the banner establishes

- **A motto: `INDEPENDENT BY DESIGN`.** It expresses the meaning of *attadīpa*:
  useful core capabilities should work locally, without a mandatory phone,
  cloud, or Internet connection.
- **The mark.** Lumar, a firefly seen from above — olive head and thorax, two antennae,
  two orange-red wings, and an amber abdomen with a real glow behind it. The
  glow is the only light source in the composition, which is worth knowing
  before anybody re-draws it smaller.
- **The wordmark.** "Atta-dipa" in orange, in a rounded
  geometric sans consistent with the Nunito Sans direction in
  [`../docs/ui/DESIGN_SYSTEM.md`](../docs/ui/DESIGN_SYSTEM.md). The typeface has
  **not** been identified from the file and no font here is pinned.

`Ikon.png` and `Favicon.png` are the same mark on a rounded square. They differ
only in the corner radius and the crop, and in the background tint — `#FDF0DF`
against `#FEF6EC`.

## Two questions the owner has now answered

**A8 — the corners were opaque black, not transparent.** `Ikon.png` and
`Favicon.png` were RGB with no alpha channel, so the area outside the rounded
square was `#000000` — invisible on the banner, which is full-bleed, but a
black box on any non-black page or launcher background. The owner said yes on
[issue #57](https://github.com/hleserg/Attadipa/issues/57) (2026-08-22), and
both files were re-exported with an alpha channel: the area outside the
rounded square is now transparent, the pixels inside it are unchanged. See
OD-15 in [`../docs/research/OWNER_DECISIONS.md`](../docs/research/OWNER_DECISIONS.md).

**A7 — the inks were not the canonical palette.** The art sampled deeper and
redder than Attadipa Orange `#FF8A40` and lighter and greener than Ink Olive
`#2F3A2E`. The owner resolved this in favour of the canonical palette (final
§42) — the values already in use across the design system and the firmware —
on the same issue. The sampled hex values are retired; the resolution and the
sampled values that lost are recorded in OD-15 rather than kept here beside
the canon they disagree with.


## Captures, not brand assets

The files below are **rendered by the desktop simulator**, captured 2026-09-07
from `build-sim/sim/attadipa_sim` at the commit that added them. `clock-night.gif`
was stitched with `ffmpeg` on one global palette; the two navigation files are
built by [`../tools/pics/make_nav_gifs.py`](../tools/pics/make_nav_gifs.py),
which reserves the readout's five colours before quantising — see the note under
the table. They are pictures of the application code, not of a board:
**nothing here is evidence about hardware**, and none of them may be cited as
`MEASURED`. The physical evidence
in the README is [`../docs/hardware/CLOCK_2026-08-26.png`](../docs/hardware/CLOCK_2026-08-26.md)
and `first-boot-waveshare.gif`, both taken off the Waveshare.

| File | What it shows | SHA-256 | Used by |
|---|---|---|---|
| `nav-honest-states.gif` | GIF, the navigation readout stepping through `ready`, `node-stale`, `node-unavailable`, `node-unknown`, `no-fix`, `waiting` | `f8a597a26bdb63c631dbf8aa56de7cdded1986854c70e4ceaf211f25861477d8` | Not used by current pages; retained for history |
| `clock-night.gif` | GIF, the Clock in the night theme, 10 s at 4 fps — the fireflies pulse and the minute turns over | `97ed8058a93e4aee373a7c6c8e539077db844936530345c1c6597e4f28c861fb` | Not used by current pages; retained for history |
| `two-watches-one-codebase.gif` | GIF, the same six states side by side on both panels, composited from two captures onto a plain ground | `39a46096b17e36e0268fb36648a1cff4f6f590165e140405468dfde2f78dc574` | Not used by current pages; retained for history |

Regenerate the two navigation files with
`python3 tools/pics/make_nav_gifs.py`, which renders the six states on both
boards, writes both GIFs and prints the SHA-256 to paste into the table above.
It is a script rather than an `ffmpeg` line for one reason: the screen is a
painted meadow, median-cut allocates its palette by pixel count, and the amber
status row is a few hundred pixels in a hundred thousand — so an ordinary adaptive
palette folds the one line that says what the watch does not know into the
foliage. The script reserves the readout's colours and then asserts the amber
survived, so a regeneration that loses it fails instead of shipping. Regenerate
`clock-night.gif` by running the simulator with the flags the README documents
and re-encoding; its frames are not kept.

`first-boot-waveshare.gif` predates this table: it is a **physical** capture of
the Waveshare's first boot from flash, and it is the one moving image here that
is evidence about a board.

## Browser design study captures

Captured **2026-09-09** from [Design Study 01 / V2](../docs/ui/prototype/index.html).
These twelve PNGs are **browser prototypes with sample data**, not LVGL simulator
captures, firmware acceptance or physical evidence. Both README languages use
their matching locale. No image was repainted, retouched or rescaled.

Source checkout: `4fdf3ae9b24242b7c507e7ddb9a4b2d19ad0c7db`.
The source page last changed in `70145a6144441bfd252dacc3fdc7da9e11aae79c`.
Capture environment: agent-browser 0.36.0, HeadlessChrome 151.0.0.0 on Linux,
1400 × 1400 viewport, device pixel ratio 1, bundled Nunito loaded, motion off.
The filenames identify locale, theme and screen; `small` means 240 × 240.
Clock, Navigation and Mesh use `ready`; Time uses Setup `time`, first field
(Day, 1/6, value 07). No real contact, location or device identifier was used.

### Reproduce

1. At the source commit, run `python3 -m http.server 8481 --bind 127.0.0.1 --directory docs/ui`.
2. Open `http://127.0.0.1:8481/prototype/?size=large&theme=night&locale=en&motion=off` in a 1400 × 1400 browser viewport at device scale 1. Change `size`, `theme` and `locale` to match the filename. Time is `large/day`; small Clock and Mesh are `small/day`.
3. For Time, choose `time` in the Setup scenario select. Wait for `document.fonts.ready` and verify the painted CSS background has loaded.
4. Capture only `#screen-clock`, `#screen-nav`, `#screen-mesh` or `#screen-setup` at its original dimensions. Other gallery articles may be hidden to bring the target into view; do not change the screen styles. With agent-browser, settle the page using `window.scrollTo({top:0,left:0,behavior:"instant"})` before `screenshot '#screen-clock' output.png`.
5. Inspect the resulting PNG, not just its dimensions: the whole screen must be present, with no review-page heading inside the crop. Confirm locale, state and disabled motion. Browser rasterisation differences can change hashes.

| File | Dimensions | SHA-256 |
|---|---|---|
| `design-clock-day-small-en.png` | 240 × 240 | `ef63056bade670a368e1ab6c84e714f7c963ce6f4c24fd8406e1f0255fd50409` |
| `design-clock-day-small-ru.png` | 240 × 240 | `bd177290afc35aef2a17900697d96023a4ffa8e1b98ef96b407fb96b5a49bc28` |
| `design-clock-night-en.png` | 410 × 502 | `76dcd09ad75a1b66dad2861d93ee1dd80145ffd34f809d34816ff2ee3a335b3d` |
| `design-clock-night-ru.png` | 410 × 502 | `f7924eb7ccb1870a6929c4ad66b6f0de4eb39b1c21368f5548a869de5119dfbb` |
| `design-mesh-day-small-en.png` | 240 × 240 | `3360c66f375ce005e7cd62b59a51ece6f5fa9e2df9362d02877bac444b668acc` |
| `design-mesh-day-small-ru.png` | 240 × 240 | `ea570b657ff97f9b21f5acbfab6b179781b85cf6e0bf9d51a38b618e7dbbb12a` |
| `design-mesh-night-en.png` | 410 × 502 | `7a86c3a4f128fa26984552247739bd30e629ea6f0ce98692b6706b09547163d0` |
| `design-mesh-night-ru.png` | 410 × 502 | `59dd4d666e7a0a8c25b1ee01b320f8cd9a15851b84774acffe3dd2c5774cd215` |
| `design-navigation-night-en.png` | 410 × 502 | `b6ba081a4566c92b55e424019a6eac28f15bd3b9d082a06059f6d502bf842eff` |
| `design-navigation-night-ru.png` | 410 × 502 | `8cab85af4a1f0f4da625adec6fc51e9e9abddf9c1f3e1fe5ba738909996c8c93` |
| `design-time-day-en.png` | 410 × 502 | `51d42198c2ae83a5cf5f2651e65b1d2aa1e05496604772fef50d5834f0279549` |
| `design-time-day-ru.png` | 410 × 502 | `00d79d059dc6569e8e50a66c51e87ae091c723d311ae6e79660078f3e4da58a0` |
