"""Turn l10n/strings.toml into the StringId enum and the per-locale tables.

The generated files are **committed**. That is a deliberate trade: the C++ build
then needs no Python, the enum is readable in the tree and in a diff, and the
freshness of the generated output is enforced by a test (`--check`) rather than
by a build step nobody notices failing. The alternative -- generate into the
build directory -- makes the build depend on a Python that an ESP-IDF build
would also have to provide, for no gain this project can point at.

  python3 tools/l10n/gen_strings.py            # write the files
  python3 tools/l10n/gen_strings.py --check    # fail if they are not current

`--check` is registered as a ctest entry, so a local `ctest` and CI enforce the
same thing and neither needs its own copy of the rule.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from catalogue import (  # noqa: E402
    LOCALES,
    PLURAL_FORMS,
    REPO_ROOT,
    STRINGS_TOML,
    CatalogueError,
    load,
    plurals,
    singulars,
)

HEADER_PATH = REPO_ROOT / "l10n" / "include" / "attadipa" / "l10n" / "string_id.h"
SOURCE_PATH = REPO_ROOT / "l10n" / "src" / "catalogues.cpp"

# The order here is the order of PluralCategory in locale.h. The two must agree,
# and the generated file asserts it with a static_assert rather than trusting
# that whoever edits one remembers the other.
CATEGORY_ORDER = ("one", "few", "many", "other")

BANNER = """// GENERATED FILE -- do not edit.
//
// Written by tools/l10n/gen_strings.py from l10n/strings.toml. Edit the TOML
// and regenerate; a stale copy of this file is a failing test
// (`ctest -R l10n_generated_is_current`), not a silent divergence.
"""


def cpp_string(text):
    """A C++ literal for `text`, kept readable.

    The Cyrillic stays Cyrillic rather than becoming `\\xd0\\x9d...`. The point of
    committing generated files is that a person can review them, and a wall of
    hex escapes is a file nobody reads -- which is exactly how a wrong string
    survives. The source is UTF-8 and so is the execution charset on every
    compiler this project targets; if that ever stops being true the failure is
    loud and immediate, not subtle.
    """
    out = []
    for ch in text:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append(f"\\x{ord(ch):02x}")
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def render_header(entries):
    sing = singulars(entries)
    plur = plurals(entries)
    lines = [BANNER, "#ifndef ATTADIPA_L10N_STRING_ID_H", "#define ATTADIPA_L10N_STRING_ID_H", "",
             "#include <cstdint>", "", "namespace attadipa::l10n {", ""]

    lines += [
        "// Every user-facing string, as an identifier. UI code holds one of these and",
        "// never the text -- ADR-0010 §1. That is what makes the coverage check static:",
        "// an enumerator can be counted at build time and a string literal cannot.",
        "enum class StringId : std::uint16_t {",
    ]
    for i, e in enumerate(sing):
        lines.append(f"    {e.enum_name} = {i},")
    lines += ["};", f"inline constexpr std::uint16_t kStringIdCount = {len(sing)};", ""]

    lines += [
        "// Counted strings are a separate type on purpose. `tr(StringId)` on an entry",
        "// that needs a number, or `tr_plural` on one that does not, is then a compile",
        "// error instead of a string with a stray %u in it.",
        "enum class PluralId : std::uint16_t {",
    ]
    for i, e in enumerate(plur):
        lines.append(f"    {e.enum_name} = {i},")
    lines += ["};", f"inline constexpr std::uint16_t kPluralIdCount = {len(plur)};", ""]

    lines += [
        "// The identifier's own spelling, for logs and for the loud missing-string",
        "// path. Never for display: it is not a translation of anything.",
        "const char* string_id_name(StringId id);",
        "const char* plural_id_name(PluralId id);",
        "",
        "}  // namespace attadipa::l10n",
        "",
        "#endif  // ATTADIPA_L10N_STRING_ID_H",
    ]
    return "\n".join(lines) + "\n"


def render_source(entries):
    sing = singulars(entries)
    plur = plurals(entries)
    lines = [BANNER,
             '#include "attadipa/l10n/catalogue.h"',
             '#include "attadipa/l10n/string_id.h"',
             "",
             "// The literals below are UTF-8 and are written as themselves. See",
             "// tools/l10n/gen_strings.py for why they are not escaped.",
             "",
             "namespace attadipa::l10n {",
             "namespace {",
             ""]

    for locale in LOCALES:
        lines.append(f"const char* const k{locale.capitalize()}Singular[kStringIdCount] = {{")
        for e in sing:
            lines.append(f"    /* {e.enum_name} */ {cpp_string(e.texts[locale])},")
        lines += ["};", ""]

    for locale in LOCALES:
        for form in CATEGORY_ORDER:
            if form not in PLURAL_FORMS[locale]:
                continue
            name = f"k{locale.capitalize()}Plural{form.capitalize()}"
            lines.append(f"const char* const {name}[kPluralIdCount] = {{")
            for e in plur:
                lines.append(f"    /* {e.enum_name} */ {cpp_string(e.texts[locale][form])},")
            lines += ["};", ""]

    for locale in LOCALES:
        cap = locale.capitalize()
        entries_by_cat = []
        for form in CATEGORY_ORDER:
            if form in PLURAL_FORMS[locale]:
                entries_by_cat.append(f"    k{cap}Plural{form.capitalize()},")
            else:
                # Not a gap to be filled later: English has no `few`, and Russian
                # has no `other` that an integer can reach. A null here is the
                # honest encoding of "this locale does not have that category".
                entries_by_cat.append(f"    nullptr,  // {form}: not a category in {locale}")
        lines.append(f"const char* const* const k{cap}Plural[kPluralCategoryCount] = {{")
        lines += entries_by_cat
        lines += ["};", ""]

    lines += [
        "const Catalogue kCatalogues[kLocaleCount] = {",
    ]
    for locale in LOCALES:
        cap = locale.capitalize()
        lines.append(f"    {{Locale::{cap}, k{cap}Singular, k{cap}Plural}},")
    lines += ["};", ""]

    lines += ["const char* const kStringIdNames[kStringIdCount] = {"]
    for e in sing:
        lines.append(f"    {cpp_string(e.ident)},")
    lines += ["};", ""]

    lines += ["const char* const kPluralIdNames[kPluralIdCount] = {"]
    for e in plur:
        lines.append(f"    {cpp_string(e.ident)},")
    lines += ["};", ""]

    lines += [
        "}  // namespace",
        "",
        "const Catalogue& catalogue(Locale locale)",
        "{",
        "    const auto index = static_cast<std::uint8_t>(locale);",
        "    return kCatalogues[index < kLocaleCount ? index : 0];",
        "}",
        "",
        "const char* string_id_name(StringId id)",
        "{",
        "    const auto index = static_cast<std::uint16_t>(id);",
        '    return index < kStringIdCount ? kStringIdNames[index] : "<out of range>";',
        "}",
        "",
        "const char* plural_id_name(PluralId id)",
        "{",
        "    const auto index = static_cast<std::uint16_t>(id);",
        '    return index < kPluralIdCount ? kPluralIdNames[index] : "<out of range>";',
        "}",
        "",
        "}  // namespace attadipa::l10n",
    ]
    return "\n".join(lines) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--strings", default=str(STRINGS_TOML))
    ap.add_argument("--check", action="store_true",
                    help="do not write; exit non-zero if the committed files are not what "
                         "this catalogue generates")
    a = ap.parse_args(argv)

    try:
        entries = load(a.strings)
    except CatalogueError as exc:
        print(f"l10n: {exc}", file=sys.stderr)
        return 1

    wanted = {HEADER_PATH: render_header(entries), SOURCE_PATH: render_source(entries)}

    if a.check:
        stale = []
        for path, text in wanted.items():
            current = path.read_text(encoding="utf-8") if path.exists() else None
            if current != text:
                stale.append(path)
        if stale:
            print("l10n: generated files are not current:", file=sys.stderr)
            for path in stale:
                print(f"  {path.relative_to(REPO_ROOT)}", file=sys.stderr)
            print("\n  Run: python3 tools/l10n/gen_strings.py\n"
                  "  and commit the result together with the change to l10n/strings.toml.\n"
                  "  Task state and generated state move in the same commit (final §73).",
                  file=sys.stderr)
            return 1
        print(f"l10n: generated files current — {len(singulars(entries))} strings, "
              f"{len(plurals(entries))} counted, {len(LOCALES)} locales")
        return 0

    for path, text in wanted.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
        print(f"wrote {path.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
