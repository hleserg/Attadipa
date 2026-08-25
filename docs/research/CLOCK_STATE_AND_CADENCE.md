# The first Clock: state, cadence, overflow and the test matrix

Research for **T-037**, done before it starts, at `f2b6853`. Issue
[#144](https://github.com/hleserg/Attadipa/issues/144). Research only — no Clock
was implemented and no production file was touched.

Final §85 asks for mature wearable UX to be reviewed before major UI pattern
work. This is that
review, plus the four project-specific things a review of other people's watches
cannot supply: what the *existing* Attadipa types already say, what the *pinned*
LVGL actually does, what the *shipped* font actually measures, and what the
simulator can and cannot photograph today.

**The short version.** The state contract is almost entirely already in
`core/`, and building a new one would be the parallel abstraction the issue warns
about. The cadence contract is *not* the one all three upstream watches use —
they poll and filter, and Attadipa should not. The overflow contract has a trap
in pinned LVGL that turns ellipsis silently off. And T-037 has **five
prerequisites** that are implementation work rather than design work — no
display-sized font, no calendar, no temporal strings, no way to photograph the
eight Child-Mode configurations or to put the clock into any particular state,
and no tick period on `AppManifest`. They are itemised in §10.

---

## 1. What already exists here, and must not be reinvented

Research question 8 first, because it constrains every answer below. Almost
every piece of the "ClockViewState" the issue sketches already has a type in
this repository, and most of them are not obviously about a clock.

| The Clock needs | It already exists as | Where |
|---|---|---|
| a wall clock | `core::WallTime` — UNIX seconds, **no arithmetic** | [`core/clock.h:86`](../../core/include/attadipa/core/clock.h) |
| elapsed time, for anything timed | `core::MonotonicTime` / `Millis` / `elapsed()` | `core/clock.h:45`, `:62` |
| "the time is not usable" | `core::Availability` — seven values, seven *remedies* | [`core/availability.h:16`](../../core/include/attadipa/core/availability.h) |
| "the time is old / not trustworthy" | `core::Validity{Unknown, Valid, Stale, Invalid}` | `core/availability.h:39` |
| a datum plus its two ages | `core::Timed<T>` | `core/availability.h:46` |
| "who is supplying the time" | `core::Capability::Time` — *"a wall clock worth displaying"* | [`core/capability.h:18`](../../core/include/attadipa/core/capability.h) |
| whether a number was measured or guessed | `core::Provenance{Unknown, Estimated, Measured}` | [`core/power_state.h`](../../core/include/attadipa/core/power_state.h) |
| screen on / screen off / asleep | `core::PowerState`, `core::WakeSource` | `core/power_state.h:29`, `:43` |
| a colour, a spacing, a type role | `ui::ColorRole`, `Space`, `ui::TypeRole::Display` | [`ui/tokens.h:152`](../../ui/include/attadipa/ui/tokens.h) |
| a translated string | `l10n::tr()`, `StringId`, runtime `set_locale()` | [ADR-0010](../adr/0010-localization.md) |
| which panel, at what density | `platform::BoardProfile`, resolved through `Dp` | `ui/metrics.h` |

**So `Capability::Time` is the clock's source, and it is not local by
definition.** `sim/main.cpp:88` already lists `Capability::Time` among what an
attached Attadipa node provides. A watch whose own RTC has never been set and
which has a node attached has a *Ready* time from `Origin::Node`; the same watch
with the node gone has the same time going `Stale` and then `Unreachable`. The
Clock must render all of that, and — ADR-0004 — it must never learn which of the
two answered.

**What does not exist, and is the gap:**

| Missing | Consequence for T-037 |
|---|---|
| any calendar arithmetic at all — no `localtime`, no civil-from-days, no month or weekday table anywhere in `core/`, `ui/`, `apps/` or `l10n/` | a `WallTime` cannot become "30 сентября" by any route that exists today |
| any date, weekday, month or clock string in the catalogue — 63 `StringId`s, none of them temporal | [`l10n/string_id.h`](../../l10n/include/attadipa/l10n/string_id.h) |
| a tick period on `AppManifest` | [`apps/app_manifest.h:22`](../../apps/include/attadipa/apps/app_manifest.h) declares `id`, `required`, `enhanced_by` and nothing else — see §5 |
| an Adult/Child switch anywhere | final §57 requires one in the simulator; `sim/options.h` has none |
| any way to inject a time into the simulator | so 00:00, 23:59 and a rollover cannot be photographed — see §8 |
| a font above 28 px | see §7 |

`core::WallTime` having no subtraction is deliberate and load-bearing
(`core/clock.h:80-93`), and a calendar must not quietly undo it. Converting
UNIX seconds to a civil date is a *pure function of one absolute instant* and
does not derive a duration, so it is compatible with the rule — but only if it
is written as one, and not as `now - midnight`.

---

## 2. Upstream, rechecked at the exact revisions

Every claim below was read from the file at the commit named, downloaded on
2026-08-23, not from documentation about it.

| Project | Revision | Licence | What Attadipa may do |
|---|---|---|---|
| InfiniTime | [`8250565`](https://github.com/InfiniTimeOrg/InfiniTime/tree/825056574f47a8187b410b860f326050566553e2) | **GPL-3.0**, no linking exception | read it, copy nothing — `INSPIRE ARCHITECTURE` only |
| ZSWatch | [`466a5ae`](https://github.com/ZSWatch/ZSWatch/tree/466a5ae5f3c1cc3dd53da6da2f1c7f50cfae0394) | **GPL-3.0**, read from **`LICENCE`** — the British spelling; `LICENSE` 404s at that commit | as above |
| wasp-os | [`5625c1d`](https://github.com/wasp-os/wasp-os/tree/5625c1df433d43b078dd511f30204f10d9c28f6c), `watch_faces/clock.py` | **GPL-3.0** at the tree (`COPYING`), **LGPL-3.0-or-later** on this file (SPDX header, line 1) — a mixed-licence tree | as above; LGPL's linking relief does not survive static linking into firmware, and it is a different language regardless |
| LVGL | `85aa60d` = v9.5.0 | **MIT** | already a pinned dependency ([DEPENDENCIES](DEPENDENCIES.md)) |
| Wear OS watch-face guidance | developer.android.com, read 2026-08-23 | documentation | read for interaction lessons; **no assets, no XML model, no visual identity** (final §85) |

The three watch firmwares are all copyleft. The ledger already records that
[boundary](REUSE_LEDGER.md) for InfiniTime; ZSWatch and wasp-os are added by
this work.

### 2.1 The finding the issue did not predict: none of them schedules on the datum

The issue's provisional reading is that InfiniTime teaches "update only fields
whose semantic value changed" and ZSWatch teaches "cadence follows the displayed
datum". The first is right. The second is **not what ZSWatch does**, and
reading the source rather than the summary changes the lesson.

**InfiniTime has no cadence at all — it has a filter.**
`WatchFaceDigital.cpp:89` creates
`lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, ...)`, so
`Refresh()` runs at the *display refresh period*. Line 106 then reads the clock
unconditionally on every one of those calls and truncates it to minutes; the
`DirtyValue` comparison at line 108 is what stops the label being rewritten. The
date is nested one level deeper (line 130) and is only even *considered* when
the minute changed. So the structure is: poll at frame rate, suppress by
semantic equality, nest the slower datum inside the faster one.

**ZSWatch polls too, on a self-rescheduling fixed period.**
`watchface_app.c:52-53` define `NORMAL_TIME_UPDATE_INTERVAL K_MSEC(1000)` and
`SMOOTH_TIME_UPDATE_INTERVAL K_MSEC(50)`; `:429` reschedules `clock_work` at
whichever of the two the setting selects, having called `set_datetime(...)`
unconditionally at `:424`. `update_work` runs every `K_SECONDS(1)` for
notifications and steps (`:415`); `date_work` runs at `SLOW_UPDATE_INTERVAL
K_MINUTES(1)` (`:441`) and, despite its name, its payload reads a *pressure
sensor* (`:433-441`). None of these is aligned to a minute boundary — they are
periods measured from whenever the work last ran.

**wasp-os is the closest to right, and it is still a 1 s tick.**
`clock.py:37` requests `wasp.system.request_tick(1000)`; `:100` returns early
unless the minute on display differs from `self._min`. One second in, one redraw
a minute out.

So: **three mature implementations, three polling loops.** The reason is the
same in all three — each already runs a display or scheduler loop for other
reasons, so a poll is free at the margin. Attadipa is not in that position by
choice: `core/power_state.h` models `Idle` (screen off, services ticking) and
`LightSleep` as distinct states precisely so that a screen which is not being
looked at costs nothing, and final §61 says *"do not poll aggressively because
it is easy"*. Copying the shape that all three converged on would be copying
their constraint, not their judgement.

**What each is genuinely worth taking:**

| Lesson | From | Evidence |
|---|---|---|
| compare the *semantic* value, not the raw one — truncate to the displayed granularity first, then compare | InfiniTime | `WatchFaceDigital.cpp:106` truncates to `minutes` **before** the dirty check at `:108` |
| a slower datum can be a derived dirty value of a faster one | InfiniTime | the date check at `:131` is unreachable unless the minute changed at `:108` — correct, because a date cannot change without the minute changing |
| an externally *set* time is an event, not a tick | ZSWatch | `:486` `BLE_COMM_DATA_TYPE_SET_TIME` → `k_work_reschedule(&date_work.work, K_NO_WAIT)`: a phone setting the clock forces an immediate refresh |
| every scheduled thing is cancelled on close **and** on suspend, separately | ZSWatch | `:280-283` on stop, `:582-584` on suspend — two different lists, and suspend keeps `general_work_item` |
| full render and incremental render are two named entry points | wasp-os | `_draw(True)` from `foreground()`/`preview()`; `_draw()` from `wake()`/`tick()` |
| the watchface time is drawn in a **monospaced** face | InfiniTime | `:61` `jetbrains_mono_extrabold_compressed`, and §7 below is why that is not a stylistic choice |

**And two mistakes worth not repeating:**

- **wasp-os hard-codes English months.** `clock.py:23`
  `MONTH = 'JanFebMarAprMayJunJulAugSepOctNovDec'`, sliced three characters at a
  time at `:69`, and assembled at `:71` with
  `'{} {} {}'.format(now[2], month, now[0])`. That is a sentence built from
  fragments and a translation table welded into a screen — the two things
  ADR-0010 and DESIGN_SYSTEM §8 exist to prevent.
- **wasp-os's incremental path depends on state the full path sets, with nothing
  enforcing the order.** `self._min` is read at `:100` and only ever assigned at
  `:113`; there is no `__init__`. `wake()` before `foreground()` would raise.
  The contract holds by lifecycle luck. Attadipa's equivalent must be safe by
  construction — see §4.

### 2.2 Wear OS, as an interaction reference only

The published guidance is worth exactly three things, and they are not visual:
time is the primary element and stays glanceable; content must stay inside the
bezel on a round face; and the interactive and ambient states are **different
designs**, not the same design dimmed. The declarative Watch Face Format's
separation of face from resources, and its versioned capabilities, are an
architecture reference and nothing more.

Explicitly **not** adopted: the XML/complications model, the visual identity,
and the "under 15 % of pixels lit in ambient" rule — that is a platform
certification criterion written for other people's panels and is not an Attadipa
PASS condition. Attadipa's equivalent number would have to be measured on the
CO5300, and has not been.

---

## 3. The semantic view state

Not a header and not a prescription — this is the *set of questions the screen
must be able to answer*, and which existing type answers each. T-037 chooses the
spelling.

| Datum | Type it should be | Domain or presentation | Why |
|---|---|---|---|
| the instant | `Timed<WallTime>` | domain | carries `Validity` and both ages with it; a node-supplied time is old in two different ways (`availability.h:46`) |
| time availability | `Availability` for `Capability::Time` | domain | `Unprovisioned` (never set), `Unreachable` (node gone), `Ready` are three different sentences and one `bool` cannot hold them |
| the civil fields — year, month, day, weekday, hour, minute | derived, presentation | presentation | a pure function of the instant plus the zone; see §1 on not undoing `WallTime`'s missing arithmetic |
| the time zone / offset | domain | domain | not currently modelled anywhere. **Open** — see §10 |
| 12/24-hour | a setting (ADR-0006) | domain | it is not only a time format: InfiniTime's `:134` shows the same setting reordering the *date* |
| locale | `l10n::locale()` | presentation | already runtime-switchable with a change handler |
| theme | `ui::Theme` | presentation | already runtime-switchable |
| geometry / density | `BoardProfile` | presentation | fixed for the life of the process; a change is a different device |
| Adult/Child | **does not exist yet** | domain (a setting) | final §49; T-038 owns the setting, T-037 must render both |
| battery percent | `Timed<uint8_t>` or an explicit optional | domain | "unknown" must be representable — `GnssCapabilities` already demonstrated the failure of a `bool` that cannot say *nobody has checked* (issue #166) |
| charging | domain | domain | a PMU fact, event-driven |
| power state | `core::PowerState` | domain | decides whether the Clock should be ticking at all (§4) |
| which mascot pose, if any | presentation | presentation | DESIGN_SYSTEM §7 already maps poses to states, so this is a lookup and not a choice at the call site |

**The rule that makes the split useful:** anything in the "domain" column
arrives through the capability registry or the settings service and is
observable; anything in "presentation" is recomputed from those and is never
stored as a second source of truth. A cached *formatted string* is presentation
and may be kept — that is the dirty-value optimisation — but it is a cache with
one owner, not state.

---

## 4. The event and cadence table

The organising rule, and it is the one thing this document would most like T-037
to take: **a displayed datum is refreshed when its own semantic value changes,
and the wake that notices the change is derived from the datum's period — not
from the frame rate, and not from a convenient round number.**

| Event | Source | What changes | Render |
|---|---|---|---|
| minute boundary | a scheduled wake aligned to the *next* boundary, not a 60 s period from now | the time field | one field |
| midnight / date rollover | the same wake; the date is a derived dirty value of the minute (InfiniTime `:131`) | the date field | one field, and only on the minute that crosses it |
| battery percent change | PMU event | the battery field | one field |
| charging attach / detach | PMU event | battery field + its icon/label | one field group |
| time set externally — node, GNSS, user, NTP | an event on `Capability::Time` (ZSWatch `:486`) | possibly every temporal field at once | **full**, because a step can cross any boundary |
| time becomes `Stale` / `Invalid` / `Unreachable` | capability change | the degraded presentation of the time and date | one field group |
| locale change | `l10n::set_locale_changed_handler` | every string, and every string's width | **full** |
| theme change | `ui::set_theme` | every colour | **full** |
| Adult ⇄ Child | a setting | layout, touch targets, imagery | **full** |
| resume from screen-off | `PowerState` → `Active` | unknown — depends on panel retention | **full**, until retention is measured (§9) |
| geometry change | cannot happen at runtime on a device | — | — |

**What must not appear in this table**, and each has a source:

- **No 30 ms poll.** That is InfiniTime's `LV_DISP_DEF_REFR_PERIOD` refresh task
  and it is affordable there because the screen is already being driven.
- **No 1 s tick.** ZSWatch's and wasp-os's, and both exist to service a *seconds*
  display or a smooth second hand. Attadipa displays neither; §88 lists time,
  date and battery, and none of them changes faster than once a minute.
- **No 50 ms smooth update.** ZSWatch's `SMOOTH_TIME_UPDATE_INTERVAL`, for an
  analogue second hand. Not a product requirement here.

**A period is not a boundary, and the difference is visible.** ZSWatch's
`date_work` reschedules `K_MINUTES(1)` from the *end of the handler* (`:441`),
which is the natural way to write it and is not the same thing as waking on the
minute. Its phase is wherever the first run happened to land, so a display
driven that way is up to 60 s stale against the wall clock — and the offset
grows slowly, because each period starts after the previous handler finished.
That is precisely why wasp-os and InfiniTime can *only* poll faster and filter:
having chosen a period, the only way to be on time is to check more often than
you need to. Computing the wake to the next boundary each time removes both
problems at once, and it is also the cheaper one — one wake a minute instead of
the sixty that a 1 s tick costs to achieve the same freshness.

**Whether the boundary wake can be a hardware alarm is board-dependent, and one
of the two answers is not known.**

| Board | RTC | Interrupt | Status |
|---|---|---|---|
| T-Watch S3 | PCF8563 | **INT → GPIO 17** | VERIFIED — [HARDWARE_MATRIX](HARDWARE_MATRIX.md):98 |
| Waveshare 2.06 | PCF85063ATL, `0x51` | **not recorded** — the row carries bus, address and rail, and no INT | **UNKNOWN** — HARDWARE_MATRIX:332 |

So on the T-Watch a per-minute RTC alarm has a routed line; on the Waveshare
nobody has traced one. Neither has been exercised. Until then the boundary wake
is a *software* timer owned by the framework, which is correct while the screen
is on and is exactly the thing that must not survive into `Idle`.

**Cadence is a function of `PowerState`.** `Active`: the boundary wake runs.
`Idle` and below: the screen is off, nothing is displayed, and the Clock's wake
must be released — otherwise the app is holding the device awake to redraw a
screen nobody is looking at. That is the whole reason `core/power_state.h`
distinguishes `Idle` from `Active`, and it is the AOD seam (§9).

---

## 5. Full versus incremental render, and who owns the timer

**Two entry points, named, from wasp-os's shape and not its code:**

- **full** — build or rebuild every element. Used on open, on resume, and on any
  event that can change everything at once (locale, theme, Adult/Child, a time
  step). It must be safe to call at any moment and must not depend on prior
  state.
- **incremental** — update the fields whose semantic value changed. It must be
  **unreachable before a full render, by construction rather than by lifecycle
  ordering** — which is precisely the guarantee wasp-os does not have (§2.1).

**The tick and teardown contract, and the gap in it.** The reuse ledger already
records the decision, from an upstream crash:

> *An app that owned its own LVGL timer crashed on exit, twice.* Commit
> `f780ac999a069b3539f5419b9e07a624ae018030`, 2021-09-28. → Applications never
> get a timer handle. The manifest declares a tick period; the framework owns
> the timer's lifetime.
> — [REUSE_LEDGER](REUSE_LEDGER.md), *The application framework*

And InfiniTime at the pinned revision still has that shape:
`WatchFaceDigital.cpp:89` creates the task in the constructor and `:94` deletes
it in the destructor, so the timer's lifetime is the screen's and a mistake in
either is a use-after-free. Final §59 says the same in one line: *"Applications
do not freely create FreeRTOS tasks and LVGL timers."*

**But `AppManifest` has no tick period.**
[`apps/app_manifest.h:22`](../../apps/include/attadipa/apps/app_manifest.h)
declares `id`, `required`/`required_count` and `enhanced_by`/`enhanced_by_count`
— that is all. So the contract exists as a recorded decision and a specification
line, and **not as anything a compiler enforces**. The first Clock application
will make the gap concrete.

This document does **not** decide it, and that is deliberate: the application
framework and the event-bus/concurrency ADR must explicitly cover *"who owns
which task … how events are delivered … UI-thread rules"*.
Minting an ADR here would create the second truth store that research question 8
warns against. What this research contributes to them is the requirement
statement: *an application declares the cadence of each datum it displays and
receives a callback; it never holds a timer handle, and the framework releases
every wake on close and on suspend* — with ZSWatch `:280-283` and `:582-584`
showing that close and suspend are two different lists.

The Clock issue must either depend on that framework or name a provisional
Clock-local tick that the framework will absorb. That scheduling decision
belongs in the issue, not this research report.

---

## 6. Overflow at pinned LVGL v9.5.0

All five modes exist at the pin — the issue names four;
[`src/widgets/label/lv_label.h:49-55`](https://github.com/lvgl/lvgl/blob/85aa60d18b3d5e5588d7b247abf90198f07c8a63/src/widgets/label/lv_label.h)
declares `WRAP`, `DOTS`, `SCROLL`, `SCROLL_CIRCULAR`, `CLIP`. Every line
reference below is `lv_label.c` at `85aa60d`, read from the tree CMake fetched.

| Mode | What it actually does at this pin | For a glanceable Clock |
|---|---|---|
| `WRAP` | grows the object's height; default (`:748`) | **acceptable for the date**, if and only if the row's height is allowed to grow and the layout below it can absorb it. On a 240 px face it usually cannot |
| `DOTS` | ellipsis — **with two preconditions, below** | **the intended answer for the date**, and it needs the preconditions met deliberately |
| `SCROLL` | infinite animation, `LV_ANIM_REPEAT_INFINITE` (`:1123`) | **reject** |
| `SCROLL_CIRCULAR` | infinite animation (`:1238`), and disables the long-text draw hint (`:880`) | **reject** |
| `CLIP` | truncates at the box edge, no ellipsis, no animation | acceptable only where the truncation is invisible by design; a half-drawn word is worse than a dotted one |

**Why SCROLL is rejected, with the source rather than by taste.** Three
independent reasons:

1. `lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE)` at `:1123` and
   `:1238`. An animation that never ends keeps LVGL's animation timer running
   and invalidates the label region every frame — a permanent redraw source on
   the one screen that is on the most. That is the opposite of §4's entire
   argument.
2. `:904-916` silently **overrides `CENTER` and `RIGHT` alignment to `LEFT`**
   when the text overflows. So a centred date is centred in English, and jumps
   to left-aligned the moment a longer Russian string arrives — a layout that
   changes its own alignment based on content.
3. Scrolling text is not glanceable. A watch is read in under two seconds; text
   that must be waited for is text that is not read.

**The DOTS preconditions, and the one that is a genuine trap.**

*First:* the ellipsis test is on **height**, not width. `:1316`:

```c
if(size.y > lv_area_get_height(&txt_coords) &&   /*Text overflows available area*/
   size.y > lv_font_get_line_height(font) &&     /*No break requested, so no dots required*/
   lv_text_get_encoded_length(label->text) > LV_LABEL_DOT_NUM) {
```

so dots appear only when the *wrapped* text is taller than the box. A label
whose height is content-sized grows with its text, `size.y > height` is never
true, and no ellipsis ever appears — which is exactly the failure
[DESIGN_SYSTEM §8](../ui/DESIGN_SYSTEM.md) found empirically in the two-column
row and is now traceable to a line. And `:820` is why the width half behaves
the way it does: a label with `LV_SIZE_CONTENT` width that no layout is sizing
gets `w = LV_COORD_MAX`, i.e. no wrapping constraint at all. **A dotted label
needs a bounded width from a layout and an explicitly bounded height.**

*Second, and this one is silent:* `lv_label_set_text_static()` and `DOTS` do not
work together. `:1375-1377`:

```c
if(label->static_txt) {
    LV_LOG_WARN("Long mode \"dots\" is not supported with static text.");
    return;
}
```

`label->dot_begin` has already been assigned two lines earlier, so the label
believes it is dotted and no ellipsis is drawn. The only signal is
`LV_LOG_WARN`, which is compiled out of a release build. **This is a live trap
for Attadipa specifically**: `tr()` returns a pointer into a static catalogue,
`lv_label_set_text_static()` is the obvious call for it and saves a heap copy
per label, and combining the two turns the project's overflow policy off without
a word. The mechanism is `lv_label_revert_dots()` — its guard at `:1360` is
`!label->static_txt`, so it will not write back into memory it does not own.
LVGL is behaving correctly; the trap is in the combination.

`LV_LABEL_DOT_NUM` is **3** (`lv_label.h:31`) and is not configurable through
`lv_conf.h` at this version, so the ellipsis is always three ASCII full stops
and never `…` (U+2026), even though U+2026 is in the project charset.

**One more thing at the pin worth recording so it is not adopted by accident.**
`:851-857` implements `LV_EVENT_TRANSLATION_LANGUAGE_CHANGED`: LVGL v9.5 has its
own translation-tag mechanism, and a label carrying a tag re-fetches through
`lv_tr()` on a language change. Attadipa already has `tr()`, `StringId` and
`set_locale_changed_handler` under ADR-0010. Using LVGL's would be a second
string store with a second missing-string policy. **Do not.**

---

## 7. Measured string widths, and the finding that changes the layout

**MEASURED**, and reproducible:

```
python3 tools/font/measure_strings.py --self-test
python3 tools/font/measure_strings.py --digits
python3 tools/font/measure_strings.py --time-span
python3 tools/font/measure_strings.py --dates
python3 tools/font/measure_strings.py --clock
```

At the time of this measurement the tool read
`assets/fonts/generated/attadipa_montserrat_*.c` — the exact bytes the firmware
and simulator linked then — and applied LVGL v9.5.0's own
integer arithmetic to them: `lv_font_fmt_txt.c:245-253` for the kerned,
per-glyph-rounded advance, `lv_text.c:lv_text_get_width()` for the sum and the
trailing letter-space trim, and `get_glyph_dsc_id()` / `get_kern_value()` for
the lookups. It is not an estimate of a renderer's behaviour; it is that
renderer's arithmetic against that renderer's data.

**How the tool was checked, because a transcription is exactly the kind of thing
that is quietly wrong.** `--self-test` re-derives the parse against what the
generated files state about themselves rather than against numbers typed into
the test: every codepoint `charset.py` asks for resolves to a glyph, every cmap
lands inside `glyph_dsc[]`, `kern_scale` is still 16, and the kerning matrix is
exactly `left_class_cnt × right_class_cnt` entries — the check that catches a
regex having picked up the wrong array, which would otherwise index into
nonsense and return a plausible number. Three things were then confirmed by
hand: truncating the kern matrix by one entry makes the shape check fail; a
codepoint outside the charset (U+4E00) resolves to glyph 0 rather than to a
neighbour, so the sparse-cmap binary search is not off by one; and kerning is
demonstrably *applied* rather than silently zero — `AV` measures 39 px kerned
against 41 px unkerned at 28 px, and `То` 33 against 35.

**What was measured, exactly:** Montserrat Medium, subset to the 181 codepoints
in `tools/font/charset.py`, at 14/16/20/28 px, **4 bpp**, `--no-compress`, with
class kerning (`kern_scale = 16`), generated by `lv_font_conv` 1.5.3 from the
TTF LVGL ships at `85aa60d`, SHA-256
`421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53`
(`tools/font/generate_ui_fonts.py:44`). **This was scaffolding, not the product
typeface.** T-037 subsequently resolved D16 to Nunito Sans Regular 400 on
2026-08-26. Every number below remains the preserved Montserrat measurement and
must not be quoted as the current Clock's geometry.

### 7.1 The figures are proportional, and the time moves

| Size | `0` | `1` | narrowest–widest figure | `:` |
|---:|---:|---:|---:|---:|
| 14 px | 9 | 5 | 5–9 px | 3 |
| 16 px | 11 | 6 | 6–11 px | 4 |
| 20 px | 13 | 7 | 7–13 px | 5 |
| 28 px | 19 | 10 | 10–19 px | 6 |

Over all 1440 minutes of a day, rendered `HH:MM`:

| Size | narrowest | widest | span | as % of the 240 px face |
|---:|---|---|---:|---:|
| 14 px | `11:11` 23 px | `04:48` 41 px | 18 px | 7.5 % |
| 16 px | `11:11` 28 px | `00:00` 48 px | 20 px | 8.3 % |
| 20 px | `11:11` 33 px | `04:48` 59 px | 26 px | 10.8 % |
| **28 px** | `11:11` **46 px** | `00:00` **82 px** | **36 px** | **15.0 %** |

**A centre-aligned time drawn in this font moves up to 18 px each side as the
minute changes** — at 28 px, on a 240 px panel. Right-aligned it moves the full
36 px on one side. This is once a minute, forever, on the screen the product is
most identified by, and it is the kind of defect that reads as "cheap" without
anybody being able to say why.

It is also the reason InfiniTime draws its time in `jetbrains_mono_extrabold_compressed`
(`WatchFaceDigital.cpp:61`) and pays a whole extra font for it, and the reason
its 12-hour path (`:123` `"%2d:%02d"`, then a re-align at `:124`) is written
differently from its 24-hour path (`:126`, `:127`).

**Three ways out, and none of them is free.** Choosing between them is T-037's,
with the pixels in front of somebody:

| Option | Cost |
|---|---|
| a per-digit fixed cell — each figure drawn in a box of the widest figure's width | no new font; the layout does the work; the letter-spacing is visibly loose on `1` |
| tabular figures from the product face — many families ship `tnum`, and instancing can bake it | depends entirely on which face D16 picks, and `lv_font_conv` has no OpenType feature switch, so it means a pre-processed TTF and re-reading the kerning trap in [FONT_MEASUREMENTS](FONT_MEASUREMENTS.md) §2 |
| a second, monospaced display face for digits only | InfiniTime's answer; a whole extra font in flash for ten glyphs, and a typographic mismatch with the rest of the UI |

**A useful consequence for later:** a layout whose digits are already pinned
tolerates the ±4 px software pixel shift that [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md)
§3.5 lists as an AOD burn-in mitigation almost for free. A layout that already
slides 36 px cannot tell the two motions apart.

### 7.2 The dates, and a correction to a rule of thumb

DESIGN_SYSTEM §8 records that Russian runs 15–30 % longer than English. For UI
labels that holds. **For dates it does not, and the direction depends on the
format.** Measured over every weekday × every month, in each language:

| Form | 28 px EN | 28 px RU | wider |
|---|---:|---:|---|
| `Wednesday, 30 September` / `понедельник, 30 сентября` | 383 px | **394 px** | RU, by 11 px |
| with the year | 459 px | **470 px** | RU, by 11 px |
| `30 September 2026` / `30 сентября 2026` | **278 px** | 258 px | **EN**, by 20 px |

The same flip holds at every generated size. The cause is simple once seen:
Russian weekday names are longer (`понедельник`, `воскресенье` — 11 characters)
while Russian genitive month names are *shorter* (`сентября` against
`September`). So **the widest string is EN or RU depending on whether the
weekday is in the format**, and a layout bound taken from one language is wrong
in the other in whichever direction the format chose.

**The months must be genitive.** A Russian date is "30 сентября", not
"30 сентябрь" — the nominative table is the classic bug, and it is also two
characters longer, so measuring the genitive is both the grammatical and the
conservative choice. This is a catalogue requirement, not a formatter one: the
strings need to exist in the form the date uses, and `MONTH[m]` from wasp-os
`clock.py:23` is the anti-pattern.

Whether Russian also needs a *nominative* set for other screens — a month
picker, a header — is an ADR-0010 question and is **not** answered here.

### 7.3 What fits, at a glance

Widths against the two panels' full width, so the layout has a bound. 28 px:

| String | px | 240 px face | 410 px face |
|---|---:|---:|---:|
| `00:00` | 82 | 34 % | 20 % |
| `10:00 AM` | 129 | 52 % | 30 % |
| `--:--` | 50 | 20 % | 12 % |
| `Wednesday, 30 September` | 383 | **159 %** | 93 % |
| `понедельник, 30 сентября` | 394 | **164 %** | 96 % |
| `Wed 30 Sep 2026` | 246 | **102 %** | 60 % |
| `пн 30 сент. 2026` | 236 | 98 % | 57 % |
| `30 Sep` / `30 сент.` | 96 / 114 | 40 % / 47 % | 23 % / 27 % |
| `100%` | 72 | 30 % | 17 % |
| `Charging` / `Зарядка` | 132 / 124 | 55 % / 51 % | 32 % / 30 % |

Read against a real face these are worse than they look, because a 240 px panel
is not 240 px of usable width once `space` padding is taken off both edges.
**At 28 px, no long date form fits the T-Watch at all, and the medium form does
not either.** At 14 px the long form is 196–199 px, which is 81–82 % of the raw
panel width — tight, and only tight because 14 px is the caption size.

**The conclusion this forces is a product one, not a typographic one:** the date
on the 240 px face is a *short* form, and the two panels do not simply scale the
same layout. That is final §54's *"the two displays are not pixel-identical
products"* landing on a specific screen with numbers attached.

### 7.4 The pre-T-037 font ladder had no display size

The baseline measured here generated **14, 16, 20 and 28 px**, and the boot
screen mapped roles onto them:

```cpp
const bool large = width_px >= 400;
case TypeRole::Display:
case TypeRole::Title:
    return large ? &attadipa_montserrat_28 : &attadipa_montserrat_16;
```

So on the T-Watch **`type.display` — the token DESIGN_SYSTEM §4 defines as
"watchface time" — resolves to 16 px.** A 16 px time on a 1.3-inch face is not a
watchface; it is a status bar. On the Waveshare it resolves to 28 px, which on a
410 × 502 panel is small for the same reason.

That function is honestly labelled scaffolding in its own comment, and the
comment gives the reason: `TypeRole` carries no sizes because final §51's four
checks have not been answered and no face is pinned. But the consequence is
concrete and belongs in T-037's dependency list rather than in its surprises: a
generated font at a display size — 48 px, 64 px, or whatever the design asks —
does not exist, and generating one is a change to `generate_ui_fonts.py`, the
checked-in artefacts and `INPUTS.sha256`. `FONT_MEASUREMENTS` has the size
table: Inter at 48 px bpp 4 compressed is 34 997 B, which is affordable and not
free.

T-037 resolved this gap: `tools/font/generate_ui_fonts.py:50` now generates
64 and 96 px display sizes as well, and the Clock pins each numeral into a
fixed-width cell rather than allowing proportional figures to move the face.

---

## 8. The reference matrix

Final §53 gives the cross product — 240×240 / 410×502, Day/Night, Adult/Child,
EN/RU — and says explicitly that *"not every screen needs 16 golden
screenshots"* but that both locales and both geometries must be exercised, and
that the Clock is one of the six minimum screens.

### 8.1 What the simulator can do today, and what it cannot

| Axis | Today | Evidence |
|---|---|---|
| geometry | **yes** — `--board t-watch-s3-plus` / `waveshare-amoled-206` | `sim/options.cpp:112` |
| theme | **yes** — `--theme`, and `T` at runtime | `sim/options.cpp:156` |
| locale | **yes** — `--locale`, and `L` at runtime | `sim/options.cpp:141` |
| **Adult/Child** | **no flag exists** | `sim/options.h` — and final §57 lists *"Adult/Child switching"* as a simulator requirement |
| **a specific time** | **no injection of any kind** | so 00:00, 09:05, 12:00, 23:59 and a rollover cannot be photographed |
| **battery / charging** | no injection | final §57 also asks for *"simulated battery"* |
| node attached / detached | **yes** — `--node` | `sim/options.cpp:128` |
| screenshot | **yes**, but the **first frame only** | `sim/main.cpp:215-223` takes the snapshot, then the frame loop runs |

And the tests that exist are two:

```cmake
foreach(_board t-watch-s3-plus waveshare-amoled-206)
    add_test(NAME simulator_renders_${_board}
             COMMAND attadipa_sim --board ${_board} --frames 3
                     --screenshot ${_attadipa_shot_dir}/${_board}.png)
```

— [`tests/CMakeLists.txt:195-198`](../../tests/CMakeLists.txt). Two boards, EN,
Day, Adult, the boot screen, and the assertion is that the PNG exists and is not
empty. **That is 2 of 16 configurations, of a screen that is not the Clock, with
no comparison against anything.**

So the matrix the issue asks for is not merely unwritten — four of the things it
needs do not exist. They are small, and they are implementation work: an
Adult/Child flag, a time injection, a battery injection, and a screenshot taken
*after* a chosen event rather than on frame one.

### 8.2 What should be asserted, and how

The issue asks which assertions are semantic and which can be screenshots, and
warns against brittle pixel hashes. The split that follows from §7 is:

**Assert as numbers, in a host test with no LVGL and no rendering** — these are
the assertions that catch a regression and can say *why*:

| Property | Why a number and not a picture |
|---|---|
| the formatted time string, per locale, per 12/24 setting, for a table of instants | a wrong string is a wrong string in every theme; testing it four times is testing it once |
| the formatted date string, including the genitive month, for all twelve months | the failure is grammatical, and a screenshot cannot tell you which |
| the rollover: 23:59 → 00:00 advances the date by exactly one day, and 23:59 on 31 December advances the year | this is arithmetic and belongs in arithmetic |
| the degraded string for **every** `Availability` the time can be in, in both locales | the enum has seven values because it has seven remedies, and no two may render identically (`availability.h:8-12`) — that is an assertion over a table, not something to eyeball |
| battery at 0, 9, 10, 99, 100, unknown, charging | boundaries, and `unknown` must not render as `0` |

**Assert as measured widths against the layout's bound** — these catch the
overflow class, and `tools/font/measure_strings.py` already does the measuring:

- the widest time string, at the chosen display size, fits the time box;
- the widest date string **in each language** — noting §7.2, which one is widest
  depends on the format — fits the date row at its long-mode's precondition;
- the widest battery string fits its chip.

A failure here names the string and the two numbers, which is a bug report. A
screenshot diff of the same failure is a picture of two words on top of each
other, which is DESIGN_SYSTEM §8's worked example.

**Photograph, and review by eye** — the 16 configurations, as artefacts for a
human, plus the transitions. These are **not** compared byte-for-byte:

- **against a golden PNG is refused** until determinism is *demonstrated*, not
  assumed. LVGL's own renderer, the SDL backend, the font rasteriser and any
  compiler flag that touches floating point are all upstream of those bytes, and
  a test that fails on an LVGL patch release teaches people to regenerate the
  goldens without looking — which is worse than no test.
- what *can* be asserted mechanically about an image, cheaply and stably: it is
  the expected size; it is not blank and not uniform; its mean luminance is on
  the right side of a threshold for the theme it claims to be (the day/night
  numbers in [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §1 are exactly this
  quantity); no pixel of foreground text sits outside the safe area.

### 8.3 The matrix

16 base configurations × the state axis. **Nothing in this table has been
produced.** The first three axes are switchable today; the mode axis and every
state axis below it need the four missing pieces in §8.1 before a single one of
their rows can be photographed.

| Axis | Values |
|---|---|
| geometry | 240×240 IPS · 410×502 AMOLED |
| theme | Day · Night |
| locale | EN · RU |
| mode | Adult · Child |
| instant | 00:00 · 09:05 · 12:00 · 23:59 · 23:59→00:00 rollover · 31 Dec 23:59→1 Jan |
| date length | the widest EN form · the widest RU form (§7.2 — they are different rows) |
| battery | 0 · 9 · 10 · 99 · 100 · charging · unknown |
| time state | Ready · Stale · Unprovisioned (never set) · Unreachable (node gone) |
| transition | locale switch · theme switch · Adult⇄Child · resume from screen-off |

The full cross product is 16 × 6 × 2 × 7 × 4 = 5 376 and is not a test suite;
it is a way to get 5 376 unreviewed PNGs. The tractable shape, and the one final
§53 actually asks for:

- **16 screenshots** — the base cross product, one representative state
  (a mid-width time, `Ready`, a mid battery). These are the design-review
  artefacts and they go in the pull request.
- **the state axis as numbers**, once, at one geometry — because a formatted
  string does not vary by theme or panel.
- **the width bounds as numbers**, at both geometries and both locales — 4
  combinations, because that is what §7.2 shows actually varies.
- **the transitions as screenshots taken in pairs** — before and after — at one
  geometry, because what they test is that nothing is left behind, and that is
  visible at either size.

That is 16 + 4 + a handful of pairs, and every one of them has a reason to exist.

---

## 9. The seam between T-037 and always-on

**T-037 does not build an AOD, and it must not be blocked waiting for one.**
Two owner questions stand open on `main` and neither is an agent's to answer:

- **A9** — does the day theme keep its near-white page on the emissive panel?
  **UNKNOWN**, asked as [#52](https://github.com/hleserg/Attadipa/issues/52).
- **A10** — what does Attadipa do about static content on the AMOLED?
  **UNKNOWN**, asked as [#53](https://github.com/hleserg/Attadipa/issues/53),
  and [WAVESHARE_ARRIVAL](WAVESHARE_ARRIVAL.md) §3.5 records that it *should not*
  be decided before the measurements in its §5 steps 7 and 8 exist.

The seam that leaves both open while letting T-037 finish is already implied by
`core/power_state.h`, and it is one sentence: **the Clock renders for
`PowerState::Active` and releases its wake for everything below.** An
always-on face is then a *different render of the same state* entered at a
different power state — not a mode inside the Clock, and not a second copy of
the state.

Three things T-037 can do that cost nothing now and buy the AOD later, and one
it must not:

| | Why |
|---|---|
| **do** — pin the digits (§7.1) | a layout that already holds still tolerates a ±4 px pixel shift, which is one of A10's six options |
| **do** — keep every colour on a `ColorRole` | so an AOD palette is a table swap, exactly as OD-11 requires of installable themes |
| **do** — make the full-render entry point independent of prior state (§5) | resume from an unknown panel state is a full render, and that is the AOD's normal entry |
| **do not** — add an "ambient" flag to the Clock's state | it would be a presentation decision stored as domain state, and A10 may make it a separate face entirely |

**Everything physical about this is NOT EXECUTED — HARDWARE REQUIRED:** whether
the panel retains its content across a display-off (which decides whether resume
is full or incremental — wasp-os's `wake()` comment at `clock.py:50-52` asserts
retention for *its* panel and that assertion does not transfer), AOD current,
burn-in behaviour, outdoor readability, and true-black against warm-dark. No
number from any upstream project transfers: a saving measured on someone else's
panel, driver and duty cycle is **UNKNOWN** here, not "roughly the same".

---

## 10. What stays open

**UNKNOWN — needs a decision or a source:**

| | |
|---|---|
| 12-hour or 24-hour, and whether it is a user setting at all | **Q4** in [OPEN_QUESTIONS](OPEN_QUESTIONS.md). A product decision. §88 does not mention it; InfiniTime shows it also reorders the date, so it is not free |
| the date format, per locale — long, medium or short, weekday or not | **Q5**. §7.3 shows the 240 px face cannot take a long one; the *choice* is the design's |
| time zones and DST | **Q6**. Nothing in `core/` models a zone. A device that only ever shows local time can carry an offset; one that receives a node's timestamp cannot avoid the question |
| whether Russian needs nominative month names as well as genitive | **Q7**, an ADR-0010 question (§7.2) |
| whether the Waveshare's PCF85063 interrupt is routed at all | **D19**. HARDWARE_MATRIX:332 has bus, address and rail, and no INT, while the T-Watch's row has one — §4 |
| whether GPIO 17 (T-Watch RTC INT) is usable as a wake source in each sleep state | **D19**, second half. It is *plausibly* inside the ESP32-S3's RTC-capable GPIO range; **not traced to the datasheet by this work**, and code must not depend on it until it is |
| ~~which typeface, and therefore every number in §7~~ | **Resolved 2026-08-26:** Nunito Sans Regular 400; the Montserrat numbers in §7 remain historical measurements |

**NOT EXECUTED — HARDWARE REQUIRED:**

| | |
|---|---|
| panel content retention across a display-off, on either board | decides full vs incremental on resume (§9) |
| AOD current, on the CO5300 | A10 |
| burn-in / image retention behaviour and timescale | A10 |
| outdoor readability of either theme | final §55 |
| render performance of any font at any size | `FONT_MEASUREMENTS` already records this gap; a Clock redrawing once a minute is the cheapest possible case, so it is unlikely to be the thing that finds the answer |
| whether a per-minute RTC alarm actually wakes either board | §4 |

**What T-037 needs that is implementation work, not design work** — filed so it
is in the plan rather than discovered:

1. **a display-sized generated font** (§7.4) — `generate_ui_fonts.py`, the
   checked-in artefacts and `INPUTS.sha256`;
2. **a calendar** — `WallTime` → civil fields, without undoing `clock.h`'s
   missing subtraction (§1);
3. **temporal strings in the catalogue**, months in the genitive (§7.2);
4. **simulator injection** — Adult/Child, a chosen instant, a battery state, and
   a screenshot after an event rather than on frame one (§8.1);
5. **a tick contract on `AppManifest`**, which belongs to T-018/T-024 (§5).

None of the five is large. All five are in front of T-037 rather than inside it,
and finding that out was the point of doing this first.
