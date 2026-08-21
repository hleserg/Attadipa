#ifndef ATTADIPA_L10N_LOCALE_H
#define ATTADIPA_L10N_LOCALE_H

#include <cstdint>

namespace attadipa::l10n {

// The locales Attadipa ships. Final §50 makes English and Russian a product
// requirement from the first vertical slice, not a later addition, so this is
// not a list that starts at one.
//
// Adding a third is a catalogue change, a plural rule and a font range — see
// ADR-0010. It is deliberately not a plugin mechanism.
enum class Locale : std::uint8_t {
    En = 0,
    Ru = 1,
};
inline constexpr std::uint8_t kLocaleCount = 2;

// The BCP 47 tag, for logs and for a settings value on the wire. Not for
// display: a user picks "Русский", not "ru".
const char* to_string(Locale locale);

// CLDR cardinal categories. Only the four an integer count can select in the
// locales above; `Two` and `Zero` exist in CLDR for other languages and are
// left out rather than carried as dead enumerators.
//
// `Other` is English's second form. In Russian it is **unreachable for an
// integer** — the rule selects one, few or many for every whole number — which
// is why the catalogue format rejects `ru.other` instead of accepting a string
// that would never be shown.
enum class PluralCategory : std::uint8_t {
    One = 0,
    Few = 1,
    Many = 2,
    Other = 3,
};
inline constexpr std::uint8_t kPluralCategoryCount = 4;

const char* to_string(PluralCategory category);

// Which form of a counted string `count` needs.
//
// The rule is CLDR's, re-expressed rather than copied — see the localization
// record in docs/research/REUSE_LEDGER.md. `count == 1 ? a : b` is wrong in
// Russian for most numbers and wrong in a way that reads as a typo, which is
// why this is a function with a test vector and not an inline conditional.
//
// Counts are unsigned because a count of things is not negative. If something
// ever needs a signed quantity in a plural — a temperature delta, say — it
// needs its own rule, not a cast.
PluralCategory plural_category(Locale locale, std::uint32_t count);

}  // namespace attadipa::l10n

#endif  // ATTADIPA_L10N_LOCALE_H
