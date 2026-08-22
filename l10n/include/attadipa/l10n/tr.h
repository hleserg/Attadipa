#ifndef ATTADIPA_L10N_TR_H
#define ATTADIPA_L10N_TR_H

#include <cstddef>

#include "attadipa/l10n/catalogue.h"
#include "attadipa/l10n/locale.h"
#include "attadipa/l10n/string_id.h"

namespace attadipa::l10n {

// The whole user-facing API is four functions, and none of them takes text.
//
// `tr` returns a pointer into the compiled-in catalogue. It is valid for the
// lifetime of the program, is never null, and must not be freed or written
// through — so a caller may hold it, but a caller that holds it across a
// locale change is holding the previous language. Widgets re-read on change;
// see set_locale_changed_handler below.

const char* tr(StringId id);
const char* tr(StringId id, Locale locale);

// The format string for a counted phrase, with the right plural form already
// chosen. It still contains the placeholder — the caller supplies the number,
// because the caller is the one that knows how the number should be rendered.
const char* tr_plural(PluralId id, std::uint32_t count);
const char* tr_plural(PluralId id, std::uint32_t count, Locale locale);

// The same thing, formatted. Returns the number of characters that *would* have
// been written, snprintf-style, so a caller can detect truncation rather than
// discover it on the screen.
//
// The catalogue string reaches this as a runtime format, which is only safe
// because the generator refuses a catalogue whose locales disagree about their
// placeholders (tools/l10n/catalogue.py).
int format_plural(char* out, std::size_t size, PluralId id, std::uint32_t count);
int format_plural(char* out, std::size_t size, PluralId id, std::uint32_t count, Locale locale);

// Language is a setting (ADR-0006, ADR-0010), switched without a reboot.
//
// This is the in-memory half only. Persistence rides on SettingsService, which
// does not exist yet — so a locale chosen here does not survive a restart, and
// saying otherwise would be the kind of half-true this repository writes
// blockers about.
Locale locale();
void set_locale(Locale locale);

// Called after the locale actually changes, so screens can rebuild. One handler,
// not a list: there is one screen owner today, and a real observer registry is
// part of the application framework rather than of this file.
using LocaleChangedHandler = void (*)();
void set_locale_changed_handler(LocaleChangedHandler handler);

// Called when a string is missing from the requested locale and the English
// entry is used instead. ADR-0010 §3 requires the fallback to be *loud*: silence
// is the failure mode that survives to production. The simulator installs a
// handler that prints; firmware can route it to the log.
using MissingStringHandler = void (*)(Locale requested, const char* identifier);
void set_missing_string_handler(MissingStringHandler handler);

}  // namespace attadipa::l10n

#endif  // ATTADIPA_L10N_TR_H
