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
from `build-sim/sim/attadipa_sim` at the commit that added them, and stitched
into GIFs with `ffmpeg` (a global palette, no dithering). They are pictures of
the application code, not of a board: **nothing here is evidence about
hardware**, and none of them may be cited as `MEASURED`. The physical evidence
in the README is [`../docs/hardware/CLOCK_2026-08-26.png`](../docs/hardware/CLOCK_2026-08-26.md)
and `first-boot-waveshare.gif`, both taken off the Waveshare.

| File | What it shows | SHA-256 | Used by |
|---|---|---|---|
| `nav-honest-states.gif` | GIF, the navigation readout stepping through `ready`, `node-stale`, `node-unavailable`, `node-unknown`, `no-fix`, `waiting` | `63862dff2644137795b0b1022b7b73ed5b83ed36957cc2d9facdc46069ae5d31` | [`../README.md`](../README.md) and [`../README.ru.md`](../README.ru.md) |
| `clock-night.gif` | GIF, the Clock in the night theme, 10 s at 4 fps — the fireflies pulse and the minute turns over | `97ed8058a93e4aee373a7c6c8e539077db844936530345c1c6597e4f28c861fb` | [`../README.md`](../README.md) and [`../README.ru.md`](../README.ru.md) |
| `two-watches-one-codebase.gif` | GIF, the same six states side by side on both panels, composited from two captures onto a plain ground | `c31edbc50ee911821d41802fa0bf50b3e7f3573e9c833b244e441c653a68ea1d` | [`../README.md`](../README.md) and [`../README.ru.md`](../README.ru.md) |

Regenerate any of them by running the simulator with the flags the README
documents and re-encoding; the frames are not kept.

`first-boot-waveshare.gif` predates this table: it is a **physical** capture of
the Waveshare's first boot from flash, and it is the one moving image here that
is evidence about a board.
