#include <cstdio>
#include <cstring>

#include "firefly/apps/app_manifest.h"
#include "firefly/core/capability_registry.h"
#include "firefly/platform/board_profile.h"
#include "firefly/platform/hardware_inventory.h"

// Host tests for the two capability layers.
//
// NOT a statement about hardware. Everything here runs on a desktop against
// board *profiles* — descriptions transcribed from schematics, not boards. It
// checks that the rules in ADR-0003, ADR-0007 and ADR-0009 are implemented as
// written. Whether the profiles match the metal is a hardware test, and no
// hardware test in this repository has been executed.

using namespace firefly;

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
    // rather than Unsupported, because a Firefly node can always provide mesh.
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
         "nobody has told us which chip is fitted (open question A2)"},
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

// ADR-0009: heading has three possible sources and neither board has the
// obvious one.
void test_heading_has_three_sources()
{
    // Waveshare: no magnetometer, but a six-axis IMU, so fusion.
    platform::ProfileInventory waveshare_inventory(*platform::find_board_profile(
        "waveshare-amoled-206"));
    bring_up(waveshare_inventory);
    core::CapabilityRegistry waveshare_caps(waveshare_inventory);
    CHECK(waveshare_inventory.present(platform::HardwareFeature::Gyroscope));
    CHECK_AVAIL(waveshare_caps, core::Capability::Heading, core::Availability::Ready);

    // T-Watch: an accelerometer with no gyroscope, so no fusion — but it has
    // GNSS, so course-over-ground. Ready here means "something can produce a
    // heading", not "the number means anything right now": a standing user is
    // Ready and Invalid at once, and that is validity, not availability.
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
    test_untouched_is_not_failed();
    test_radio_is_not_lora();
    test_waveshare_position_comes_from_a_node();
    test_unsupported_is_terminal();
    test_supports_is_stable();
    test_heading_has_three_sources();
    test_states_are_distinguishable();
    test_launcher_gating();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("capability registry: all checks passed (host only — no hardware involved)\n");
    return 0;
}
