# Brand assets

Supplied by the project owner on 2026-08-21, for publication. Unlike
[`../docs/ui/reference/`](../docs/ui/reference/README.md) — which holds design
*inputs* that the UI answers to — these are finished marks meant to be shown as
they are.

| File | Format | SHA-256 | Intended use |
|---|---|---|---|
| `AttadipaBanner.png` | PNG, 1788 × 894, RGB (no alpha) | `ad7c60d4bb0f808265ecb8a6f17704af054fbd2098276774ab7cc7d2a9e473a3` | repository banner — in use at the top of [`../README.md`](../README.md) |
| `Ikon.png` | PNG, 1254 × 1254, **RGBA** | `46f3b9cf4bfd931f37635f473dd064212ad46ff79179c958dced24ebe78a76cb` | application / launcher icon — **not yet used** |
| `Favicon.png` | PNG, 1254 × 1254, **RGBA** | `f24025e4ae7c68533af964fc1dad9ac0bb2d29063653dcff3da811c552f22891` | site favicon — **not yet used** |

## What the banner establishes

- **A motto: `INDEPENDENT BY DESIGN`.** It expresses the meaning of *attadīpa*:
  useful core capabilities should work locally, without a mandatory phone,
  cloud, or Internet connection.
- **The mark.** Lumar, a firefly seen from above — olive head and thorax, two antennae,
  two orange-red wings, and an amber abdomen with a real glow behind it. The
  glow is the only light source in the composition, which is worth knowing
  before anybody re-draws it smaller.
- **The wordmark.** "Attadipa" in orange, in a rounded
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
OD-12 in [`../docs/research/OWNER_DECISIONS.md`](../docs/research/OWNER_DECISIONS.md).

**A7 — the inks were not the canonical palette.** The art sampled deeper and
redder than Attadipa Orange `#FF8A40` and lighter and greener than Ink Olive
`#2F3A2E`. The owner resolved this in favour of the canonical palette (final
§42) — the values already in use across the design system and the firmware —
on the same issue. The sampled hex values are retired; the resolution and the
sampled values that lost are recorded in OD-12 rather than kept here beside
the canon they disagree with.
