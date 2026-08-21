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
FORMAT_RE = re.compile(r"%[-+ #0]*[0-9]*(?:\.[0-9]+)?(?:hh|h|ll|l|j|z|t|L)?[diouxXeEfgGaAcsp%]")


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
    """
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


def _check_formats(entry):
    signatures = {}
    for locale, value in entry.texts.items():
        items = value.items() if isinstance(value, dict) else [("", value)]
        for form, text in items:
            signatures[f"{locale}{'.' + form if form else ''}"] = _format_signature(text)
    distinct = set(signatures.values())
    if len(distinct) > 1:
        detail = "\n".join(f"    {k:<12} {list(v)}" for k, v in sorted(signatures.items()))
        raise CatalogueError(
            f"'{entry.ident}' does not use the same placeholders in every locale:\n{detail}\n"
            f"  The catalogue string reaches snprintf as a *runtime* format, so a mismatch is "
            f"undefined behaviour that no compiler warning will catch."
        )


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
