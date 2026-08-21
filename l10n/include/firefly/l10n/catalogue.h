#ifndef FIREFLY_L10N_CATALOGUE_H
#define FIREFLY_L10N_CATALOGUE_H

#include "firefly/l10n/locale.h"
#include "firefly/l10n/string_id.h"

namespace firefly::l10n {

// One locale's strings: parallel arrays indexed by the enum, no map, no
// allocation, nothing to initialise at boot. The shape is lv_i18n's, and that
// is recorded as INSPIRE ARCHITECTURE in the reuse ledger — its lookup is a
// `strcmp` over a string key, and replacing that key with an index is the
// change that makes the coverage check something a build can do.
//
// A null entry means "this locale does not have this string". The generator
// makes that impossible for the two locales that ship, because a missing entry
// is an error there rather than a fallback. The representation still allows it,
// because a locale added later will be incomplete for a while and the runtime
// has to behave — loudly — rather than draw an empty label.
struct Catalogue {
    Locale locale;
    const char* const* singular;         // kStringIdCount entries
    const char* const* const* plural;    // kPluralCategoryCount arrays of kPluralIdCount
};

// The compiled-in catalogues. Generated: l10n/src/catalogues.cpp.
const Catalogue& catalogue(Locale locale);

// A single catalogue's answer, or nullptr. No fallback, no logging: this is the
// piece the fallback is built out of, and keeping it dumb is what lets a test
// hand it a catalogue with a hole in it.
const char* find(const Catalogue& catalogue, StringId id);
const char* find(const Catalogue& catalogue, PluralId id, PluralCategory category);

}  // namespace firefly::l10n

#endif  // FIREFLY_L10N_CATALOGUE_H
