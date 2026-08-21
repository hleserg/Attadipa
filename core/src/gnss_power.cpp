#include "firefly/core/gnss_power.h"

namespace firefly::core {
namespace {

// Past this, a receiver's ephemeris is old enough that the start is warm rather
// than hot whatever it kept. Two hours is the usual broadcast validity for GPS
// ephemeris; it is ESTIMATED here, from the constellation's published cadence
// rather than from either of our candidate receivers, and T-051/T-052 are where
// it becomes a fact about the part we actually have.
constexpr Millis kHotStartWindow{2 * 60 * 60 * 1000};

}  // namespace

StartKind start_kind(const GnssContext& context)
{
    if (context.ephemeris_retained && context.since_last_fix < kHotStartWindow) {
        return StartKind::Hot;
    }
    // Warm needs somewhere for the almanac and the clock to have survived. A
    // receiver with no backup domain came up knowing nothing, however recently
    // it was last on.
    if (context.capabilities.backup_domain) {
        return StartKind::Warm;
    }
    return StartKind::Cold;
}

bool transition_is_legal(GnssState from, GnssState to, const GnssCapabilities& capabilities)
{
    if (from == to) {
        return true;
    }

    // A state the hardware cannot hold is not a state. Refusing it here means
    // the impossible case never reaches a driver that would have to invent
    // behaviour for it.
    if (to == GnssState::Backup && !capabilities.backup_domain) {
        return false;
    }
    if (to == GnssState::PowerSave && !capabilities.power_save_mode) {
        return false;
    }

    // Off is always reachable: the rail can be dropped from any state, and a
    // model that said otherwise would be a model that argues with the PMU.
    if (to == GnssState::Off) {
        return true;
    }

    switch (from) {
        case GnssState::Off:
        case GnssState::Backup:
            // Powering up always begins by searching, even from backup. A
            // retained almanac makes the search short; it does not skip it.
            return to == GnssState::Acquiring;

        case GnssState::Acquiring:
            return to == GnssState::Tracking || to == GnssState::Degraded ||
                   to == GnssState::Backup;

        case GnssState::Tracking:
            return to == GnssState::Degraded || to == GnssState::PowerSave ||
                   to == GnssState::Backup || to == GnssState::Acquiring;

        case GnssState::PowerSave:
            return to == GnssState::Tracking || to == GnssState::Acquiring ||
                   to == GnssState::Degraded || to == GnssState::Backup;

        case GnssState::Degraded:
            return to == GnssState::Acquiring || to == GnssState::Tracking ||
                   to == GnssState::Backup;
    }
    return false;
}

GnssState next_state(GnssState current, const GnssContext& context)
{
    const GnssCapabilities& can = context.capabilities;

    // The device's own power state outranks everything. A receiver kept warm
    // through a hibernate would make "off" mean "off except for the part that
    // draws the most" — the same conflation the power model exists to prevent.
    if (context.device_power == PowerState::PowerOff ||
        context.device_power == PowerState::DeepSleep) {
        return can.backup_domain ? GnssState::Backup : GnssState::Off;
    }

    // An application waiting for a position outranks thrift, but not physics:
    // it moves the receiver towards a fix, it does not conjure one.
    if (context.fresh_fix_requested) {
        if (current == GnssState::Off || current == GnssState::Backup) {
            return GnssState::Acquiring;
        }
        if (current == GnssState::PowerSave) {
            return GnssState::Tracking;
        }
        return current;
    }

    // Nobody is asking, and the wrist is not moving. This is the ordinary state
    // of a watch on a bedside table, and it is where the charge is saved.
    if (!context.device_moving && current == GnssState::Tracking) {
        if (can.power_save_mode) {
            return GnssState::PowerSave;
        }
        if (can.backup_domain) {
            return GnssState::Backup;
        }
        return GnssState::Off;
    }

    // Moving again after a rest. Retained ephemeris is what makes this cheap,
    // and start_kind() is where that shows up as a shorter expected wait.
    if (context.device_moving &&
        (current == GnssState::PowerSave || current == GnssState::Backup)) {
        return GnssState::Acquiring;
    }

    return current;
}

const char* to_string(GnssState state)
{
    switch (state) {
        case GnssState::Off:       return "Off";
        case GnssState::Backup:    return "Backup";
        case GnssState::Acquiring: return "Acquiring";
        case GnssState::Tracking:  return "Tracking";
        case GnssState::PowerSave: return "PowerSave";
        case GnssState::Degraded:  return "Degraded";
    }
    return "?";
}

const char* to_string(StartKind kind)
{
    switch (kind) {
        case StartKind::Cold: return "Cold";
        case StartKind::Warm: return "Warm";
        case StartKind::Hot:  return "Hot";
    }
    return "?";
}

}  // namespace firefly::core
