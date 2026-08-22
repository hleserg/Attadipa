#include "clock_host.h"

#include "attadipa/apps/clock_screen.h"
#include "attadipa/ui/metrics.h"

#include "boot_screen.h"

namespace attadipa::sim {
namespace {

bool   g_child_mode = false;
Screen g_screen     = Screen::Clock;

const platform::HardwareInventory* g_inventory = nullptr;
const core::CapabilityRegistry*    g_caps      = nullptr;

}  // namespace

void toggle_child_mode()
{
    g_child_mode = !g_child_mode;
    rebuild_current_screen();
}

void set_child_mode(bool on)
{
    g_child_mode = on;
}

void set_screen(Screen screen)
{
    g_screen = screen;
}

void toggle_screen()
{
    g_screen = g_screen == Screen::Clock ? Screen::Diagnostic : Screen::Clock;
    rebuild_current_screen();
}

void rebuild_current_screen()
{
    if (g_inventory == nullptr || g_caps == nullptr) {
        return;
    }
    if (g_screen == Screen::Diagnostic) {
        build_boot_screen(*g_inventory, *g_caps);
    } else {
        build_clock_screen(*g_inventory, *g_caps);
    }
}

void build_clock_screen(const platform::HardwareInventory& inventory,
                        const core::CapabilityRegistry&    caps)
{
    g_inventory = &inventory;
    g_caps      = &caps;

    apps::ClockModel model;

    // The simulator has no RTC and this is not one. A fixed, obviously-chosen
    // time is better than the host's clock here: a screenshot that changes every
    // minute cannot be compared against the previous one, and the visual test
    // matrix (final §53) is built out of comparisons.
    //
    // What it does model honestly is the *unknown* case, which is what a watch
    // actually shows in the seconds after a cold boot: `Time` not Ready means no
    // time, not midnight.
    const bool time_ready = caps.availability(core::Capability::Time) == core::Availability::Ready;
    model.time_known = time_ready;
    model.hour       = 9;
    model.minute     = 41;

    model.date_known = time_ready;
    model.weekday    = 5;   // Saturday
    model.day        = 22;
    model.month      = 8;

    // Battery is a hardware fact and nobody has measured one. `BatterySense`
    // being present is not the same as having read it, so the gauge is unknown
    // unless the part is Ready — and even then the number below is INVENTED for
    // the simulator and is labelled as such wherever it is reported.
    model.battery_known =
        inventory.state(platform::HardwareFeature::BatterySense) == platform::HardwareState::Ready;
    model.battery_percent = 62;
    model.charging        = false;

    // The status row: three capabilities, and the registry answers for each.
    //
    // The composition root is where this belongs — an application asks what the
    // device can do and gets an `Availability`, never a chip. On the Waveshare
    // that means `Unsupported` for both the mesh and a position, and the row
    // draws neither: a board with no LoRa and no GNSS should not carry two dead
    // icons to prove it.
    model.mesh     = caps.availability(core::Capability::MeshMessaging);
    model.position = caps.availability(core::Capability::Position);
    model.phone    = caps.availability(core::Capability::CompanionLink);

    model.child_mode = g_child_mode;

    apps::build_clock(lv_screen_active(), model,
                      ui::Metrics::for_dpi(inventory.display().dpi()), current_theme());
}

}  // namespace attadipa::sim
