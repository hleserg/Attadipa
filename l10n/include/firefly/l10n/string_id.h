// GENERATED FILE -- do not edit.
//
// Written by tools/l10n/gen_strings.py from l10n/strings.toml. Edit the TOML
// and regenerate; a stale copy of this file is a failing test
// (`ctest -R l10n_generated_is_current`), not a silent divergence.

#ifndef FIREFLY_L10N_STRING_ID_H
#define FIREFLY_L10N_STRING_ID_H

#include <cstdint>

namespace firefly::l10n {

// Every user-facing string, as an identifier. UI code holds one of these and
// never the text -- ADR-0010 §1. That is what makes the coverage check static:
// an enumerator can be counted at build time and a string literal cannot.
enum class StringId : std::uint16_t {
    DiagnosticCapabilities = 0,
    DiagnosticGeometry = 1,
    DiagnosticHardware = 2,
    DiagnosticRadio = 3,
    DiagnosticRadioChip = 4,
    DiagnosticRadioLora = 5,
    DiagnosticRadioMeshcore = 6,
    Language = 7,
    LanguageEnglish = 8,
    LanguageRussian = 9,
    No = 10,
    ProductName = 11,
    Yes = 12,
};
inline constexpr std::uint16_t kStringIdCount = 13;

// Counted strings are a separate type on purpose. `tr(StringId)` on an entry
// that needs a number, or `tr_plural` on one that does not, is then a compile
// error instead of a string with a stray %u in it.
enum class PluralId : std::uint16_t {
    DiagnosticCapabilityCount = 0,
};
inline constexpr std::uint16_t kPluralIdCount = 1;

// The identifier's own spelling, for logs and for the loud missing-string
// path. Never for display: it is not a translation of anything.
const char* string_id_name(StringId id);
const char* plural_id_name(PluralId id);

}  // namespace firefly::l10n

#endif  // FIREFLY_L10N_STRING_ID_H
