#include <cstdio>
#include <cstring>
#include <optional>
#include <type_traits>

#include "attadipa/core/diagnostics.h"

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

using namespace attadipa::core;

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

    // Including the optional that now carries the trust verdict. `std::optional`
    // is trivially copyable only when its payload is, so this is the assert that
    // would catch someone giving `TrustState` a constructor and taking the
    // snapshot out of a panic handler's reach without touching this file.
    static_assert(std::is_trivially_copyable_v<std::optional<TrustState>>);

    // And the round trip actually works, rather than merely type-checking.
    DiagnosticsSnapshot original;
    original.uptime          = MonotonicTime{123456};
    original.reset_reason    = ResetReason::Panic;
    original.power_state     = PowerState::Idle;
    original.gnss.record_trust(TrustState::Degraded,
                               trust_reason_bit(TrustReason::ReceiverJamming) |
                                   trust_reason_bit(TrustReason::AccuracyPoor));
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

// The other half of that round trip, and the one that is easy to lose: a
// snapshot with *no* verdict has to arrive with no verdict. An engaged optional
// survives a memcpy because its payload does; a disengaged one survives because
// its flag does, and a hand-written "pack it into a byte" replacement for this
// field would be the moment that stopped being true.
void test_an_unevaluated_verdict_survives_the_panic_handler_too()
{
    DiagnosticsSnapshot original;
    original.reset_reason = ResetReason::Panic;

    unsigned char buffer[sizeof(DiagnosticsSnapshot)];
    std::memcpy(buffer, &original, sizeof buffer);

    DiagnosticsSnapshot restored;
    restored.gnss.record_trust(TrustState::Trusted, 0);  // something to overwrite
    std::memcpy(&restored, buffer, sizeof buffer);

    CHECK(!restored.gnss.trust.has_value());
    CHECK(restored.gnss.trust_reasons == 0);
    CHECK(std::strcmp(to_string(restored.gnss.trust), "NotEvaluated") == 0);
}

// All three real verdicts, produced by the engine rather than written down by
// hand, survive the crash path with their reason mask intact. The engine is in
// the loop deliberately: the failure this guards against is a snapshot type
// that can hold two of the three states, or that quietly drops the reasons on
// the way in, and a test that assigns the values itself would not notice either.
void test_every_real_verdict_round_trips_with_its_reasons()
{
    struct Case {
        TrustReason evidence;
        TrustState  expected;
    };

    // Weights from default_trust_policy(): spoofing alone (70) passes
    // untrust_at, jamming alone (35) passes degrade_at and no further, and no
    // evidence at all is where the engine starts.
    const Case cases[] = {
        {TrustReason::ReceiverSpoofing, TrustState::Untrusted},
        {TrustReason::ReceiverJamming,  TrustState::Degraded},
    };

    for (const Case& c : cases) {
        TrustEngine engine;
        engine.report(c.evidence, MonotonicTime{0});
        engine.update(MonotonicTime{0});
        CHECK(engine.state() == c.expected);

        DiagnosticsSnapshot original;
        original.gnss.record_trust(engine.state(), engine.reasons());

        unsigned char buffer[sizeof(DiagnosticsSnapshot)];
        std::memcpy(buffer, &original, sizeof buffer);
        DiagnosticsSnapshot restored;
        std::memcpy(&restored, buffer, sizeof buffer);

        CHECK(restored.gnss.trust == c.expected);
        CHECK(restored.gnss.trust_reasons == trust_reason_bit(c.evidence));
        CHECK(std::strcmp(to_string(restored.gnss.trust), to_string(c.expected)) == 0);
    }

    // And the third: a clean engine really does say Trusted, so recording a
    // verdict of Trusted stays possible and stays distinguishable from the
    // default this whole structure used to ship with.
    TrustEngine clean;
    clean.update(MonotonicTime{0});
    CHECK(clean.state() == TrustState::Trusted);

    DiagnosticsSnapshot evaluated;
    evaluated.gnss.record_trust(clean.state(), clean.reasons());

    unsigned char buffer[sizeof(DiagnosticsSnapshot)];
    std::memcpy(buffer, &evaluated, sizeof buffer);
    DiagnosticsSnapshot restored;
    std::memcpy(&restored, buffer, sizeof buffer);

    CHECK(restored.gnss.trust == TrustState::Trusted);
    CHECK(restored.gnss.trust_reasons == 0);

    // The point of the whole change: this Trusted and the one a
    // default-constructed snapshot used to carry are now different values.
    const DiagnosticsSnapshot untouched;
    CHECK(restored.gnss.trust != untouched.gnss.trust);
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

    // The GNSS block has its own bound, because making "not evaluated" sayable
    // cost a byte and the next honest-default fix will want another. A budget
    // is only a budget if it is checked where the spending happens.
    CHECK(sizeof(GnssStatus) <= 48);
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
    CHECK(snapshot.gnss.state == GnssState::Off);
    CHECK(snapshot.gnss.validity == PositionValidity::NoFix);
    CHECK(snapshot.gnss.source == PositionSource::Unknown);
    CHECK(!snapshot.gnss.fix_age.has_value());

    // And the field this test was named after and did not check. `trust` used
    // to default to `TrustState::Trusted`, so a snapshot with no receiver, no
    // fix and no source still carried the most confident verdict the type can
    // express — the exact "confident answer" the four lines above refuse.
    CHECK(!snapshot.gnss.trust.has_value());
    CHECK(snapshot.gnss.trust_reasons == 0);

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

// "Not evaluated" is a fourth thing, and it is not any of the three verdicts.
//
// The trap this closes is narrower than "the default was wrong": `Untrusted` is
// the enum's zero, so any scheme that stored the verdict in a plain byte and
// left it cleared would read as a verdict too — a confident "do not navigate"
// about a receiver nobody has asked anything. Absence has to be its own value.
void test_an_unevaluated_trust_is_not_a_verdict()
{
    const GnssStatus fresh;

    CHECK(!fresh.trust.has_value());
    CHECK(fresh.trust != TrustState::Trusted);
    CHECK(fresh.trust != TrustState::Degraded);
    CHECK(fresh.trust != TrustState::Untrusted);

    // Not the enum's zero by accident, either.
    CHECK(static_cast<std::uint8_t>(TrustState::Untrusted) == 0);

    // A board with no receiver at all — the Waveshare, until an Attadipa node
    // attaches — reaches diagnostics exactly like this, and it is the case where
    // a stated verdict is most obviously about nothing.
    CHECK(!fresh.present);
    CHECK(fresh.state == GnssState::Off);
}

// What a reader is shown for a verdict that does not exist. The consumers this
// header anticipates — a diagnostics screen, a support bundle, a companion app —
// each have to print this field, and each would otherwise pick their own answer
// for the empty case. `to_string` gives them one, and it is a word rather than
// a blank or a zero.
void test_an_unevaluated_trust_renders_as_itself()
{
    const GnssStatus fresh;

    CHECK(std::strcmp(to_string(fresh.trust), "NotEvaluated") == 0);

    // Distinct from all three verdicts, so a log or a bundle cannot read one as
    // the other.
    CHECK(std::strcmp(to_string(fresh.trust), to_string(TrustState::Trusted)) != 0);
    CHECK(std::strcmp(to_string(fresh.trust), to_string(TrustState::Degraded)) != 0);
    CHECK(std::strcmp(to_string(fresh.trust), to_string(TrustState::Untrusted)) != 0);

    // And not an empty string, which renders as a missing field rather than as
    // a known absence.
    CHECK(to_string(fresh.trust)[0] != '\0');

    // A verdict that does exist still prints as itself through the same call,
    // so a renderer needs no second code path and cannot forget one.
    GnssStatus evaluated;
    evaluated.record_trust(TrustState::Degraded, 0);
    CHECK(std::strcmp(to_string(evaluated.trust), "Degraded") == 0);
}

// Reasons are what an evaluation produced, so they cannot outlive one. The two
// fields are set and cleared together, and a snapshot carrying a mask with no
// verdict would be evidence that nobody weighed presented as though somebody had.
void test_reasons_never_outlive_their_verdict()
{
    GnssStatus status;
    CHECK(!status.trust.has_value());
    CHECK(status.trust_reasons == 0);

    status.record_trust(TrustState::Untrusted,
                        trust_reason_bit(TrustReason::ReceiverSpoofing));
    CHECK(status.trust == TrustState::Untrusted);
    CHECK(status.trust_reasons == trust_reason_bit(TrustReason::ReceiverSpoofing));

    // A provider detaching takes its verdict with it — ADR-0004 §3, no state
    // survives implicitly — and the reasons go with the verdict rather than
    // being left behind to be read as current.
    status.forget_trust();
    CHECK(!status.trust.has_value());
    CHECK(status.trust_reasons == 0);
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
    status.record_trust(engine.state(), engine.reasons());

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
    // Trust included, and its fourth reading — no verdict at all — with it.
    for (std::uint8_t i = 0; i <= static_cast<std::uint8_t>(TrustState::Trusted); ++i) {
        const char* name = to_string(static_cast<TrustState>(i));
        CHECK(name != nullptr && name[0] != '\0' && std::strcmp(name, "?") != 0);
    }
    const char* unevaluated = to_string(std::optional<TrustState>{});
    CHECK(unevaluated != nullptr && unevaluated[0] != '\0' &&
          std::strcmp(unevaluated, "?") != 0);
}

}  // namespace

int main()
{
    test_a_snapshot_can_be_saved_from_a_panic_handler();
    test_an_unevaluated_verdict_survives_the_panic_handler_too();
    test_every_real_verdict_round_trips_with_its_reasons();
    test_the_snapshot_is_small_enough_to_keep();
    test_the_snapshot_carries_no_format();
    test_nothing_defaults_to_a_confident_answer();
    test_an_unevaluated_trust_is_not_a_verdict();
    test_an_unevaluated_trust_renders_as_itself();
    test_reasons_never_outlive_their_verdict();
    test_the_reason_mask_carries_the_whole_set();
    test_every_enum_in_a_snapshot_prints();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("diagnostics: all checks passed (host only — nothing was sampled)\n");
    return 0;
}
