// A fixture, compiled twice on purpose — the other direction of the same idea.
//
// Linked against firefly_apps it compiles, and linked against firefly_core it
// must not. docs/adr/0010-localization.md §4: the core does not speak English.
// A service that can call tr() will eventually call it, and the moment a
// service produces a sentence instead of a code plus its parameters, the UI can
// no longer translate it — §50.5 of the final prompt names that exact failure.
//
// The rule cannot be enforced by review alone: `tr(StringId::Foo)` in a service
// looks entirely reasonable in a diff. It is enforced by not putting the header
// on core's compile line, and this fixture is what notices when someone does.

#include "firefly/l10n/tr.h"

int main()
{
    // Odr-use it so the include cannot be optimised into a no-op, and exit 0 —
    // the positive control has to be a test that passes.
    return firefly::l10n::tr(firefly::l10n::StringId::ProductName) == nullptr ? 1 : 0;
}
