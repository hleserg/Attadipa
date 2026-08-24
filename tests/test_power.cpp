#include <cstdio>
#include <cstring>
#include <iterator>

#include "attadipa/core/gnss_power.h"
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

// Three capability sets, not two, and the third is the one the project is
// actually in. Backup and power-save are hardware features, and a state machine
// that assumed them would drive a receiver into a state it does not have —
// which on a GNSS module means it silently never starts, exactly the failure
// CLAUDE.md warns about. But "this part does not have a backup domain" and
// "nobody has read the datasheet" are different facts with the same fail-safe
// consequence, and a suite that only tests the two extremes cannot tell whether
// the middle one exists at all.
//
// `kUnknown` is a default-constructed value on purpose: what a caller gets by
// writing `GnssCapabilities{}` is precisely what has to be safe.
constexpr GnssCapabilities kUnknown{};
constexpr GnssCapabilities kNone{SupportState::Unsupported, SupportState::Unsupported,
                                 SupportState::Unsupported, SupportState::Unsupported};
constexpr GnssCapabilities kFull{SupportState::Supported, SupportState::Supported,
                                 SupportState::Supported, SupportState::Supported};

// The two that must be refused, and the labels a failure will print.
constexpr GnssCapabilities kRefusing[]   = {kUnknown, kNone};
constexpr const char*      kRefusingName[] = {"unchecked", "plain"};

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

// Exhaustive again, and against both receivers that may not have it: every
// state a capability gates must be unreachable, from everywhere, with no
// exceptions hiding in a corner of the table. The unchecked receiver is in the
// loop beside the one proven to lack the feature, because the whole point of
// the third state is that it is not allowed to be optimistic.
void test_a_state_the_receiver_does_not_have_is_unreachable()
{
    for (std::size_t c = 0; c < std::size(kRefusing); ++c) {
        for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
            const GnssState from = static_cast<GnssState>(f);
            if (from != GnssState::Backup) {
                CHECK_GNSS(from, GnssState::Backup, kRefusing[c], false, kRefusingName[c]);
            }
            if (from != GnssState::PowerSave) {
                CHECK_GNSS(from, GnssState::PowerSave, kRefusing[c], false, kRefusingName[c]);
            }
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
        CHECK_GNSS(static_cast<GnssState>(f), GnssState::Off, kUnknown, true, "unchecked");
        CHECK_GNSS(static_cast<GnssState>(f), GnssState::Off, kNone, true, "plain");
        CHECK_GNSS(static_cast<GnssState>(f), GnssState::Off, kFull, true, "full");
    }
}

// The finding this file was extended for, and the shortest statement of it: a
// capability nobody has established is a third value, not the absent one.
//
// A `GnssCapabilities` that is four `bool`s cannot compile the first two checks
// here, which is the mutation proof — the old model does not merely fail this
// test, it fails to build it. tests/CMakeLists.txt registers the other half of
// that claim: `GnssCapabilities{false, false, false, false}` must not compile
// either, so a bool cannot creep back in through an aggregate initializer.
void test_an_unchecked_capability_is_not_a_missing_one()
{
    // 1. The default is Unknown, in every field. This is what a caller who
    //    filled nothing in gets, and it is the state both candidate receivers
    //    are genuinely in until T-051 and T-052 land.
    const GnssCapabilities fresh{};
    CHECK(fresh.backup_domain == SupportState::Unknown);
    CHECK(fresh.power_save_mode == SupportState::Unknown);
    CHECK(fresh.assistance == SupportState::Unknown);
    CHECK(fresh.orbit_prediction == SupportState::Unknown);

    // 2. Three values, three meanings, none of them equal to another.
    CHECK(SupportState::Unknown != SupportState::Unsupported);
    CHECK(SupportState::Unsupported != SupportState::Supported);
    CHECK(SupportState::Unknown != SupportState::Supported);

    // Only Supported is spendable; only Unknown is unestablished. The two
    // predicates answer different questions, which is the entire point — if
    // they agreed, one `bool` would still do.
    CHECK(!is_supported(SupportState::Unknown));
    CHECK(!is_supported(SupportState::Unsupported));
    CHECK(is_supported(SupportState::Supported));
    CHECK(!is_established(SupportState::Unknown));
    CHECK(is_established(SupportState::Unsupported));
    CHECK(is_established(SupportState::Supported));

    // 3. Each renders as itself. A diagnostics screen that printed Unknown as
    //    "Unsupported" would put the collision back one layer up, where it is
    //    harder to see and nothing is testing for it.
    const char* unknown     = to_string(SupportState::Unknown);
    const char* unsupported = to_string(SupportState::Unsupported);
    const char* supported   = to_string(SupportState::Supported);
    CHECK(unknown != nullptr && unknown[0] != '\0');
    CHECK(unsupported != nullptr && unsupported[0] != '\0');
    CHECK(supported != nullptr && supported[0] != '\0');
    CHECK(std::strcmp(unknown, unsupported) != 0);
    CHECK(std::strcmp(unsupported, supported) != 0);
    CHECK(std::strcmp(unknown, supported) != 0);

    // 4. A profile with a gap is not a finished profile. This is the check that
    //    T-051 and T-052 are actually done, and it is mechanical rather than
    //    somebody remembering which of four fields they filled in.
    CHECK(!fresh.fully_established());
    CHECK(!kUnknown.fully_established());
    CHECK(kNone.fully_established());
    CHECK(kFull.fully_established());

    // One gap is enough, in any field, and a proven absence closes it just as
    // well as a proven presence — "established" is about the research, not
    // about the answer.
    GnssCapabilities partial = kFull;
    partial.orbit_prediction = SupportState::Unknown;
    CHECK(!partial.fully_established());
    partial.orbit_prediction = SupportState::Unsupported;
    CHECK(partial.fully_established());

    GnssCapabilities gap_in_assistance = kNone;
    gap_in_assistance.assistance = SupportState::Unknown;
    CHECK(!gap_in_assistance.fully_established());
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
    CHECK(is_supported(context.capabilities.backup_domain));
    CHECK(start_kind(context) == StartKind::Cold);

    // Supported and actually retained is the one route to Warm. Stated
    // positively so the three checks below are refusals of something that does
    // otherwise happen, rather than of a path that never existed.
    GnssContext warm;
    warm.capabilities      = kFull;
    warm.backup_retained   = true;
    CHECK(start_kind(warm) == StartKind::Warm);

    // A receiver with no backup domain at all cannot be warm however the flags
    // are set, because there was nowhere for the almanac to survive.
    GnssContext plain;
    plain.capabilities     = kNone;
    plain.backup_retained  = true;
    CHECK(start_kind(plain) == StartKind::Cold);

    // And neither can one nobody has checked. The MS412FE cell is on the
    // T-Watch's GNSS daughterboard and whether it backs the receiver's RAM is
    // UNKNOWN until T-051 — so a warm start here is a guess with a stopwatch
    // attached, and the fail-safe answer is the cold one.
    GnssContext unchecked;
    unchecked.capabilities    = kUnknown;
    unchecked.backup_retained = true;
    CHECK(start_kind(unchecked) == StartKind::Cold);

    // Hot is a different fact and does not go through the capability at all: it
    // is about retained ephemeris and a recent fix. An unchecked receiver that
    // demonstrably still holds ephemeris is still hot, because that is an
    // observation rather than a promise.
    unchecked.ephemeris_retained = true;
    unchecked.since_last_fix     = Millis{60000};
    CHECK(start_kind(unchecked) == StartKind::Hot);
}

// §8 of the brief, and it is a prohibition rather than a feature: assistance
// data must never be a dependency. A watch in a forest with no network has to
// get a fix, slowly, on its own.
void test_assistance_is_never_required()
{
    constexpr SupportState kEvery[] = {SupportState::Unknown, SupportState::Unsupported,
                                       SupportState::Supported};

    // Whatever the assistance capability says — including that nobody knows —
    // a receiver asked for a fix goes and looks for one.
    for (SupportState assistance : kEvery) {
        for (SupportState orbit : kEvery) {
            GnssContext context;
            context.capabilities                  = kFull;
            context.capabilities.assistance       = assistance;
            context.capabilities.orbit_prediction = orbit;
            context.assistance_available          = false;
            context.fresh_fix_requested           = true;

            const GnssState next = next_state(GnssState::Off, context);
            CHECK(next != GnssState::Off);
            CHECK(transition_is_legal(GnssState::Off, next, context.capabilities));
        }
    }

    // Stronger, and the version that survives a refactor: the two optional
    // capabilities must not influence the plan *at all*. Not merely "a fix is
    // still attempted" — the same decision, from every state, under every
    // condition. A planner that started preferring one route when assistance
    // was declared would have made it load-bearing without anybody writing
    // "required" anywhere, which is exactly how §8's prohibition gets broken.
    const PowerState powers[] = {PowerState::Active, PowerState::Idle, PowerState::LightSleep,
                                 PowerState::MeshListenSleep, PowerState::DeepSleep,
                                 PowerState::PowerOff};

    for (const GnssCapabilities& base : {kUnknown, kNone, kFull}) {
        for (PowerState power : powers) {
            for (int moving = 0; moving < 2; ++moving) {
                for (int wanted = 0; wanted < 2; ++wanted) {
                    for (int available = 0; available < 2; ++available) {
                        for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
                            const GnssState from = static_cast<GnssState>(f);

                            GnssState        reference{};
                            bool             have_reference = false;
                            for (SupportState assistance : kEvery) {
                                for (SupportState orbit : kEvery) {
                                    GnssContext context;
                                    context.capabilities                  = base;
                                    context.capabilities.assistance       = assistance;
                                    context.capabilities.orbit_prediction = orbit;
                                    context.device_power                  = power;
                                    context.device_moving                 = moving != 0;
                                    context.fresh_fix_requested           = wanted != 0;
                                    context.assistance_available          = available != 0;

                                    const GnssState to = next_state(from, context);
                                    if (!have_reference) {
                                        reference      = to;
                                        have_reference = true;
                                    } else if (to != reference) {
                                        std::fprintf(
                                            stderr,
                                            "FAIL line %d: from %s the plan changed to %s when "
                                            "assistance became %s — assistance is not allowed to "
                                            "be a dependency\n",
                                            __LINE__, to_string(from), to_string(to),
                                            to_string(assistance));
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
    plain.capabilities = kNone;
    CHECK(next_state(GnssState::Tracking, plain) == GnssState::Off);

    // And on a receiver nobody has checked, the same answer for a different
    // reason: the rail comes down. Holding a domain that may not exist would
    // spend current on a promise nothing has verified, and the cost of being
    // wrong is a flat battery rather than a slow fix.
    GnssContext unchecked = context;
    unchecked.capabilities = kUnknown;
    CHECK(next_state(GnssState::Tracking, unchecked) == GnssState::Off);
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
    // Every combination of the two *gating* capabilities, at every support
    // state — nine sets rather than the two extremes. The two-set version could
    // not see a mixed receiver at all, and could not see an unchecked one,
    // which between them are most of the receivers this project will meet
    // before T-051 and T-052 close.
    constexpr SupportState kEvery[] = {SupportState::Unknown, SupportState::Unsupported,
                                       SupportState::Supported};
    GnssCapabilities sets[std::size(kEvery) * std::size(kEvery)];
    std::size_t      set_count = 0;
    for (SupportState backup : kEvery) {
        for (SupportState save : kEvery) {
            sets[set_count].backup_domain   = backup;
            sets[set_count].power_save_mode = save;
            ++set_count;
        }
    }

    const PowerState powers[]     = {PowerState::Active, PowerState::Idle, PowerState::LightSleep,
                                     PowerState::MeshListenSleep, PowerState::DeepSleep,
                                     PowerState::PowerOff};

    for (const GnssCapabilities& caps : sets) {
        for (PowerState power : powers) {
            for (int moving = 0; moving < 2; ++moving) {
                for (int wanted = 0; wanted < 2; ++wanted) {
                    for (int retained = 0; retained < 2; ++retained) {
                        for (std::uint8_t f = 0; f < kGnssStateCount; ++f) {
                            GnssContext context;
                            context.capabilities        = caps;
                            context.device_power        = power;
                            context.device_moving       = moving != 0;
                            context.fresh_fix_requested = wanted != 0;
                            context.ephemeris_retained  = retained != 0;
                            context.since_last_fix      = Millis{retained != 0 ? 1000u : 0u};

                            const GnssState from = static_cast<GnssState>(f);
                            const GnssState to   = next_state(from, context);
                            if (!transition_is_legal(from, to, caps)) {
                                std::fprintf(stderr,
                                             "FAIL line %d: next_state proposed %s -> %s, which "
                                             "transition_is_legal refuses\n",
                                             __LINE__, to_string(from), to_string(to));
                                ++failures;
                            }
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
    test_an_unchecked_capability_is_not_a_missing_one();
    test_the_start_kind_follows_what_was_retained();
    test_assistance_is_never_required();
    test_the_device_state_outranks_the_receiver();
    test_an_off_receiver_does_not_enter_backup();
    test_next_state_never_proposes_an_illegal_move();
    test_every_gnss_name_is_readable();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("power: all checks passed (host only — nothing was measured)\n");
    return 0;
}
