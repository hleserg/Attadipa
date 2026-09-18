"""Prove the catalogue checks can fail, and fail for the right reason.

A check nobody has seen reject anything is a check nobody knows works. This
repository has been bitten by that shape already — `WILL_FAIL TRUE` on a build
test passes on *any* non-zero exit, so a renamed target reported success
(tests/expect_build_failure.cmake). The same discipline applies here: each
fixture in tests/l10n/fixtures/ is a specific mistake, and this asserts both
that it is rejected and that the message names the mistake rather than something
else that happened to go wrong.

  python3 tools/l10n/selftest.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from catalogue import REPO_ROOT, CatalogueError, load  # noqa: E402
import check_glyphs  # noqa: E402

FIXTURES = REPO_ROOT / "tests" / "l10n" / "fixtures"

# fixture -> a fragment the message must contain. The fragment is the *reason*,
# not just any word from the output: a fixture that is rejected for the wrong
# reason is a failing test here, exactly as a build that fails for the wrong
# reason is in expect_build_failure.cmake.
PARSE_CASES = {
    "duplicate_id.toml":     "settings_title",
    "missing_ru.toml":       "has no 'ru' entry",
    "ru_other.toml":         "never be shown",
    "missing_ru_few.toml":   "missing ['few']",
    "format_mismatch.toml":  "same placeholders",
    "empty_value.toml":      "is empty",
    "bad_identifier.toml":   "not a usable identifier",
    # The four the equality check cannot see. Every locale and every form agrees
    # in each of these files, so "the placeholders match" is true of all of them
    # and true of none of the things that matter at the snprintf call.
    "plural_format_type.toml":    "read as a pointer",
    "plural_format_width.toml":   "length modifier 'll'",
    "plural_format_missing.toml": "has 0 count conversion",
    "plural_format_twice.toml":   "has 2 count conversion",
    "plural_locale_not_a_table.toml": "unknown locale(s): ['de']",
}

# Accepted on purpose. Over-strictness is the failure mode of a check written
# from one crash: it is still a rejection, so every negative case above keeps
# passing while correct strings start being refused. This file is the only thing
# that fails when that happens.
ACCEPT_CASES = {
    "plural_format_valid.toml": "a count with width, flags and a literal %%",
}

# ONE SPELLING PER LINE, because a file per spelling is a dozen fixtures that
# differ by one character and nobody reads the twelfth. Each line below is
# written to a real catalogue and put through `load()` -- the same function
# `gen_strings.py` calls -- with the text repeated in all five forms, so the
# placeholder comparison agrees and the count contract is the only thing left
# to object. A fragment is the reason the spelling must be refused; `None` says
# it must be accepted.
SPELLING_CASES = [
    # These three are refused by the rule that covers every string, not by the
    # count contract -- which is why they are worth keeping here as well as in
    # `SINGULAR_CASES`: a plural form is a string like any other first.
    ("%q message",    "begins no conversion this catalogue understands"),
    ("%*u message",   "begins no conversion this catalogue understands"),
    ("%u message %",  "begins no conversion this catalogue understands"),
    # NOT A LITERAL PERCENT, whatever it looks like. Both of these parse as the
    # conversion `%-100%` / `%-60%`, which C leaves undefined at snprintf, and
    # the second is the one that reads as ordinary prose and slipped through:
    # skip percent-terminated matches and it has exactly one count conversion
    # and nothing to object to. Refusing the spelling itself catches both.
    ("%-100% items",  "not a literal percent sign"),
    ("50%-60%: %u",   "not a literal percent sign"),
    ("%+u message",   "flag(s) ['+']"),
    ("%d message",    "reads a signed int"),
    ("%i message",    "reads a signed int"),
    ("%hu message",   "truncates the count"),
    # A percent somebody meant literally. It is not the spelling it looks like:
    # `% d` is one conversion with the space flag, so it passes the count and
    # flag branches and is refused by the type branch -- which is where the
    # hint about `%%` has to be said, and was not.
    ("100% done",     "A literal percent sign is written `%%`"),
    # Accepted: the `#` flag on the conversions C defines it for, and a percent
    # written the way the contract says to write one.
    ("%#x message",   None),
    ("%u of 100%%",   None),
]

# The singular half, which the count contract does not reach at all.
SINGULAR_CASES = [
    # `%-100%` is refused for being what it is, before anything compares the two
    # locales. It used to be caught one step later, as a placeholder mismatch
    # against a Russian line that spells the percent as text -- a real
    # disagreement, but the wrong thing to tell the translator, and it depended
    # on the other locale happening to disagree. Two locales that spelled it
    # the same way were accepted.
    (("0%-100%", "0-100 %"), "not a literal percent sign"),
    # And the comparison itself, which used to be the only check a singular pair
    # got and must keep working: both formats here are valid on their own.
    (("%u km", "%s км"),     "same placeholders"),
    # THE HOLE THE COMPARISON LEAVES, and it is the reason the percent rule is
    # not a plural rule. `FORMAT_RE` does not match a trailing bare `%`, so both
    # of these have the signature `("%s",)`, they agree, and the string reached
    # `apps/src/mesh.cpp:209` -- "                      l10n::tr(StringId::MeshPinned, locale), want);"
    # -- as a format whose last conversion specification is incomplete. Two
    # locales that agree about a mistake still make it.
    (("pinned %s%", "закреплён %s%"), "begins no conversion"),
    # The same hole with a conversion character that does not exist, and with a
    # width snprintf reads an argument for. Neither is a match, so neither is in
    # a signature, so agreeing hid both.
    (("%q here", "%q здесь"),         "begins no conversion"),
    (("%*u m", "%*u м"),              "begins no conversion"),
]

_PLURAL_TEMPLATE = """[count_spelling]
plural = true
en.one = "{text}"
en.other = "{text}"
ru.one = "{text}"
ru.few = "{text}"
ru.many = "{text}"
"""

_SINGULAR_TEMPLATE = """[label]
en = "{en}"
ru = "{ru}"
"""


def _load_text(toml_text):
    """`load()` on a catalogue written to a temporary file."""
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".toml", encoding="utf-8",
                                     delete=False) as handle:
        handle.write(toml_text)
        name = handle.name
    try:
        return load(name)
    finally:
        Path(name).unlink()


# Rejected by the glyph check rather than by the parser: the file is valid TOML
# and a valid catalogue, and still cannot be drawn.
GLYPH_CASES = {
    "glyph_outside_charset.toml": ("U+4E16", "U+1F30D"),
}


def run():
    failures = []

    for name, expected in PARSE_CASES.items():
        path = FIXTURES / name
        if not path.exists():
            failures.append(f"{name}: fixture is missing")
            continue
        try:
            load(path)
        except CatalogueError as exc:
            message = str(exc)
            if expected not in message:
                failures.append(
                    f"{name}: rejected, but not for the reason this fixture is about.\n"
                    f"    expected the message to contain: {expected!r}\n"
                    f"    got: {message}"
                )
            else:
                print(f"  ok  {name:<28} rejected: {message.splitlines()[0][:78]}")
        else:
            failures.append(f"{name}: ACCEPTED, and it must not be. The check is not working.")

    for name, what in ACCEPT_CASES.items():
        path = FIXTURES / name
        if not path.exists():
            failures.append(f"{name}: fixture is missing")
            continue
        try:
            entries = load(path)
        except CatalogueError as exc:
            failures.append(f"{name}: REJECTED, and it must not be -- {what} is valid.\n"
                            f"    got: {exc}")
        else:
            print(f"  ok  {name:<28} accepted: {len(entries)} entries, {what}")

    for text, expected in SPELLING_CASES:
        toml_text = _PLURAL_TEMPLATE.format(text=text)
        try:
            _load_text(toml_text)
        except CatalogueError as exc:
            message = str(exc)
            if expected is None:
                failures.append(f"spelling {text!r}: REJECTED, and it must not be.\n"
                                f"    got: {message}")
            elif expected not in message:
                failures.append(
                    f"spelling {text!r}: rejected, but not for the reason it is about.\n"
                    f"    expected the message to contain: {expected!r}\n"
                    f"    got: {message}"
                )
            else:
                print(f"  ok  {text!r:<28} rejected: {expected}")
        else:
            if expected is None:
                print(f"  ok  {text!r:<28} accepted")
            else:
                failures.append(f"spelling {text!r}: ACCEPTED, and it must not be. "
                                f"Expected {expected!r}.")

    for (en, ru), expected in SINGULAR_CASES:
        toml_text = _SINGULAR_TEMPLATE.format(en=en, ru=ru)
        try:
            _load_text(toml_text)
        except CatalogueError as exc:
            if expected not in str(exc):
                failures.append(f"singular {en!r}/{ru!r}: rejected, but not for its own reason.\n"
                                f"    expected: {expected!r}\n    got: {exc}")
            else:
                print(f"  ok  {en!r} vs {ru!r} rejected: {expected}")
        else:
            failures.append(f"singular {en!r} vs {ru!r}: ACCEPTED, and it must not be. "
                            f"Expected {expected!r}.")

    for name, expected_fragments in GLYPH_CASES.items():
        path = FIXTURES / name
        if not path.exists():
            failures.append(f"{name}: fixture is missing")
            continue
        try:
            load(path)
        except CatalogueError as exc:
            failures.append(f"{name}: rejected by the parser ({exc}); it should reach the "
                            f"glyph check, which is the thing being tested.")
            continue
        import io
        import contextlib
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check_glyphs.main([str(path)])
        output = buffer.getvalue()
        if code == 0:
            failures.append(f"{name}: the glyph check ACCEPTED characters outside the subset.")
            continue
        missing = [f for f in expected_fragments if f not in output]
        if missing:
            failures.append(f"{name}: rejected, but did not name {missing}.\n    got: {output}")
        else:
            print(f"  ok  {name:<28} rejected: names {', '.join(expected_fragments)}")

    if failures:
        print("\nl10n selftest FAILED:\n", file=sys.stderr)
        for failure in failures:
            print(f"  * {failure}", file=sys.stderr)
        return 1

    rejected = (len(PARSE_CASES) + len(GLYPH_CASES) + len(SINGULAR_CASES)
                + sum(1 for _, expected in SPELLING_CASES if expected is not None))
    accepted = len(ACCEPT_CASES) + sum(1 for _, expected in SPELLING_CASES if expected is None)
    print(f"\nl10n selftest: {rejected} deliberate mistakes, all rejected, each for its own "
          f"reason; {accepted} correct catalogue(s) accepted")
    return 0


if __name__ == "__main__":
    sys.exit(run())
