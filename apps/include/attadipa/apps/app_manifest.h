#pragma once

#include <cstdint>

#include "attadipa/core/availability.h"
#include "attadipa/core/capability.h"
#include "attadipa/core/capability_registry.h"

// The application layer.
//
// Everything here is written against attadipa::core. There is no way to reach
// attadipa::platform from this library — not by convention, but because the link
// line does not include it (docs/adr/0007-two-capability-layers.md §5). An
// application that wants to know which GPIO powers the GNSS module cannot find
// out, and that is the feature.

namespace attadipa::apps {

// docs/adr/0004-capability-sources.md §5 writes this with std::span. The
// project is C++17 (see the top-level CMakeLists), so it is a pointer and a
// count until that changes — same contract, older spelling.
struct AppManifest {
    const char* id = "";

    // Cannot run without these.
    const core::Capability* required       = nullptr;
    std::uint8_t            required_count = 0;

    // Better with, fine without. Never a reason to hide an application.
    const core::Capability* enhanced_by       = nullptr;
    std::uint8_t            enhanced_by_count = 0;
};

enum class LauncherEntry : std::uint8_t {
    // Every required capability is Ready. Open it and it works.
    Available,
    // It is offered, and opening it will explain what is missing and what the
    // user can do. This is the ordinary case on a Waveshare board with no node
    // attached, not the exotic one — ADR-0007's "Testable" note.
    NeedsAttention,
    // No configuration of this device can ever run it. Offering it would be a
    // promise the hardware cannot keep.
    Hidden,
};

// Whether an application appears in the launcher, and how.
LauncherEntry launcher_entry(const AppManifest& manifest, const core::CapabilityRegistry& caps);

// The least available required capability, and its availability — the pair a
// screen needs to write one honest sentence about why it cannot run. Returns
// false when the manifest requires nothing.
bool blocking_capability(const AppManifest&               manifest,
                         const core::CapabilityRegistry&  caps,
                         core::Capability&                capability_out,
                         core::Availability&              availability_out);

}  // namespace attadipa::apps
