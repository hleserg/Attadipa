# Brand assets

Supplied by the project owner on 2026-08-21, for publication. Unlike
[`../docs/ui/reference/`](../docs/ui/reference/README.md) — which holds design
*inputs* that the UI answers to — these are finished marks meant to be shown as
they are.

| File | Format | SHA-256 | Intended use |
|---|---|---|---|
| `AttadipaBanner.png` | PNG, 1788 × 894, RGB (no alpha) | regenerated for Attadipa | repository banner — in use at the top of [`../README.md`](../README.md) |
| `Ikon.png` | PNG, 1254 × 1254, RGB (no alpha) | `6e1fa8e735eff4aa2008c6dd01403adf2bcf20a9e01e65af6f3cbefc66969676` | application / launcher icon — **not yet used** |
| `Favicon.png` | PNG, 1254 × 1254, RGB (no alpha) | `9605119b0a297352004ca18c918c5336cdf256e759d3966af1e178e7d2debfac` | site favicon — **not yet used** |

## What the banner establishes

- **A tagline: `GLOW · GUIDE · CONNECT`.** It did not exist before this file and
  is not in the master prompt. Three words that happen to name the three things
  the product claims to be: a light, a navigator, and a mesh.
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

## Two things a later reader needs to know

**The corners are opaque black, not transparent.** All three files are RGB with
no alpha channel, so the area outside the rounded square in `Ikon.png` and
`Favicon.png` is `#000000`. On the banner that is invisible, because the banner
is full-bleed. On an icon it is not: a favicon on any non-black page, and a
launcher icon on any non-black background, will show black corners. Converting
them is a five-line job, but it changes the owner's art, so it has not been
done — see A8 in
[`../docs/research/OPEN_QUESTIONS.md`](../docs/research/OPEN_QUESTIONS.md).

**The inks are not the canonical palette.** Sampled from the art — approximate,
because everything here is gradient-filled and carries a paper texture:

| Role | Sampled from the art | Canonical token (final §42) | Same? |
|---|---|---|---|
| wordmark / wings | `#E16439` … `#EC552A` | Attadipa Orange `#FF8A40` | **no** — the art is deeper and redder |
| glow | `#FECD5C`, `#FDBC29` | Glow Amber `#FFC857` | close |
| head, thorax, tagline | `#595E3A` … `#666A46` | Ink Olive `#2F3A2E` | **no** — the art is much lighter |
| hills, leaves | `#9BB4A7`, `#BBC7B6` | Leaf Sage `#A7B49C` / Meadow Green `#6FA07A` | between the two |
| background | `#FAEEE0`, `#FDF0DF`, `#FEF6EC` | Warm Ivory `#FFF6E8` | close, but three different values across three files |

This is a real conflict and it is recorded rather than smoothed over: the
design system says one thing and the published mark says another. It is an
identity decision, so it belongs to the owner — A7 in
[`../docs/research/OPEN_QUESTIONS.md`](../docs/research/OPEN_QUESTIONS.md).
Until it is answered, `DESIGN_SYSTEM.md` keeps the §42 values and this file
keeps the sampled ones, and neither pretends to be the other.
