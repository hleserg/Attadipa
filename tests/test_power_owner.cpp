#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "attadipa/core/power_owner.h"

// Host tests for the power owner: the lease table, and the sleep transaction.
//
// docs/adr/0016-one-power-owner.md. Nothing here runs on hardware and nothing
// here is evidence about a board — CLAUDE.md's rule. What it is evidence about
// is the property that made the owner worth building: that a transaction which
// fails half-way leaves the hardware exactly as it found it, and that the
// journal is what decides "exactly", rather than a second derivation of what
// should have happened.
//
// The fake below therefore does two things a fake usually should not. It
// **records the order of every call**, because "resume exactly the recorded
// consumers in exact reverse order" is a claim about order and a test that only
// counts calls cannot fail on it. And it lets any single operation be failed on
// demand, because a fake that always succeeds turns every rollback test into a
// test of nothing.

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

class FakeHardware final : public PowerHardware {
public:
    std::vector<std::string> calls;

    // Fail exactly one operation, named the way the log names it.
    std::string fail_on;
    // A second, so a failing unwind can be tested alongside a failing step.
    std::string fail_on_too;

    std::uint16_t causes_to_report = 0;
    bool          sleep_succeeds   = true;
    int           sleeps           = 0;

    bool suspend(PowerDomain domain) override { return record("suspend", to_string(domain)); }
    bool resume(PowerDomain domain) override { return record("resume", to_string(domain)); }

    bool set_rail(PowerDomain domain, bool on) override
    {
        return record(on ? "rail-on" : "rail-off", to_string(domain));
    }

    bool arm_wake(WakeSource source) override { return record("arm", to_string(source)); }
    bool disarm_wake(WakeSource source) override { return record("disarm", to_string(source)); }

    bool sleep(PowerState state, std::uint16_t& causes) override
    {
        ++sleeps;
        causes = causes_to_report;
        return record("sleep", to_string(state)) && sleep_succeeds;
    }

    bool logged(const char* what) const
    {
        for (const std::string& call : calls) {
            if (call == what) {
                return true;
            }
        }
        return false;
    }

    int index_of(const char* what) const
    {
        for (std::size_t i = 0; i < calls.size(); ++i) {
            if (calls[i] == what) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int count(const char* what) const
    {
        int n = 0;
        for (const std::string& call : calls) {
            if (call == what) {
                ++n;
            }
        }
        return n;
    }

private:
    bool record(const char* verb, const char* subject)
    {
        std::string call = std::string(verb) + ":" + subject;
        calls.push_back(call);
        return call != fail_on && call != fail_on_too;
    }
};

SleepPlan light_sleep_plan()
{
    SleepPlan plan;
    plan.state        = PowerState::LightSleep;
    plan.wake_sources = wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button) |
                        wake_bit(WakeSource::Touch);
    plan.suspend = domain_bit(PowerDomain::Display);
    return plan;
}

constexpr MonotonicTime kNow{1000};

// ---------------------------------------------------------------------------
// Leases.

void test_acquire_and_release_one_domain()
{
    PowerLeases leases;
    LeaseError  why = LeaseError::Exhausted;

    const LeaseId id = leases.acquire(domain_bit(PowerDomain::Gnss), {}, why);
    CHECK(id != kNoLease);
    CHECK(why == LeaseError::None);
    CHECK(leases.held() == domain_bit(PowerDomain::Gnss));
    CHECK(leases.holders(PowerDomain::Gnss) == 1);
    CHECK(leases.outstanding() == 1);

    CHECK(leases.release(id, why));
    CHECK(why == LeaseError::None);
    CHECK(leases.held() == 0);
    CHECK(leases.holders(PowerDomain::Gnss) == 0);
    CHECK(leases.outstanding() == 0);
}

void test_two_independent_leases_on_one_domain()
{
    // The reference count is the whole point: BLE and a GNSS provider can both
    // be holding the same rail, and the first one to finish must not take it
    // out from under the second.
    PowerLeases leases;
    LeaseError  why = LeaseError::None;

    const LeaseId first  = leases.acquire(domain_bit(PowerDomain::NodeLink), {}, why);
    const LeaseId second = leases.acquire(domain_bit(PowerDomain::NodeLink), {}, why);
    CHECK(first != kNoLease && second != kNoLease);
    CHECK(first != second);
    CHECK(leases.holders(PowerDomain::NodeLink) == 2);

    CHECK(leases.release(first, why));
    CHECK(leases.holders(PowerDomain::NodeLink) == 1);
    CHECK(leases.held() == domain_bit(PowerDomain::NodeLink));

    // Only the last release frees it.
    CHECK(leases.release(second, why));
    CHECK(leases.holders(PowerDomain::NodeLink) == 0);
    CHECK(leases.held() == 0);
}

void test_a_lease_over_several_domains_is_all_or_nothing()
{
    PowerLeases leases;
    LeaseError  why = LeaseError::None;

    const std::uint16_t both =
        domain_bit(PowerDomain::Radio) | domain_bit(PowerDomain::Gnss);
    const LeaseId id = leases.acquire(both, {}, why);
    CHECK(id != kNoLease);
    CHECK(leases.held() == both);
    CHECK(leases.release(id, why));
    CHECK(leases.held() == 0);

    // An empty lease is refused rather than granted as a no-op: a handle that
    // holds nothing is one nobody will ever remember to release.
    CHECK(leases.acquire(0, {}, why) == kNoLease);
    CHECK(why == LeaseError::NoDomains);
    CHECK(leases.outstanding() == 0);
}

void test_a_second_release_is_reported_and_never_wraps()
{
    PowerLeases leases;
    LeaseError  why = LeaseError::None;

    const LeaseId id = leases.acquire(domain_bit(PowerDomain::Display), {}, why);
    CHECK(leases.release(id, why));
    CHECK(!leases.release(id, why));
    CHECK(why == LeaseError::NotHeld);
    CHECK(leases.holders(PowerDomain::Display) == 0);
    CHECK(leases.outstanding() == 0);

    // And a handle that was never issued.
    CHECK(!leases.release(static_cast<LeaseId>(0xBEEF), why));
    CHECK(why == LeaseError::NotHeld);
    CHECK(!leases.release(kNoLease, why));
    CHECK(why == LeaseError::NotHeld);
    CHECK(leases.outstanding() == 0);
}

void test_a_handle_whose_slot_was_reused_does_not_release_the_new_lease()
{
    // Without the generation this is the dangerous one: slot 0 is freed, slot 0
    // is handed to somebody else, and the first consumer's stale handle
    // releases a lease it does not own.
    PowerLeases leases;
    LeaseError  why = LeaseError::None;

    const LeaseId stale = leases.acquire(domain_bit(PowerDomain::Radio), {}, why);
    CHECK(leases.release(stale, why));
    const LeaseId fresh = leases.acquire(domain_bit(PowerDomain::Radio), {}, why);
    CHECK(fresh != kNoLease);
    CHECK(fresh != stale);

    CHECK(!leases.release(stale, why));
    CHECK(why == LeaseError::NotHeld);
    CHECK(leases.holders(PowerDomain::Radio) == 1);

    CHECK(leases.release(fresh, why));
    CHECK(leases.holders(PowerDomain::Radio) == 0);
}

void test_exhaustion_grants_nothing_and_moves_no_count()
{
    PowerLeases leases;
    LeaseError  why = LeaseError::None;
    LeaseId     held[PowerLeases::kCapacity]{};

    for (std::uint8_t i = 0; i < PowerLeases::kCapacity; ++i) {
        held[i] = leases.acquire(domain_bit(PowerDomain::Imu), {}, why);
        CHECK(held[i] != kNoLease);
    }
    CHECK(leases.holders(PowerDomain::Imu) == PowerLeases::kCapacity);

    const LeaseId refused = leases.acquire(domain_bit(PowerDomain::Imu), {}, why);
    CHECK(refused == kNoLease);
    CHECK(why == LeaseError::Exhausted);
    // Invariant 1: a refused acquire grants nothing and restores the count.
    CHECK(leases.holders(PowerDomain::Imu) == PowerLeases::kCapacity);
    CHECK(leases.outstanding() == PowerLeases::kCapacity);

    // A release makes room, and the table is usable again rather than wedged.
    CHECK(leases.release(held[0], why));
    CHECK(leases.acquire(domain_bit(PowerDomain::Imu), {}, why) != kNoLease);
}

void test_an_overdue_lease_is_reported_and_not_reclaimed()
{
    PowerLeases leases;
    LeaseError  why = LeaseError::None;

    const LeaseId id =
        leases.acquire(domain_bit(PowerDomain::Gnss), MonotonicTime{500}, why);
    CHECK(id != kNoLease);
    CHECK(leases.overdue(MonotonicTime{400}) == 0);
    CHECK(leases.overdue(MonotonicTime{500}) == 0);

    CHECK(leases.overdue(MonotonicTime{501}) == domain_bit(PowerDomain::Gnss));
    // Invariant 3. Reported, and still held: a consumer that believes it holds
    // hardware it does not is worse than one holding it too long, because
    // nothing tells it.
    CHECK(leases.holders(PowerDomain::Gnss) == 1);
    CHECK(leases.held() == domain_bit(PowerDomain::Gnss));

    // A lease with no deadline never goes overdue.
    const LeaseId forever = leases.acquire(domain_bit(PowerDomain::Radio), {}, why);
    CHECK(forever != kNoLease);
    CHECK((leases.overdue(MonotonicTime{1'000'000}) & domain_bit(PowerDomain::Radio)) == 0);
}

// ---------------------------------------------------------------------------
// The sleep transaction.

void test_a_clean_cycle_suspends_arms_sleeps_and_unwinds_in_reverse()
{
    FakeHardware hw;
    hw.causes_to_report = wake_bit(WakeSource::Touch);
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::Woken);
    CHECK(report.slept());
    CHECK(report.hardware_known);
    CHECK(report.wake_causes == wake_bit(WakeSource::Touch));
    CHECK(report.unexpected_causes == 0);
    CHECK(owner.cycles() == 1);
    CHECK(owner.state() == PowerState::Active);
    CHECK(owner.availability() == Availability::Ready);

    // Suspend before arm, arm before sleep, and the unwind exactly mirrored.
    CHECK(hw.index_of("suspend:Display") < hw.index_of("arm:Timer"));
    CHECK(hw.index_of("arm:Touch") < hw.index_of("sleep:LightSleep"));
    CHECK(hw.index_of("sleep:LightSleep") < hw.index_of("disarm:Touch"));
    CHECK(hw.index_of("disarm:Timer") < hw.index_of("resume:Display"));
    // Arming ascends the source order and disarming descends it.
    CHECK(hw.index_of("arm:Timer") < hw.index_of("arm:Button"));
    CHECK(hw.index_of("arm:Button") < hw.index_of("arm:Touch"));
    CHECK(hw.index_of("disarm:Touch") < hw.index_of("disarm:Button"));
    CHECK(hw.index_of("disarm:Button") < hw.index_of("disarm:Timer"));
    // Nothing touched a rail. Rail gating is authorised by a measurement.
    CHECK(!hw.logged("rail-off:Display"));
    CHECK(hw.count("suspend:Display") == 1);
    CHECK(hw.count("resume:Display") == 1);
}

void test_two_wake_causes_at_once_both_survive()
{
    // The defect this replaces: `esp_sleep_get_wakeup_cause()` returns one
    // value, its own header warns simultaneous sources are lost, and the old
    // path then re-read a pin to guess which. A bitmap keeps both.
    FakeHardware hw;
    hw.causes_to_report = wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button);
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::Woken);
    CHECK((report.wake_causes & wake_bit(WakeSource::Timer)) != 0);
    CHECK((report.wake_causes & wake_bit(WakeSource::Button)) != 0);
    CHECK(report.unexpected_causes == 0);
}

void test_a_cause_nobody_armed_is_reported_rather_than_masked()
{
    // The SoC holding a source the software believes is not armed is finding 2
    // of POWER_OWNERSHIP.md. Masking it would hide exactly the state the owner
    // exists to make visible.
    FakeHardware hw;
    hw.causes_to_report = wake_bit(WakeSource::Touch) | wake_bit(WakeSource::Pmu);
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::Woken);
    CHECK(report.unexpected_causes == wake_bit(WakeSource::Pmu));
    CHECK((report.wake_causes & wake_bit(WakeSource::Pmu)) != 0);
}

void test_no_cause_at_all_is_not_invented()
{
    FakeHardware hw;
    hw.causes_to_report = 0;
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::Woken);
    CHECK(report.wake_causes == 0);
    CHECK(report.unexpected_causes == 0);
}

void test_an_illegal_wake_plan_never_reaches_hardware()
{
    FakeHardware hw;
    PowerOwner   owner(hw);

    // MeshListenSleep exists to keep the radio listening, and touch is exactly
    // what it may not arm: the screen is off, and a finger on a dark panel is
    // not the reason the device is awake.
    SleepPlan plan    = light_sleep_plan();
    plan.state        = PowerState::MeshListenSleep;
    plan.wake_sources = wake_bit(WakeSource::RadioIrq) | wake_bit(WakeSource::Touch);

    const SleepReport report = owner.sleep(plan, kNow);
    CHECK(report.outcome == SleepOutcome::RefusedWakePlan);
    CHECK(report.blocked_by == wake_bit(WakeSource::Touch));
    CHECK(hw.calls.empty());
    CHECK(owner.cycles() == 0);
    CHECK(owner.state() == PowerState::Active);
}

void test_a_rail_cut_under_a_consumer_nobody_suspended_is_refused()
{
    FakeHardware hw;
    PowerOwner   owner(hw);

    SleepPlan plan  = light_sleep_plan();
    plan.rails_off  = domain_bit(PowerDomain::Radio);  // not in plan.suspend

    const SleepReport report = owner.sleep(plan, kNow);
    CHECK(report.outcome == SleepOutcome::RefusedRailBeforeSuspend);
    CHECK(report.blocked_by == domain_bit(PowerDomain::Radio));
    CHECK(hw.calls.empty());
}

void test_sleep_is_refused_while_a_lease_holds_what_it_would_take()
{
    FakeHardware hw;
    PowerOwner   owner(hw);
    LeaseError   why = LeaseError::None;

    const LeaseId id = owner.acquire(domain_bit(PowerDomain::Display), {}, why);
    CHECK(id != kNoLease);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::RefusedLeaseHeld);
    CHECK(report.blocked_by == domain_bit(PowerDomain::Display));
    CHECK(hw.calls.empty());
    CHECK(hw.sleeps == 0);

    // Release it, and the same plan goes through.
    CHECK(owner.release(id, why));
    CHECK(owner.sleep(light_sleep_plan(), kNow).outcome == SleepOutcome::Woken);
}

void test_a_lease_on_an_untouched_domain_does_not_block_sleep()
{
    FakeHardware hw;
    PowerOwner   owner(hw);
    LeaseError   why = LeaseError::None;

    CHECK(owner.acquire(domain_bit(PowerDomain::Radio), {}, why) != kNoLease);
    CHECK(owner.sleep(light_sleep_plan(), kNow).outcome == SleepOutcome::Woken);
}

void test_an_overdue_lease_is_carried_in_the_report_and_still_blocks()
{
    FakeHardware hw;
    PowerOwner   owner(hw);
    LeaseError   why = LeaseError::None;

    CHECK(owner.acquire(domain_bit(PowerDomain::Display), MonotonicTime{100}, why) !=
          kNoLease);

    const SleepReport report = owner.sleep(light_sleep_plan(), MonotonicTime{5000});
    CHECK(report.overdue_leases == domain_bit(PowerDomain::Display));
    // Reported, not reclaimed: it still refuses the plan.
    CHECK(report.outcome == SleepOutcome::RefusedLeaseHeld);
}

void test_a_failed_suspend_rolls_back_and_arms_nothing()
{
    FakeHardware hw;
    hw.fail_on = "suspend:Radio";
    PowerOwner owner(hw);

    SleepPlan plan = light_sleep_plan();
    plan.suspend   = domain_bit(PowerDomain::Display) | domain_bit(PowerDomain::Radio);

    const SleepReport report = owner.sleep(plan, kNow);
    CHECK(report.outcome == SleepOutcome::FailedSuspend);
    CHECK(report.blocked_by == domain_bit(PowerDomain::Radio));
    CHECK(report.hardware_known);
    CHECK(owner.availability() == Availability::Ready);

    // Display was suspended and is resumed. Radio was not suspended, so it is
    // not resumed: a teardown issued to a device that never went down is the
    // uhrwerk-rs bug.
    CHECK(hw.logged("resume:Display"));
    CHECK(!hw.logged("resume:Radio"));
    // And nothing was ever armed, because arming comes after suspension.
    CHECK(!hw.logged("arm:Timer"));
    CHECK(hw.sleeps == 0);
    CHECK(owner.cycles() == 0);
}

void test_a_failed_rail_rolls_back_the_rails_it_cut_and_the_consumers_it_suspended()
{
    FakeHardware hw;
    hw.fail_on = "rail-off:Radio";
    PowerOwner owner(hw);

    SleepPlan plan = light_sleep_plan();
    plan.suspend   = domain_bit(PowerDomain::Display) | domain_bit(PowerDomain::Radio);
    plan.rails_off = domain_bit(PowerDomain::Display) | domain_bit(PowerDomain::Radio);

    const SleepReport report = owner.sleep(plan, kNow);
    CHECK(report.outcome == SleepOutcome::FailedRail);
    CHECK(report.blocked_by == domain_bit(PowerDomain::Radio));
    CHECK(report.hardware_known);

    CHECK(hw.logged("rail-off:Display"));
    CHECK(hw.logged("rail-on:Display"));   // cut, so restored
    CHECK(!hw.logged("rail-on:Radio"));    // never cut, so not restored
    CHECK(hw.logged("resume:Display"));
    CHECK(hw.logged("resume:Radio"));
    CHECK(!hw.logged("arm:Timer"));
    CHECK(hw.sleeps == 0);
}

void test_a_partly_armed_wake_plan_disarms_exactly_what_it_armed()
{
    // This is the defect the owner was built for. The old path armed GPIO, then
    // the timer, and an early return left both set with no sleep entered — and
    // `esp_sleep_disable_wakeup_source` appeared once in the entire tree.
    FakeHardware hw;
    hw.fail_on = "arm:Touch";
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::FailedArm);
    CHECK(report.blocked_by == wake_bit(WakeSource::Touch));
    CHECK(report.hardware_known);

    CHECK(hw.logged("disarm:Timer"));
    CHECK(hw.logged("disarm:Button"));
    // Touch failed to arm, so it is not disarmed: the journal records what
    // succeeded, and only that is un-done.
    CHECK(!hw.logged("disarm:Touch"));
    CHECK(hw.logged("resume:Display"));
    CHECK(hw.sleeps == 0);
    CHECK(owner.cycles() == 0);
    CHECK(owner.state() == PowerState::Active);
}

void test_a_failed_sleep_still_disarms_and_still_resumes()
{
    FakeHardware hw;
    hw.sleep_succeeds = false;
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::FailedSleep);
    CHECK(report.hardware_known);
    CHECK(hw.logged("disarm:Touch"));
    CHECK(hw.logged("disarm:Timer"));
    CHECK(hw.logged("resume:Display"));
    CHECK(owner.cycles() == 0);
    CHECK(owner.state() == PowerState::Active);
    CHECK(owner.availability() == Availability::Ready);
}

void test_a_failed_unwind_publishes_failed_and_refuses_everything_after()
{
    // ADR-0016 §4. An honest Failed is what lets the layer above decide to
    // reboot; a hopeful Ready is how a watch shows a stale screen.
    FakeHardware hw;
    hw.fail_on = "resume:Display";
    PowerOwner owner(hw);

    const SleepReport report = owner.sleep(light_sleep_plan(), kNow);
    CHECK(report.outcome == SleepOutcome::Woken);  // it did sleep and wake
    CHECK(!report.hardware_known);                 // but the board is unknown now
    CHECK(owner.availability() == Availability::Failed);
    CHECK(owner.state() != PowerState::Active);

    LeaseError why = LeaseError::None;
    CHECK(owner.acquire(domain_bit(PowerDomain::Gnss), {}, why) == kNoLease);
    CHECK(why == LeaseError::HardwareFailed);

    const SleepReport second = owner.sleep(light_sleep_plan(), kNow);
    CHECK(second.outcome == SleepOutcome::RefusedHardwareFailed);
    CHECK(!second.hardware_known);
    CHECK(hw.sleeps == 1);  // it did not try again

    // Only an explicit re-initialisation clears it. Nothing does it on a timer.
    owner.reinitialised();
    CHECK(owner.availability() == Availability::Ready);
    CHECK(owner.acquire(domain_bit(PowerDomain::Gnss), {}, why) != kNoLease);
}

void test_a_release_is_accepted_even_when_the_hardware_is_unknown()
{
    FakeHardware hw;
    PowerOwner   owner(hw);
    LeaseError   why = LeaseError::None;

    const LeaseId id = owner.acquire(domain_bit(PowerDomain::Gnss), {}, why);
    CHECK(id != kNoLease);

    hw.fail_on = "resume:Display";
    (void)owner.sleep(light_sleep_plan(), kNow);
    CHECK(owner.availability() == Availability::Failed);

    // Refusing this would strand a consumer doing exactly the right thing.
    CHECK(owner.release(id, why));
    CHECK(why == LeaseError::None);
}

void test_two_cycles_leave_no_residue()
{
    FakeHardware hw;
    hw.causes_to_report = wake_bit(WakeSource::Button);
    PowerOwner owner(hw);

    CHECK(owner.sleep(light_sleep_plan(), kNow).outcome == SleepOutcome::Woken);
    const std::size_t after_first = hw.calls.size();
    CHECK(owner.sleep(light_sleep_plan(), MonotonicTime{2000}).outcome ==
          SleepOutcome::Woken);

    CHECK(owner.cycles() == 2);
    CHECK(hw.sleeps == 2);
    CHECK(owner.state() == PowerState::Active);
    // The second cycle did exactly the same work as the first. A residue would
    // show up as a shorter second half — a source still armed, or a consumer
    // still suspended, and therefore not touched again.
    CHECK(hw.calls.size() == after_first * 2);
    CHECK(hw.count("arm:Timer") == 2);
    CHECK(hw.count("disarm:Timer") == 2);
    CHECK(hw.count("suspend:Display") == 2);
    CHECK(hw.count("resume:Display") == 2);
}

void test_only_a_resting_state_is_a_sleep()
{
    // Idle -> Active is a legal transition and is not a sleep; Idle ->
    // DeepSleep is a legal transition and is a reboot boundary, not a resume
    // (ADR-0016 §7). Both are refused here, and legality alone would have
    // admitted them: the first version of this validation did.
    FakeHardware hw;
    PowerOwner   owner(hw);

    SleepPlan plan = light_sleep_plan();
    plan.state     = PowerState::Active;
    CHECK(owner.sleep(plan, kNow).outcome == SleepOutcome::RefusedTransition);

    plan.state = PowerState::Idle;
    CHECK(owner.sleep(plan, kNow).outcome == SleepOutcome::RefusedTransition);

    plan.state        = PowerState::DeepSleep;
    plan.wake_sources = wake_bit(WakeSource::Timer) | wake_bit(WakeSource::Button);
    const SleepReport report = owner.sleep(plan, kNow);
    CHECK(report.outcome == SleepOutcome::RefusedTransition);
    CHECK(hw.calls.empty());
    CHECK(hw.sleeps == 0);

    plan.state        = PowerState::MeshListenSleep;
    plan.wake_sources = wake_bit(WakeSource::Timer) | wake_bit(WakeSource::RadioIrq);
    plan.suspend      = domain_bit(PowerDomain::Display);
    CHECK(owner.sleep(plan, kNow).outcome == SleepOutcome::Woken);
    CHECK(hw.logged("sleep:MeshListenSleep"));
}

void test_every_new_name_is_readable()
{
    // A log line is where a wake gets explained, and "?" explains nothing.
    for (std::uint8_t i = 0; i < kPowerDomainCount; ++i) {
        CHECK(std::strcmp(to_string(static_cast<PowerDomain>(i)), "?") != 0);
    }
    const LeaseError errors[] = {LeaseError::None, LeaseError::NoDomains,
                                 LeaseError::Exhausted, LeaseError::NotHeld,
                                 LeaseError::HardwareFailed};
    for (const LeaseError error : errors) {
        CHECK(std::strcmp(to_string(error), "?") != 0);
    }
    const SleepOutcome outcomes[] = {
        SleepOutcome::Woken,             SleepOutcome::RefusedTransition,
        SleepOutcome::RefusedWakePlan,   SleepOutcome::RefusedRailBeforeSuspend,
        SleepOutcome::RefusedLeaseHeld,  SleepOutcome::RefusedHardwareFailed,
        SleepOutcome::FailedSuspend,     SleepOutcome::FailedRail,
        SleepOutcome::FailedArm,         SleepOutcome::FailedSleep};
    for (const SleepOutcome outcome : outcomes) {
        CHECK(std::strcmp(to_string(outcome), "?") != 0);
    }
}

}  // namespace

int main()
{
    test_acquire_and_release_one_domain();
    test_two_independent_leases_on_one_domain();
    test_a_lease_over_several_domains_is_all_or_nothing();
    test_a_second_release_is_reported_and_never_wraps();
    test_a_handle_whose_slot_was_reused_does_not_release_the_new_lease();
    test_exhaustion_grants_nothing_and_moves_no_count();
    test_an_overdue_lease_is_reported_and_not_reclaimed();

    test_a_clean_cycle_suspends_arms_sleeps_and_unwinds_in_reverse();
    test_two_wake_causes_at_once_both_survive();
    test_a_cause_nobody_armed_is_reported_rather_than_masked();
    test_no_cause_at_all_is_not_invented();
    test_an_illegal_wake_plan_never_reaches_hardware();
    test_a_rail_cut_under_a_consumer_nobody_suspended_is_refused();
    test_sleep_is_refused_while_a_lease_holds_what_it_would_take();
    test_a_lease_on_an_untouched_domain_does_not_block_sleep();
    test_an_overdue_lease_is_carried_in_the_report_and_still_blocks();
    test_a_failed_suspend_rolls_back_and_arms_nothing();
    test_a_failed_rail_rolls_back_the_rails_it_cut_and_the_consumers_it_suspended();
    test_a_partly_armed_wake_plan_disarms_exactly_what_it_armed();
    test_a_failed_sleep_still_disarms_and_still_resumes();
    test_a_failed_unwind_publishes_failed_and_refuses_everything_after();
    test_a_release_is_accepted_even_when_the_hardware_is_unknown();
    test_two_cycles_leave_no_residue();
    test_only_a_resting_state_is_a_sleep();
    test_every_new_name_is_readable();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all power owner checks passed\n");
    return 0;
}
