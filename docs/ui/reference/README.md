# Owner-provided visual references

These three files are **owner-provided project art**. They arrived with the
final master specification
([`docs/master-prompt-final.md`](../../master-prompt-final.md), preamble and
§40) and are named there as *canonical visual references* — not mood-board
decoration.

They are **immutable design references**. Do not edit them, do not re-export
them, do not overwrite them with a "cleaned up" version. Derived and cleaned
artwork belongs in `ui/assets/source/`; generated target assets belong in
`ui/assets/generated/`; the scripts that produce them belong in `tools/assets/`
(§45). Nothing in this directory is ever compiled into firmware — a 1448×1086
PNG is a desktop concept sheet, not a watch asset.

| File | SHA-256 | Size | Pixels |
|---|---|---|---|
| `attadipa_brand_identity.png` | `4f31ce764bc56a69f72b8e8782020aec4e47d932be2e8bea51b00ca0b5511ac1` | regenerated | 1440 × 1086 |
| `attadipa_visual_style_board.png` | `72c23e0c6852127067c3cf41705344c2ade02101b99420cae9feabe52a06b875` | regenerated | 1440 × 1086 |
| `lumar_mascot_sheet.png` | `6b0aeec6c1701357b299a8933c3399374e95543ab539ed169bf97b698027df58` | regenerated | 1440 × 1086 |

The hashes are here so that a later "I cleaned up the source art" commit is
visible as one. Verify with `sha256sum docs/ui/reference/*.png`.

## What each one carries

**`attadipa_brand_identity.png`** — the wordmark *Attadipa*, the tagline
`GLOW · GUIDE · CONNECT`, the mascot, an icon mark, an app icon, and horizontal
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

They matter here for two reasons. They add a tagline that did not previously
exist — **`GLOW · GUIDE · CONNECT`** — and they carry inks that are *not* the
§42 palette: the wordmark orange samples around `#E16439`, against Attadipa
Orange `#FF8A40`. That is a wider gap than "disagree by a digit", so the rule
above does not settle it, and it has been raised as A7 in
[`../../research/OPEN_QUESTIONS.md`](../../research/OPEN_QUESTIONS.md) rather
than decided here. Sampled values and the reasoning are in
[`../../../pics/README.md`](../../../pics/README.md).
