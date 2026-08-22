# Owner-provided visual references

These three files are **owner-provided project art**. They arrived with the
final master specification
([`docs/master-prompt-final.md`](../../master-prompt-final.md), preamble and
§40) and are named there as *canonical visual references* — not mood-board
decoration.

They are **canonical design references**. Update them only through an explicit
brand decision; do not silently "clean up" or redraw them. Derived and cleaned
artwork belongs in `ui/assets/source/`; generated target assets belong in
`ui/assets/generated/`; the scripts that produce them belong in `tools/assets/`
(§45). Nothing in this directory is ever compiled into firmware — a 1448×1086
PNG is a desktop concept sheet, not a watch asset.

| File | SHA-256 | Size | Pixels |
|---|---|---|---|
| `attadipa_brand_identity.png` | `9c5c93a701347163724905a543e502f8df7280b0733c1dc50b4660d50023c54b` | regenerated | 1440 × 1086 |
| `attadipa_visual_style_board.png` | `02703056cd84661560f58c51d661fd7a45790ace322954efd5689ea951860374` | regenerated | 1440 × 1086 |
| `lumar_mascot_sheet.png` | `34e480e9b5b0a91525dc360c4ce6a6aa105d3e8cba6c6942d79e3903af92372c` | regenerated | 1440 × 1086 |

The hashes are here so that a later "I cleaned up the source art" commit is
visible as one. Verify with `sha256sum docs/ui/reference/*.png`.

## What each one carries

**`attadipa_brand_identity.png`** — the wordmark *Attadipa*, the motto
`INDEPENDENT BY DESIGN`, the mascot, an icon mark, an app icon, and horizontal
and stacked lockups. Its palette is labelled Honey / Apricot / Warm Coral /
Sage / Warm Teal / Cream / Dark Olive.

**`attadipa_visual_style_board.png`** — the fuller system: a ten-swatch palette,
a typography specimen (Nunito Sans, Inter), an icon-style row, tone and values,
day and night mockups of six screens, five design principles, and a component
row (primary/secondary button, toggle, slider).

**`lumar_mascot_sheet.png`** — the mascot in a hero pose plus four named
states, which are the ones the UI is expected to use:

| Pose | Named on the sheet | Where it belongs |
|---|---|---|
| 1 | `NEUTRAL` | onboarding, About, idle empty states |
| 2 | `GUIDING / NAVIGATION` | Navigator, "no fix yet", arrival |
| 3 | `MESSAGE RECEIVED` | mesh delivery success, unread |
| 4 | `THINKING / EXPLORING` | scanning, pairing, searching, "working on it" |

Four value cards below them — `NAVIGATE`, `MESSAGE`, `WEARABLE`, `CONNECT` —
name the product's four pillars in the owner's own words.

The mascot is six-legged and insect-bodied, wears round glasses, and glows at
the antenna tips and abdomen. §40 is explicit that it must stay that way: *"Do
not add human clothes/body proportions that turn it into a person with wings."*

## What these images are not

§41 of the specification is blunt about this, and it is worth repeating where
the files actually live, because this is where somebody will come looking for a
spec:

> **Concept art is not a hardware spec.**

The style board shows a **heart-rate card**. Neither target board has a
heart-rate sensor, and shipping that card would violate §97 ("do not ship fake
features"). It shows **`attadipa.org`** — no such domain is known to exist. It
shows sample names, sample messages, an example navigation distance, Wi-Fi and
Bluetooth statuses and sample dates. All of it is mock visual content. None of
it is a product fact, a default value, or a promise.

The mockups are also drawn on a **round** watchface. Neither target is round:
the T-Watch S3 Plus is 240 × 240 square and the Waveshare AMOLED 2.06 is
410 × 502 ([HARDWARE_MATRIX](../../research/HARDWARE_MATRIX.md)). Compositions
must be re-derived for the real geometries rather than letter-boxed from these.

What *is* binding here: visual language, composition, palette direction,
mascot, wordmark, icon language, and the warmth. Product functionality comes
from the specification and from verified hardware.

## The two palettes

The two boards give different hex values for similarly-named colours. §42
settles it rather than leaving it open: the ten-swatch style-board palette is
the canonical starting point, the brand-identity swatches are *close variants*,
and *"do not treat minor raster-board differences as sacred."* Both are recorded
as source data in [`../DESIGN_SYSTEM.md`](../DESIGN_SYSTEM.md), and neither is
used as a literal in UI code — tokens are, and the token values are settled by
contrast testing on the real panels, not by picking a favourite swatch (§55).

The hex values recorded in `DESIGN_SYSTEM.md` are taken from §42 of the
specification, which is text, rather than sampled from these PNGs, which are
raster. Where the two disagree by a digit, the text wins.

## And the brand assets, which are a different thing

[`../../pics/`](../../../pics/README.md) arrived later the same day and is not
part of this directory, because it is not a reference. Those files are finished
marks — a banner, an application icon, a favicon — meant to be published as
they are, and the banner is now at the top of the repository
[`README`](../../../README.md).

They matter here for two reasons. They establish the motto
**`INDEPENDENT BY DESIGN`** and they carried inks that were *not* the §42
palette. The wordmark orange sampled far enough from Attadipa Orange `#FF8A40`
that the rule above — sources disagreeing by a digit — never settled it, and it
was raised as A7. **The owner decided it on 2026-08-22:
§42 wins.** The sampled values that lost are not repeated here or in
[`../../../pics/README.md`](../../../pics/README.md) — a palette this repository
does not use has no business sitting beside the one it does — and are recorded
once, with the reasoning, in
[OD-15](../../research/OWNER_DECISIONS.md#od-15--a7-and-a8-the-canonical-palette-wins-and-the-icon-may-lose-its-black-corners).
