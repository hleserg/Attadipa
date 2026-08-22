# Attadipa design system

Required by [final §54](../master-prompt-final.md). This is the written half;
the other half is code tokens, which land with the first simulator screen
(T-036). Neither is allowed to drift from the other — a token named here that
does not exist in code is a lie, and a hex value in a widget is a bug.

**Status: proposed.** Every colour value below is a *starting point derived from
owner-provided art*, not a tested value. None of it has been shown on either
panel. Final §55 is explicit: *"Do not preserve a concept-board hex value if it
fails real display readability."* Values will change; the token names should
not.

## 1. The rule the whole system exists for

> No raw RGB, no raw pixel count, no raw millisecond, no font size and no
> corner radius appears in UI code. Only tokens do.

The reason is not tidiness. There are two displays with different sizes,
different technologies and different gamma; two themes; two locales with
different string lengths; and a Child Mode with different touch targets. That is
sixteen visual configurations (final §53). A literal in a widget is correct in
at most one of them, and there is no way to find out which.

## 2. Source material

Three owner-provided images, recorded and hashed in
[`reference/README.md`](reference/README.md). They are the source of the visual
language. They are not a spec for what the product *does* — the style board
shows a heart-rate card, and no target board has a heart-rate sensor.

The two boards carry two close palette explorations. Final §42 settles which is
the starting point rather than leaving it as a conflict to resolve by taste.

**Canonical starting palette** — from the visual style board, as transcribed in
final §42:

| Name | Hex |
|---|---|
| Attadipa Orange | `#FF8A40` |
| Glow Amber | `#FFC857` |
| Meadow Green | `#6FA07A` |
| Leaf Sage | `#A7B49C` |
| Sky Teal | `#6FB7B5` |
| Warm Ivory | `#FFF6E8` |
| Sand Beige | `#F3E8D1` |
| Soft Clay | `#E9DCC2` |
| Cocoa Brown | `#7A5E3A` |
| Ink Olive | `#2F3A2E` |

**Close variants** — from the brand identity board:

| Name | Hex |
|---|---|
| Honey | `#FFC24D` |
| Apricot | `#FFB26B` |
| Warm Coral | `#FF7A57` |
| Warm Teal | `#4F7F76` |
| Cream | `#FFF6E6` |
| Dark Olive | `#3C4033` |

Final §42: *"Do not treat minor raster-board differences as sacred."* Both
lists are recorded because both are owner-provided; only the first seeds tokens.
The values are transcribed from §42, which is text, in preference to sampling
the PNGs, which are raster and lossy about intent.

## 3. Colour tokens

Semantic names. A screen asks for `color.accent.primary`, never for orange.

### Day

| Token | Seed | Role |
|---|---|---|
| `color.background.primary` | Warm Ivory `#FFF6E8` | the page |
| `color.background.surface` | Sand Beige `#F3E8D1` | cards, sheets, list rows |
| `color.background.raised` | Soft Clay `#E9DCC2` | the layer above a surface |
| `color.text.primary` | Ink Olive `#2F3A2E` | body and headings |
| `color.text.muted` | Cocoa Brown `#7A5E3A` | secondary, units, timestamps |
| `color.accent.primary` | Attadipa Orange `#FF8A40` | the one thing on screen that acts |
| `color.accent.glow` | Glow Amber `#FFC857` | the attadipa light; highlights, focus |
| `color.success` | Meadow Green `#6FA07A` | delivered, connected, fix acquired |
| `color.warning` | Attadipa Orange `#FF8A40` | needs attention, not yet wrong |
| `color.danger` | **UNKNOWN** | not in either palette — see §3.1 |
| `color.navigation` | Sky Teal `#6FB7B5` | bearing, route, target |
| `color.border.subtle` | Leaf Sage `#A7B49C` | dividers, inactive outlines |

### Night

Final §47: night is **not inverted day**. It is warm and dark — dark olive, not
blue-black — and it also changes brightness, contrast, glow intensity, animation
intensity and sound behaviour. Only the colour half lives here.

| Token | Seed | Role |
|---|---|---|
| `color.night.background.primary` | Ink Olive `#2F3A2E` | the page |
| `color.night.background.surface` | Dark Olive `#3C4033` | cards — the brand board's variant earns its keep here |
| `color.night.text.primary` | Warm Ivory `#FFF6E8` | body |
| `color.night.text.muted` | Leaf Sage `#A7B49C` | secondary |
| `color.night.accent.primary` | Glow Amber `#FFC857` | amber reads better than orange on dark; **untested** |
| `color.night.accent.glow` | Glow Amber `#FFC857` at reduced luminance | restrained |

On the Waveshare AMOLED, a true-black background costs less power than a dark
olive one, because an AMOLED pixel that is off draws nothing. That is a real
trade against final §47's "warm and calm, not harsh blue-black". It is
**unresolved** and needs measurement on hardware, not a preference. Recorded in
[RESOURCE_BUDGET](../architecture/RESOURCE_BUDGET.md) rather than decided here.

### Day, on an emissive panel

A9 ([OWNER_DECISIONS.md](../research/OWNER_DECISIONS.md) OD-16, 2026-08-22): the
day theme does not keep the near-white page above on a panel where a lit pixel
draws its own current. It renders against this column instead, chosen
automatically from the panel — the theme toggle is unchanged, and a user still
only ever picks day or night.

| Token | Seed | Role |
|---|---|---|
| `color.background.primary` | Ink Olive `#2F3A2E` | the page — night's value, not a new one |
| `color.background.surface` | Dark Olive `#3C4033` | cards |
| `color.text.primary` | Warm Ivory `#FFF6E8` | body — the day page's own colour, now the ink |
| `color.text.muted` | Leaf Sage `#A7B49C` | secondary |
| `color.accent.primary` | Attadipa Orange `#FF8A40` | **kept at day's own value** — see below |
| `color.accent.glow` | Glow Amber `#FFC857` | unchanged in every column |

Every other foreground role (`success`, `warning`, `navigation`,
`border.subtle`) takes the value night already resolves to by fall-through —
day's own, since night does not redefine them either. `color.background.raised`
and `color.danger` are undefined here, the same two gaps night already has.

**`color.accent.primary` is the one row that does not just borrow night's.**
Reusing night's palette wholesale for day-on-an-emissive-panel would make the
theme toggle invisible on that board — day and night would be the same
picture under two names, which is exactly what option 4 was chosen over
option 3 to avoid having to accept as a design cost. Keeping Attadipa Orange
here is deliberate: on this page it clears **5.08:1**, comfortably past body
text, where none of it clears even 2.2:1 on Warm Ivory (§3.2) — a colour that
could only ever be emphasis on the day page turns out to be legible prose on
this one, for free, because the failure was never the hue.

### 3.1 The gap

There is **no red** in either owner palette. Attadipa's warmest accent, Attadipa
Orange, is doing duty as both "acts" and "warning", which is one job too many,
and there is nothing left for danger — SOS, critical battery, transmit blocked
by an unknown region. Inventing a red is a visual-identity decision and belongs
to the owner, so `color.danger` is `UNKNOWN` rather than quietly assigned.

Meanwhile: no state may be signalled by colour alone (final §55). SOS carries an
icon and a word; delivery success carries a mascot pose; a warning carries text.
That is required for red/green colour-blindness regardless, and it is what makes
the missing red survivable in the interim.

### 3.2 Contrast, measured

Not an opinion and not a review note: WCAG 2.1 relative luminance, computed from
the seeds above by `ui/src/color.cpp` and asserted in `tests/test_ui_tokens.cpp`.
AA wants **4.5:1** for body text and **3:1** for large text, icons and the
boundary of a control. Every number below is a ratio against the background
named in the column, in the theme named in the section.

**Day**

| Foreground | on the page | on a surface | on a raised card |
|---|---|---|---|
| `color.text.primary` | 11.10 | 9.78 | 8.77 |
| `color.text.muted` | 5.62 | 4.95 | **4.44** |
| `color.accent.primary` | **2.19** | **1.93** | **1.73** |
| `color.accent.glow` | **1.44** | **1.27** | **1.13** |
| `color.success` | **2.81** | **2.47** | **2.22** |
| `color.warning` | **2.19** | **1.93** | **1.73** |
| `color.navigation` | **2.15** | **1.89** | **1.70** |
| `color.border.subtle` | **2.03** | **1.79** | **1.60** |

**Night** — there is no raised layer; §3.1 records that gap.

| Foreground | on the page | on a surface |
|---|---|---|
| `color.text.primary` | 11.10 | 9.93 |
| `color.text.muted` | 5.47 | 4.89 |
| `color.accent.primary` | 7.73 | 6.92 |
| `color.accent.glow` | 7.73 | 6.92 |
| `color.success` | 3.96 | 3.54 |
| `color.warning` | 5.08 | 4.54 |
| `color.navigation` | 5.16 | 4.62 |
| `color.border.subtle` | 5.47 | 4.89 |

**Day, on an emissive panel** — `ui::PixelCost::PerPixel`, OD-16. Also no raised
layer: it borrows the same background column as night and inherits the same
gap.

| Foreground | on the page | on a surface |
|---|---|---|
| `color.text.primary` | 11.10 | 9.93 |
| `color.text.muted` | 5.47 | 4.89 |
| `color.accent.primary` | **5.08** | **4.54** |
| `color.accent.glow` | 7.73 | 6.92 |
| `color.success` | 3.96 | 3.54 |
| `color.warning` | 5.08 | 4.54 |
| `color.navigation` | 5.16 | 4.62 |
| `color.border.subtle` | 5.47 | 4.89 |

Identical to night's table row for row, except `color.accent.primary`: night
carries Glow Amber there and this column keeps Attadipa Orange, at 5.08:1
against 7.73:1. Both clear body text; the difference is not a legibility
finding, it is the one place this table was built to differ from night's on
purpose (see "Day, on an emissive panel" above). Its tightest case is the same
as night's and for the same reason — it *is* night's value: `color.success` at
3.96:1 on the page, enough to be seen, not enough to be read as a word.

Two things follow from the two backlit tables above, and both are consequences
rather than complaints.

**The day accents cannot carry meaning on their own.** Every accent in the day
palette is under 3:1 even against the brightest background it will ever sit on.
A thin glyph, a one-pixel outline, a word in Sky Teal — none of them is legible
to the standard, and the shortfall is large rather than marginal: Glow Amber at
1.44:1 is very nearly the same luminance as Warm Ivory. So on the day theme an
accent is **emphasis**, and the meaning travels in the icon and the word beside
it. §3.1 already required that for colour-blindness; it turns out to be
load-bearing for everyone. Where an accent must be read — a value, a status
word — it is drawn on a dark chip rather than tinted, or it is drawn in
`color.text.primary` with the accent as its background.

**Muted text fails on a raised card, and only there.** 4.44:1 against Soft Clay,
six hundredths under the threshold, having passed on the page and on a surface.
This is exactly the failure a review by eye does not catch, and it lands on the
most ordinary thing in the system — a timestamp or a unit under a list row. The
remedy is local and needs no new colour: muted text does not go on
`color.background.raised`, or the thing it sits on is not raised.

The night palette holds up throughout. Its tightest case is `color.success` on a
card at 3.54:1 — fine as a graphic, not enough for a word — and the four roles
the night table does not define fall through to their day values and stay
legible doing it, which is the condition that makes the fall-through in
`color()` defensible at all.

None of this is a proposal to change the palette. The colours are the owner's
(final §42), and the published brand art's sampled values — which disagreed —
were resolved in favour of these on 2026-08-22
([OWNER_DECISIONS.md](../research/OWNER_DECISIONS.md) OD-15). What changed here
is that the numbers now exist, are computed rather than eyeballed, and break a
test if they move. The emissive-day column above is built from the same seed
set for the same reason (OD-16) — it is a new combination of existing values,
not a new value.

## 4. Typography

The boards specify **Nunito Sans** (Light / Regular / Medium / SemiBold / Bold)
and **Inter** (Regular / Medium). Final §51 is clear that these are *visual
references, not frozen dependencies*, and that four things must be checked
before either is adopted:

| Check | State |
|---|---|
| Licence | **not verified** — both are widely distributed under the SIL Open Font License, which has not been confirmed from the font files this project would actually embed |
| Cyrillic coverage | **not verified** — and this is the one that can eliminate a font outright |
| Legibility at real pixel size | **not tested** — 240 × 240 is unforgiving |
| Generated LVGL font size in flash | **not measured** |

No font is pinned. Tokens are named for role so that the pin can change:

| Token | Role |
|---|---|
| `type.display` | watchface time |
| `type.title` | screen titles |
| `type.body` | body text and list rows |
| `type.label` | buttons, chips, tabs |
| `type.caption` | units, timestamps, secondary |
| `type.mono.diag` | diagnostics only — raw values, hex, coordinates |

**The subset ships Cyrillic from the first generated font.** Final §51: *"Do not
first create Latin-only embedded fonts and 'add Cyrillic later'."* The subset is
Basic Latin + Cyrillic + digits + the punctuation, symbols and units the UI
actually uses — deliberately chosen, not all of Unicode.

## 5. Spacing, radius, motion, size

Seeded from the style board's generous spacing and rounded forms; all values are
**proposed** and none has been checked at 240 × 240.

| Family | Tokens |
|---|---|
| `space` | `xs 4` · `sm 8` · `md 12` · `lg 16` · `xl 24` · `xxl 32` |
| `radius` | `sm 6` · `md 12` · `lg 20` · `pill 999` |
| `motion.duration` | `instant 0` · `fast 120ms` · `base 200ms` · `slow 320ms` |
| `motion.easing` | `standard` · `enter` · `exit` |
| `icon.size` | `sm 16` · `md 20` · `lg 24` · `xl 32` |
| `image.size` | `inline 32` · `spot 64` · `hero 120` · `hero.large 200` |
| `touch.min` | `44` adult · `56` Child Mode |
| `elevation` | `flat` · `raised` · `overlay` — realised as a border and a tint, not a blurred shadow, which costs fill rate |

Spacing is expressed in **density-independent units resolved per board**, not in
raw pixels, against a 160 dpi reference — the density the touch-target guidance
is already written in, so that "44" means the ~7 mm it is meant to mean rather
than a number this project invented.

What that buys, at the two densities the board profiles compute (261 dpi for the
T-Watch, taking the conservative 1.3-inch reading of a diagonal
[HARDWARE_MATRIX](../research/HARDWARE_MATRIX.md) records as CONFLICTING; 315 dpi
for the Waveshare):

| Token | T-Watch | Waveshare | physical |
|---|---|---|---|
| `space.sm` (8) | 13 px | 16 px | 1.27 mm both |
| `space.lg` (16) | 26 px | 31 px | 2.51 mm / 2.49 mm |
| `touch.min` adult (44) | 72 px | 87 px | 7.01 mm / 7.02 mm |
| `touch.min` child (56) | 91 px | 110 px | 8.86 mm / 8.86 mm |

Written as pixels instead, a 44 would be 4.3 mm on the Waveshare and 5.1 mm on
the T-Watch — under the guidance on both boards, by different amounts, from one
source line. That is the failure the `Dp` type exists to make unwritable.

**`radius.pill` is not a length.** 999 is the CSS idiom for "round the ends
completely"; resolved as a measurement at 261 dpi it is 1630 px, larger than
either panel. In code it is a *rule* — half the shorter side of the thing being
drawn — and `is_pill()` says so in the type system rather than leaving a magic
number to be multiplied by accident.

**Durations are not scaled.** A denser panel does not make time pass differently.
`motion.duration.instant` exists so that "reduce motion" and low-power modes have
somewhere to go without an `if` in every animation.

### 5.1 Where this lives in code

`ui/` — `metrics.h` (the `Dp` type and the per-board resolution), `color.h` /
`color.cpp` (the palette and the contrast arithmetic), `tokens.h` / `tokens.cpp`
(everything above). The library links `attadipa_headers` and **not**
`attadipa_platform`: a screen asks for `space.md`, and only the composition root
knows which panel answered. That is [ADR-0007](../adr/0007-two-capability-layers.md)
applied to pixels.

`ui::PixelCost` (OD-16) is the same rule applied to the emissive-day column:
`color.h` declares only a two-valued "does a lit pixel cost power here", never
a panel or a chip. `sim/boot_screen.cpp`'s `pixel_cost()` is the one function
in the codebase that maps `platform::PanelTechnology` to it — the composition
root's job, the same as `metrics()` immediately above it in that file.

Three tests hold the line. `tests/test_ui_tokens.cpp` asserts the properties —
one token is one physical size on both panels, no gap rounds away, the night
fall-through stays legible. `tools/ui/check_raw_values.py` refuses a colour, a
pixel count or a duration written as a number anywhere under `ui/`, `sim/` or
`apps/` — including the channel-by-channel form `Rgb{0xFF, 0xF6, 0xE8}`, which is
what somebody copying a line out of the palette would paste. Exactly one file is
exempt, `ui/src/color.cpp`, because being the palette is its job; six other
candidates were tried and removed on finding they were exempt from a rule they
never broke. `tools/ui/selftest.py` proves the checker rejects nine real
mistakes and accepts nine correct lines.

## 6. Sound and haptics are tokens too

Final §48 makes them semantic feedback, not effects. They are named, centralized
and user-controllable, and an application never encodes motor timing.

| Family | Tokens |
|---|---|
| `haptic` | `tap` · `success` · `warning` · `message` · `navigation` · `error` · `sos` |
| `sound.category` | `system` · `notifications` · `mesh` · `alarms` · `navigation` |

The two boards have very different haptic hardware — the T-Watch has a driver
IC, the Waveshare board has a bare motor on a GPIO through a transistor
([VERIFIED_FACTS](../research/VERIFIED_FACTS.md)). The same token must feel as
similar as the hardware permits and must not fail on the weaker one. Realising
`haptic.sos` is a platform-layer job; choosing it is an application one.

`HardwareCoordinator` may delay a non-critical haptic to protect a sensitive
measurement (final §48). `haptic.sos` is never delayed.

## 7. Imagery

Final §44: imagery is part of the UI language, and the mascot is used
**contextually** — not on every screen, and never at the cost of glanceability.

The mascot sheet supplies four named poses. They map to states, so that
"which picture goes here" is answered by the state machine and not by taste:

| Pose | Used for |
|---|---|
| `NEUTRAL` | onboarding, About, idle empty state |
| `GUIDING / NAVIGATION` | Navigator, no-fix-yet, arrival |
| `MESSAGE RECEIVED` | mesh delivery success, unread |
| `THINKING / EXPLORING` | scanning, pairing, searching, working |

Rules that follow from final §41, §86 and §97:

- The adult UI is restrained. A mascot on an operational screen must not
  displace the information the screen exists to show.
- Art is re-derived for the watch, not scaled down. A 1448-pixel illustration
  becomes noise at 40 px; small sizes are drawn deliberately.
- No illustration may imply a feature that does not exist.
- Every image has a measured flash cost, tracked per board
  ([RESOURCE_BUDGET](../architecture/RESOURCE_BUDGET.md)).

### 7.1 Icons, and the three rules the pipeline makes mechanical

T-034 turned the second and fourth of those from sentences into checks. The
pipeline is [`ui/assets/README.md`](../../ui/assets/README.md); what belongs
here is what a designer needs to know before drawing one.

**An icon carries no colour.** Assets are alpha-only masks and colour arrives at
draw time through a `ColorRole` — the same route as text, and for the same
reason. It means a theme reaches an icon without regenerating anything, and it
means `legible_as_graphic()` can be asked whether a role clears 3:1 before an
icon is painted in it. §3.2's day table is why that matters: **no accent in the
day palette clears 3:1 against Warm Ivory**, so an orange icon on the day theme
is not a stylistic choice, it is an illegible one.

**An icon is drawn at each size it is used at.** Not once and scaled. The
authored geometry lives in `tools/assets/icon_drawings.py` with an entry per
pixel size — stroke weight, feature radii, inset — because 33 px and 47 px are
different drawing problems and the difference is exactly the detail a resampler
destroys. The pipeline refuses a size it has no drawing for, and the lookup
returns nothing rather than the nearest one it has.

**A size is a pixel count, never a board.** The four `icon.size.*` tokens across
the two panel densities land on **seven** distinct pixel sizes, and two of them
collide: `icon.size.lg` at 261 dpi and `icon.size.md` at 315 dpi are both 39 px
and share one file. Naming assets by board would ship the same picture twice.

| Token | T-Watch, 261 dpi | Waveshare, 315 dpi |
|---|---|---|
| `icon.size.sm` — 16 dp | 26 px | 32 px |
| `icon.size.md` — 20 dp | **33 px** | **39 px** |
| `icon.size.lg` — 24 dp | **39 px** | **47 px** |
| `icon.size.xl` — 32 dp | 52 px | 63 px |

The four sizes in bold are the ones that exist; `sm` and `xl` are not generated,
because nothing draws them yet and a mask costs its pixel count in flash. Asking
for one returns nothing, which is the honest answer and not an oversight.

### 7.2 The first three icons

| Icon | What it means | Why it is drawn that way |
|---|---|---|
| `mesh` | one node reaching two others | The first attempt was a hub with three peers, and at 33 px the four discs merged into a lump. The second was a triangle of nodes, one row away in silhouette from `warning`. The third has a silhouette that is neither |
| `position` | a position is known | A pin and deliberately **not** a satellite, an antenna or a phone. An application is never told where a fix came from — the wrist, a companion node, or somebody else's message — so the icon must not draw a source |
| `warning` | a degraded state | It exists **because** §3.1 forbids signalling state by colour alone. On the day palette that is not merely an accessibility courtesy, since no accent clears 4.5:1 against Warm Ivory. A degraded state has to have a shape |

The review sheet is [`specimens/sheet-icons.png`](specimens/sheet-icons.png) —
day and night, at 1:1 and not magnified, because an icon that only reads at 3×
is an icon that does not read.

## 8. Localization is a design constraint, not a translation step

Every reusable component defines wrap, max lines, ellipsis, flexible width,
minimum touch size and overflow behaviour before it is used
(final §52). **The two-column row is the worked example, and it earned the rule
twice.** With two content-sized labels and `SPACE_BETWEEN`, a long name and a
long state are pushed to opposite edges and then drawn straight through each
other — nothing clips, nothing warns, and the screenshot shows two unreadable
words on top of one another. Giving the left label `flex_grow` fixes that and
produces the next bug: LVGL's ellipsis mode cannot shorten a label whose height
is content-sized, so it wraps to a second line and the row grows into the one
below it. The rule that survives both: **the value column is content-sized and
never shrinks** — a state that reads "не настроено" instead of "не наст…" is the
entire point of the row — **the label column takes the remainder, is exactly one
line tall, and ellipsizes.** Which is also why labels are chosen short: the
fallback should be rare, not routine. Russian strings are commonly 15–30 % longer than English, and a
layout that is correct only because an English word fit is a layout that is
broken in the other locale and nobody noticed.

Concretely: no fixed-width label sized to its English content, no sentence
assembled from fragments, and both locales exercised in the visual test matrix
(final §53) rather than tested in English and translated afterwards.

See [ADR-0010](../adr/0010-localization.md) for the mechanism.

## 9. What is deliberately not decided here

| | Why |
|---|---|
| Which LVGL version the tokens compile against | T-032; it decides the font and image tooling |
| Final contrast-tested colour values | needs a powered panel — final §55 |
| `color.danger` | no red exists in the owner palette; an identity decision |
| Font pin | licence and Cyrillic coverage unverified |
| True black versus dark olive on AMOLED | a power measurement, not a preference |
| Watchface catalogue | M1 delivers one; the rest is M7 |
