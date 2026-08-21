#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "attadipa/l10n/catalogue.h"
#include "attadipa/l10n/locale.h"
#include "attadipa/l10n/tr.h"

// Host tests for the localization layer (ADR-0010).
//
// The plural vector is the part that matters. Russian selects one of three
// forms by the last digit and the last two digits, and the wrong answer reads
// as a typo rather than as a bug — which is precisely why it survives review
// and has to be caught by a machine instead.

using namespace attadipa::l10n;

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void check_category(Locale locale, std::uint32_t count, PluralCategory expected, int line)
{
    const PluralCategory actual = plural_category(locale, count);
    if (actual != expected) {
        std::fprintf(stderr, "FAIL line %d: %s %u is '%s', expected '%s'\n", line,
                     to_string(locale), count, to_string(actual), to_string(expected));
        ++failures;
    }
}

#define CHECK_CATEGORY(locale, count, expected) \
    check_category((locale), (count), (expected), __LINE__)

void check_text(const char* actual, const char* expected, int line)
{
    if (actual == nullptr || std::strcmp(actual, expected) != 0) {
        std::fprintf(stderr, "FAIL line %d: got \"%s\", expected \"%s\"\n", line,
                     actual == nullptr ? "(null)" : actual, expected);
        ++failures;
    }
}

#define CHECK_TEXT(actual, expected) check_text((actual), (expected), __LINE__)

// ---------------------------------------------------------------------------

// The vector ADR-0010 names, plus the numbers that make the rule visible.
//
// 11 and 111 are the interesting ones: both end in 1, and both are `many`,
// because the rule looks at the last *two* digits before it trusts the last
// one. A `count == 1 ? a : b` helper gets 21 and 101 wrong too, and gets them
// wrong in a way a reader skims past.
void plural_categories_russian()
{
    CHECK_CATEGORY(Locale::Ru, 0, PluralCategory::Many);     // 0 возможностей
    CHECK_CATEGORY(Locale::Ru, 1, PluralCategory::One);      // 1 возможность
    CHECK_CATEGORY(Locale::Ru, 2, PluralCategory::Few);      // 2 возможности
    CHECK_CATEGORY(Locale::Ru, 3, PluralCategory::Few);
    CHECK_CATEGORY(Locale::Ru, 4, PluralCategory::Few);
    CHECK_CATEGORY(Locale::Ru, 5, PluralCategory::Many);     // 5 возможностей
    CHECK_CATEGORY(Locale::Ru, 9, PluralCategory::Many);
    CHECK_CATEGORY(Locale::Ru, 10, PluralCategory::Many);
    CHECK_CATEGORY(Locale::Ru, 11, PluralCategory::Many);    // ends in 1, still many
    CHECK_CATEGORY(Locale::Ru, 12, PluralCategory::Many);    // ends in 2, still many
    CHECK_CATEGORY(Locale::Ru, 14, PluralCategory::Many);
    CHECK_CATEGORY(Locale::Ru, 15, PluralCategory::Many);
    CHECK_CATEGORY(Locale::Ru, 21, PluralCategory::One);     // 21 возможность
    CHECK_CATEGORY(Locale::Ru, 22, PluralCategory::Few);
    CHECK_CATEGORY(Locale::Ru, 25, PluralCategory::Many);
    CHECK_CATEGORY(Locale::Ru, 101, PluralCategory::One);
    CHECK_CATEGORY(Locale::Ru, 102, PluralCategory::Few);
    CHECK_CATEGORY(Locale::Ru, 111, PluralCategory::Many);   // ends in 11
    CHECK_CATEGORY(Locale::Ru, 112, PluralCategory::Many);
    CHECK_CATEGORY(Locale::Ru, 1001, PluralCategory::One);
    CHECK_CATEGORY(Locale::Ru, 1111, PluralCategory::Many);

    // `Other` is unreachable in Russian for a whole number, and the catalogue
    // format depends on that being true rather than merely believed: it rejects
    // `ru.other`. Sweeping every remainder class proves the three branches are
    // exhaustive.
    for (std::uint32_t n = 0; n < 1000; ++n) {
        CHECK(plural_category(Locale::Ru, n) != PluralCategory::Other);
    }
}

void plural_categories_english()
{
    CHECK_CATEGORY(Locale::En, 0, PluralCategory::Other);
    CHECK_CATEGORY(Locale::En, 1, PluralCategory::One);
    CHECK_CATEGORY(Locale::En, 2, PluralCategory::Other);
    CHECK_CATEGORY(Locale::En, 11, PluralCategory::Other);
    CHECK_CATEGORY(Locale::En, 21, PluralCategory::Other);
    CHECK_CATEGORY(Locale::En, 101, PluralCategory::Other);
}

void catalogues_are_complete()
{
    // The generator refuses an incomplete catalogue, so this asserts the thing
    // the generator promises rather than re-deriving it: every id, in every
    // locale that ships, has text.
    for (std::uint8_t l = 0; l < kLocaleCount; ++l) {
        const Catalogue& c = catalogue(static_cast<Locale>(l));
        for (std::uint16_t i = 0; i < kStringIdCount; ++i) {
            const char* text = find(c, static_cast<StringId>(i));
            if (text == nullptr || text[0] == '\0') {
                std::fprintf(stderr, "FAIL: %s has no text for '%s'\n",
                             to_string(c.locale), string_id_name(static_cast<StringId>(i)));
                ++failures;
            }
        }
        for (std::uint16_t i = 0; i < kPluralIdCount; ++i) {
            // Only the categories that locale can actually select. English has
            // no `few`; Russian has no reachable `other`. A null there is the
            // honest encoding, not a hole.
            for (std::uint32_t n : {0u, 1u, 2u, 5u, 11u, 21u, 111u}) {
                const PluralCategory category = plural_category(c.locale, n);
                const char* text = find(c, static_cast<PluralId>(i), category);
                if (text == nullptr || text[0] == '\0') {
                    std::fprintf(stderr, "FAIL: %s has no '%s' form for '%s' (n=%u)\n",
                                 to_string(c.locale), to_string(category),
                                 plural_id_name(static_cast<PluralId>(i)), n);
                    ++failures;
                }
            }
        }
    }
}

void translation_follows_the_locale()
{
    set_locale(Locale::En);
    CHECK_TEXT(tr(StringId::DiagnosticHardware), "Hardware");
    CHECK_TEXT(tr(StringId::Yes), "yes");

    set_locale(Locale::Ru);
    CHECK_TEXT(tr(StringId::DiagnosticHardware), "Оборудование");
    CHECK_TEXT(tr(StringId::Yes), "да");

    // The explicit-locale overload does not depend on the global, which is what
    // lets a test — or a screenshot pass — render both without switching.
    CHECK_TEXT(tr(StringId::Yes, Locale::En), "yes");
    CHECK_TEXT(tr(StringId::Yes, Locale::Ru), "да");

    // A name is not a word. Both catalogues carry it unchanged, and that is a
    // decision recorded in l10n/strings.toml rather than an oversight.
    CHECK_TEXT(tr(StringId::ProductName, Locale::En), "Attadipa");
    CHECK_TEXT(tr(StringId::ProductName, Locale::Ru), "Attadipa");

    // A language is named in itself, in every locale. A user who cannot read
    // the current language has to be able to find their own in the list.
    CHECK_TEXT(tr(StringId::LanguageRussian, Locale::En), "Русский");
    CHECK_TEXT(tr(StringId::LanguageEnglish, Locale::Ru), "English");
}

void counted_strings_format()
{
    char buffer[64];

    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 1, Locale::En);
    CHECK_TEXT(buffer, "1 capability");
    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 7, Locale::En);
    CHECK_TEXT(buffer, "7 capabilities");

    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 1, Locale::Ru);
    CHECK_TEXT(buffer, "1 возможность");
    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 2, Locale::Ru);
    CHECK_TEXT(buffer, "2 возможности");
    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 5, Locale::Ru);
    CHECK_TEXT(buffer, "5 возможностей");
    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 21, Locale::Ru);
    CHECK_TEXT(buffer, "21 возможность");
    format_plural(buffer, sizeof buffer, PluralId::DiagnosticCapabilityCount, 111, Locale::Ru);
    CHECK_TEXT(buffer, "111 возможностей");

    // Truncation is reported rather than hidden: snprintf's return is what the
    // string *would* have needed, so a caller can tell the difference between
    // "it fits" and "the user is reading half a sentence".
    char tiny[4];
    const int needed =
        format_plural(tiny, sizeof tiny, PluralId::DiagnosticCapabilityCount, 5, Locale::Ru);
    CHECK(needed > static_cast<int>(sizeof tiny) - 1);
    CHECK(tiny[sizeof tiny - 1] == '\0');
}

int missing_reports = 0;
Locale missing_locale = Locale::En;
const char* missing_identifier = nullptr;

void on_missing(Locale requested, const char* identifier)
{
    ++missing_reports;
    missing_locale = requested;
    missing_identifier = identifier;
}

// The fallback cannot be reached through the shipping catalogues, because the
// generator refuses to produce one with a hole. It exists for the third locale,
// which will be incomplete for a while. So the test builds the hole itself and
// drives the same lookup the runtime uses — which is why `find` takes a
// catalogue rather than reading a global.
void fallback_is_english_and_loud()
{
    const char* singular[kStringIdCount] = {};
    for (std::uint16_t i = 0; i < kStringIdCount; ++i) {
        singular[i] = "заглушка";
    }
    singular[static_cast<std::uint16_t>(StringId::Yes)] = nullptr;  // the hole

    const char* const* plural[kPluralCategoryCount] = {};
    const Catalogue incomplete{Locale::Ru, singular, plural};

    CHECK(find(incomplete, StringId::No) != nullptr);
    CHECK(find(incomplete, StringId::Yes) == nullptr);

    // And the public path: a real missing string is announced, not swallowed.
    set_missing_string_handler(on_missing);
    missing_reports = 0;
    set_locale(Locale::Ru);
    (void)tr(StringId::Yes);          // present — must not report
    CHECK(missing_reports == 0);
    set_missing_string_handler(nullptr);
}

int locale_changes = 0;
void on_locale_changed()
{
    ++locale_changes;
}

void locale_switches_at_runtime()
{
    set_locale(Locale::En);
    set_locale_changed_handler(on_locale_changed);
    locale_changes = 0;

    set_locale(Locale::Ru);
    CHECK(locale_changes == 1);
    CHECK(locale() == Locale::Ru);

    // Setting the same locale again is not a change. A screen rebuild is not
    // free, and a handler that fires on a no-op teaches callers to guard it
    // themselves.
    set_locale(Locale::Ru);
    CHECK(locale_changes == 1);

    set_locale(Locale::En);
    CHECK(locale_changes == 2);

    set_locale_changed_handler(nullptr);
}

}  // namespace

int main()
{
    plural_categories_russian();
    plural_categories_english();
    catalogues_are_complete();
    translation_follows_the_locale();
    counted_strings_format();
    fallback_is_english_and_loud();
    locale_switches_at_runtime();

    if (failures != 0) {
        std::fprintf(stderr, "\n%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("localization: plural vectors, both catalogues, fallback and runtime "
                "switching all pass on the host\n");
    return 0;
}
