// A fixture, compiled twice on purpose.
//
// Linked against attadipa_platform it compiles, and linked against attadipa_apps
// it must not. That pair is the test: it shows the failure comes from the link
// line rather than from a mistyped path, which a single negative test could not
// distinguish. docs/adr/0007-two-capability-layers.md §5 says the boundary is
// enforced by the build; tests/CMakeLists.txt is where that claim is checked.

#include "attadipa/platform/hardware_feature.h"

int main()
{
    // Odr-use the type so the include cannot be reduced to a no-op, and still
    // exit 0 — the positive control is a test that has to pass.
    const auto feature = attadipa::platform::HardwareFeature::Radio;
    return attadipa::platform::to_string(feature) == nullptr ? 1 : 0;
}
