#include "attadipa/apps/app_manifest.h"

namespace attadipa::apps {

using core::Availability;
using core::Capability;

LauncherEntry launcher_entry(const AppManifest& manifest, const core::CapabilityRegistry& caps)
{
    bool needs_attention = false;

    for (std::uint8_t i = 0; i < manifest.required_count; ++i) {
        const Availability availability = caps.availability(manifest.required[i]);

        if (availability == Availability::Unsupported) {
            return LauncherEntry::Hidden;
        }
        if (availability != Availability::Ready) {
            needs_attention = true;
        }
    }

    // enhanced_by is deliberately not consulted. A capability that makes an
    // application better and is missing is not a reason to warn about the
    // application — it is something the application itself degrades around,
    // which is the difference between "required" and "enhanced by".
    return needs_attention ? LauncherEntry::NeedsAttention : LauncherEntry::Available;
}

bool blocking_capability(const AppManifest&              manifest,
                         const core::CapabilityRegistry& caps,
                         Capability&                     capability_out,
                         Availability&                   availability_out)
{
    if (manifest.required_count == 0) {
        return false;
    }

    Capability   worst_capability   = manifest.required[0];
    Availability worst_availability = caps.availability(worst_capability);

    for (std::uint8_t i = 1; i < manifest.required_count; ++i) {
        const Availability availability = caps.availability(manifest.required[i]);
        // Least actionable first: if one requirement can never be met and
        // another merely needs turning on, the sentence to write is about the
        // first one. Telling the user to enable something that would still
        // leave the application unrunnable is worse than saying nothing.
        if (core::remedy_rank(availability) < core::remedy_rank(worst_availability)) {
            worst_capability   = manifest.required[i];
            worst_availability = availability;
        }
    }

    capability_out   = worst_capability;
    availability_out = worst_availability;
    return true;
}

}  // namespace attadipa::apps
