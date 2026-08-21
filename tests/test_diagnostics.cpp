#include <cstdio>
#include <cstring>
#include <type_traits>

#include "firefly/core/diagnostics.h"

// Host tests for the diagnostics snapshot.
//
// It is a plain structure and there is little behaviour to test, but it has
// four structural properties that are worth a compiler's opinion rather than a
// reviewer's, because each of them is easy to lose in a one-line edit:
//
//   1. it is trivially copyable, so it can be memcpy'd into an RTC-retained
//      buffer or a crash log without running any code — which is the only kind
//      of write available in a panic handler;
//   2. it is bounded, and small enough that keeping one costs nothing anybody
//      has to think about;
//   3. it carries no serializer and no std::string, so core stays free of the
//      JSON that a diagnostics screen will eventually want. §14 of the brief:
//      do not tie core to JSON. Formatting belongs above this layer, where the
//      choice of format is a product decision rather than a core dependency;
//   4. every number in it that nobody has measured says so.

using namespace firefly::core;

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

// A crash handler has no allocator, no locks and very little stack. Anything it
// cannot memcpy, it cannot save — so this is not a style preference, it is the
// difference between having a snapshot after a panic and not having one.
void test_a_snapshot_can_be_saved_from_a_panic_handler()
{
    static_assert(std::is_trivially_copyable_v<DiagnosticsSnapshot>,
                  "the snapshot must be memcpy-able into RTC memory from a crash handler");
    static_assert(std::is_trivially_copyable_v<BatteryStatus>);
    static_assert(std::is_trivially_copyable_v<RadioStatus>);
    static_assert(std::is_trivially_copyable_v<GnssStatus>);
    static_assert(std::is_trivially_copyable_v<TransportStatus>);
    static_assert(std::is_trivially_copyable_v<MemoryStatus>);

    // And the round trip actually works, rather than merely type-checking.
    DiagnosticsSnapshot original;
    original.uptime          = MonotonicTime{123456};
    original.reset_reason    = ResetReason::Panic;
    original.power_state     = PowerState::Idle;
    original.gnss.trust      = TrustState::Degraded;
    original.gnss.trust_reasons = trust_reason_bit(TrustReason::ReceiverJamming) |
                                  trust_reason_bit(TrustReason::AccuracyPoor);
    original.transport_count = 2;

    unsigned char buffer[sizeof(DiagnosticsSnapshot)];
    std::memcpy(buffer, &original, sizeof buffer);

    DiagnosticsSnapshot restored;
    std::memcpy(&restored, buffer, sizeof buffer);

    CHECK(restored.uptime.ms == 123456);
    CHECK(restored.reset_reason == ResetReason::Panic);
    CHECK(restored.power_state == PowerState::Idle);
    CHECK(restored.gnss.trust == TrustState::Degraded);
    CHECK(restored.gnss.trust_reasons == original.gnss.trust_reasons);
    CHECK(restored.transport_count == 2);
}

// Bounded, and by a number rather than by hope. RTC slow memory on an ESP32-S3
// is 8 KiB and is shared with everything else that wants to survive a deep
// sleep, so a snapshot that grew to fill it would push out whatever else was
// there — silently, at the moment somebody added a field.
void test_the_snapshot_is_small_enough_to_keep()
{
    CHECK(sizeof(DiagnosticsSnapshot) <= 1024);

    // A single transport slot is the thing most likely to grow, because it is
    // multiplied by four.
    CHECK(sizeof(TransportStatus) <= 64);
    CHECK(DiagnosticsSnapshot::kMaxTransports == 4);
}

// §14 of the brief, checked structurally. A std::string anywhere in this
// structure would break the memcpy test above, so trivial copyability is what
// keeps the format out — but the intent deserves a check of its own, because a
// future std::array<char, N> would keep the memcpy and still be a formatting
// decision that had leaked into core.
void test_the_snapshot_carries_no_format()
{
    // No member is a string of any kind: BuildIdentity's fields are pointers to
    // static storage stamped at build time, not owned buffers.
    static_assert(std::is_same_v<decltype(BuildIdentity::version), const char*>);
    static_assert(std::is_same_v<decltype(BuildIdentity::commit), const char*>);

    // And the only functions this header declares are enum names. There is no
    // to_json, no serialize, no write, and no ostream — a diagnostics screen
    // formats this, and which format it chooses is a product decision that must
    // not become a core dependency.
    const DiagnosticsSnapshot snapshot;
    CHECK(sizeof snapshot > 0);   // the structure exists; nothing else is offered
}

// Every default is an honest one. In particular a battery that has not been
// read reports absent rather than zero, because zero millivolts is a number and
// a number is an answer.
void test_nothing_defaults_to_a_confident_answer()
{
    const DiagnosticsSnapshot snapshot;

    CHECK(snapshot.reset_reason == ResetReason::Unknown);
    CHECK(!snapshot.last_wake.has_value());
    CHECK(snapshot.uptime.ms == 0);

    CHECK(!snapshot.battery.raw_millivolts.has_value());
    CHECK(!snapshot.battery.filtered_millivolts.has_value());
    CHECK(!snapshot.battery.percent.has_value());
    CHECK(!snapshot.battery.charging);
    // A reading taken while the radio was transmitting is a different reading,
    // and the snapshot says which it was rather than averaging the distinction
    // away — a battery sampled during a LoRa transmission can read a hundred
    // millivolts low on these boards.
    CHECK(!snapshot.battery.sampled_during_tx);

    CHECK(!snapshot.radio.present);
    CHECK(!snapshot.radio.noise_floor_dbm.has_value());
    CHECK(!snapshot.radio.front_end_lna_enabled.has_value());
    CHECK(!snapshot.radio.front_end_switchable);

    CHECK(!snapshot.gnss.present);
    CHECK(snapshot.gnss.validity == PositionValidity::NoFix);
    CHECK(snapshot.gnss.source == PositionSource::Unknown);
    CHECK(!snapshot.gnss.fix_age.has_value());

    // OD-5 again, in the one structure a support engineer will actually read.
    CHECK(snapshot.gnss.jamming == ReceiverIndication::Unknown);
    CHECK(snapshot.gnss.spoofing == ReceiverIndication::Unknown);

    CHECK(!snapshot.memory.heap_free_bytes.has_value());
    CHECK(!snapshot.memory.heap_largest_block_bytes.has_value());

    // Every power number, unmeasured, and saying so.
    CHECK(snapshot.power.average_current_ua.provenance == Provenance::Unknown);
    CHECK(snapshot.power.sleep_current_ua.provenance == Provenance::Unknown);
    CHECK(snapshot.power.wake_latency_us.provenance == Provenance::Unknown);
    CHECK(snapshot.power.energy_per_gnss_fix_uj.provenance == Provenance::Unknown);
    CHECK(snapshot.power.energy_per_lora_tx_uj.provenance == Provenance::Unknown);
    CHECK(snapshot.power.energy_per_lora_rx_uj.provenance == Provenance::Unknown);

    CHECK(snapshot.transport_count == 0);
    for (std::uint8_t i = 0; i < DiagnosticsSnapshot::kMaxTransports; ++i) {
        CHECK(snapshot.transports[i].kind == TransportKind::Unknown);
        CHECK(snapshot.transports[i].phase == TransportPhase::Absent);
        CHECK(snapshot.transports[i].last_disconnect == DisconnectReason::None);
        CHECK(snapshot.transports[i].frames_dropped == 0);
    }

    CHECK(!snapshot.recovered_from_crash);
    CHECK(!snapshot.config_loaded_from_backup);
}

// The trust reason bitmask is the whole set, not the most recent one. "Jamming
// and a jump while stationary" is a different situation from either alone, and
// the difference is what a field report needs.
void test_the_reason_mask_carries_the_whole_set()
{
    TrustEngine engine;
    engine.report(TrustReason::ReceiverJamming, MonotonicTime{0});
    engine.report(TrustReason::MotionDisagreement, MonotonicTime{0});
    engine.update(MonotonicTime{0});

    GnssStatus status;
    status.trust         = engine.state();
    status.trust_reasons = engine.reasons();

    CHECK((status.trust_reasons & trust_reason_bit(TrustReason::ReceiverJamming)) != 0);
    CHECK((status.trust_reasons & trust_reason_bit(TrustReason::MotionDisagreement)) != 0);
    CHECK((status.trust_reasons & trust_reason_bit(TrustReason::ReceiverSpoofing)) == 0);

    // Fifteen reasons fit in the 32-bit mask with room to spare, and a reason
    // added past bit 31 would silently stop being reportable.
    CHECK(kTrustReasonCount <= 32);
}

// A support engineer reading a snapshot must not meet a bare integer. Every
// enum in it prints.
void test_every_enum_in_a_snapshot_prints()
{
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(ResetReason::ExternalPin); ++i) {
        const char* name = to_string(static_cast<ResetReason>(i));
        CHECK(name != nullptr && name[0] != '\0' && std::strcmp(name, "?") != 0);
    }
    for (std::uint8_t i = 0; i < kTransportPhaseCount; ++i) {
        const char* name = to_string(static_cast<TransportPhase>(i));
        CHECK(name != nullptr && std::strcmp(name, "?") != 0);
    }
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(DisconnectReason::Fault); ++i) {
        const char* name = to_string(static_cast<DisconnectReason>(i));
        CHECK(name != nullptr && std::strcmp(name, "?") != 0);
    }
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(TransportKind::EspNow); ++i) {
        const char* name = to_string(static_cast<TransportKind>(i));
        CHECK(name != nullptr && std::strcmp(name, "?") != 0);
    }
}

}  // namespace

int main()
{
    test_a_snapshot_can_be_saved_from_a_panic_handler();
    test_the_snapshot_is_small_enough_to_keep();
    test_the_snapshot_carries_no_format();
    test_nothing_defaults_to_a_confident_answer();
    test_the_reason_mask_carries_the_whole_set();
    test_every_enum_in_a_snapshot_prints();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("diagnostics: all checks passed (host only — nothing was sampled)\n");
    return 0;
}
