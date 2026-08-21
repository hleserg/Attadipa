# Firefly design system

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
| Firefly Orange | `#FF8A40` |
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
| `color.accent.primary` | Firefly Orange `#FF8A40` | the one thing on screen that acts |
| `color.accent.glow` | Glow Amber `#FFC857` | the firefly light; highlights, focus |
| `color.success` | Meadow Green `#6FA07A` | delivered, connected, fix acquired |
| `color.warning` | Firefly Orange `#FF8A40` | needs attention, not yet wrong |
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

### 3.1 The gap

There is **no red** in either owner palette. Firefly's warmest accent, Firefly
Orange, is doing duty as both "acts" and "warning", which is one job too many,
and there is nothing left for danger — SOS, critical battery, transmit blocked
by an unknown region. Inventing a red is a visual-identity decision and belongs
to the owner, so `color.danger` is `UNKNOWN` rather than quietly assigned.

Meanwhile: no state may be signalled by colour alone (final §55). SOS carries an
icon and a word; delivery success carries a mascot pose; a warning carries text.
That is required for red/green colour-blindness regardless, and it is what makes
the missing red survivable in the interim.

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
raw pixels. 8 px on a 240 × 240 1.54-inch panel and 8 px on a 410 × 502 2.06-inch
panel are not the same physical distance, and touch targets are physical.

`motion.duration.instant` exists so that "reduce motion" and low-power modes have
somewhere to go without an `if` in every animation.

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

## 8. Localization is a design constraint, not a translation step

Every reusable component defines wrap, max lines, ellipsis, flexible width,
minimum touch size and overflow behaviour before it is used
(final §52). Russian strings are commonly 15–30 % longer than English, and a
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
