// GENERATED FILE -- do not edit.
//
// Written by tools/l10n/gen_strings.py from l10n/strings.toml. Edit the TOML
// and regenerate; a stale copy of this file is a failing test
// (`ctest -R l10n_generated_is_current`), not a silent divergence.

#include "attadipa/l10n/catalogue.h"
#include "attadipa/l10n/string_id.h"

// The literals below are UTF-8 and are written as themselves. See
// tools/l10n/gen_strings.py for why they are not escaped.

namespace attadipa::l10n {
namespace {

const char* const kEnSingular[kStringIdCount] = {
    /* DiagnosticCapabilities */ "Capabilities",
    /* DiagnosticGeometry */ "%s\n%u × %u  %u dpi",
    /* DiagnosticHardware */ "Hardware",
    /* DiagnosticRadio */ "Radio",
    /* DiagnosticRadioChip */ "chip",
    /* DiagnosticRadioLora */ "LoRa",
    /* DiagnosticRadioMeshcore */ "MeshCore",
    /* Language */ "Language",
    /* LanguageEnglish */ "English",
    /* LanguageRussian */ "Русский",
    /* No */ "no",
    /* ProductName */ "Attadipa",
    /* Yes */ "yes",
};

const char* const kRuSingular[kStringIdCount] = {
    /* DiagnosticCapabilities */ "Возможности",
    /* DiagnosticGeometry */ "%s\n%u × %u  %u dpi",
    /* DiagnosticHardware */ "Оборудование",
    /* DiagnosticRadio */ "Радио",
    /* DiagnosticRadioChip */ "чип",
    /* DiagnosticRadioLora */ "LoRa",
    /* DiagnosticRadioMeshcore */ "MeshCore",
    /* Language */ "Язык",
    /* LanguageEnglish */ "English",
    /* LanguageRussian */ "Русский",
    /* No */ "нет",
    /* ProductName */ "Attadipa",
    /* Yes */ "да",
};

const char* const kEnPluralOne[kPluralIdCount] = {
    /* DiagnosticCapabilityCount */ "%u capability",
};

const char* const kEnPluralOther[kPluralIdCount] = {
    /* DiagnosticCapabilityCount */ "%u capabilities",
};

const char* const kRuPluralOne[kPluralIdCount] = {
    /* DiagnosticCapabilityCount */ "%u возможность",
};

const char* const kRuPluralFew[kPluralIdCount] = {
    /* DiagnosticCapabilityCount */ "%u возможности",
};

const char* const kRuPluralMany[kPluralIdCount] = {
    /* DiagnosticCapabilityCount */ "%u возможностей",
};

const char* const* const kEnPlural[kPluralCategoryCount] = {
    kEnPluralOne,
    nullptr,  // few: not a category in en
    nullptr,  // many: not a category in en
    kEnPluralOther,
};

const char* const* const kRuPlural[kPluralCategoryCount] = {
    kRuPluralOne,
    kRuPluralFew,
    kRuPluralMany,
    nullptr,  // other: not a category in ru
};

const Catalogue kCatalogues[kLocaleCount] = {
    {Locale::En, kEnSingular, kEnPlural},
    {Locale::Ru, kRuSingular, kRuPlural},
};

const char* const kStringIdNames[kStringIdCount] = {
    "diagnostic_capabilities",
    "diagnostic_geometry",
    "diagnostic_hardware",
    "diagnostic_radio",
    "diagnostic_radio_chip",
    "diagnostic_radio_lora",
    "diagnostic_radio_meshcore",
    "language",
    "language_english",
    "language_russian",
    "no",
    "product_name",
    "yes",
};

const char* const kPluralIdNames[kPluralIdCount] = {
    "diagnostic_capability_count",
};

}  // namespace

const Catalogue& catalogue(Locale locale)
{
    const auto index = static_cast<std::uint8_t>(locale);
    return kCatalogues[index < kLocaleCount ? index : 0];
}

const char* string_id_name(StringId id)
{
    const auto index = static_cast<std::uint16_t>(id);
    return index < kStringIdCount ? kStringIdNames[index] : "<out of range>";
}

const char* plural_id_name(PluralId id)
{
    const auto index = static_cast<std::uint16_t>(id);
    return index < kPluralIdCount ? kPluralIdNames[index] : "<out of range>";
}

}  // namespace attadipa::l10n
