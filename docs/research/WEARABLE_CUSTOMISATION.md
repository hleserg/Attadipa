# Design customisation on wearables — what the field actually does

Status: **research, complete for its questions.** Read 2026-08-22. Every claim
below carries a source; where a claim is folklore rather than documentation, it
says so. Nothing here is a decision — the recommendations at the end are inputs
to tasks, not tasks.

Raised by the owner, who asked for the survey *instead of* a feature task:

> *"надо подумать че по кастомизации дизайна вообще учудить. Давай так лучше,
> забей на это задание а вместо этого назначь в план исследование по
> кастомизации дизайна на носимых смарт часах. Че кто и как делает, как
> реализует, какие-то удачные дизайнерские и программные фишки поищи."*

The starting requirements are [OD-11](OWNER_DECISIONS.md#od-11--themes-are-installable-and-the-layout-survives-them):
themes, fonts, icons and animations that a user can download and install like
applications, with the layout surviving them — plus the Flipper-style idle
animation the owner named, and *"чтобы детишкам нравилось"*.

---

## 1. The one thing the whole industry agrees on

**Every platform that started with executable watch faces has moved away from
them, and none has moved back.**

| Platform | Started as | Now |
|---|---|---|
| Wear OS | Android service rendering in code (AndroidX / Wearable Support Library) | **Watch Face Format** — declarative XML, no executable code at all. Legacy faces could no longer be installed from Google Play from **14 January 2026** |
| Fitbit | JavaScript + SVG + CSS apps and clock faces | third-party **apps** dropped on Sense 2 / Versa 4; clock faces kept |
| Samsung | Tizen native, then Android | **Watch Face Studio** — a no-code editor emitting tag expressions and conditional lines |
| Garmin | Monkey C, still executable | executable, but fenced in by a bytecode watchdog and a **power budget the device enforces at runtime** |
| Pebble | C, executable, 24 KB of app RAM | discontinued; the community kept the C SDK |
| InfiniTime | C++ compiled into firmware | still compiled in — a custom face means rebuilding and reflashing |

Google's stated reasons for the move are worth quoting because they are the same
reasons this project would have:

> *"the Wear OS platform handles the logic needed to render the watch face so
> developers can focus on creative ideas rather than code optimizations or
> battery performance"* — and on dual-chip watches the platform can run a
> declarative face **on the low-power co-processor without waking the main
> processor at all**, which is impossible if the face is arbitrary code.

Secondary but real: a declarative face **cannot be reviewed for battery
behaviour by reading it**, and an executable one cannot be reviewed at all at
scale. Faces also stop needing updates to benefit from platform improvements.

**For Attadipa this is close to settled by inheritance.** OD-11's default answer
to "may a theme carry executable content" was already **no**; the industry
evidence is that every platform that answered yes later reversed it, at
considerable cost to its developers.

---

## 2. The numbers, because this is where customisation actually fails

Wear OS is the only platform publishing hard limits, and they are the most useful
artefact in this survey. From the Wear OS app quality guidelines and the WFF
documentation, read 2026-08-22:

| Rule | Limit |
|---|---|
| **Ambient pixels lit** (WO-P7) | **≤ 15 %**, averaged across the face: opaque white = 100 %, black = 0 %, RGB interpolated linearly. Sampled at ~10-minute intervals across a whole day, and **every** sample must pass |
| Memory, ambient (WO-P8) | **10 MB** of assets |
| Memory, interactive (WO-P8) | **100 MB** of assets |
| Complication slots (WO-P10) | **8** per face |
| Watch face XML source (WO-G11) | **10 MB** |
| Distinct face shapes (WO-G10) | **10** `<WatchFace>` elements |
| Essential text (WO-V14) | **≥ 12 sp** |
| Non-essential text (WO-V14) | **≥ 10 sp** |
| Touch targets (WO-V2) | **≥ 48 × 48 dp** |

How the memory figure is computed is as instructive as the figure: decompress,
apply the optimisations the platform would apply (resize to the screen, crop
transparent pixels, downsample to RGB565 where lossless), then
`4 × w × h` for RGBA8888, `2 × w × h` for RGB565, `w × h` for ALPHA_8. For an
animation, **the union of the bounding boxes across all frames**. Ambient is
budgeted as up to three full-screen layers — background, moving parts, the rest —
and the documentation notes that *large bitmap fonts usually dominate the ambient
budget*.

**Two of these land on Attadipa immediately.**

- **`touch.min.adult` is 44 dp in `ui/tokens.h` and Wear OS requires 48 dp.**
  The 44 came from the touch-target literature the 160 dpi reference belongs to;
  Wear's own number for a wrist is larger. On the T-Watch that is 61 px versus
  66 px — 7.0 mm versus 7.6 mm — which is not a rounding difference. **Filed as
  a finding, not changed here:** the token is a design decision and the two
  sources genuinely disagree.
- **The 15 % ambient rule is the only quantitative always-on constraint anybody
  publishes**, it is trivially computable from a rendered frame, and Attadipa
  already computes per-pixel arithmetic for contrast. It is a candidate for
  exactly the same treatment: a number the installer checks, not a guideline.

Garmin's approach to the same problem is the interesting counter-design. Rather
than a static budget it enforces at runtime: `onPartialUpdate()` is called each
second **"as long as the device power budget is not exceeded"**, the face must
`setClip()` to the smallest possible region, and if a call exceeds the budget
*"the partial update will not draw"* and a power-budget-exceeded callback fires.
Separately a **bytecode watchdog** counts instructions executed before control
returns to the VM. So Garmin permits arbitrary code and then makes the platform,
not the developer, the thing that decides when it stops.

Pebble's constraint is the historical baseline and puts the rest in perspective:
**128 KB of RAM total, of which 24 KB was the app's**, with 84 KB for the OS,
12 KB for a background worker and 8 KB for app services — and a thriving
watch-face ecosystem regardless.

---

## 3. Animations, which is what the owner actually asked about

### Flipper Zero is the closest thing to a specification

It is worth reading in full because it is small, complete, and solves the exact
problem — an idle animation with personality, installable as data, on a device
with no memory. The format:

```
/dolphin/<AnimationName>/
    frame_0.bm … frame_N.bm      the frames, in Flipper's own bitmap format
    meta.txt                     what to do with them
manifest.txt                     which animations exist and when each may appear
```

`meta.txt` fields, with their documented meanings:

| Field | Meaning |
|---|---|
| `Width`, `Height` | frame size, **max 128 × 64**; frames align bottom-left, not configurable |
| `Passive frames` | the idle loop. **Minimum 1** — an animation without one does not work |
| `Active frames` | frames played once on a trigger; 0 if none |
| `Frames order` | the bitmap indices in sequence, passive first then active. Every file must be referenced at least once; frames may repeat |
| `Active cycles` | how many times the active sequence repeats. Must agree with `Active frames` or compilation fails |
| `Frame rate` | integer fps, **minimum 1, recommended 1–8** |
| `Duration` | seconds before switching to another animation. Default **3600** |
| `Active cooldown` | seconds after an active burst before it can trigger again |
| `Bubble slots` | count of random speech-bubble sequences; one is chosen at random per trigger |

`manifest.txt` then gates each animation on device state — `Min butthurt` /
`Max butthurt` (a mood value, 0–14), `Min level` / `Max level` (1–3) and a
`Weight` for random selection.

**Four ideas here are directly transferable and one is a warning.**

1. **The passive/active split is the power model in disguise.** A cheap loop runs
   when nothing is happening; the expensive sequence only plays on an event. On a
   watch the trigger is already there and the owner named it — *"поднимает
   человек руку"*. Wrist-raise is exactly Flipper's "active", and the rest of the
   day is passive or nothing at all.
2. **1–8 fps is the recommended range**, on a device with no battery anxiety at
   all. Anybody proposing 30 fps on a watch face should have to explain why
   Flipper does not.
3. **`Duration` and `Active cooldown` exist so that an animation cannot dominate.**
   A pack cannot pin one animation on screen forever, and cannot re-trigger the
   expensive path continuously. Both are limits the *format* imposes on the
   *content*, which is the pattern OD-11 needs.
4. **Content is gated on device state, and the gate is data.** `manifest.txt`
   chooses by mood and level, not by code. Attadipa has richer state to gate on —
   battery, mesh, GNSS trust, Child Mode — and the same mechanism fits.
5. **The warning: mismatched fields are a compile error, and users hit it.** Both
   `Active cycles` and `Active cooldown` must agree with `Active frames` or the
   pack does not build. A community tool exists purely to write manifests
   correctly. A format with cross-field invariants needs a validator shipped with
   it, or the invariants become somebody's bad afternoon.

### What the watch platforms allow

Wear OS's WFF has animated images with transforms, and its memory accounting
takes the **union of frame bounding boxes** — so a full-screen animation costs a
full-screen buffer even if only the centre moves. That is an incentive shaped
into the arithmetic: keep the moving part small and it is cheap.

Garmin permits timers and animation **only in high-power mode**, which lasts
about ten seconds after a gesture; in low power there are no timers and
`onUpdate()` is called once a minute. That is the same shape as Flipper's
passive/active, arrived at from the other direction.

---

## 4. Packaging and distribution, briefly

| Platform | Unit | Notes |
|---|---|---|
| Wear OS | APK containing only resources and XML | validated by Google's open-source [WFF validator and memory validator](https://github.com/google/watchface) before submission |
| Zepp OS | `.zpk`, containing `device.zip` and `app.zip`, configured by `app.json` with a `configVersion` | installable in developer mode by QR code |
| Bangle.js | JS files served by an **App Loader** web page over Web Bluetooth; apps described in `metadata.json` | must be HTTPS for Web Bluetooth. Filenames limited to 28 characters |
| Flipper | a folder on the SD card plus a manifest entry | no signing, no validation beyond the compile step |
| InfiniTime | a firmware rebuild | there is an external-resources mechanism for images and fonts, but a **watch face is C++ compiled in**; the user-space limit is about 400 KB |

Two observations.

**The validator is part of the format.** Google ships one, and the Flipper
community wrote one because the format needed it. A declarative format without a
validator is a format whose rules are enforced by whichever renderer the author
happened to test on.

**Bangle.js's app loader is the most interesting distribution model for a device
like this** — a static web page over Web Bluetooth, no store, no server, self
hostable, `metadata.json` as the index. It costs nothing to run and it is exactly
the shape a small project can maintain. Its cost is also visible in its own
documentation: every app switch is a full reset, so nothing persists that was not
written to flash.

---

## 5. Accessibility under customisation — a negative result worth recording

**No platform surveyed guarantees legibility for a user-installed face.** The
industry answer is *system-level overrides* rather than *content validation*:

- Apple Watch — Bold Text, Text Size / Dynamic Type, Reduce Transparency;
- Samsung — high-contrast fonts (forces white), colour correction, greyscale,
  magnification, bold font and font size.

Wear OS's quality guidelines set minimum text sizes and touch targets for *apps*,
and the WFF quality rules constrain a face's memory and ambient pixels — but the
rules that are checked are the ones about **power and memory**, not the ones
about **whether the result can be read**.

**This is a gap, and Attadipa is unusually well placed to fill it.** `ui/color.cpp`
already computes WCAG 2.1 relative luminance and contrast ratios, and the same
arithmetic already found two failures in the *owner's own* palette that nobody
had noticed by looking. Refusing — or honestly labelling — a theme whose text
cannot be read is a check nobody else performs, it costs almost nothing, and it
is the difference between *"install anything"* and *"install anything and the
watch still works"*, which is exactly OD-11's *"чтобы всё не поехало"*.

---

## 6. What to copy, what to avoid

**Copy**

1. **Declarative, not executable.** Universal, expensive to reverse, and Google's
   stated reason — the platform, not the pack, owns rendering and therefore
   power — applies verbatim to a device with a co-processor-free 240 × 240 panel
   and a coin-sized battery.
2. **Flipper's passive/active split, with wrist-raise as the trigger.** It is the
   power model and the delight in one mechanism, and the owner described it
   independently before anybody read the format.
3. **Limits that live in the format, not in a style guide** — Flipper's
   `Duration` and `Active cooldown`, Wear's 15 % and its memory arithmetic. A
   theme that *cannot* misbehave beats a theme that is asked not to.
4. **A validator shipped with the format**, run at install time on the device and
   available to authors before they publish. Google ships one; Flipper's community
   had to write one.
5. **Bangle.js's app-loader shape** for distribution: a static index, no store, no
   server, self-hostable.
6. **The contrast check nobody else does.** The arithmetic already exists here.

**Avoid**

1. **Executable faces.** Every platform that shipped them regretted it. Garmin's
   mitigations — a bytecode watchdog and a runtime power budget with a
   "you were cut off" callback — are the price of keeping them, and they are not
   cheap to build.
2. **Letting a pack control layout.** OD-11 already says a theme carries colours,
   a font and icons and never a pixel count. The row that broke on 2026-08-22
   with *our own* two labels is the argument: layout fails from ordinary content,
   without anybody being malicious.
3. **Cross-field invariants without a validator.** Flipper's `Active cycles` must
   agree with `Active frames`; the community's answer was a manifest-writing tool.
4. **Full-screen animation.** The memory arithmetic that everybody uses takes the
   union of frame bounding boxes, so a small moving element is cheap and a
   full-screen one is not — regardless of how little of it changes.
5. **Assuming ambient is a dimmer version of interactive.** Wear budgets ambient
   at a tenth of interactive memory and 15 % of pixels. On the Waveshare AMOLED
   an unlit pixel genuinely costs nothing, which makes the rule more relevant
   here rather than less.

---

## 7. What this survey did **not** establish

- **No power measurement.** Every figure above is somebody else's published limit
  or a documented behaviour. What an idle animation costs on *these* two panels
  is `UNKNOWN` and needs hardware — the T-Watch's IPS and the Waveshare's AMOLED
  will not give the same answer, and the AMOLED's answer depends on which pixels.
- **No format proposed.** Deliberately: OD-11 says the format, distribution and
  signing are not decided, and a survey is not the place to decide them.
- **Apple and Fitbit are thinly covered.** Apple's model is complications inside
  first-party faces rather than user-authored faces, so it constrains little that
  matters here; Fitbit's third-party story has been shrinking since Fitbit Studio
  shut down on 20 April 2023.
- **Zepp OS's actual capability envelope is unread.** `app.json` and `.zpk` are
  established; what the format can express is not.

## Sources

Read 2026-08-22.

- [Watch Face Format — Android Developers](https://developer.android.com/training/wearables/wff)
- [WFF: optimise memory usage](https://developer.android.com/training/wearables/wff/memory-usage)
- [WFF: save power using ambient mode](https://developer.android.com/training/wearables/wff/ambient)
- [Wear OS app quality guidelines](https://developer.android.com/docs/quality-guidelines/wear-app-quality)
- [Introducing the Watch Face Format for Wear OS](https://android-developers.googleblog.com/2023/05/introducing-watch-face-format-for-wear-os.html)
- [Upcoming changes to Wear OS watch faces](https://android-developers.googleblog.com/2025/06/upcoming-changes-to-wear-os-watch-faces.html)
- [Toybox.WatchUi.WatchFace — Garmin Connect IQ API](https://developer.garmin.com/connect-iq/api-docs/Toybox/WatchUi/WatchFace.html)
- [Watch Face Low- and High-Power Modes — Garmin Connect IQ](https://forums.garmin.com/developer/connect-iq/b/news-announcements/posts/changes-to-watch-face-low--and-high-power-modes)
- [Installing custom animations — Flipper Community Wiki](https://flipper.wiki/tutorials/how2anim/guide/)
- [Animation meta.txt in-depth guide — Flipper Community Wiki](https://flipper.wiki/tutorials/Animation_guide_meta/Meta_settings_guide/)
- [Watch Face Studio — Samsung Developer](https://developer.samsung.com/watch-face-studio/overview.html)
- [Apply conditional lines on watch faces — Samsung Developer](https://developer.samsung.com/codelab/watch-face-studio/conditional-lines.html)
- [The configuration of watchface — Zepp OS Developers](https://docs.zepp.com/docs/watchface/app-json/)
- [Bangle.js App Loader customisation — Espruino](https://www.espruino.com/Bangle.js+App+Loader+Custom)
- [Custom watchface — PineTime documentation](https://pine64.org/documentation/PineTime/Watchfaces/Custom_watchface/)
- [Pebble (watch) — Wikipedia](https://en.wikipedia.org/wiki/Pebble_(watch)), for the 128 KB / 24 KB memory split
- [Fitbit Studio retirement — dev.fitbit.com](https://dev.fitbit.com/build/fitbit-studio)
- [Accessibility features on Samsung smart watches](https://www.samsung.com/us/support/answer/ANS10002952/)
- [Use accessibility features on your Apple Watch](https://support.apple.com/en-mide/102253)
