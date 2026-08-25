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
