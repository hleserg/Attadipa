# 0010 — English and Russian from the first screen

Status: **accepted**
Date: 2026-08-21

Accepted rather than proposed, because it is not our decision to make. Final
§50 states it as a binding product requirement, and the first UI screen cannot
be written without it settled — final §74 forbids building on a decision that is
nominally provisional while treating it as fixed.

## Context

The repository contained **nothing** about localization. Not a deferred plan,
not a backlog item, not a note. The only occurrences of the word "Russian" were
statements that the specification documents happen to be written in it.

Final §50 makes English and Russian a product requirement in the same register
as MeshCore compatibility and standalone operation:

> Attadipa must support at least English / Русский **from the first implemented
> UI vertical slice**. Localization is architecture, not later polish.

There are three specific reasons this cannot be deferred, and each of them is a
thing that gets expensive rather than merely annoying:

1. **The font.** An embedded LVGL font is a generated glyph bitmap with a fixed
   character range. A Latin-only font is not a font missing some characters —
   it is a different artefact, of a different size, with different flash cost
   and different rendering performance. Deciding the pipeline after the layout
   is "finished" means measuring the layout against the wrong asset. Final §51
   says so directly: *"Do not first create Latin-only embedded fonts and 'add
   Cyrillic later'."*
2. **The layout.** Russian runs longer than English — commonly 15–30 % on UI
   chrome, and much more on short words (`On` → `Вкл` is shorter; `Settings` →
   `Настройки` is longer; `Off` → `Выкл`; `Search` → `Поиск`). A layout tuned
   until the English fits is a layout that breaks in the other locale, and on a
   240 × 240 screen it breaks visibly.
3. **The error path.** Once a service, a node or a companion has emitted an
   English sentence, the UI cannot translate it. Final §50.5 names this exact
   failure. It is not fixable at the boundary later, because by then the
   structure the translation needed has been thrown away.

## Decision

**No user-facing string literal exists above the platform boundary. Every
user-visible string is a catalogue lookup, and both catalogues ship together
from the first screen.**

Five parts.

### 1. Strings are identifiers, not text

UI code names a string; it never contains one:

```cpp
label->set_text(tr(StringId::SettingsTitle));
```

`StringId` is a generated enum. The catalogues are generated from a single
source of truth, one entry per identifier per locale. The exact API — return
type, buffer ownership, whether it is `const char*` or a span, how it interacts
with LVGL's own text handling — is a design task (T-033) and is deliberately
not frozen here. What is frozen is that **the identifier is what the code
holds.**

### 2. Both catalogues ship from the first screen

Not English-first-then-Russian. A screen is not done until both exist for it
(final §56). This is the requirement that makes the rest enforceable: a project
that ships one locale and plans the other will discover every one of the three
problems above at the worst moment.

### 3. Fallback is English, and it is loud

A missing Russian string renders the English one — never an empty label, never
the raw identifier, never a placeholder box. Silence is the failure mode that
survives to production.

In development and simulator builds a missing key is logged loudly at the point
of use, and CI fails on it (final §79). The three checks that must be
machine-enforced:

| Check | Fails when |
|---|---|
| Coverage | an identifier exists with no English or no Russian entry |
| Uniqueness | an identifier is defined twice |
| Font subset | a glyph required by any catalogue entry is not in the generated font subset |

The third one is the interesting one, and it is why this ADR and the font
pipeline are the same decision: a Russian string in the catalogue that the
embedded font cannot draw is a blank on the screen, and nothing else in the
build will notice.

### 4. The core does not speak English

Core, services, drivers and both external protocols emit **structured** errors —
a code, plus parameters — and the UI translates at the boundary:

```
NOT_CONFIGURED { what: RegionProfile }        →  "Регион не выбран"
STALE          { age_ms: 47000, limit: 30000 }→  "Данные устарели: 47 с"
NO_FIX         { satellites: 3 }              →  "Нет спутникового сигнала"
```

The error codes are the ones in final §62 and already in
[ARCHITECTURE](../architecture/ARCHITECTURE.md). This ADR adds the rule that
they may **never** be accompanied by a human-readable English string travelling
with them, because that string is what a later reader will render instead of
translating.

This binds the node protocol ([ADR-0005](0005-node-protocol.md)) and the
companion protocol: neither may carry display text. A node reports
`PROVIDER_INCOMPATIBLE { their_version, our_version }`, not
`"Node firmware too old"`.

### 5. User content is never localized

Contact names, mesh message bodies, Android notification text and user labels
are **data**, treated as UTF-8 and rendered unchanged (final §50.6). The chrome
around a message is translated; the message is not.

The practical consequence is that the font subset must cover more than the
catalogues do — a mesh message can contain any UTF-8 the sender chose. What
happens to a glyph the font does not have is a defined behaviour (a visible
replacement, never a silent blank) and not an accident.

### Language is a setting, switchable at runtime

Persisted through `SettingsService` ([ADR-0006](0006-settings-and-bounded-values.md)),
switched without a reboot. Default is chosen at first boot and is changeable
in the first Settings screen — final §88 lists language first among Settings
items, which is the right instinct: a user who cannot read the settings screen
cannot change the language from it.

## Alternatives considered

**Defer Russian until the layout settles.** Rejected — it is the thing final §50
forbids by name, and each of the three reasons above is a cost that only grows.
The font is the clearest: the measurement that decides the layout is taken
against the wrong asset.

**`gettext`-style lookup by English source string** — `tr("Settings")`.
Rejected. It puts user-facing English text back into UI code, which is the rule
this ADR exists to enforce, and it makes the coverage check impossible to run
statically: a key that is a string literal cannot be enumerated at build time.
It also makes two different "Settings" — the app title and the button — one
entry that cannot be translated differently, which is a real problem in Russian
where case endings differ by context.

**Runtime-loaded catalogues from the filesystem.** Rejected for now. It buys
adding a language without a firmware update, and costs a filesystem dependency
on the boot path, a failure mode when the file is missing or corrupt, and a
translation that can disagree with the build. Final §45 makes the same argument
about assets: *"avoid runtime filesystem complexity unless it buys something
real."* Revisit when there is a third language and a user asking for a fourth.

**A general i18n framework — ICU, full CLDR.** Rejected: final §52 says so
outright (*"You do not need a giant desktop CLDR stack"*) and final §95 warns
against exactly this shape of abstraction. What is needed is correct, testable
behaviour for two locales: dates, times, relative times, distances, units, and
**plural forms**.

Plurals are the part that is easy to underestimate. English has two forms.
Russian has three, selected by a rule on the last digit and the last two digits:
`1 сообщение` · `2 сообщения` · `5 сообщений` · `21 сообщение` · `111
сообщений`. A `count == 1 ? singular : plural` helper is wrong in Russian for
most numbers, and it is wrong in a way that looks like a typo rather than a bug.
So: a plural-category function per locale, with a test vector covering
0, 1, 2, 5, 11, 21, 101, 111, 1001 — not a general CLDR engine, and not an
`if`.

## Consequences

**Easier.** Adding a third language is a catalogue and a font-range change, not
a code change. The error model gets structurally better for a reason unrelated
to language: a service that must emit `STALE { age_ms }` instead of `"data is
old"` has been forced to keep the number.

**Harder.** Every screen costs two catalogue entries and a layout that survives
both. Every component must define its overflow behaviour up front rather than
when it first overflows. The font subset must be maintained as a build input,
and it grows.

**Committed to.** A build-time coverage check that can fail. A font pipeline
that generates Cyrillic from the first font it generates. A protocol rule — no
display text on the wire, from either the node or the companion — that has to be
enforced by review because no compiler checks it.

**Testable.** The visual test matrix (final §53) includes both locales for every
covered screen, so a Russian layout break is a failing test rather than a
discovery. CI fails on a missing key, a duplicate key, or a glyph the font
cannot draw.

**Open.** The exact `tr()` signature, catalogue storage format and code
generator (T-033). Whether locale selection at first boot can use anything
better than a default — there is no system locale to read on a bare device, and
guessing from the regulatory region would be wrong (a region is not a language,
and A4 is unanswered anyway).
