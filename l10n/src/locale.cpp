#include "firefly/l10n/locale.h"

namespace firefly::l10n {

const char* to_string(Locale locale)
{
    switch (locale) {
        case Locale::En: return "en";
        case Locale::Ru: return "ru";
    }
    return "??";
}

const char* to_string(PluralCategory category)
{
    switch (category) {
        case PluralCategory::One:   return "one";
        case PluralCategory::Few:   return "few";
        case PluralCategory::Many:  return "many";
        case PluralCategory::Other: return "other";
    }
    return "??";
}

namespace {

// English, CLDR cardinal: `i = 1 and v = 0 → one`, everything else `other`.
// `v` is the number of visible fraction digits and is always 0 here, because a
// count is an integer.
PluralCategory english(std::uint32_t count)
{
    return count == 1 ? PluralCategory::One : PluralCategory::Other;
}

// Russian, CLDR cardinal, for integers:
//
//     v = 0 and i % 10 = 1 and i % 100 != 11                       → one
//     v = 0 and i % 10 = 2..4 and i % 100 != 12..14                → few
//     v = 0 and i % 10 = 0, or i % 10 = 5..9, or i % 100 = 11..14  → many
//
// The three branches are exhaustive over the whole numbers: every last digit is
// covered by one of them, so `other` is unreachable. That is not an accident of
// this transcription — it is why the catalogue format rejects `ru.other`.
//
// The rule is the reason this file exists. `count == 1 ? singular : plural`
// gives "1 сообщение / 2 сообщение / 5 сообщение", which reads as a typo rather
// than as a bug and therefore survives review.
PluralCategory russian(std::uint32_t count)
{
    const std::uint32_t last  = count % 10;
    const std::uint32_t last2 = count % 100;

    if (last == 1 && last2 != 11) {
        return PluralCategory::One;
    }
    if (last >= 2 && last <= 4 && !(last2 >= 12 && last2 <= 14)) {
        return PluralCategory::Few;
    }
    return PluralCategory::Many;
}

}  // namespace

PluralCategory plural_category(Locale locale, std::uint32_t count)
{
    switch (locale) {
        case Locale::En: return english(count);
        case Locale::Ru: return russian(count);
    }
    return PluralCategory::Other;
}

}  // namespace firefly::l10n
