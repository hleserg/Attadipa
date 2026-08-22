#include <cstddef>
#include <cstdio>

#include "attadipa/core/gnss_power.h"
#include "attadipa/core/motion.h"
#include "attadipa/core/power_state.h"

// Host tests for the device power states and the GNSS receiver's own.
//
// Both files export a `transition_is_legal`, and a predicate like that is
// unusually easy to test badly: a suite that only walks the legal paths passes
// against an implementation that returns true for everything. So both tables
// below are exhaustive over every (from, to) pair, and the illegal ones are the
// half that carries the information.
//
// Nothing here measures anything. Every number in PowerMetrics is `Unknown`
// until an instrument has been attached to a board, and one of the tests exists
// specifically to make sure that stays true by construction rather than by
// discipline.

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

// ---------------------------------------------------------------------------
// Device power states.

// The whole table, written out. Row is `from`, column is `to`, in the order
// Active, Idle, LightSleep, MeshListenSleep, DeepSleep, PowerOff.
//
// Reading it top to bottom is the design: Active only relaxes into Idle; every
// sleep is chosen from Idle, so exactly one place decides which; a light sleep
// wakes into Idle rather than Active, because whether a wake deserves the
// screen is not this file's decision; deep sleep does not resume at all — the
// chip reboots, and modelling it as a return to Idle would hide that every
// RAM-resident thing is gone. Off is reachable from anywhere, because a held
// button and a flat battery are not requests.
constexpr bool kLegal[kPowerStateCount][kPowerStateCount] = {
    /* from Active          */ {true,  true,  false, false, false, true},
    /* from Idle            */ {true,  true,  true,  true,  true,  true},
    /* from LightSleep      */ {false, true,  true,  false, false, true},
    /* from MeshListenSleep */ {false, true,  false, true,  false, true},
    /* from DeepSleep       */ {true,  false, false, false, true,  true},
    /* from PowerOff        */ {true,  false, false, false, false, true},
};

void test_every_power_transition()
{
    for (std::uint8_t f = 0; f < kPowerStateCount; ++f) {
        for (std::uint8_t t = 0; t < kPowerStateCount; ++t) {
            const PowerState from = static_cast<PowerState>(f);
            const PowerState to   = static_cast<PowerState>(t);
            const bool       want = kLegal[f][t];
            if (transition_is_legal(from, to) != want) {
                std::fprintf(stderr, "FAIL line %d: %s -> %s is %s, expected %s\n", __LINE__,
                             to_string(from), to_string(to),
                             transition_is_legal(from, to) ? "legal" : "illegal",
                             want ? "legal" : "illegal");
                ++failures;
            }
        }
    }

    // A sanity check on the table itself: it must actually refuse things. A
    // table of all-true would make the loop above pass against an
    // implementation that never refuses anything, which is the failure this
    // whole file is built to catch.
    int refused = 0;
    for (std::uint8_t f = 0; f < kPowerStateCount; ++f) {
        for (std::uint8_t t = 0; t < kPowerStateCount; ++t) {
            if (!kLegal[f][t]) {
                ++refused;
            }
        }
    }
    CHECK(refused >= 12);
}

// The rule that matters most here, stated on its own so a future edit to the
// table cannot quietly lose it. ADR-0011 and the MeshCore review: upstream's
// V4-R8 "hibernate" left the front end in receive and armed EXT1 on the radio's
// DIO1, which is a deep sleep that is neither deep nor asleep. A wake source
// that only exists while the radio is powered has no business being armed in a
// state where the radio is not.
void test_deep_sleep_cannot_be_woken_by_a_radio_that_is_off()
{
    const std::uint16_t deep = legal_wake_sources(PowerState::DeepSleep);
    CHECK((deep & wake_bit(WakeSource::RadioIrq)) == 0);
    CHECK((deep & wake_bit(WakeSource::NodeLink)) == 0);
    CHECK((deep & wake_bit(WakeSource::Touch)) == 0);
    CHECK((deep & wake_bit(WakeSource::Usb)) == 0);
    CHECK((deep & wake_bit(WakeSource::Timer)) != 0);
    CHECK((deep & wake_bit(WakeSource::Button)) != 0);

    CHECK(!wake_plan_is_legal(PowerState::DeepSleep, wake_bit(WakeSource::RadioIrq)));
    CHECK(wake_plan_is_legal(PowerState::DeepSleep,
                             wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button)));

    // And the state that exists precisely so that "asleep but listening" does
    // not have to be spelled `DeepSleep` must allow the radio.
    const std::uint16_t mesh = legal_wake_sources(PowerState::MeshListenSleep);
    CHECK((mesh & wake_bit(WakeSource::RadioIrq)) != 0);
    CHECK((mesh & wake_bit(WakeSource::NodeLink)) != 0);
    // The screen is off and the host is not the reason we are asleep.
    CHECK((mesh & wake_bit(WakeSource::Touch)) == 0);
    CHECK((mesh & wake_bit(WakeSource::Usb)) == 0);

    // Powered off, nothing that arrives over the air may bring it back. A
    // device that a stranger's packet can switch on is not switched off.
    const std::uint16_t off = legal_wake_sources(PowerState::PowerOff);
    CHECK((off & wake_bit(WakeSource::RadioIrq)) == 0);
    CHECK((off & wake_bit(WakeSource::NodeLink)) == 0);
    CHECK((off & wake_bit(WakeSource::Timer)) == 0);
    CHECK((off & wake_bit(WakeSource::Button)) != 0);
    CHECK((off & wake_bit(WakeSource::Pmu)) != 0);   // a charger, or a flat battery
}

// Arming several sources is the normal case, and one illegal source in the set
// must condemn the whole plan rather than being quietly dropped — a plan that
// is silently narrowed is a device that sleeps differently from how the caller
// thinks it does.
void test_one_illegal_source_condemns_the_plan()
{
    const std::uint16_t mostly_fine = wake_bit(WakeSource::Timer) |
                                      wake_bit(WakeSource::Button) |
                                      wake_bit(WakeSource::RadioIrq);
    CHECK(!wake_plan_is_legal(PowerState::DeepSleep, mostly_fine));
    CHECK(wake_plan_is_legal(PowerState::MeshListenSleep, mostly_fine));

    // Arming nothing is legal and is not the same as arming everything. A sleep
    // with no wake source is a device that has to be reset, and that is a
    // decision for the caller to make deliberately — not one this predicate
    // should refuse on its behalf.
    CHECK(wake_plan_is_legal(PowerState::DeepSleep, 0));
}

// CLAUDE.md's rule with a compiler behind it: an estimate must never be able to
// read as a measurement. The default is `Unknown` — not zero, not "probably
// fine" — and a metric nobody has established reports itself unusable.
void test_a_number_nobody_measured_says_so()
{
    PowerMetrics metrics;
    CHECK(metrics.average_current_ua.provenance == Provenance::Unknown);
    CHECK(metrics.sleep_current_ua.provenance == Provenance::Unknown);
    CHECK(metrics.wake_latency_us.provenance == Provenance::Unknown);
    CHECK(metrics.energy_per_gnss_fix_uj.provenance == Provenance::Unknown);
    CHECK(metrics.energy_per_lora_tx_uj.provenance == Provenance::Unknown);
    CHECK(metrics.energy_per_lora_rx_uj.provenance == Provenance::Unknown);

    CHECK(!metrics.sleep_current_ua.usable());

    // A datasheet figure is usable and is still not a measurement.
    metrics.sleep_current_ua = PowerMetric{10, Provenance::Estimated};
    CHECK(metrics.sleep_current_ua.usable());
    CHECK(metrics.sleep_current_ua.provenance != Provenance::Measured);

    // Zero with no provenance is the trap this type exists to close: it is not
    // "no current", it is "nobody looked".
    const PowerMetric never_taken;
    CHECK(never_taken.value == 0);
    CHECK(!never_taken.usable());
}

void test_every_power_name_is_readable()
{
    for (std::uint8_t i = 0; i < kPowerStateCount; ++i) {
        const char* name = to_string(static_cast<PowerState>(i));
        CHECK(name != nullptr && name[0] != '\0');
    }
    for (std::uint8_t i = 0; i < kWakeSourceCount; ++i) {
        const char* name = to_string(static_cast<WakeSource>(i));
        CHECK(name != nullptr && name[0] != '\0');
    }
    CHECK(to_string(Provenance::Measured)[0] != '\0');
}

// ---------------------------------------------------------------------------
// The GNSS receiver's own states.

// Two capability sets, because the interesting transitions are the ones that
// exist on one receiver and not on the other. Backup and power-save are both
// hardware features, and a state machine that assumed them would drive a
// receiver into a state it does not have — which on a GNSS module means it
// silently never starts, exactly the failure CLAUDE.md warns about.
constexpr GnssCapabilities kPlain{false, false, false, false};
constexpr GnssCapabilities kFull{true, true, true, true};

void check_gnss(GnssState from, GnssState to, const GnssCapabilities& caps, bool want,
                const char* which, int line)
{
    const bool got = transition_is_legal(from, to, caps);
    if (got != want) {
        std::fprintf(stderr, "FAIL line %d: %s -> %s on a %s receiver is %s, expected %s\n", line,
                     to_string(from), to_string(to), which, got ? "legal" : "illegal",
                     want ? "legal" : "illegal");
        ++failures;
    }
}

#define CHECK_GNSS(from, to, caps, want, which) \
    check_gnss((from), (to), (caps), (want), (which), __LINE__)

// Exhaustive again, and against the receiver that has nothing: every state a
// capability gates must be unreachable, from everywhere, with no exceptions
// hiding in a corner of the table.
void test_a_state_the_receiver_does_not_have_is_unreachable()
{
    for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
        const GnssState from = static_cast<GnssState>(f);
        if (from != GnssState::Backup) {
            CHECK_GNSS(from, GnssState::Backup, kPlain, false, "plain");
        }
        if (from != GnssState::PowerSave) {
            CHECK_GNSS(from, GnssState::PowerSave, kPlain, false, "plain");
        }
    }

    // And on a receiver that has them, at least one route in exists — otherwise
    // the capability is declared and unusable, which is worse than absent
    // because it reads as supported.
    bool backup_reachable = false;
    bool save_reachable   = false;
    for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
        const GnssState from = static_cast<GnssState>(f);
        if (from != GnssState::Backup && transition_is_legal(from, GnssState::Backup, kFull)) {
            backup_reachable = true;
        }
        if (from != GnssState::PowerSave && transition_is_legal(from, GnssState::PowerSave, kFull)) {
            save_reachable = true;
        }
    }
    CHECK(backup_reachable);
    CHECK(save_reachable);
}

// Off is always reachable, on any receiver. Cutting the rail is not a request.
void test_a_receiver_can_always_be_switched_off()
{
    for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
        CHECK_GNSS(static_cast<GnssState>(f), GnssState::Off, kPlain, true, "plain");
        CHECK_GNSS(static_cast<GnssState>(f), GnssState::Off, kFull, true, "full");
    }
}

// A cold start is minutes and a hot start is seconds, and the difference is
// what makes a duty cycle worth having. Getting this backwards means a receiver
// that is switched off between fixes and then takes a cold start every time —
// which costs more energy than never sleeping at all.
void test_the_start_kind_follows_what_was_retained()
{
    GnssContext context;
    context.capabilities = kFull;

    // Nothing retained: from scratch.
    context.ephemeris_retained = false;
    context.backup_retained    = false;
    context.since_last_fix     = Millis{0};
    CHECK(start_kind(context) == StartKind::Cold);

    // Ephemeris still valid: the fastest a receiver can be.
    context.ephemeris_retained = true;
    context.backup_retained    = true;
    context.since_last_fix     = Millis{60000};
    CHECK(start_kind(context) == StartKind::Hot);

    // Ephemeris kept but old. Broadcast ephemeris is good for about four hours
    // and the almanac for weeks, so this is warm rather than hot — and it is
    // not cold, which is the mistake that makes duty cycling pointless.
    context.ephemeris_retained = true;
    context.since_last_fix     = Millis{6u * 60u * 60u * 1000u};
    CHECK(start_kind(context) == StartKind::Warm);

    // The one this test was written to catch. Having a backup domain is not
    // using one: a receiver switched off at the rail retained nothing, and
    // reporting a warm start there promises a fix in thirty seconds that
    // arrives in several minutes.
    context.ephemeris_retained = false;
    context.backup_retained    = false;
    CHECK(context.capabilities.backup_domain);
    CHECK(start_kind(context) == StartKind::Cold);

    // A receiver with no backup domain at all cannot be warm however the flags
    // are set, because there was nowhere for the almanac to survive.
    GnssContext plain;
    plain.capabilities     = kPlain;
    plain.backup_retained  = true;
    CHECK(start_kind(plain) == StartKind::Cold);
}

// §8 of the brief, and it is a prohibition rather than a feature: assistance
// data must never be a dependency. A watch in a forest with no network has to
// get a fix, slowly, on its own.
void test_assistance_is_never_required()
{
    GnssContext context;
    context.capabilities         = kFull;
    context.assistance_available = false;
    context.fresh_fix_requested  = true;

    // With no assistance at all, the receiver still goes and looks.
    const GnssState next = next_state(GnssState::Off, context);
    CHECK(next != GnssState::Off);
    CHECK(transition_is_legal(GnssState::Off, next, context.capabilities));

    // And a receiver with no assistance capability whatsoever behaves the same.
    GnssContext plain;
    plain.capabilities        = kPlain;
    plain.fresh_fix_requested = true;
    const GnssState plain_next = next_state(GnssState::Off, plain);
    CHECK(plain_next != GnssState::Off);
    CHECK(transition_is_legal(GnssState::Off, plain_next, plain.capabilities));
}

// Whatever the receiver would like to do, the device's own state outranks it.
// A GNSS module drawing 25 mA through a deep sleep is a flat battery by
// morning, and the decision cannot live in the receiver driver because the
// driver does not know what the rest of the device is doing.
void test_the_device_state_outranks_the_receiver()
{
    GnssContext context;
    context.capabilities        = kFull;
    context.fresh_fix_requested = true;
    context.device_power        = PowerState::DeepSleep;

    const GnssState next = next_state(GnssState::Tracking, context);
    CHECK(next == GnssState::Off || next == GnssState::Backup);
    CHECK(transition_is_legal(GnssState::Tracking, next, context.capabilities));

    // On a receiver with no backup domain there is only one answer, and it is
    // the expensive one — which is a fact about the part, not a bug.
    GnssContext plain = context;
    plain.capabilities = kPlain;
    CHECK(next_state(GnssState::Tracking, plain) == GnssState::Off);
}

// The gate OD-10 asks for, and the shape ADR-0013 gives it. A watch on a
// bedside table is the case that pays for the whole feature, so it is checked
// first — a body-labelled gate that never fires would satisfy every test below
// this one and save nothing.
void test_a_body_at_rest_sleeps_its_own_receiver()
{
    GnssContext context;
    context.capabilities  = kFull;
    context.receiver_body = SensorBody::Watch;
    context.motion        = MotionEvidence{SensorBody::Watch, true, false};

    CHECK(next_state(GnssState::Tracking, context) == GnssState::PowerSave);

    // And it comes back when that same body moves again.
    context.motion = MotionEvidence{SensorBody::Watch, true, true};
    CHECK(next_state(GnssState::PowerSave, context) == GnssState::Acquiring);
    CHECK(next_state(GnssState::Backup, context) == GnssState::Acquiring);
}

// ADR-0013 §2. `device_moving` was a plain `bool` whose default `false` read as
// "at rest", so a receiver could be powered down on a sample nobody had taken —
// and then, with nothing moving to wake it, stay down.
//
// Not known is not still. It moves the receiver in neither direction, which is
// the one reading that cannot be wrong: the guarantee that a fix eventually
// happens anyway is OD-10's ceiling, and the ceiling is not in this function.
void test_unknown_motion_moves_the_receiver_in_neither_direction()
{
    GnssContext context;
    context.capabilities  = kFull;
    context.receiver_body = SensorBody::Watch;
    context.motion        = MotionEvidence{};  // nobody sampled anything

    CHECK(next_state(GnssState::Tracking, context) == GnssState::Tracking);
    CHECK(next_state(GnssState::PowerSave, context) == GnssState::PowerSave);
    CHECK(next_state(GnssState::Backup, context) == GnssState::Backup);

    // A receiver on a body nobody named is in the same position, even with a
    // perfectly good sample in hand: the sample is about something, and this
    // context has not said what this receiver is.
    GnssContext nameless;
    nameless.capabilities  = kFull;
    nameless.receiver_body = SensorBody::Unknown;
    nameless.motion        = MotionEvidence{SensorBody::Watch, true, false};
    CHECK(next_state(GnssState::Tracking, nameless) == GnssState::Tracking);
}

// ADR-0013 §3, and the reason the field carries a body at all. The wearer is at
// a desk; the node is in a bag going down the corridor. Sleeping the node's
// receiver because this wrist is resting is a decision about somebody else's
// hardware taken on evidence that was never about it.
void test_a_wrist_at_rest_does_not_sleep_a_nodes_receiver()
{
    GnssContext node;
    node.capabilities  = kFull;
    node.receiver_body = SensorBody::Node;
    node.motion        = MotionEvidence{SensorBody::Watch, true, false};

    CHECK(next_state(GnssState::Tracking, node) == GnssState::Tracking);

    // The node's own IMU is the only thing that may sleep it...
    node.motion = MotionEvidence{SensorBody::Node, true, false};
    CHECK(next_state(GnssState::Tracking, node) == GnssState::PowerSave);

    // ...and the wrist walking is not what wakes it up again either.
    node.motion = MotionEvidence{SensorBody::Watch, true, true};
    CHECK(next_state(GnssState::PowerSave, node) == GnssState::PowerSave);

    node.motion = MotionEvidence{SensorBody::Node, true, true};
    CHECK(next_state(GnssState::PowerSave, node) == GnssState::Acquiring);
}

// The mirror of the above, kept separate because it is the direction that
// fails quietly: the watch's own receiver is not gated by the node's chassis.
void test_a_still_node_does_not_sleep_the_watchs_receiver()
{
    GnssContext watch;
    watch.capabilities  = kFull;
    watch.receiver_body = SensorBody::Watch;
    watch.motion        = MotionEvidence{SensorBody::Node, true, false};

    CHECK(next_state(GnssState::Tracking, watch) == GnssState::Tracking);
}

// An application waiting for a position outranks the gate, whichever body is
// still. Checked because §2's "unknown moves nothing" must not have quietly
// become "nothing moves it".
void test_a_waiting_application_outranks_a_still_body()
{
    GnssContext context;
    context.capabilities        = kFull;
    context.receiver_body       = SensorBody::Watch;
    context.motion              = MotionEvidence{SensorBody::Watch, true, false};
    context.fresh_fix_requested = true;

    CHECK(next_state(GnssState::Tracking, context) == GnssState::Tracking);
    CHECK(next_state(GnssState::PowerSave, context) == GnssState::Tracking);
    CHECK(next_state(GnssState::Backup, context) == GnssState::Acquiring);
}

// A sample that knows something must know whose. Nothing in this file may
// construct the incoherent combination, and `speaks_for` refuses it anyway.
void test_evidence_that_knows_something_knows_whose()
{
    const MotionEvidence nobody_asked{};
    const MotionEvidence knows_without_a_subject{SensorBody::Unknown, true, false};
    const MotionEvidence proper{SensorBody::Watch, true, false};

    CHECK(nobody_asked.is_coherent());
    CHECK(!knows_without_a_subject.is_coherent());
    CHECK(proper.is_coherent());

    for (std::uint8_t b = 0; b < kSensorBodyCount; ++b) {
        const SensorBody about = static_cast<SensorBody>(b);
        CHECK(!nobody_asked.speaks_for(about));
        CHECK(!knows_without_a_subject.speaks_for(about));
    }
    CHECK(proper.speaks_for(SensorBody::Watch));
    CHECK(!proper.speaks_for(SensorBody::Node));
    CHECK(!proper.speaks_for(SensorBody::Unknown));

    // The two directions are not each other's negation — "not at rest" is not
    // "in motion", because "not known" is neither.
    CHECK(!nobody_asked.says_at_rest(SensorBody::Watch));
    CHECK(!nobody_asked.says_in_motion(SensorBody::Watch));
}

// A receiver that is already off has nothing for a backup domain to keep, so
// the device going to sleep must leave it off rather than powering a domain to
// retain nothing. Found by the exhaustive cross-check below; kept here on its
// own so a future edit cannot lose it in the noise of a loop.
void test_an_off_receiver_does_not_enter_backup()
{
    GnssContext context;
    context.capabilities = kFull;
    context.device_power = PowerState::DeepSleep;

    CHECK(next_state(GnssState::Off, context) == GnssState::Off);
    CHECK(next_state(GnssState::Tracking, context) == GnssState::Backup);
    CHECK(next_state(GnssState::Backup, context) == GnssState::Backup);

    context.device_power = PowerState::PowerOff;
    CHECK(next_state(GnssState::Off, context) == GnssState::Off);
}

// Whatever next_state proposes, from wherever, it must be a move the receiver
// can actually make. The two functions are written separately and this is the
// only thing keeping them agreeing.
void test_next_state_never_proposes_an_illegal_move()
{
    const GnssCapabilities sets[] = {kPlain, kFull};
    const PowerState powers[]     = {PowerState::Active, PowerState::Idle, PowerState::LightSleep,
                                     PowerState::MeshListenSleep, PowerState::DeepSleep,
                                     PowerState::PowerOff};

    // Every motion sample the type can hold, including the two incoherent ones
    // — a sample that claims to know something about nobody. They are here
    // precisely because nothing should construct them: if one ever reaches
    // next_state it must still not produce an illegal move.
    MotionEvidence samples[1 + 2 * kSensorBodyCount];
    std::size_t    sample_count = 0;
    samples[sample_count++]     = MotionEvidence{};
    for (std::uint8_t b = 0; b < kSensorBodyCount; ++b) {
        const SensorBody body   = static_cast<SensorBody>(b);
        samples[sample_count++] = MotionEvidence{body, true, false};
        samples[sample_count++] = MotionEvidence{body, true, true};
    }

    for (const GnssCapabilities& caps : sets) {
        for (PowerState power : powers) {
            for (std::uint8_t rb = 0; rb < kSensorBodyCount; ++rb) {
                for (std::size_t m = 0; m < sample_count; ++m) {
                    for (int wanted = 0; wanted < 2; ++wanted) {
                        for (int retained = 0; retained < 2; ++retained) {
                            for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
                                GnssContext context;
                                context.capabilities        = caps;
                                context.device_power        = power;
                                context.receiver_body       = static_cast<SensorBody>(rb);
                                context.motion              = samples[m];
                                context.fresh_fix_requested = wanted != 0;
                                context.ephemeris_retained  = retained != 0;
                                context.since_last_fix      = Millis{retained != 0 ? 1000u : 0u};

                                const GnssState from = static_cast<GnssState>(f);
                                const GnssState to   = next_state(from, context);
                                if (!transition_is_legal(from, to, caps)) {
                                    std::fprintf(stderr,
                                                 "FAIL line %d: next_state proposed %s -> %s for a "
                                                 "%s receiver, which transition_is_legal refuses\n",
                                                 __LINE__, to_string(from), to_string(to),
                                                 to_string(context.receiver_body));
                                    ++failures;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// The gate is a decision about one body, so nothing that happens on another one
// may change its answer. Exhaustive over every state, every sample and every
// pair of bodies: whenever the sample is about a body this receiver is not,
// next_state must give the same answer it gives with no sample at all.
void test_evidence_about_another_body_changes_nothing()
{
    const GnssCapabilities sets[] = {kPlain, kFull};

    for (const GnssCapabilities& caps : sets) {
        for (std::uint8_t rb = 0; rb < kSensorBodyCount; ++rb) {
            for (std::uint8_t sb = 0; sb < kSensorBodyCount; ++sb) {
                if (rb == sb) {
                    continue;  // same body: the sample is supposed to matter
                }
                for (int moving = 0; moving < 2; ++moving) {
                    for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
                        GnssContext blind;
                        blind.capabilities  = caps;
                        blind.receiver_body = static_cast<SensorBody>(rb);

                        GnssContext told = blind;
                        told.motion      = MotionEvidence{static_cast<SensorBody>(sb), true,
                                                          moving != 0};

                        const GnssState from = static_cast<GnssState>(f);
                        if (next_state(from, blind) != next_state(from, told)) {
                            std::fprintf(stderr,
                                         "FAIL line %d: a %s sample changed a %s receiver's move "
                                         "from %s\n",
                                         __LINE__, to_string(static_cast<SensorBody>(sb)),
                                         to_string(static_cast<SensorBody>(rb)), to_string(from));
                            ++failures;
                        }
                    }
                }
            }
        }
    }
}

void test_every_gnss_name_is_readable()
{
    for (std::uint8_t i = 0; i < kGnssStateCount; ++i) {
        const char* name = to_string(static_cast<GnssState>(i));
        CHECK(name != nullptr && name[0] != '\0');
    }
    CHECK(to_string(StartKind::Cold)[0] != '\0');
    CHECK(to_string(StartKind::Warm)[0] != '\0');
    CHECK(to_string(StartKind::Hot)[0] != '\0');
}

// A body that cannot be named cannot be shown in Diagnostics, and Diagnostics
// is the only place the node's IMU surfaces at all (ADR-0013 §1).
void test_every_body_name_is_readable()
{
    for (std::uint8_t i = 0; i < kSensorBodyCount; ++i) {
        const char* name = to_string(static_cast<SensorBody>(i));
        CHECK(name != nullptr && name[0] != '\0');
    }
}

}  // namespace

int main()
{
    test_every_power_transition();
    test_deep_sleep_cannot_be_woken_by_a_radio_that_is_off();
    test_one_illegal_source_condemns_the_plan();
    test_a_number_nobody_measured_says_so();
    test_every_power_name_is_readable();

    test_a_state_the_receiver_does_not_have_is_unreachable();
    test_a_receiver_can_always_be_switched_off();
    test_the_start_kind_follows_what_was_retained();
    test_assistance_is_never_required();
    test_the_device_state_outranks_the_receiver();
    test_a_body_at_rest_sleeps_its_own_receiver();
    test_unknown_motion_moves_the_receiver_in_neither_direction();
    test_a_wrist_at_rest_does_not_sleep_a_nodes_receiver();
    test_a_still_node_does_not_sleep_the_watchs_receiver();
    test_a_waiting_application_outranks_a_still_body();
    test_evidence_that_knows_something_knows_whose();
    test_an_off_receiver_does_not_enter_backup();
    test_next_state_never_proposes_an_illegal_move();
    test_evidence_about_another_body_changes_nothing();
    test_every_gnss_name_is_readable();
    test_every_body_name_is_readable();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("power: all checks passed (host only — nothing was measured)\n");
    return 0;
}
