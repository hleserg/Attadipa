#pragma once

#include <cstdint>

#include "firefly/core/clock.h"

// The device's power states, and the wake sources each one is allowed to arm.
//
// The two middle states are the whole reason this file exists. Upstream
// MeshCore's `HeltecV4R8Board::powerOff()` is literally `enterDeepSleep(0)`,
// which leaves the front end in RX and arms EXT1 on the radio's DIO1 line — so
// "off" ends at the next received packet (issue #3165, fix #3168 still open;
// docs/upstream/meshcore-1.17-review.md §5). Two behaviours that differ only in
// their wake sources were given one function name, and a review did not catch
// it because a name is not a type.
//
// Here they are two states, and `wake_plan_is_legal()` refuses the combination
// rather than trusting anyone to remember. That is the same technique the
// capability boundary and the localization boundary already use: make the rule
// mechanical, because the rules that are only written down are the ones that
// get broken quietly.
//
// Nothing in this header calls ESP-IDF. It cannot: this repository has no
// ESP-IDF target yet (T-004). What it has is the model, host-testable now, so
// that the driver written later has something to be wrong against.

namespace firefly::core {

enum class PowerState : std::uint8_t {
    Active,           // screen on, everything the user is using is running
    Idle,             // screen off, CPU awake, services still ticking
    LightSleep,       // CPU retained; wakes on time, touch, button or an IRQ
    MeshListenSleep,  // as LightSleep, but the radio is deliberately left
                      // listening and its IRQ is a wake source. This is what
                      // upstream calls deep sleep with a timeout
    DeepSleep,        // radio off, RTC memory retained, wakes on time or button
    PowerOff,         // as off as firmware can make it. Only a human restarts it
};

inline constexpr std::uint8_t kPowerStateCount = static_cast<std::uint8_t>(PowerState::PowerOff) + 1;

// What may bring the device back. A bitmask, because a state arms a set.
enum class WakeSource : std::uint8_t {
    Timer,        // an RTC or ULP timer
    Button,       // a physical key, including the PMU's power key
    Touch,        // the touch panel
    RadioIrq,     // the transceiver has something — DIO1 on an SX1262
    NodeLink,     // the transport that reaches an attached Firefly node
    Usb,          // a host attaching or writing
    Accelerometer,// a wrist-raise or tap
    Pmu,          // charger attach, low battery, the PMU's own alarm
};

inline constexpr std::uint8_t kWakeSourceCount =
    static_cast<std::uint8_t>(WakeSource::Pmu) + 1;

constexpr std::uint16_t wake_bit(WakeSource source)
{
    return static_cast<std::uint16_t>(1u << static_cast<std::uint16_t>(source));
}

// The set a state is permitted to arm.
//
// Deliberately a whitelist. A blacklist would mean that a wake source added
// later is legal everywhere by default, which is precisely how a radio ended up
// armed inside somebody's idea of "off".
std::uint16_t legal_wake_sources(PowerState state);

// Would this plan be honest?
//
// The one that matters: `DeepSleep` and `PowerOff` may not arm the radio or the
// node link. If they could, "off" would mean "off until somebody transmits",
// and the user who pressed and held the button would be wrong about their own
// device.
bool wake_plan_is_legal(PowerState state, std::uint16_t armed);

// Whether a transition is one the device may make directly. Notably `PowerOff`
// is reachable from anywhere — a user holding the button, or a critically flat
// battery, does not negotiate — while the sleeps are entered from `Idle`.
bool transition_is_legal(PowerState from, PowerState to);

// How a measurement got its value.
//
// CLAUDE.md's rule in the type system: an estimate must never be able to read
// as a measurement. A field of this type sitting next to a number means the
// number cannot be quoted without its provenance.
enum class Provenance : std::uint8_t {
    Unknown,    // nobody has established it. The default, and it stays that way
    Estimated,  // calculated, or taken from a datasheet
    Measured,   // an instrument was attached to this board and read
};

struct PowerMetric {
    std::uint32_t value      = 0;
    Provenance    provenance = Provenance::Unknown;

    constexpr bool usable() const { return provenance != Provenance::Unknown; }
};

// The numbers this project has to be able to state, and cannot yet.
//
// Every one is `Unknown` today, and that is the honest state of a project with
// no current-measurement setup and no board in hand. The slots exist so the
// question "what is our sleep current?" has a place to be answered rather than
// a conversation to be had, and so that filling one is a visible change.
// docs/testing/HIL_PLANS.md is how they get filled.
struct PowerMetrics {
    PowerMetric average_current_ua;
    PowerMetric sleep_current_ua;
    PowerMetric wake_latency_us;
    PowerMetric energy_per_gnss_fix_uj;
    PowerMetric energy_per_lora_tx_uj;
    PowerMetric energy_per_lora_rx_uj;
};

// Why the device woke, kept because a wearable that wakes for no visible reason
// is a battery complaint nobody can debug. Upstream's #3168 is exactly this
// question asked too late.
struct WakeRecord {
    PowerState    from = PowerState::Active;
    WakeSource    by   = WakeSource::Timer;
    MonotonicTime at{};
};

const char* to_string(PowerState state);
const char* to_string(WakeSource source);
const char* to_string(Provenance provenance);

}  // namespace firefly::core
