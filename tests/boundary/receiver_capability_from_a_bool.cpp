// A fixture, compiled twice on purpose.
//
// The interesting half is the negative one. `GnssCapabilities` used to be four
// plain `bool`s, so `{false, false, false, false}` meant both "this receiver
// has no backup domain" and "nobody has read the datasheet yet" — the collision
// issue #166 is about. Making the fields a scoped `SupportState` fixes that only
// as long as a `bool` cannot get back in through an aggregate initializer, and
// "a scoped enum will not accept a bool" is a claim about the language that is
// worth exactly as much as the test that breaks when somebody adds an implicit
// conversion, a converting constructor, or a helpful `operator bool`.
//
// ATTADIPA_EXPECT_BOOL_REJECTED selects the half being built. Both halves are
// compiled from this one file so that the failure is known to come from the
// initializer rather than from a mistyped include — which a single negative
// test could not distinguish.

#include "attadipa/core/gnss_power.h"

using attadipa::core::GnssCapabilities;
using attadipa::core::SupportState;

#if defined(ATTADIPA_EXPECT_BOOL_REJECTED)

// Must not compile. The message the build emits is what the ctest entry in
// tests/CMakeLists.txt matches on, so this cannot pass by failing elsewhere.
constexpr GnssCapabilities kFromBooleans{false, false, false, false};

int main()
{
    return kFromBooleans.fully_established() ? 1 : 0;
}

#else

// Must compile: the same shape written honestly. Without this half, a renamed
// header or a broken build would make the negative test green while proving
// nothing at all.
constexpr GnssCapabilities kFromVerdicts{SupportState::Unsupported, SupportState::Unsupported,
                                         SupportState::Unsupported, SupportState::Unsupported};

int main()
{
    // Odr-use it, and assert the property the positive control exists to show:
    // four proven verdicts *is* a finished profile, so the negative half is
    // refusing the booleans rather than the arity.
    return kFromVerdicts.fully_established() ? 0 : 1;
}

#endif
