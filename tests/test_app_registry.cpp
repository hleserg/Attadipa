#include <cstdio>
#include <cstring>

#include "attadipa/apps/app_registry.h"
#include "attadipa/apps/clock.h"
#include "attadipa/apps/navigation.h"
#include "attadipa/core/capability_registry.h"
#include "attadipa/platform/board_profile.h"
#include "attadipa/platform/hardware_inventory.h"

// The application registry, the Settings declaration, and the cadence rule.
//
// Host tests against board *profiles*, like every other test in this directory:
// nothing here is a statement about a board. What it checks is that a
// composition root asking these three questions gets the answers ADR-0007 and
// the manifests say it should.

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

// A board with nothing on it. Not a real profile and not meant to be one: it is
// the configuration in which every capability is Unsupported, which is the only
// way to state "Settings appears even here" as a test rather than as a hope.
class NothingInventory final : public platform::HardwareInventory {
public:
    bool present(platform::HardwareFeature) const override { return false; }
    platform::HardwareState state(platform::HardwareFeature) const override
    {
        return platform::HardwareState::Absent;
    }
    const platform::RadioInfo* radio() const override { return nullptr; }
    const platform::DisplayInfo& display() const override
    {
        static const platform::DisplayInfo none{};
        return none;
    }
    const char* board_id() const override { return "nothing"; }
    const char* board_name() const override { return "a board with nothing on it"; }
};

const apps::AppManifest* manifest_with_id(const char* id)
{
    const apps::AppRegistry& registry = apps::installed_apps();
    for (std::uint8_t i = 0; i < registry.count; ++i) {
        if (registry.apps[i] != nullptr && std::strcmp(registry.apps[i]->id, id) == 0) {
            return registry.apps[i];
        }
    }
    return nullptr;
}

// 1. The registry enumerates every installed application.
//
// Named rather than counted: an assertion on the count alone passes when one
// manifest is swapped for another, and the point of the registry is that a
// board reaches *each* of these without naming it.
void test_registry_enumerates_every_application()
{
    const apps::AppRegistry& registry = apps::installed_apps();

    CHECK(registry.apps != nullptr);
    CHECK(registry.count == 3);

    CHECK(manifest_with_id("clock") == &apps::clock_manifest());
    CHECK(manifest_with_id("navigation") == &apps::navigation_manifest());
    CHECK(manifest_with_id("settings") == &apps::settings_manifest());

    for (std::uint8_t i = 0; i < registry.count; ++i) {
        CHECK(registry.apps[i] != nullptr);
        CHECK(registry.apps[i]->id[0] != '\0');
    }

    // Static storage, so a second call is the same table and not a rebuild.
    CHECK(&apps::installed_apps() == &registry);
    CHECK(apps::installed_apps().apps == registry.apps);
}

// 2. Settings is offered whatever the device turns out to be.
//
// "Every capability Unsupported" is not a configuration this registry can be
// put in, and finding that out is worth recording: on an inventory holding
// nothing at all, Time and PersistentStorage are still Ready, and Position,
// Heading and MeshMessaging are Unprovisioned rather than Unsupported, because
// a node could provide them (docs/adr/0004-capability-sources.md §4). So the
// property is stated the way it is actually true — Settings' entry is the same
// on every inventory the project can build, while its neighbours' are not.
void test_settings_is_permanent()
{
    CHECK(apps::settings_manifest().required_count == 0);

    NothingInventory nothing;
    core::CapabilityRegistry bare(nothing);

    platform::ProfileInventory waveshare(*platform::find_board_profile("waveshare-amoled-206"));
    core::CapabilityRegistry waveshare_caps(waveshare);

    platform::ProfileInventory twatch(*platform::find_board_profile("t-watch-s3-plus"));
    core::CapabilityRegistry twatch_caps(twatch);

    CHECK(apps::launcher_entry(apps::settings_manifest(), bare) ==
          apps::LauncherEntry::Available);
    CHECK(apps::launcher_entry(apps::settings_manifest(), waveshare_caps) ==
          apps::LauncherEntry::Available);
    CHECK(apps::launcher_entry(apps::settings_manifest(), twatch_caps) ==
          apps::LauncherEntry::Available);

    // And it has nothing to explain, which is what makes the screen reachable
    // rather than a dead end that says why it is dead.
    core::Capability   blocking{};
    core::Availability blocking_availability{};
    CHECK(!apps::blocking_capability(apps::settings_manifest(), bare, blocking,
                                     blocking_availability));

    // The contrast that makes the three checks above mean something: an
    // application that requires something does not answer the same on all three.
    static const core::Capability wants_infrared[] = {core::Capability::InfraredBlast};
    apps::AppManifest remote;
    remote.id             = "remote";
    remote.required       = wants_infrared;
    remote.required_count = 1;
    CHECK(apps::launcher_entry(remote, bare) == apps::LauncherEntry::Hidden);

    static const core::Capability wants_motion[] = {core::Capability::MotionSensing};
    apps::AppManifest steps;
    steps.id             = "steps";
    steps.required       = wants_motion;
    steps.required_count = 1;
    CHECK(apps::launcher_entry(steps, bare) == apps::LauncherEntry::Hidden);
    CHECK(apps::launcher_entry(steps, twatch_caps) != apps::LauncherEntry::Hidden);
}

// 3. The cadence rule, including the case the composition root depends on: a
//    manifest with no cadence of its own never stops the timer.
void test_ui_period()
{
    const core::Millis board{1000};

    apps::AppManifest quiet;  // tick_period{} — event-driven only
    CHECK(apps::ui_period(quiet, board) == board);
    CHECK(apps::ui_period(quiet, board).value != 0);

    apps::AppManifest faster;
    faster.tick_period = core::Millis{500};
    CHECK(apps::ui_period(faster, board) == core::Millis{500});

    apps::AppManifest slower;
    slower.tick_period = core::Millis{5000};
    CHECK(apps::ui_period(slower, board) == board);

    apps::AppManifest exact;
    exact.tick_period = board;
    CHECK(apps::ui_period(exact, board) == board);

    // The two shipped manifests, through the same door the board uses.
    CHECK(apps::ui_period(apps::clock_manifest(), board) == core::Millis{1000});
    CHECK(apps::ui_period(apps::navigation_manifest(), board) == core::Millis{1000});
    CHECK(apps::ui_period(apps::settings_manifest(), board) == board);
}

// 4. Every installed manifest can be put through the gate, which is the loop a
//    composition root runs before it decides what to offer and what to tick.
void test_every_installed_manifest_answers()
{
    NothingInventory nothing;
    core::CapabilityRegistry caps(nothing);

    const apps::AppRegistry& registry = apps::installed_apps();
    std::uint8_t hidden = 0;
    std::uint8_t available = 0;
    std::uint8_t needs_attention = 0;

    for (std::uint8_t i = 0; i < registry.count; ++i) {
        switch (apps::launcher_entry(*registry.apps[i], caps)) {
        case apps::LauncherEntry::Hidden:
            ++hidden;
            break;
        case apps::LauncherEntry::Available:
            ++available;
            break;
        case apps::LauncherEntry::NeedsAttention:
            ++needs_attention;
            break;
        }
    }

    // Nothing shipped today is Hidden on a board holding nothing, and that is
    // the honest answer rather than a weak one: the clock needs Time, which is
    // Ready; Settings needs nothing; and the navigator needs Position, which a
    // node could still provide, so it is offered and will say what is missing.
    CHECK(hidden == 0);
    CHECK(available == 2);
    CHECK(needs_attention == 1);
    CHECK(apps::launcher_entry(apps::navigation_manifest(), caps) ==
          apps::LauncherEntry::NeedsAttention);

    // A board must be able to reach every manifest's cadence the same way.
    for (std::uint8_t i = 0; i < registry.count; ++i) {
        CHECK(apps::ui_period(*registry.apps[i], core::Millis{1000}).value != 0);
    }
}

}  // namespace

int main()
{
    test_registry_enumerates_every_application();
    test_settings_is_permanent();
    test_ui_period();
    test_every_installed_manifest_answers();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("app registry: all checks passed\n");
    return 0;
}
