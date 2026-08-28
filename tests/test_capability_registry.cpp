#include <cstdio>
#include <cstring>

#include "attadipa/apps/app_manifest.h"
#include "attadipa/core/capability_registry.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/platform/hardware_inventory.h"

// Host tests for the two capability layers.
//
// NOT a statement about hardware. Everything here runs on a desktop against
// board *profiles* — descriptions transcribed from schematics, not boards. It
// checks that the rules in ADR-0003, ADR-0007 and ADR-0009 are implemented as
// written. Whether the profiles match the metal is a hardware test, and no
// hardware test in this repository has been executed.

using namespace attadipa;

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

void check_availability(const core::CapabilityRegistry& caps, core::Capability capability,
                        core::Availability expected, int line)
{
    const core::Availability actual = caps.availability(capability);
    if (actual != expected) {
        std::fprintf(stderr, "FAIL line %d: %s is %s, expected %s\n", line,
                     core::to_string(capability), core::to_string(actual),
                     core::to_string(expected));
        ++failures;
    }
}

#define CHECK_AVAIL(caps, cap, expected) \
    check_availability((caps), (cap), (expected), __LINE__)

// Availability and origin together, and deliberately read through the two
// separate public accessors rather than through CapabilityRegistry::source().
//
// The defect these checks exist for (issue #174) was not a wrong answer inside
// one function — it was two functions answering the same question by different
// routes and disagreeing. Asking source() for both halves would agree with
// itself by construction and prove nothing. These go in through availability()
// and provider(), so if either is ever re-derived independently again the
// suite says so.
void check_source(const core::CapabilityRegistry& caps, core::Capability capability,
                  core::Availability expected_availability, core::Origin expected_origin,
                  const char* scenario, int line)
{
    const core::Availability actual_availability = caps.availability(capability);
    const core::Origin       actual_origin       = caps.provider(capability).origin;
    if (actual_availability != expected_availability || actual_origin != expected_origin) {
        std::fprintf(stderr, "FAIL line %d: %s, %s: %s from %s, expected %s from %s\n", line,
                     core::to_string(capability), scenario,
                     core::to_string(actual_availability), core::to_string(actual_origin),
                     core::to_string(expected_availability), core::to_string(expected_origin));
        ++failures;
    }
}

#define CHECK_SOURCE(caps, cap, availability, origin, scenario) \
    check_source((caps), (cap), (availability), (origin), (scenario), __LINE__)

platform::BoardProfile twatch(platform::RadioChip chip)
{
    const platform::BoardProfile* base = platform::find_board_profile("t-watch-s3-plus");
    platform::BoardProfile        profile = *base;
    profile.radio                        = platform::radio_info_for(chip);
    return profile;
}

platform::BoardProfile waveshare()
{
    return *platform::find_board_profile("waveshare-amoled-206");
}

void bring_up(platform::ProfileInventory& inventory)
{
    for (std::uint8_t i = 0; i < platform::kHardwareFeatureCount; ++i) {
        const auto feature = static_cast<platform::HardwareFeature>(i);
        if (inventory.present(feature)) {
            inventory.set_state(feature, platform::HardwareState::Ready);
        }
    }
}

core::NodeLink attached_node()
{
    core::NodeLink link;
    link.bound      = true;
    link.reachable  = true;
    link.compatible = true;
    link.provides   = core::capability_bit(core::Capability::Position) |
                    core::capability_bit(core::Capability::Heading) |
                    core::capability_bit(core::Capability::MeshMessaging);
    return link;
}

// ---------------------------------------------------------------------------

// The profiles must survive the round trip through find_board_profile, and the
// derived density must be right, because the spacing tokens will divide by it.
void test_board_profiles()
{
    CHECK(platform::find_board_profile("t-watch-s3-plus") != nullptr);
    CHECK(platform::find_board_profile("waveshare-amoled-206") != nullptr);
    CHECK(platform::find_board_profile("nonsense") == nullptr);
    CHECK(platform::find_board_profile(nullptr) == nullptr);

    // 240x240 across 1.3 inches, 410x502 across 2.06 inches.
    //
    // 261 is not a measurement, and it is no longer the conservative reading
    // either: D15 is RESOLVED — the panel is 1.54" at 220 ppi, MEASURED
    // 2026-08-28. This assertion locks the *arithmetic* against the stale
    // 1300 the profile still carries, so it fails the moment #323 corrects
    // that value. Becoming 220 there is the fix, not a regression.
    CHECK(twatch(platform::RadioChip::Unknown).display.dpi() == 261);
    CHECK(waveshare().display.dpi() == 315);

    // The absences that make the two boards different devices rather than two
    // trims of one device.
    CHECK(!waveshare().present(platform::HardwareFeature::Radio));
    CHECK(!waveshare().present(platform::HardwareFeature::GnssReceiver));
    CHECK(!waveshare().present(platform::HardwareFeature::IrTransmitter));
    CHECK(!twatch(platform::RadioChip::Sx1262).present(platform::HardwareFeature::Gyroscope));
    CHECK(!twatch(platform::RadioChip::Sx1262).present(platform::HardwareFeature::SdCard));
    // Neither board has one, and that is the whole magnetometer story.
    CHECK(!waveshare().present(platform::HardwareFeature::MagnetometerSensor));
    CHECK(!twatch(platform::RadioChip::Sx1262).present(platform::HardwareFeature::MagnetometerSensor));
}

// THE SHIPPED PROFILE, which every case above overrides before looking at it.
//
// `twatch()` replaces `.radio` on its way out, so nothing else in this file
// reads what `make_twatch()` actually built -- and that field is the one an
// owner answer puts pressure on. A2 is now ANSWERED and struck through in the
// register, reading "SX1262 (868 MHz)" from an order listing; the enum must not
// move on that, because a listing is a seller's claim and this value is what
// the firmware bets a radio on (ADR-0003). Until this test existed, setting
// `board_profiles.cpp:95` to `RadioChip::Sx1262` left the suite green at 24/24
// and the device advertised MeshMessaging as Ready on a watch nobody has held.
// The comment saying not to was the only thing in the way, and a comment is not
// a check. Found in review.
void test_shipped_twatch_radio_is_unread()
{
    const platform::BoardProfile* shipped = platform::find_board_profile("t-watch-s3-plus");
    CHECK(shipped != nullptr);
    if (shipped == nullptr) {
        return;
    }

    // Not "some chip we are unsure about" -- the marking has not been read.
    CHECK(shipped->radio.chip == platform::RadioChip::Unknown);
    CHECK(shipped->radio.meshcore != platform::MeshCoreSupport::Supported);

    // And the consequence, which is the sentence that reaches a user: the
    // launcher may offer mesh through an Attadipa node, never through this
    // watch's own radio.
    platform::ProfileInventory inventory(*shipped);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);
    CHECK_AVAIL(caps, core::Capability::MeshMessaging, core::Availability::Unprovisioned);
}

// THE BAND TRAP, pinned so it cannot stop being true quietly.
//
// Reading the marking off the part settles the CHIP and nothing else. Band is
// set by the matching network and the antenna fitted, and is readable neither
// over SPI nor off the package -- so A2's "SX1262 at 868 MHz" rests, for its
// second half, on the same seller's listing the chip half refuses.
//
// What that costs if the checklist is done by halves is exactly this: set
// `RadioChip::Sx1262` from the marking alone and `radio_info_for()` publishes
// RadioLib's DRIVER limits as this unit's coverage, so `covers()` answers yes
// for EU868, US915 and AS433 at once -- three mutually exclusive regional
// networks -- and `MeshMessaging` goes Ready with nobody having looked at the
// matching network. The code is not lying; the checklist would be incomplete.
//
// This test asserts the trap rather than closing it, because closing it needs
// a band observation the data model has nowhere to put yet -- filed as T-143.
// What it buys is that the sentence stops being prose: narrow those numbers to
// a real unit's band, or add the observation, and this test says so.
void test_sx1262_bands_are_the_drivers_not_this_units()
{
    const platform::RadioInfo info = platform::radio_info_for(platform::RadioChip::Sx1262);

    // RadioLib's SX1262 driver range, verbatim. Not a regulatory band, not a
    // measurement, and not a fact about any board in this project.
    CHECK(info.band_count == 1);
    CHECK(info.bands[0].lo_hz == 150'000'000u);
    CHECK(info.bands[0].hi_hz == 960'000'000u);

    // Three regions a single unit cannot all be built for, all answered yes.
    const platform::BandRange eu868{863'000'000u, 870'000'000u};
    const platform::BandRange us915{902'000'000u, 928'000'000u};
    const platform::BandRange as433{433'050'000u, 434'790'000u};
    CHECK(info.covers(eu868));
    CHECK(info.covers(us915));
    CHECK(info.covers(as433));

    // And the reason the shipped profile is safe today is the enum, not the
    // band: nothing consults these numbers while the chip is unread.
    CHECK(platform::radio_info_for(platform::RadioChip::Unknown).band_count == 0);
}

// Owning a part is not initialising it. A part that has not been brought up
// must read as Off — something the user can act on — and never as Failed.
void test_untouched_is_not_failed()
{
    const platform::BoardProfile profile = twatch(platform::RadioChip::Sx1262);
    platform::ProfileInventory   inventory(profile);

    CHECK(inventory.state(platform::HardwareFeature::Radio) ==
          platform::HardwareState::Untouched);
    CHECK(inventory.state(platform::HardwareFeature::SdCard) == platform::HardwareState::Absent);

    core::CapabilityRegistry caps(inventory);
    CHECK_AVAIL(caps, core::Capability::MeshMessaging, core::Availability::Off);
    CHECK_AVAIL(caps, core::Capability::MotionSensing, core::Availability::Off);

    // A part that is not there cannot fail, and must not be made to look as if
    // it did.
    inventory.set_state(platform::HardwareFeature::SdCard, platform::HardwareState::Failed);
    CHECK(inventory.state(platform::HardwareFeature::SdCard) == platform::HardwareState::Absent);
}

// ADR-0003, and the reason it exists: a radio being present says nothing about
// whether this device can join a mesh.
void test_radio_is_not_lora()
{
    struct Case {
        platform::RadioChip chip;
        core::Availability  expected;
        const char*         why;
    };

    // Without a node, every non-working local radio lands on Unprovisioned
    // rather than Unsupported, because an Attadipa node can always provide mesh.
    // That is what keeps supports() stable — see test_supports_is_stable.
    const Case cases[] = {
        {platform::RadioChip::Sx1262, core::Availability::Ready,
         "LoRa, sub-GHz, supported by the pinned MeshCore"},
        {platform::RadioChip::Cc1101, core::Availability::Unprovisioned,
         "no LoRa modulator at all — FSK/OOK only"},
        {platform::RadioChip::Si4432, core::Availability::Unprovisioned,
         "no LoRa modulator at all"},
        {platform::RadioChip::Sx1280, core::Availability::Unprovisioned,
         "LoRa, but 2.4 GHz only, and absent from MeshCore"},
        {platform::RadioChip::Lr1121, core::Availability::Unprovisioned,
         "LoRa, but the pinned MeshCore needs driver work"},
        {platform::RadioChip::Unknown, core::Availability::Unprovisioned,
         "no marking has been read off the fitted chip; A2's answer is a "
         "seller's listing, which does not move this enum"},
    };

    for (const Case& c : cases) {
        const platform::BoardProfile profile = twatch(c.chip);
        platform::ProfileInventory   inventory(profile);
        bring_up(inventory);
        core::CapabilityRegistry caps(inventory);

        const core::Availability actual = caps.availability(core::Capability::MeshMessaging);
        if (actual != c.expected) {
            std::fprintf(stderr, "FAIL %s: MeshMessaging is %s, expected %s (%s)\n",
                         platform::to_string(c.chip), core::to_string(actual),
                         core::to_string(c.expected), c.why);
            ++failures;
        }
    }

    // Two of the five cannot be made to work by any amount of software, and the
    // inventory says so rather than leaving it to be discovered at runtime.
    CHECK(platform::radio_info_for(platform::RadioChip::Cc1101).meshcore ==
          platform::MeshCoreSupport::Impossible);
    CHECK(!platform::radio_info_for(platform::RadioChip::Cc1101).can_do_lora());
    CHECK(platform::radio_info_for(platform::RadioChip::Sx1280).can_do_lora());

    // SX1280 does LoRa and still cannot hear an 868 MHz mesh. "Supports LoRa"
    // was never the question.
    const platform::BandRange eu868{863'000'000U, 870'000'000U};
    CHECK(platform::radio_info_for(platform::RadioChip::Sx1262).covers(eu868));
    CHECK(!platform::radio_info_for(platform::RadioChip::Sx1280).covers(eu868));
}

// ADR-0007's headline example: a board with no GNSS receiver still supports
// Position, because a node provides it.
void test_waveshare_position_comes_from_a_node()
{
    const platform::BoardProfile profile = waveshare();
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    CHECK(!inventory.present(platform::HardwareFeature::GnssReceiver));
    CHECK_AVAIL(caps, core::Capability::Position, core::Availability::Unprovisioned);
    CHECK(caps.supports(core::Capability::Position));
    CHECK(!caps.is_available(core::Capability::Position));

    caps.set_node_link(attached_node());
    CHECK_AVAIL(caps, core::Capability::Position, core::Availability::Ready);
    CHECK(caps.is_available(core::Capability::Position));
    CHECK(caps.provider(core::Capability::Position).origin == core::Origin::Node);

    // Bound but out of range is a different sentence from never paired.
    core::NodeLink far_away = attached_node();
    far_away.reachable      = false;
    caps.set_node_link(far_away);
    CHECK_AVAIL(caps, core::Capability::Position, core::Availability::Unreachable);

    core::NodeLink wrong_version = attached_node();
    wrong_version.compatible    = false;
    caps.set_node_link(wrong_version);
    CHECK_AVAIL(caps, core::Capability::Position, core::Availability::Incompatible);
}

// No node can ever provide an infrared transmitter, so this one is terminal —
// which is what Unsupported is reserved for.
void test_unsupported_is_terminal()
{
    const platform::BoardProfile profile = waveshare();
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);
    caps.set_node_link(attached_node());

    CHECK_AVAIL(caps, core::Capability::InfraredBlast, core::Availability::Unsupported);
    CHECK(!caps.supports(core::Capability::InfraredBlast));

    // And the T-Watch, which has no SD slot, against the Waveshare, which does.
    platform::ProfileInventory twatch_inventory(*platform::find_board_profile("t-watch-s3-plus"));
    bring_up(twatch_inventory);
    core::CapabilityRegistry twatch_caps(twatch_inventory);
    CHECK(!twatch_caps.supports(core::Capability::RemovableStorage));
    CHECK(caps.supports(core::Capability::RemovableStorage));
}

// supports() answers "could this device, ever" and must not change while
// running. An application that appears and disappears from the launcher as a
// node comes and goes is the bug this property prevents.
void test_supports_is_stable()
{
    const platform::BoardProfile profile = waveshare();
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    bool before[core::kCapabilityCount];
    for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
        before[i] = caps.supports(static_cast<core::Capability>(i));
    }

    caps.set_node_link(attached_node());

    for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
        const auto capability = static_cast<core::Capability>(i);
        if (caps.supports(capability) != before[i]) {
            std::fprintf(stderr, "FAIL supports(%s) changed when a node attached\n",
                         core::to_string(capability));
            ++failures;
        }
    }
}

// ADR-0009: Heading needs an absolute reference to north — a magnetometer, or
// GNSS course-over-ground. Accelerometer+gyroscope fusion is not a third
// source: without a magnetometer yaw is unobservable, and gyro-only
// integration drifts without bound (ADR-0009 §"Alternatives considered";
// docs/upstream/research-integration.md §9, verdict REJECT). This used to be
// named test_heading_has_three_sources and asserted the fusion path as
// Ready — that assertion was the bug (issue #21), not a spec.
void test_heading_needs_an_absolute_reference()
{
    // Waveshare: a six-axis IMU (accel + gyro), but no magnetometer and no
    // local GNSS. Local availability must not read the IMU as a heading
    // source — there is no absolute reference here at all.
    platform::ProfileInventory waveshare_inventory(*platform::find_board_profile(
        "waveshare-amoled-206"));
    bring_up(waveshare_inventory);
    core::CapabilityRegistry waveshare_caps(waveshare_inventory);
    CHECK(waveshare_inventory.present(platform::HardwareFeature::Accelerometer));
    CHECK(waveshare_inventory.present(platform::HardwareFeature::Gyroscope));
    CHECK(!waveshare_inventory.present(platform::HardwareFeature::MagnetometerSensor));
    CHECK(!waveshare_inventory.present(platform::HardwareFeature::GnssReceiver));
    // Heading is still in kNodeProvidable, so with no node bound the honest
    // answer is Unprovisioned, not Unsupported and not Ready.
    CHECK_AVAIL(waveshare_caps, core::Capability::Heading, core::Availability::Unprovisioned);
    CHECK(!waveshare_caps.is_available(core::Capability::Heading));

    // T-Watch: an accelerometer with no gyroscope, so fusion was never even
    // arguable here — but it has GNSS, so course-over-ground. Ready here
    // means "something can produce a heading", not "the number means
    // anything right now": a standing user is Ready and Invalid at once, and
    // that is validity, not availability.
    platform::ProfileInventory twatch_inventory(*platform::find_board_profile("t-watch-s3-plus"));
    bring_up(twatch_inventory);
    core::CapabilityRegistry twatch_caps(twatch_inventory);
    CHECK(!twatch_inventory.present(platform::HardwareFeature::Gyroscope));
    CHECK_AVAIL(twatch_caps, core::Capability::Heading, core::Availability::Ready);

    // And if the GNSS receiver is powered down, the heading goes with it.
    twatch_inventory.set_state(platform::HardwareFeature::GnssReceiver,
                               platform::HardwareState::RailOff);
    CHECK_AVAIL(twatch_caps, core::Capability::Heading, core::Availability::Off);
}

// A node that actually offers Heading (course-over-ground from its own GNSS)
// is a legitimate absolute reference, and must still light Heading up —
// mirrors test_waveshare_position_comes_from_a_node. The bug this issue fixed
// was the local IMU claiming Ready on its own; a real remote source claiming
// it is the opposite failure mode and must keep working.
void test_waveshare_heading_comes_from_a_node()
{
    platform::ProfileInventory inventory(*platform::find_board_profile("waveshare-amoled-206"));
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    CHECK_AVAIL(caps, core::Capability::Heading, core::Availability::Unprovisioned);
    CHECK(caps.supports(core::Capability::Heading));
    CHECK(!caps.is_available(core::Capability::Heading));
    const bool supported_before_bind = caps.supports(core::Capability::Heading);

    caps.set_node_link(attached_node());
    CHECK_AVAIL(caps, core::Capability::Heading, core::Availability::Ready);
    CHECK(caps.is_available(core::Capability::Heading));
    CHECK(caps.provider(core::Capability::Heading).origin == core::Origin::Node);

    // supports() answers "could this device, ever" and must not move when the
    // node attaches (test_supports_is_stable covers this generically over
    // every capability; this is the specific case the issue named).
    CHECK(caps.supports(core::Capability::Heading) == supported_before_bind);
}

// ---------------------------------------------------------------------------
// Where the answer came from — issue #174.
//
// ADR-0004 §2 makes the provider an axis rather than five more enum values, on
// the argument that dispatch, the rail service, the coexistence coordinator,
// Settings and Diagnostics all need to know which side is serving a capability.
// That is only worth having if the axis is right in the states those five care
// about most, which are the degraded ones: a node that walked away, and a node
// whose firmware cannot be spoken to.
//
// Until this block existed, `provider()` reached `Origin::Node` only when the
// node was already `Ready`, so the same unchanged node reported itself as the
// local device the moment it went out of range.

// The headline regression. One node, one bind, nothing about the *provider*
// changing — only the link between us and it. The origin must not move, and
// ADR-0004 §2 says so in as many words: "a capability can be Ready from a node
// and Unreachable from the same node a second later".
void test_provider_origin_survives_the_link_going_away()
{
    const platform::BoardProfile profile = waveshare();
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    // The premise, checked rather than assumed: this board has no local source
    // for either of these, so every non-Unsupported answer below is the node's.
    CHECK(!inventory.present(platform::HardwareFeature::GnssReceiver));
    CHECK(!inventory.present(platform::HardwareFeature::MagnetometerSensor));

    const core::Capability remote[] = {core::Capability::Position, core::Capability::Heading};

    for (const core::Capability capability : remote) {
        caps.set_node_link(attached_node());
        CHECK_SOURCE(caps, capability, core::Availability::Ready, core::Origin::Node,
                     "node attached and answering");

        core::NodeLink out_of_range = attached_node();
        out_of_range.reachable      = false;
        caps.set_node_link(out_of_range);
        CHECK_SOURCE(caps, capability, core::Availability::Unreachable, core::Origin::Node,
                     "the same node, out of range");

        core::NodeLink wrong_version = attached_node();
        wrong_version.compatible    = false;
        caps.set_node_link(wrong_version);
        CHECK_SOURCE(caps, capability, core::Availability::Incompatible, core::Origin::Node,
                     "the same node, no agreed protocol version");

        // And back. A round trip, because a state machine that recovers to a
        // different answer than it left from is the failure mode a one-way walk
        // does not catch.
        caps.set_node_link(attached_node());
        CHECK_SOURCE(caps, capability, core::Availability::Ready, core::Origin::Node,
                     "the same node, back in range");
    }
}

// The same property over the whole matrix of node states, including the one
// this board reaches with nothing bound at all. A board with no local source
// for a capability has exactly one place the answer can come from, so `Local`
// is never a defensible answer here — whatever the availability is.
void test_no_local_source_means_a_remote_provider()
{
    const platform::BoardProfile profile = waveshare();
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    CHECK(!inventory.present(platform::HardwareFeature::GnssReceiver));
    CHECK(!inventory.present(platform::HardwareFeature::MagnetometerSensor));
    CHECK(!inventory.present(platform::HardwareFeature::Radio));

    struct Case {
        core::NodeLink     link;
        core::Availability expected;
        const char*        scenario;
    };

    core::NodeLink no_node;  // nothing bound: every field at its default

    core::NodeLink bound_but_offers_nothing;
    bound_but_offers_nothing.bound      = true;
    bound_but_offers_nothing.reachable  = true;
    bound_but_offers_nothing.compatible = true;
    bound_but_offers_nothing.provides   = 0;

    core::NodeLink out_of_range = attached_node();
    out_of_range.reachable      = false;

    core::NodeLink wrong_version = attached_node();
    wrong_version.compatible    = false;

    const Case cases[] = {
        {no_node, core::Availability::Unprovisioned, "no node has ever been bound"},
        // A node that does not do this one is still the thing the remedy is
        // about — "a different node would" — so it is not the local device
        // either. capability_registry.cpp says the same in prose.
        {bound_but_offers_nothing, core::Availability::Unprovisioned,
         "a node is bound and does not offer it"},
        {wrong_version, core::Availability::Incompatible, "bound, reachable, version skewed"},
        {out_of_range, core::Availability::Unreachable, "bound, compatible, out of range"},
        {attached_node(), core::Availability::Ready, "bound and answering"},
    };

    const core::Capability remote[] = {core::Capability::Position, core::Capability::Heading,
                                       core::Capability::MeshMessaging};

    for (const Case& c : cases) {
        caps.set_node_link(c.link);
        for (const core::Capability capability : remote) {
            CHECK_SOURCE(caps, capability, c.expected, core::Origin::Node, c.scenario);
        }
    }
}

// The other half of ADR-0008 §4's preference, and the half a fix aimed only at
// the node can quietly break: a local source that works keeps the capability,
// and keeps the origin, with a perfectly good node attached.
void test_a_working_local_source_outranks_a_node()
{
    const platform::BoardProfile profile = twatch(platform::RadioChip::Sx1262);
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);
    caps.set_node_link(attached_node());

    CHECK(inventory.present(platform::HardwareFeature::GnssReceiver));

    // The node offers all three of these and loses all three, because local is
    // preferred when it works.
    CHECK_SOURCE(caps, core::Capability::Position, core::Availability::Ready, core::Origin::Local,
                 "on-board GNSS with a node also attached");
    CHECK_SOURCE(caps, core::Capability::Heading, core::Availability::Ready, core::Origin::Local,
                 "course-over-ground from on-board GNSS, node also attached");
    CHECK_SOURCE(caps, core::Capability::MeshMessaging, core::Availability::Ready,
                 core::Origin::Local, "an SX1262 the pinned MeshCore drives, node also attached");
}

// A local part that is switched off or broken must not be reported as somebody
// else's, and it must not stop being reported as ours when a node appears that
// cannot help either.
//
// This is also the one place the origin legitimately *does* move under a link
// change, and it is worth pinning rather than leaving to be rediscovered: with
// a broken on-board GNSS, a reachable node wins the capability (it can answer)
// and an unreachable one does not (it cannot, and "service the receiver you
// have" is the better remedy — remedy_rank puts Failed above Unreachable).
void test_a_degraded_local_source_stays_local()
{
    const platform::BoardProfile profile = twatch(platform::RadioChip::Sx1262);
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    inventory.set_state(platform::HardwareFeature::GnssReceiver, platform::HardwareState::RailOff);
    CHECK_SOURCE(caps, core::Capability::Position, core::Availability::Off, core::Origin::Local,
                 "on-board GNSS powered down, no node");

    inventory.set_state(platform::HardwareFeature::GnssReceiver, platform::HardwareState::Failed);
    CHECK_SOURCE(caps, core::Capability::Position, core::Availability::Failed, core::Origin::Local,
                 "on-board GNSS did not come up, no node");

    core::NodeLink out_of_range = attached_node();
    out_of_range.reachable      = false;
    caps.set_node_link(out_of_range);
    CHECK_SOURCE(caps, core::Capability::Position, core::Availability::Failed, core::Origin::Local,
                 "on-board GNSS broken, node out of range");

    caps.set_node_link(attached_node());
    CHECK_SOURCE(caps, core::Capability::Position, core::Availability::Ready, core::Origin::Node,
                 "on-board GNSS broken, node answering");
}

// ADR-0004 §6: `Unsupported` is not offered, and there is nothing to configure,
// diagnose, dispatch to or power. So no provider exists — and `Origin` has no
// value that says that.
//
// This test pins the convention rather than closing the gap, in the manner of
// test_sx1262_bands_are_the_drivers_not_this_units: `provider()` answers
// `Local` here because the enum has two values and neither of them means
// "nobody". What keeps it from being a silent lie is the discriminator that
// travels with it — `CapabilitySource` carries the availability that says the
// field is not an answer, and `availability.h` says so on the struct. Whether
// the axis gains a third value is the ADR question in issues #83 and #174 and
// is not settled here.
void test_terminal_unsupported_has_no_provider()
{
    const platform::BoardProfile waveshare_profile = waveshare();
    platform::ProfileInventory   waveshare_inventory(waveshare_profile);
    bring_up(waveshare_inventory);
    core::CapabilityRegistry waveshare_caps(waveshare_inventory);
    waveshare_caps.set_node_link(attached_node());

    // No infrared transmitter on the board, and no node will ever carry one.
    CHECK_SOURCE(waveshare_caps, core::Capability::InfraredBlast, core::Availability::Unsupported,
                 core::Origin::Local, "nothing on either side provides infrared");

    const platform::BoardProfile twatch_profile = twatch(platform::RadioChip::Sx1262);
    platform::ProfileInventory   twatch_inventory(twatch_profile);
    bring_up(twatch_inventory);
    core::CapabilityRegistry twatch_caps(twatch_inventory);
    twatch_caps.set_node_link(attached_node());
    CHECK_SOURCE(twatch_caps, core::Capability::RemovableStorage, core::Availability::Unsupported,
                 core::Origin::Local, "no card slot on this board, and no node offers one");
}

// The exception to ADR-0004 §2's invariant, recorded because it is real and
// because a table test that quietly skipped it would be hiding it.
//
// The invariant reads: "`Unprovisioned`, `Unreachable` and `Incompatible` imply
// a remote provider. A local capability can never be in them." `CompanionLink`
// and `NotificationRelay` are in two of those three with `Origin::Local`, and
// that is not this issue's defect — it is the sentence being written about
// nodes while a second remote peer exists.
//
// Which of the two is wrong is not obvious, and is deliberately not decided
// here. ADR-0002 forbids the phone from *providing* a capability, so on that
// reading the provider is the on-board BLE radio and `Local` is right; on the
// other reading `Origin` simply has no value for a phone. Either way it is the
// same shape as T-111's third source, and it is noted there.
void test_the_companion_is_not_on_the_origin_axis()
{
    const platform::BoardProfile profile = waveshare();
    platform::ProfileInventory   inventory(profile);
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);
    caps.set_node_link(attached_node());

    CHECK(inventory.present(platform::HardwareFeature::Ble));

    CHECK_SOURCE(caps, core::Capability::CompanionLink, core::Availability::Unprovisioned,
                 core::Origin::Local, "BLE on the board, no phone paired");

    core::CompanionLinkState away;
    away.bound     = true;
    away.reachable = false;
    caps.set_companion(away);
    CHECK_SOURCE(caps, core::Capability::CompanionLink, core::Availability::Unreachable,
                 core::Origin::Local, "a phone is paired and not in range");
    CHECK_SOURCE(caps, core::Capability::NotificationRelay, core::Availability::Unreachable,
                 core::Origin::Local, "a phone is paired and not in range");
}

// The table form, over every capability on both boards: with every part brought
// up, moving *only* the link must not move the origin of anything.
//
// Every part brought up is load-bearing and not incidental. A local source that
// is Off or Failed can legitimately hand the capability to a reachable node and
// take it back when the node leaves — test_a_degraded_local_source_stays_local
// pins that case on purpose. What must never happen is the origin moving when
// the only thing that changed is whether we can hear a node we are still bound
// to, which is what this sweeps for across all thirteen capabilities.
void test_origin_does_not_move_when_only_the_link_does()
{
    const platform::BoardProfile boards[] = {waveshare(), twatch(platform::RadioChip::Sx1262)};

    for (const platform::BoardProfile& board : boards) {
        platform::ProfileInventory inventory(board);
        bring_up(inventory);
        core::CapabilityRegistry caps(inventory);

        caps.set_node_link(attached_node());

        core::Origin before[core::kCapabilityCount];
        for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
            before[i] = caps.provider(static_cast<core::Capability>(i)).origin;
        }

        core::NodeLink out_of_range = attached_node();
        out_of_range.reachable      = false;

        core::NodeLink wrong_version = attached_node();
        wrong_version.compatible    = false;

        const core::NodeLink degraded[] = {out_of_range, wrong_version};

        for (const core::NodeLink& link : degraded) {
            caps.set_node_link(link);
            for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
                const auto capability = static_cast<core::Capability>(i);
                const core::Origin now = caps.provider(capability).origin;
                if (now != before[i]) {
                    std::fprintf(stderr,
                                 "FAIL %s on %s: provider moved from %s to %s when only the "
                                 "link changed\n",
                                 core::to_string(capability), board.id, core::to_string(before[i]),
                                 core::to_string(now));
                    ++failures;
                }
            }
        }
    }
}

// The two whose remote peer is a phone rather than a node — see
// test_the_companion_is_not_on_the_origin_axis.
bool backed_by_the_phone(core::Capability capability)
{
    return capability == core::Capability::CompanionLink ||
           capability == core::Capability::NotificationRelay;
}

// One origin and where it was read from, so a failure names the accessor.
struct OriginReading {
    core::Origin origin;
    const char*  accessor;
};

// ADR-0004 §2's invariant, swept over the whole matrix: "`Unprovisioned`,
// `Unreachable` and `Incompatible` imply a remote provider."
//
// The two capabilities that are in those states with `Origin::Local` are named
// rather than skipped, and the difference matters: an exclusion list that says
// *these two, for this reason* fails when a third one appears, which is the
// case worth catching. Dropping the three states from the sweep instead would
// have made the invariant unfalsifiable exactly where it is interesting.
void test_remote_states_imply_a_remote_provider()
{
    core::NodeLink no_node;

    core::NodeLink bound_but_offers_nothing;
    bound_but_offers_nothing.bound      = true;
    bound_but_offers_nothing.reachable  = true;
    bound_but_offers_nothing.compatible = true;

    core::NodeLink out_of_range = attached_node();
    out_of_range.reachable      = false;

    core::NodeLink wrong_version = attached_node();
    wrong_version.compatible    = false;

    const core::NodeLink links[] = {no_node, bound_but_offers_nothing, wrong_version, out_of_range,
                                    attached_node()};

    core::CompanionLinkState no_phone;
    core::CompanionLinkState phone_away{true, false};
    core::CompanionLinkState phone_here{true, true};
    const core::CompanionLinkState phones[] = {no_phone, phone_away, phone_here};

    const platform::BoardProfile boards[] = {waveshare(), twatch(platform::RadioChip::Sx1262),
                                             twatch(platform::RadioChip::Cc1101)};

    for (const platform::BoardProfile& board : boards) {
        platform::ProfileInventory inventory(board);
        bring_up(inventory);
        core::CapabilityRegistry caps(inventory);

        for (const core::NodeLink& link : links) {
            caps.set_node_link(link);
            for (const core::CompanionLinkState& phone : phones) {
                caps.set_companion(phone);

                for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
                    const auto capability = static_cast<core::Capability>(i);
                    if (backed_by_the_phone(capability)) {
                        continue;
                    }

                    const core::Availability availability = caps.availability(capability);
                    const bool               remote_state =
                        availability == core::Availability::Unprovisioned ||
                        availability == core::Availability::Unreachable ||
                        availability == core::Availability::Incompatible;
                    if (!remote_state) {
                        continue;
                    }

                    // Both accessors, and not only because they must agree.
                    // The defect lived in provider(); a sweep that consulted
                    // source() alone would pass over a re-divergence in the
                    // exact function that had it.
                    const OriginReading readings[] = {
                        {caps.provider(capability).origin, "provider()"},
                        {caps.source(capability).provider.origin, "source()"},
                    };

                    for (const OriginReading& reading : readings) {
                        if (reading.origin == core::Origin::Local) {
                            std::fprintf(stderr,
                                         "FAIL %s on %s: %s says %s from local — ADR-0004 §2 "
                                         "says those three states imply a remote provider\n",
                                         core::to_string(capability), board.id, reading.accessor,
                                         core::to_string(availability));
                            ++failures;
                        }
                    }
                }
            }
        }
    }
}

// And that there is only one decision behind the three accessors.
//
// check_source() already pins availability() against provider(), which is the
// pair that disagreed. This closes the third side of the triangle, so a future
// source() that grows a rule of its own is caught by the suite rather than by
// a screen.
void test_the_three_accessors_are_one_answer()
{
    core::NodeLink out_of_range = attached_node();
    out_of_range.reachable      = false;

    core::NodeLink wrong_version = attached_node();
    wrong_version.compatible    = false;

    const core::NodeLink links[] = {core::NodeLink{}, wrong_version, out_of_range,
                                    attached_node()};

    const platform::BoardProfile boards[] = {waveshare(), twatch(platform::RadioChip::Sx1262)};

    for (const platform::BoardProfile& board : boards) {
        platform::ProfileInventory inventory(board);
        bring_up(inventory);
        core::CapabilityRegistry caps(inventory);

        for (const core::NodeLink& link : links) {
            caps.set_node_link(link);
            for (std::uint8_t i = 0; i < core::kCapabilityCount; ++i) {
                const auto                   capability = static_cast<core::Capability>(i);
                const core::CapabilitySource source     = caps.source(capability);
                CHECK(source.availability == caps.availability(capability));
                CHECK(source.provider.origin == caps.provider(capability).origin);
                CHECK(source.provider.id == caps.provider(capability).id);
            }
        }
    }
}

// ADR-0001, carried into ADR-0007: no two availability states may render
// identically, because each one is a different sentence to a user.
void test_states_are_distinguishable()
{
    const char* seen[7];
    for (std::uint8_t i = 0; i < 7; ++i) {
        seen[i] = core::to_string(static_cast<core::Availability>(i));
        CHECK(seen[i] != nullptr && std::strcmp(seen[i], "?") != 0);
        for (std::uint8_t j = 0; j < i; ++j) {
            CHECK(std::strcmp(seen[i], seen[j]) != 0);
        }
    }

    // And the ranking is a total order over the seven, so "which of these two
    // do we tell the user about" always has one answer.
    for (std::uint8_t i = 0; i < 7; ++i) {
        for (std::uint8_t j = 0; j < i; ++j) {
            CHECK(core::remedy_rank(static_cast<core::Availability>(i)) !=
                  core::remedy_rank(static_cast<core::Availability>(j)));
        }
    }
}

// The launcher rule: hide only what can never run; offer what merely cannot run
// yet, and let it explain itself.
void test_launcher_gating()
{
    static const core::Capability navigator_needs[] = {core::Capability::Position,
                                                       core::Capability::Heading};
    static const core::Capability remote_needs[]    = {core::Capability::InfraredBlast};

    apps::AppManifest navigator;
    navigator.id             = "navigator";
    navigator.required       = navigator_needs;
    navigator.required_count = 2;

    apps::AppManifest remote;
    remote.id             = "remote";
    remote.required       = remote_needs;
    remote.required_count = 1;

    platform::ProfileInventory inventory(*platform::find_board_profile("waveshare-amoled-206"));
    bring_up(inventory);
    core::CapabilityRegistry caps(inventory);

    // No node: the Navigator is offered and will explain itself.
    CHECK(apps::launcher_entry(navigator, caps) == apps::LauncherEntry::NeedsAttention);
    core::Capability   blocking{};
    core::Availability blocking_availability{};
    CHECK(apps::blocking_capability(navigator, caps, blocking, blocking_availability));
    CHECK(blocking == core::Capability::Position);
    CHECK(blocking_availability == core::Availability::Unprovisioned);

    // With a node it simply works.
    caps.set_node_link(attached_node());
    CHECK(apps::launcher_entry(navigator, caps) == apps::LauncherEntry::Available);

    // The infrared remote never runs on this board, node or no node.
    CHECK(apps::launcher_entry(remote, caps) == apps::LauncherEntry::Hidden);
}

}  // namespace

int main()
{
    test_board_profiles();
    test_shipped_twatch_radio_is_unread();
    test_sx1262_bands_are_the_drivers_not_this_units();
    test_untouched_is_not_failed();
    test_radio_is_not_lora();
    test_waveshare_position_comes_from_a_node();
    test_unsupported_is_terminal();
    test_supports_is_stable();
    test_heading_needs_an_absolute_reference();
    test_waveshare_heading_comes_from_a_node();
    test_provider_origin_survives_the_link_going_away();
    test_no_local_source_means_a_remote_provider();
    test_a_working_local_source_outranks_a_node();
    test_a_degraded_local_source_stays_local();
    test_terminal_unsupported_has_no_provider();
    test_the_companion_is_not_on_the_origin_axis();
    test_origin_does_not_move_when_only_the_link_does();
    test_remote_states_imply_a_remote_provider();
    test_the_three_accessors_are_one_answer();
    test_states_are_distinguishable();
    test_launcher_gating();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("capability registry: all checks passed (host only — no hardware involved)\n");
    return 0;
}
