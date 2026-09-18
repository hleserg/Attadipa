"""Read and validate l10n/strings.toml.

Separate from the generator because two different tools need the same answer to
"what is in the catalogue": the generator that writes the C++ and the check that
asks whether the font can draw it. A second parser would drift from the first,
and the drift would be invisible until a Russian string came out blank on a
wrist.

Everything here raises `CatalogueError` with a message that names the id, the
locale and what to do about it. A check that fails without saying which string
is a check people learn to ignore.
"""
import re
import tomllib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
STRINGS_TOML = REPO_ROOT / "l10n" / "strings.toml"

LOCALES = ("en", "ru")

# CLDR cardinal categories, per locale, for *integers*.
#
# Read out of lv_i18n's generated output (docs/research/REUSE_LEDGER.md), which
# compiles them from cldr-core. The Russian list has no `other`: for an integer
# the CLDR rule can never select it, so an `ru.other` entry is a string that
# would never be shown, and accepting one silently is how a translator's work
# disappears.
PLURAL_FORMS = {
    "en": ("one", "other"),
    "ru": ("one", "few", "many"),
}

ID_RE = re.compile(r"^[a-z][a-z0-9_]*$")

# printf conversions, deliberately without `%n` -- nothing in a catalogue has
# any business writing through a pointer.
#
# In parts, because two different questions are asked of the same match. The
# cross-locale check only needs the whole spelling; the plural check needs to
# know that `%llu` carries a length modifier and `%s` a conversion that is not
# an integer, and a tuple of strings cannot say that without parsing twice.
FORMAT_RE = re.compile(
    r"%(?P<flags>[-+ #0]*)(?P<width>[0-9]*)(?P<precision>\.[0-9]+)?"
    r"(?P<length>hh|h|ll|l|j|z|t|L)?(?P<conv>[diouxXeEfgGaAcsp%])"
)

# What `format_plural` passes is one `std::uint32_t`, which reaches the variadic
# call as an `unsigned int`. These are the conversions that read exactly that.
#
# `d` and `i` are not here. They read the same bits as an `int`, which is only
# defined while the value fits in one, and a count is a `std::uint32_t` whose
# top bit nothing forbids -- so "works for every count we have shipped so far"
# is the claim it would let through, and that is the claim this check exists to
# stop making. `c` takes an int too, and prints a byte rather than a number.
PLURAL_COUNT_CONVERSIONS = ("u", "o", "x", "X")

# `#` is defined for o, x and X only; for u the standard says the behaviour is
# undefined. `+` and a leading space are for signed conversions, so a plural
# form carrying one is either a signed conversion that slipped past or -- far
# more often -- a literal percent somebody forgot to double, as in "100% done",
# which snprintf reads as the conversion `% d`.
PLURAL_COUNT_FLAGS = "-0"
PLURAL_COUNT_HASH_CONVERSIONS = ("o", "x", "X")


class CatalogueError(Exception):
    pass


class Entry:
    """One identifier, in every locale it has to exist in."""

    def __init__(self, ident, is_plural, texts):
        self.ident = ident
        self.is_plural = is_plural
        # plain:  {"en": "Settings", "ru": "Настройки"}
        # plural: {"en": {"one": ..., "other": ...}, "ru": {...}}
        self.texts = texts

    @property
    def enum_name(self):
        return "".join(part.capitalize() for part in self.ident.split("_"))

    def all_strings(self):
        for value in self.texts.values():
            if isinstance(value, dict):
                yield from value.values()
            else:
                yield value


def _format_signature(text):
    """The sequence of conversions in a string, with `%%` dropped.

    Compared across locales. `%u` becoming `%s` in translation is undefined
    behaviour at the snprintf call and nothing in the toolchain warns about it,
    because by then the format string is a runtime value.

    Equality is not safety, and this function is not the whole check: five forms
    that agree on `%s` agree, and still hand an integer to snprintf as a
    pointer. For plural entries `_check_count_format` asks the other question.
    """
    # `!= "%%"` and not `conv != "%"`. The difference used to matter on its own:
    # `FORMAT_RE` matches flags and a width between the two percent signs, so
    # `"0%-100%"` is one match whose conversion is `%`, and dropping every
    # percent-terminated spelling would take it out of the signature as well.
    # `_reject_malformed_percent` now refuses that spelling before this runs, so
    # what is left here is the narrow claim it reads as: `%%` is text and every
    # other match is an argument.
    return tuple(m.group(0) for m in FORMAT_RE.finditer(text) if m.group(0) != "%%")


def _check_id(ident):
    if not ID_RE.match(ident):
        raise CatalogueError(
            f"'{ident}' is not a usable identifier. Use lower_snake_case starting with a "
            f"letter -- it becomes a C++ enumerator, and the mapping has to be reversible."
        )


def _check_plain(ident, table):
    unknown = set(table) - set(LOCALES)
    if unknown:
        raise CatalogueError(f"'{ident}' has unknown locale(s): {sorted(unknown)}")
    for locale in LOCALES:
        if locale not in table:
            raise CatalogueError(
                f"'{ident}' has no '{locale}' entry. Both catalogues ship together from the "
                f"first screen (ADR-0010 §2) -- this is not something the runtime falls back "
                f"out of, because a fallback here would be a permanent English string nobody "
                f"ever notices."
            )
        if not isinstance(table[locale], str):
            raise CatalogueError(f"'{ident}'.{locale} must be a string, not {type(table[locale]).__name__}")
        if not table[locale]:
            raise CatalogueError(f"'{ident}'.{locale} is empty. An empty label is the failure mode "
                                 f"ADR-0010 §3 exists to prevent; write the string or delete the id.")


def _check_plural(ident, table):
    # The same rule `_check_plain` applies one function up, and it belongs here
    # rather than as a second isinstance guard downstream: the loop below reads
    # `LOCALES`, so a third locale was carried, untouched, as far as
    # `_check_formats`, where `.items()` on a plain string raised AttributeError
    # -- a traceback where `gen_strings.py` promises a message naming the id.
    unknown = set(table) - set(LOCALES)
    if unknown:
        raise CatalogueError(f"plural '{ident}' has unknown locale(s): {sorted(unknown)}")
    for locale in LOCALES:
        if locale not in table:
            raise CatalogueError(f"plural '{ident}' has no '{locale}' forms")
        forms = table[locale]
        if not isinstance(forms, dict):
            raise CatalogueError(
                f"plural '{ident}'.{locale} must be a table of forms "
                f"({', '.join(PLURAL_FORMS[locale])}), not a single string"
            )
        expected = set(PLURAL_FORMS[locale])
        got = set(forms)
        missing = expected - got
        extra = got - expected
        if missing:
            raise CatalogueError(
                f"plural '{ident}'.{locale} is missing {sorted(missing)}. "
                f"{locale} needs exactly {list(PLURAL_FORMS[locale])}."
            )
        if extra:
            hint = ""
            if locale == "ru" and "other" in extra:
                hint = (" For an integer, the CLDR rule for Russian can never select `other` -- "
                        "1 selects one, 2 selects few, 5 and 0 and 11 select many. An `ru.other` "
                        "entry is a string that will never be shown.")
            raise CatalogueError(f"plural '{ident}'.{locale} has unexpected form(s) {sorted(extra)}.{hint}")
        for form, text in forms.items():
            if not isinstance(text, str) or not text:
                raise CatalogueError(f"plural '{ident}'.{locale}.{form} must be a non-empty string")


_COUNT_CONTRACT = (
    "  A plural form is a *runtime* format string handed to\n"
    "  `int format_plural(char*, std::size_t, PluralId, std::uint32_t count)`, which\n"
    "  passes exactly one argument: the count, as an unsigned int. So every form has\n"
    "  to contain exactly one conversion, and it has to be that argument -- "
    f"{', '.join('%' + c for c in PLURAL_COUNT_CONVERSIONS)},\n"
    "  with an optional width, precision and `-`/`0` flag -- and `#` with `%o`, `%x`\n"
    "  or `%X`, which changes how the number is spelled and not what is read --, and\n"
    "  no length modifier.\n"
    "  A literal percent is `%%` and does not count. Nothing checks this later: the\n"
    "  compiler cannot see a format it reads out of a table at run time."
)


def _unrecognised_percent(text):
    """The first `%` that is not part of a conversion this parser understands.

    snprintf does not skip one. "If a conversion specification is invalid, the
    behavior is undefined" -- so `%q` and `%*u` are as unsafe as `%s` here, and
    a check that only inspected the conversions it *did* recognise would report
    a format containing one as clean.
    """
    covered = set()
    for match in FORMAT_RE.finditer(text):
        covered.add(match.start())
        if match.group("conv") == "%":
            # The closing percent of `%%`, which is the only percent-terminated
            # spelling that gets this far: `_reject_malformed_percent` refuses
            # every other one before the count contract is asked anything.
            covered.add(match.end() - 1)
    for index, char in enumerate(text):
        if char == "%" and index not in covered:
            return index
    return None


def _check_count_format(ident, locale, form, text):
    """One plural form against the one argument `format_plural` passes.

    Separate from the singular strings on purpose. They are formatted by their
    own call sites with their own arguments -- `"%u.%u km"` and `"heard %s ago"`
    both ship today -- and forcing them through this one-unsigned-int contract
    would reject correct strings to make a sentence shorter.
    """
    where = f"plural '{ident}'.{locale}.{form}"

    index = _unrecognised_percent(text)
    if index is not None:
        raise CatalogueError(
            f"{where} has a '%' that is not a conversion this catalogue understands: "
            f"{text[index:index + 8]!r}.\n{_COUNT_CONTRACT}"
        )

    specs = [m for m in FORMAT_RE.finditer(text) if m.group("conv") != "%"]
    # A percent somebody meant literally does not read as text: snprintf takes
    # the space in "100% done" as the space flag and prints a signed int. It
    # arrives here as one conversion too many, so the hint belongs on both the
    # count and the flag message rather than only on the one it is named for.
    literal = (" A literal percent sign is written `%%`; `100% done` is read by snprintf "
               "as the conversion `% d`." if any(" " in m.group("flags") for m in specs) else "")
    if len(specs) != 1:
        seen = ", ".join(m.group(0) for m in specs) or "none"
        raise CatalogueError(
            f"{where} has {len(specs)} count conversion(s) ({seen}), and must have exactly "
            f"one.{literal}\n{_COUNT_CONTRACT}"
        )

    spec = specs[0]
    length = spec.group("length")
    if length:
        # `h` and `hh` are not the same mistake as `ll`. The argument is still
        # promoted to an int either way, so nothing is read out of bounds --
        # the count is simply cut down before it is printed, which is a wrong
        # number on the screen and no crash anywhere to find it by.
        if length in ("h", "hh"):
            why = (f"the length modifier '{length}' truncates the count before printing it, so "
                   f"a count of 70000 appears as 4464")
        else:
            why = (f"the length modifier '{length}' reads an argument of whatever width that "
                   f"type has on the target, and on any target where that is wider than the "
                   f"count the rest of the number is whatever happened to sit next to it")
        raise CatalogueError(f"{where} uses '{spec.group(0)}': {why}.\n{_COUNT_CONTRACT}")
    conversion = spec.group("conv")
    if conversion not in PLURAL_COUNT_CONVERSIONS:
        if conversion in "sp":
            why = ("the count is read as a pointer and dereferenced. A count of 1 is the "
                   "address 1, and the process faults before the screen is drawn")
        elif conversion in "di":
            why = ("it reads a signed int, which is the same value only while the count fits "
                   "in one. A `std::uint32_t` above INT_MAX does not, and nothing bounds it")
        else:
            why = "it does not read an unsigned int"
        raise CatalogueError(
            f"{where} uses '{spec.group(0)}': {why}.{literal}\n{_COUNT_CONTRACT}"
        )
    bad_flags = [f for f in spec.group("flags") if f not in PLURAL_COUNT_FLAGS
                 and not (f == "#" and conversion in PLURAL_COUNT_HASH_CONVERSIONS)]
    if bad_flags:
        raise CatalogueError(
            f"{where} uses '{spec.group(0)}', whose flag(s) {sorted(set(bad_flags))} are not "
            f"defined for that conversion.{literal}\n{_COUNT_CONTRACT}"
        )


# A `%` conversion is a literal percent only when it is spelled `%%`. Anything
# between the two signs makes it an invalid conversion specification, and C says
# the behaviour of snprintf on one is undefined -- there is no "it prints a
# percent anyway" to fall back on. `"50%-60%: %u"` is the spelling that gets
# here: it reads as prose, it parses as the conversion `%-60%` followed by one
# `%u`, and a count check that skips percent-terminated matches sees exactly one
# count conversion and accepts it.
#
# So it is rejected here, for every entry, rather than inside the plural check
# that found it. The singular strings reach snprintf through their own call
# sites with their own arguments and are just as undefined, and this is the one
# function every string in the catalogue passes through.
def _reject_malformed_percent(where, text):
    for match in FORMAT_RE.finditer(text):
        if match.group("conv") == "%" and match.group(0) != "%%":
            raise CatalogueError(
                f"{where} has {match.group(0)!r}, which is not a literal percent sign. "
                f"Flags, a width or a precision between the two signs make it an invalid "
                f"conversion specification, and snprintf's behaviour on one is undefined. "
                f"Write a literal percent as `%%`: `50%%-60%%`, not `50%-60%`."
            )


def _check_formats(entry):
    signatures = {}
    for locale, value in entry.texts.items():
        items = value.items() if isinstance(value, dict) else [("", value)]
        for form, text in items:
            where = f"'{entry.ident}'.{locale}{'.' + form if form else ''}"
            _reject_malformed_percent(where, text)
            signatures[f"{locale}{'.' + form if form else ''}"] = _format_signature(text)
    distinct = set(signatures.values())
    if len(distinct) > 1:
        detail = "\n".join(f"    {k:<12} {list(v)}" for k, v in sorted(signatures.items()))
        raise CatalogueError(
            f"'{entry.ident}' does not use the same placeholders in every locale:\n{detail}\n"
            f"  The catalogue string reaches snprintf as a *runtime* format, so a mismatch is "
            f"undefined behaviour that no compiler warning will catch."
        )

    # Second, and only for plurals: agreeing is not the same as being right.
    # This runs after the comparison so that a locale that disagrees is reported
    # as a disagreement, which is what the translator who caused it can act on.
    if entry.is_plural:
        for locale, forms in entry.texts.items():
            for form, text in forms.items():
                _check_count_format(entry.ident, locale, form, text)


def load(path=STRINGS_TOML):
    """Every entry in the catalogue, validated.

    Duplicate identifiers are caught by `tomllib` itself -- a repeated `[table]`
    is a parse error in TOML -- so the uniqueness check ADR-0010 §3 asks for is
    enforced by the format rather than by us. `tests/l10n/fixtures` keeps a
    duplicate around anyway, so that "TOML would catch it" stays a tested claim
    rather than a remembered one.
    """
    path = Path(path)
    try:
        raw = tomllib.loads(path.read_text(encoding="utf-8"))
    except tomllib.TOMLDecodeError as exc:
        raise CatalogueError(f"{path}: {exc}") from None

    entries = []
    for ident, table in raw.items():
        if not isinstance(table, dict):
            raise CatalogueError(
                f"'{ident}' is a bare value at the top level. Every entry is a table: "
                f"[{ident}] on its own line, then en = ... and ru = ..."
            )
        _check_id(ident)
        is_plural = bool(table.pop("plural", False))
        # A plural entry may also be recognised by its shape, so that forgetting
        # the flag is an error about the flag rather than a confusing one about
        # types further down.
        looks_plural = any(isinstance(v, dict) for v in table.values())
        if looks_plural and not is_plural:
            raise CatalogueError(f"'{ident}' has per-form tables but no `plural = true`")
        if is_plural and not looks_plural:
            raise CatalogueError(f"'{ident}' is marked `plural = true` but has no forms")

        if is_plural:
            _check_plural(ident, table)
        else:
            _check_plain(ident, table)

        entry = Entry(ident, is_plural, table)
        _check_formats(entry)
        entries.append(entry)

    if not entries:
        raise CatalogueError(f"{path} has no entries")

    by_enum = {}
    for entry in entries:
        if entry.enum_name in by_enum:
            raise CatalogueError(
                f"'{entry.ident}' and '{by_enum[entry.enum_name]}' both become "
                f"StringId::{entry.enum_name}"
            )
        by_enum[entry.enum_name] = entry.ident

    # Sorted by identifier so that the generated files depend on the *content*
    # of strings.toml and not on the order someone typed it in -- reordering the
    # TOML then produces no diff, and a real change produces a small one.
    #
    # Inserting a string does renumber the ones after it. That is safe only
    # because a StringId is never persisted and never crosses a wire: the
    # catalogues are compiled in beside the enum. If either of those ever stops
    # being true, the identifier has to become the stable key and this sort has
    # to stop deciding the numbers.
    entries.sort(key=lambda e: e.ident)
    return entries


def singulars(entries):
    return [e for e in entries if not e.is_plural]


def plurals(entries):
    return [e for e in entries if e.is_plural]
