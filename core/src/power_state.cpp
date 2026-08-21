#include "attadipa/core/power_state.h"

namespace attadipa::core {

std::uint16_t legal_wake_sources(PowerState state)
{
    switch (state) {
        case PowerState::Active:
        case PowerState::Idle:
            // Awake. "Wake source" is not a meaningful question, and returning
            // everything rather than nothing keeps callers from special-casing
            // the awake states out of a loop.
            return 0xFFFFU;

        case PowerState::LightSleep:
            return wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button) |
                   wake_bit(WakeSource::Touch) | wake_bit(WakeSource::RadioIrq) |
                   wake_bit(WakeSource::NodeLink) | wake_bit(WakeSource::Usb) |
                   wake_bit(WakeSource::Accelerometer) | wake_bit(WakeSource::Pmu);

        case PowerState::MeshListenSleep:
            // The radio is the *point* of this state. Touch and USB are not:
            // the screen is off and the host is not the reason we are asleep.
            return wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button) |
                   wake_bit(WakeSource::RadioIrq) | wake_bit(WakeSource::NodeLink) |
                   wake_bit(WakeSource::Pmu);

        case PowerState::DeepSleep:
            // No radio, no node link. This is the line upstream crossed.
            return wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button) |
                   wake_bit(WakeSource::Pmu);

        case PowerState::PowerOff:
            // A human, or a charger. Nothing that arrives over the air.
            return wake_bit(WakeSource::Button) | wake_bit(WakeSource::Pmu);
    }
    return 0;
}

bool wake_plan_is_legal(PowerState state, std::uint16_t armed)
{
    return (armed & ~legal_wake_sources(state)) == 0;
}

bool transition_is_legal(PowerState from, PowerState to)
{
    if (from == to) {
        return true;
    }

    // Off is always reachable. A held button and a flat battery are not
    // requests, and a state machine that could refuse them would be a device
    // that will not turn off.
    if (to == PowerState::PowerOff) {
        return true;
    }

    switch (from) {
        case PowerState::Active:
            return to == PowerState::Idle;

        case PowerState::Idle:
            // Every sleep is entered from Idle, so there is exactly one place
            // that decides which one — and therefore exactly one place to get
            // the decision wrong, rather than one per caller.
            return to == PowerState::Active || to == PowerState::LightSleep ||
                   to == PowerState::MeshListenSleep || to == PowerState::DeepSleep;

        case PowerState::LightSleep:
        case PowerState::MeshListenSleep:
            // Waking returns to Idle, never straight to Active: something has
            // to decide whether the wake is worth lighting the screen for, and
            // that decision belongs above this file.
            return to == PowerState::Idle;

        case PowerState::DeepSleep:
            // The chip reboots out of deep sleep. It does not resume, and
            // modelling it as a transition to Idle would hide the fact that
            // every RAM-resident piece of state is gone.
            return to == PowerState::Active;

        case PowerState::PowerOff:
            return to == PowerState::Active;
    }
    return false;
}

const char* to_string(PowerState state)
{
    switch (state) {
        case PowerState::Active:          return "Active";
        case PowerState::Idle:            return "Idle";
        case PowerState::LightSleep:      return "LightSleep";
        case PowerState::MeshListenSleep: return "MeshListenSleep";
        case PowerState::DeepSleep:       return "DeepSleep";
        case PowerState::PowerOff:        return "PowerOff";
    }
    return "?";
}

const char* to_string(WakeSource source)
{
    switch (source) {
        case WakeSource::Timer:         return "Timer";
        case WakeSource::Button:        return "Button";
        case WakeSource::Touch:         return "Touch";
        case WakeSource::RadioIrq:      return "RadioIrq";
        case WakeSource::NodeLink:      return "NodeLink";
        case WakeSource::Usb:           return "Usb";
        case WakeSource::Accelerometer: return "Accelerometer";
        case WakeSource::Pmu:           return "Pmu";
    }
    return "?";
}

const char* to_string(Provenance provenance)
{
    switch (provenance) {
        case Provenance::Unknown:   return "UNKNOWN";
        case Provenance::Estimated: return "ESTIMATED";
        case Provenance::Measured:  return "MEASURED";
    }
    return "?";
}

}  // namespace attadipa::core
