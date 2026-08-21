#pragma once

namespace firefly::core {
class CapabilityRegistry;
}
namespace firefly::platform {
class HardwareInventory;
}

namespace firefly::sim {

// The simulator's first screen.
//
// This is a developer diagnostic, not a product screen, and the distinction is
// worth being loud about. It is in English only, which
// docs/adr/0010-localization.md forbids for anything a user sees — a rule about
// product surfaces, and this is not one. It uses Montserrat, which has no
// Cyrillic and is not the product typeface. It uses hex colours written in
// place, which T-009 forbids for UI code — for the same reason.
//
// What it is for: proving, at runtime and end to end, that the two capability
// layers work through the boundary. It shows what the board has, and what an
// application would be allowed to ask for, side by side. The first product
// screen is the Clock, and it is T-037.
void build_boot_screen(const platform::HardwareInventory& inventory,
                       const core::CapabilityRegistry&    caps);

}  // namespace firefly::sim
