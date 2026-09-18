# The localization pipeline

[ADR-0010](../../docs/adr/0010-localization.md) in code. One catalogue,
[`l10n/strings.toml`](../../l10n/strings.toml); a generator; three checks that
fail a build; and a fourth that proves the three can fail.

| Script | What it does |
|---|---|
| [`catalogue.py`](catalogue.py) | reads and validates the catalogue. The only parser — the generator and the glyph check share it so they cannot drift |
| [`gen_strings.py`](gen_strings.py) | writes `l10n/include/attadipa/l10n/string_id.h` and `l10n/src/catalogues.cpp`. `--check` fails if the committed copies are stale |
| [`check_glyphs.py`](check_glyphs.py) | fails if a catalogue string needs a character outside [`tools/font/charset.py`](../font/charset.py) |
| [`selftest.py`](selftest.py) | runs the checks over a catalogue of deliberate mistakes and requires each to be rejected **for its own reason**, and over correct catalogues that must still be accepted. It prints both counts as it finishes, so this line does not carry them: it said twelve and one for a run that had grown to twenty-three and three |

```bash
python3 tools/l10n/gen_strings.py          # after editing strings.toml
ctest --test-dir build-host -L localization
```

## Why the generated files are committed

So the C++ build needs no Python — an ESP-IDF build would otherwise have to
provide one — and so the enum is readable in the tree and in a diff. The cost is
that they can go stale, and that cost is paid by `--check` running as a test
rather than by anyone remembering.

## The checks, and which one matters

ADR-0010 §3 names three. Two of them are cheap:

- **Uniqueness** is enforced by the file format. A repeated `[table]` is a TOML
  parse error, so this check exists but is not ours. `duplicate_id.toml` keeps
  that a tested claim rather than a remembered one.
- **Coverage** is a generator failure: a missing `en` or `ru` entry cannot
  produce a table, so it cannot produce a build.

The third is the one that is invisible without a machine:

- **Font subset.** A Russian string containing a character the embedded font
  does not have is a *blank on the screen*, in one locale, on hardware. Nothing
  else in the build notices — not the compiler, not the linker, not a test that
  compares strings.

It compares the catalogue against `charset.py` rather than against a font,
because no font is chosen yet ([D16](../../docs/research/OPEN_QUESTIONS.md)) and
none is vendored. Transitivity carries it: catalogue ⊆ charset, and the font is
generated *from* charset, so the font can draw the catalogue.
[`tools/font/check_coverage.py`](../font/check_coverage.py) guards the other
half on the day there is a font to guard.

The simulator has a runtime sibling of this check, which asks the font that is
actually linked in. Today the two disagree on purpose — see below.

## Three things the generator refuses that look fine

**`ru.other`.** Russian's CLDR cardinal rule selects `one`, `few` or `many` for
every whole number; `other` is unreachable for an integer. An entry there is a
translation that will never be shown, so it is an error rather than a spare.
`tests/test_l10n.cpp` sweeps every remainder class to keep that true rather than
believed.

**Placeholders that differ between locales.** `%u` in English and `%s` in
Russian is undefined behaviour at the `snprintf` call, and no compiler warning
can reach it, because by then the format string is a runtime value read out of a
table.

**A plural placeholder that is not the count.** Locales agreeing is necessary
and not sufficient: `%s` in all five forms of an entry agrees with itself, and
`format_plural` still hands `snprintf` an integer where it will read a pointer.
So a plural form is checked against the one argument that call actually passes
— exactly one conversion, reading an `unsigned int` (`%u`, or `%o`/`%x`/`%X`),
with an optional width, precision and `-`/`0` flag — plus `#` with `%o`, `%x`
or `%X`, which changes how the number is spelled and not what is read — and no
length modifier.
`%%` is a literal and does not count; a `%` this parser does not recognise at
all, like `%q` or `%*u`, is refused rather than ignored, because `snprintf`
does not ignore it either.

Singular strings keep their own placeholders and their own typed call sites —
`"%u.%u km"` and `"heard %s ago"` both ship — so this contract is the plural
API's alone, and the check says so in the message when it fails.

## What does not work yet, and why it is not hidden

LVGL ships Montserrat and unscii. Both are Latin — Montserrat's own generated
header says `-r 0x20-0x7F,0xB0,0x2022`. **No built-in LVGL font has Cyrillic**,
so the simulator cannot draw the Russian catalogue at all:

```
$ SDL_VIDEODRIVER=dummy attadipa_sim --board t-watch-s3-plus --locale ru --frames 2
  U+0412 cannot be drawn — first seen in 'diagnostic_capabilities'
  ...
l10n: 26 codepoint(s) of the ru catalogue cannot be drawn by the font in this build.
```

Twenty-six in Russian, and seven even in English, because a language is named in
itself and `Русский` is in the English catalogue too. The simulator names the
codepoints rather than rendering boxes and leaving a reviewer to guess.

This is ADR-0010 §1's argument arriving on schedule: *a Latin-only font is not a
font missing some characters — it is a different artefact.* It stops being true
when the font pipeline's output is linked into the simulator, which needs the
font choice (D16) and the asset pipeline (T-034).
