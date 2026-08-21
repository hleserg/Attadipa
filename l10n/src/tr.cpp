#include "firefly/l10n/tr.h"

#include <cstdio>

namespace firefly::l10n {

const char* find(const Catalogue& catalogue, StringId id)
{
    const auto index = static_cast<std::uint16_t>(id);
    if (index >= kStringIdCount) {
        return nullptr;
    }
    return catalogue.singular[index];
}

const char* find(const Catalogue& catalogue, PluralId id, PluralCategory category)
{
    const auto index = static_cast<std::uint16_t>(id);
    const auto slot  = static_cast<std::uint8_t>(category);
    if (index >= kPluralIdCount || slot >= kPluralCategoryCount) {
        return nullptr;
    }
    const char* const* forms = catalogue.plural[slot];
    return forms == nullptr ? nullptr : forms[index];
}

namespace {

Locale               g_locale          = Locale::En;
LocaleChangedHandler g_locale_changed  = nullptr;
MissingStringHandler g_missing         = nullptr;

void report_missing(Locale requested, const char* identifier)
{
    if (g_missing != nullptr) {
        g_missing(requested, identifier);
    }
}

}  // namespace

Locale locale()
{
    return g_locale;
}

void set_locale(Locale locale)
{
    if (locale == g_locale) {
        return;
    }
    g_locale = locale;
    if (g_locale_changed != nullptr) {
        g_locale_changed();
    }
}

void set_locale_changed_handler(LocaleChangedHandler handler)
{
    g_locale_changed = handler;
}

void set_missing_string_handler(MissingStringHandler handler)
{
    g_missing = handler;
}

const char* tr(StringId id, Locale locale)
{
    if (const char* text = find(catalogue(locale), id)) {
        return text;
    }
    // English is the fallback, and it is announced rather than swallowed. The
    // generator makes this unreachable for en and ru; it is here for the third
    // locale, which will be incomplete for a while and must never render blank.
    report_missing(locale, string_id_name(id));
    if (const char* text = find(catalogue(Locale::En), id)) {
        return text;
    }
    // Not "": an empty label looks like a layout bug and gets filed as one. The
    // identifier is ugly on purpose — it is a bug that names itself.
    return string_id_name(id);
}

const char* tr(StringId id)
{
    return tr(id, g_locale);
}

const char* tr_plural(PluralId id, std::uint32_t count, Locale locale)
{
    const PluralCategory category = plural_category(locale, count);
    if (const char* text = find(catalogue(locale), id, category)) {
        return text;
    }
    report_missing(locale, plural_id_name(id));
    const PluralCategory english = plural_category(Locale::En, count);
    if (const char* text = find(catalogue(Locale::En), id, english)) {
        return text;
    }
    return plural_id_name(id);
}

const char* tr_plural(PluralId id, std::uint32_t count)
{
    return tr_plural(id, count, g_locale);
}

int format_plural(char* out, std::size_t size, PluralId id, std::uint32_t count, Locale locale)
{
    const char* format = tr_plural(id, count, locale);
#if defined(__GNUC__)
#pragma GCC diagnostic push
// The format is a catalogue entry rather than a literal, so the compiler cannot
// check it. What can be checked is checked earlier and by machine: the
// generator refuses a catalogue whose locales disagree about their placeholders
// (tools/l10n/catalogue.py), which is the mismatch this warning is about.
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
    return std::snprintf(out, size, format, count);
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

int format_plural(char* out, std::size_t size, PluralId id, std::uint32_t count)
{
    return format_plural(out, size, id, count, g_locale);
}

}  // namespace firefly::l10n
