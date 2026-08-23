#pragma once

#include <cstdint>
#include <optional>

#include "attadipa/core/clock.h"
#include "attadipa/core/gnss_power.h"
#include "attadipa/core/position.h"
#include "attadipa/core/power_state.h"
#include "attadipa/core/transport_state.h"
#include "attadipa/core/trust.h"

// One structured snapshot of what the device currently believes about itself.
//
// Two rules, and the second is the one that gets broken elsewhere:
//
//   1. **Every field is optional and "not known" is not zero.** A snapshot with
//      `rssi = 0` where nothing measured RSSI is a lie that reads as a
//      measurement. This is the same argument as the GNSS observation's
//      (position.h) and the same argument as `Provenance` in power_state.h.
//
//   2. **There is no serializer here, and core does not know what JSON is.**
//      A snapshot is *rendered* by whoever is displaying or shipping it — the
//      simulator today, a support bundle or a companion app later — and the
//      layer that produces a fact does not get to decide how it will be written
//      down. That is exactly the boundary ADR-0010 §4 draws for language,
//      applied to encoding.
//
// It is a plain aggregate with no I/O, so it works identically in the simulator,
// in a host test and on a device. That is not a convenience; a diagnostic that
// behaves differently where it is tested is not a diagnostic.

namespace attadipa::core {

// Enough to identify exactly what is running, because "latest firmware" is not
// an answer to a field report.
struct BuildIdentity {
    const char*   version    = nullptr;  // ATTADIPA_VERSION_STRING
    const char*   commit     = nullptr;  // short hash, or null if not stamped
    const char*   built_at   = nullptr;  // ISO-8601, or null
    std::uint8_t  board      = 0;        // BoardId; 0 means not recorded
};

// Why the device is running at all. Distinguishing a clean boot from a panic
// from a wake is the first question of every field investigation.
enum class ResetReason : std::uint8_t {
    Unknown,
    PowerOn,
    Software,
    Panic,
    Watchdog,
    Brownout,
    DeepSleepWake,
    ExternalPin,
};

struct BatteryStatus {
    std::optional<std::uint16_t> raw_millivolts;       // straight from the ADC
    std::optional<std::uint16_t> filtered_millivolts;  // after smoothing
    std::optional<std::uint8_t>  percent;
    bool charging = false;

    // Sampling during a transmit reads the sag, not the battery. MeshCore's
    // #2627 is a Heltec V4 below ~50% shutting itself down mid-packet for
    // exactly this reason. The flag is here so a reader can discard a sample
    // rather than wonder about it.
    bool sampled_during_tx = false;
};

struct RadioStatus {
    bool                          present = false;
    std::optional<std::uint32_t>  frequency_hz;
    std::optional<std::int8_t>    tx_power_dbm;
    std::optional<std::int16_t>   last_rssi_dbm;
    std::optional<std::int8_t>    last_snr_db;
    std::optional<std::int16_t>   noise_floor_dbm;
    std::uint32_t                 packets_sent     = 0;
    std::uint32_t                 packets_received = 0;
    std::uint32_t                 crc_errors       = 0;
    std::uint32_t                 tx_deferred_lbt  = 0;  // listen-before-talk backoffs
    std::uint32_t                 radio_errors     = 0;

    // Whether this board has a switchable front end at all — a *board*
    // capability, never inferred from the transceiver part number. The Heltec
    // V4 detects a GC1109 or a KCT8103L at boot from a shared GPIO's pull level
    // and only one of them can gate its LNA (meshcore-1.17-review §4).
    bool                       front_end_switchable = false;
    std::optional<bool>        front_end_lna_enabled;
};

struct TransportStatus {
    TransportKind    kind             = TransportKind::Unknown;
    TransportPhase   phase            = TransportPhase::Absent;
    DisconnectReason last_disconnect  = DisconnectReason::None;
    std::uint32_t    sessions         = 0;
    std::uint32_t    frames_sent      = 0;
    std::uint32_t    frames_received  = 0;
    std::uint32_t    frames_dropped   = 0;  // queue full — counted, never silent
    std::uint32_t    frames_malformed = 0;
    std::uint32_t    resyncs          = 0;
};

struct MemoryStatus {
    std::optional<std::uint32_t> heap_free_bytes;
    std::optional<std::uint32_t> heap_largest_block_bytes;  // the number that matters
    std::optional<std::uint32_t> heap_minimum_ever_bytes;
    std::optional<std::uint32_t> psram_free_bytes;
    std::optional<std::uint32_t> psram_largest_block_bytes;
};

struct GnssStatus {
    bool                          present = false;
    GnssState                     state   = GnssState::Off;
    StartKind                     last_start = StartKind::Cold;
    PositionValidity              validity   = PositionValidity::NoFix;

    // Empty means no verdict has been reached, and that is the default.
    //
    // `TrustState` is the *output* of an evaluation: `Untrusted`, `Degraded`
    // and `Trusted` are three conclusions somebody drew after weighing evidence
    // (ADR-0011 §5). None of them can say "the evaluator has not run" — so
    // while this defaulted to `TrustState::Trusted`, a snapshot taken at boot,
    // in a panic handler, or on a board with no receiver at all made the most
    // reassuring claim in the set about a position that does not exist. That is
    // rule 1 at the top of this header broken by an enum rather than by a zero;
    // `validity` on the line above defaults to `NoFix` for exactly this reason.
    //
    // Not `Untrusted` instead, safe though that would have been: `Untrusted`
    // says a verdict *was* reached and it was bad, and anything counting
    // integrity alarms across a fleet of support bundles would believe it.
    // The fact is that there is no verdict, and `std::optional` is how the rest
    // of this header states one.
    //
    // Not a fourth `TrustState` either. That enum is ordered — thresholds,
    // recovery and the transition log in trust.cpp all compare its values — and
    // a member with no place in that order would need one invented at every
    // comparison site.
    std::optional<TrustState>     trust;

    // The `TrustReason` bitmask behind that verdict, and zero while there is
    // none: reasons are what an evaluation produced, so evidence without an
    // evaluation is nobody's conclusion. Kept beside the verdict rather than
    // collapsed into it, because "jamming *and* a jump while stationary" is a
    // different situation from either alone (ADR-0011 §5).
    std::uint32_t                 trust_reasons = 0;

    PositionSource                source     = PositionSource::Unknown;
    std::optional<std::uint8_t>   satellites_used;
    std::optional<std::uint8_t>   satellites_in_view;
    std::optional<std::uint8_t>   cn0_max_dbhz;
    std::optional<std::uint32_t>  horizontal_accuracy_mm;
    std::optional<Millis>         fix_age;
    ReceiverIndication            jamming  = ReceiverIndication::Unknown;
    ReceiverIndication            spoofing = ReceiverIndication::Unknown;

    // The verdict and its reasons are one fact, so they move together. A
    // producer that assigns the two fields separately can leave them out of
    // step in both directions — a mask with no verdict is evidence nobody
    // weighed, and a verdict with an empty mask is the collapsed answer
    // ADR-0011 §5 exists to refuse — and neither is visible at the call site.
    void record_trust(TrustState verdict, std::uint32_t reasons)
    {
        trust         = verdict;
        trust_reasons = reasons;
    }

    // And the way back to "nothing has been evaluated". This is the state a
    // provider walking away leaves behind: a verdict about a receiver that is
    // no longer attached is about nothing, and no state survives implicitly
    // (ADR-0004 §3).
    void forget_trust()
    {
        trust.reset();
        trust_reasons = 0;
    }
};

struct DiagnosticsSnapshot {
    BuildIdentity   build{};
    MonotonicTime   uptime{};
    ResetReason     reset_reason = ResetReason::Unknown;
    std::optional<WakeRecord> last_wake;

    PowerState      power_state = PowerState::Active;
    PowerMetrics    power{};       // every field Unknown until HIL says otherwise
    BatteryStatus   battery{};

    RadioStatus     radio{};
    GnssStatus      gnss{};
    MemoryStatus    memory{};

    // One per interface. Four because that is what the multi-interface link
    // supports (link/), and the array is fixed so a snapshot never allocates.
    static constexpr std::uint8_t kMaxTransports = 4;
    TransportStatus transports[kMaxTransports] = {};
    std::uint8_t    transport_count = 0;

    // Whether the device came up after something went wrong, and whether it is
    // running from a fallback copy of its configuration. Crash-safe persistence
    // is a rule (T-046); this is how a user's report says which branch was taken.
    bool recovered_from_crash    = false;
    bool config_loaded_from_backup = false;
};

const char* to_string(ResetReason reason);

}  // namespace attadipa::core
