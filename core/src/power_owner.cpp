#include "attadipa/core/power_owner.h"

namespace attadipa::core {
namespace {

// Suspension order, and therefore resume order read backwards.
//
// Ascending domain, descending resume. The display is first out and last back
// because it is the only one a person watches: bringing it up before the
// services behind it are running is how a watch shows a stale screen and
// answers nothing.
constexpr PowerDomain kOrder[kPowerDomainCount] = {
    PowerDomain::Display, PowerDomain::Radio, PowerDomain::NodeLink,
    PowerDomain::Gnss,    PowerDomain::Imu,
};

constexpr WakeSource kWakeOrder[kWakeSourceCount] = {
    WakeSource::Timer,    WakeSource::Button,        WakeSource::Touch,
    WakeSource::RadioIrq, WakeSource::NodeLink,      WakeSource::Usb,
    WakeSource::Accelerometer, WakeSource::Pmu,
};

}  // namespace

const char* to_string(PowerDomain domain)
{
    switch (domain) {
        case PowerDomain::Display:  return "Display";
        case PowerDomain::Radio:    return "Radio";
        case PowerDomain::NodeLink: return "NodeLink";
        case PowerDomain::Gnss:     return "Gnss";
        case PowerDomain::Imu:      return "Imu";
    }
    return "?";
}

const char* to_string(LeaseError error)
{
    switch (error) {
        case LeaseError::None:           return "None";
        case LeaseError::NoDomains:      return "NoDomains";
        case LeaseError::Exhausted:      return "Exhausted";
        case LeaseError::NotHeld:        return "NotHeld";
        case LeaseError::HardwareFailed: return "HardwareFailed";
    }
    return "?";
}

const char* to_string(SleepOutcome outcome)
{
    switch (outcome) {
        case SleepOutcome::Woken:                    return "Woken";
        case SleepOutcome::RefusedTransition:        return "RefusedTransition";
        case SleepOutcome::RefusedWakePlan:          return "RefusedWakePlan";
        case SleepOutcome::RefusedRailBeforeSuspend: return "RefusedRailBeforeSuspend";
        case SleepOutcome::RefusedLeaseHeld:         return "RefusedLeaseHeld";
        case SleepOutcome::RefusedHardwareFailed:    return "RefusedHardwareFailed";
        case SleepOutcome::FailedSuspend:            return "FailedSuspend";
        case SleepOutcome::FailedRail:               return "FailedRail";
        case SleepOutcome::FailedArm:                return "FailedArm";
        case SleepOutcome::FailedSleep:              return "FailedSleep";
    }
    return "?";
}

// ---------------------------------------------------------------------------

LeaseId PowerLeases::acquire(std::uint16_t domains, MonotonicTime deadline,
                             LeaseError& why)
{
    constexpr std::uint16_t kAllDomains =
        static_cast<std::uint16_t>((1u << kPowerDomainCount) - 1u);
    domains = static_cast<std::uint16_t>(domains & kAllDomains);
    if (domains == 0) {
        why = LeaseError::NoDomains;
        return kNoLease;
    }

    // The slot comes first, and every counter after it. Invariant 1 is then
    // structural rather than a restore path that has to be got right: there is
    // no point between the two at which a refusal can leave a count raised.
    std::uint8_t slot = kCapacity;
    for (std::uint8_t i = 0; i < kCapacity; ++i) {
        if (entries_[i].generation == 0) {
            slot = i;
            break;
        }
    }
    if (slot == kCapacity) {
        why = LeaseError::Exhausted;
        return kNoLease;
    }

    std::uint16_t generation = static_cast<std::uint16_t>(next_generation_[slot] + 1u);
    // Generation zero means "free", so it is never handed out; the wrap skips it.
    const std::uint16_t kGenerationLimit =
        static_cast<std::uint16_t>(0xFFFFU >> kSlotBits);
    if (generation == 0 || generation > kGenerationLimit) {
        generation = 1;
    }
    next_generation_[slot] = generation;

    entries_[slot].generation = generation;
    entries_[slot].domains    = domains;
    entries_[slot].deadline   = deadline;
    ++outstanding_;
    for (std::uint8_t d = 0; d < kPowerDomainCount; ++d) {
        if ((domains & (1u << d)) != 0) {
            ++holders_[d];
        }
    }

    why = LeaseError::None;
    return static_cast<LeaseId>(slot | (generation << kSlotBits));
}

bool PowerLeases::release(LeaseId id, LeaseError& why)
{
    const std::uint16_t slot       = static_cast<std::uint16_t>(id & kSlotMask);
    const std::uint16_t generation = static_cast<std::uint16_t>(id >> kSlotBits);
    if (id == kNoLease || slot >= kCapacity || generation == 0 ||
        entries_[slot].generation != generation) {
        // Invariant 2. A second release of the same handle lands here, because
        // the slot's generation moved on the moment the first one freed it.
        why = LeaseError::NotHeld;
        return false;
    }

    const std::uint16_t domains = entries_[slot].domains;
    for (std::uint8_t d = 0; d < kPowerDomainCount; ++d) {
        if ((domains & (1u << d)) != 0 && holders_[d] > 0) {
            --holders_[d];
        }
    }
    entries_[slot] = Entry{};
    --outstanding_;
    why = LeaseError::None;
    return true;
}

std::uint16_t PowerLeases::held() const
{
    std::uint16_t mask = 0;
    for (std::uint8_t d = 0; d < kPowerDomainCount; ++d) {
        if (holders_[d] > 0) {
            mask = static_cast<std::uint16_t>(mask | (1u << d));
        }
    }
    return mask;
}

std::uint8_t PowerLeases::holders(PowerDomain domain) const
{
    return holders_[static_cast<std::uint8_t>(domain)];
}

std::uint16_t PowerLeases::overdue(MonotonicTime now) const
{
    std::uint16_t mask = 0;
    for (const Entry& entry : entries_) {
        if (entry.generation != 0 && entry.deadline.ms != 0 &&
            now.ms > entry.deadline.ms) {
            mask = static_cast<std::uint16_t>(mask | entry.domains);
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------

LeaseId PowerOwner::acquire(std::uint16_t domains, MonotonicTime deadline,
                            LeaseError& why)
{
    if (!hardware_known_) {
        // ADR-0016 §4: every lease depending on hardware nobody read back is
        // refused until a successful re-initialisation.
        why = LeaseError::HardwareFailed;
        return kNoLease;
    }
    return leases_.acquire(domains, deadline, why);
}

bool PowerOwner::release(LeaseId id, LeaseError& why)
{
    // A release is always accepted, failed hardware or not. Refusing one would
    // strand a consumer that is doing exactly the right thing.
    return leases_.release(id, why);
}

void PowerOwner::reinitialised()
{
    hardware_known_ = true;
    state_          = PowerState::Active;
}

bool PowerOwner::unwind_suspend(std::uint16_t suspended, SleepReport& report)
{
    bool ok = true;
    // Exact reverse of the order they were recorded in, and only the ones that
    // were recorded. Re-deriving the set from the plan would resume a consumer
    // that was never suspended, which is how a teardown gets issued to a device
    // whose clocks already stopped.
    for (std::uint8_t i = kPowerDomainCount; i > 0; --i) {
        const PowerDomain domain = kOrder[i - 1];
        if ((suspended & domain_bit(domain)) == 0) {
            continue;
        }
        if (!hardware_.resume(domain)) {
            ok                     = false;
            report.hardware_known  = false;
            report.blocked_by      = static_cast<std::uint16_t>(
                report.blocked_by | domain_bit(domain));
        }
    }
    return ok;
}

bool PowerOwner::unwind_rails(std::uint16_t cut, SleepReport& report)
{
    bool ok = true;
    for (std::uint8_t i = kPowerDomainCount; i > 0; --i) {
        const PowerDomain domain = kOrder[i - 1];
        if ((cut & domain_bit(domain)) == 0) {
            continue;
        }
        if (!hardware_.set_rail(domain, true)) {
            ok                    = false;
            report.hardware_known = false;
            report.blocked_by     = static_cast<std::uint16_t>(
                report.blocked_by | domain_bit(domain));
        }
    }
    return ok;
}

bool PowerOwner::unwind_wake(std::uint16_t armed, SleepReport& report)
{
    bool ok = true;
    for (std::uint8_t i = kWakeSourceCount; i > 0; --i) {
        const WakeSource source = kWakeOrder[i - 1];
        if ((armed & wake_bit(source)) == 0) {
            continue;
        }
        if (!hardware_.disarm_wake(source)) {
            // This is finding 2 of POWER_OWNERSHIP.md, and it is the one that
            // must never be silent: a source the SoC still holds while the
            // software believes it does not is a wake nobody can explain.
            ok                    = false;
            report.hardware_known = false;
        }
    }
    return ok;
}

SleepReport PowerOwner::sleep(const SleepPlan& plan, MonotonicTime now)
{
    SleepReport report;
    report.overdue_leases = leases_.overdue(now);
    report.hardware_known = hardware_known_;

    if (!hardware_known_) {
        report.outcome = SleepOutcome::RefusedHardwareFailed;
        return report;
    }

    // Validate. Every sleep is entered from Idle (power_state.cpp), so the walk
    // is state -> Idle -> plan.state, and both halves have to be legal before
    // anything is touched.
    //
    // Legality is necessary and not sufficient: Idle -> Active is a legal move
    // and is not a sleep, and Idle -> DeepSleep is legal and is a reboot
    // boundary rather than a resume (ADR-0016 §7), so this path may not take it
    // and pretend the unwind below means anything. Two resting states, named,
    // rather than a hole shaped like every other transition the table allows.
    const bool restable = plan.state == PowerState::LightSleep ||
                          plan.state == PowerState::MeshListenSleep;
    if (!restable || !transition_is_legal(state_, PowerState::Idle) ||
        !transition_is_legal(PowerState::Idle, plan.state)) {
        report.outcome = SleepOutcome::RefusedTransition;
        return report;
    }
    if (!wake_plan_is_legal(plan.state, plan.wake_sources)) {
        report.outcome    = SleepOutcome::RefusedWakePlan;
        report.blocked_by = static_cast<std::uint16_t>(
            plan.wake_sources & ~legal_wake_sources(plan.state));
        return report;
    }
    // ADR-0016 §3: a device's own low-power command is issued while its clock
    // and bus are still up, which is why rail gating comes after consumer
    // suspension. A rail in the plan whose consumer is not being suspended has
    // the order backwards, and that is a plan bug rather than a runtime one.
    if ((plan.rails_off & ~plan.suspend) != 0) {
        report.outcome    = SleepOutcome::RefusedRailBeforeSuspend;
        report.blocked_by = static_cast<std::uint16_t>(plan.rails_off & ~plan.suspend);
        return report;
    }
    const std::uint16_t blocked =
        static_cast<std::uint16_t>(leases_.held() & (plan.suspend | plan.rails_off));
    if (blocked != 0) {
        report.outcome    = SleepOutcome::RefusedLeaseHeld;
        report.blocked_by = blocked;
        return report;
    }

    // Suspend consumers, recording each success.
    std::uint16_t suspended = 0;
    for (const PowerDomain domain : kOrder) {
        if ((plan.suspend & domain_bit(domain)) == 0) {
            continue;
        }
        if (!hardware_.suspend(domain)) {
            report.outcome    = SleepOutcome::FailedSuspend;
            report.blocked_by = domain_bit(domain);
            (void)unwind_suspend(suspended, report);
            hardware_known_ = report.hardware_known;
            return report;
        }
        suspended = static_cast<std::uint16_t>(suspended | domain_bit(domain));
    }
    state_ = PowerState::Idle;

    // Apply the rail plan. Empty in the first implementation, and that is not
    // an oversight: gating a rail is authorised by a measurement, and every
    // measurement it would need is UNKNOWN.
    std::uint16_t cut = 0;
    for (const PowerDomain domain : kOrder) {
        if ((plan.rails_off & domain_bit(domain)) == 0) {
            continue;
        }
        if (!hardware_.set_rail(domain, false)) {
            report.outcome    = SleepOutcome::FailedRail;
            report.blocked_by = domain_bit(domain);
            (void)unwind_rails(cut, report);
            (void)unwind_suspend(suspended, report);
            state_          = report.hardware_known ? PowerState::Active : state_;
            hardware_known_ = report.hardware_known;
            return report;
        }
        cut = static_cast<std::uint16_t>(cut | domain_bit(domain));
    }

    // Arm the wake sources last, so the window in which the SoC holds an armed
    // source with no sleep entered is as short as the transaction can make it.
    std::uint16_t armed = 0;
    for (const WakeSource source : kWakeOrder) {
        if ((plan.wake_sources & wake_bit(source)) == 0) {
            continue;
        }
        if (!hardware_.arm_wake(source)) {
            report.outcome    = SleepOutcome::FailedArm;
            report.blocked_by = wake_bit(source);
            (void)unwind_wake(armed, report);
            (void)unwind_rails(cut, report);
            (void)unwind_suspend(suspended, report);
            state_          = report.hardware_known ? PowerState::Active : state_;
            hardware_known_ = report.hardware_known;
            return report;
        }
        armed = static_cast<std::uint16_t>(armed | wake_bit(source));
    }

    const PowerState resting = plan.state;
    state_                   = resting;
    std::uint16_t causes     = 0;
    const bool     slept     = hardware_.sleep(resting, causes);

    // Waking returns to Idle, never straight to Active: whether this wake is
    // worth lighting the screen for is decided above this file.
    state_ = PowerState::Idle;

    // The unwind is the same code on both paths. A sleep that failed still
    // armed sources and still suspended consumers, and leaving either behind is
    // exactly the defect this transaction exists to remove.
    (void)unwind_wake(armed, report);
    (void)unwind_rails(cut, report);
    (void)unwind_suspend(suspended, report);

    hardware_known_ = report.hardware_known;
    if (report.hardware_known) {
        state_ = PowerState::Active;
    }

    if (!slept) {
        report.outcome = SleepOutcome::FailedSleep;
        return report;
    }
    report.outcome           = SleepOutcome::Woken;
    report.wake_causes       = causes;
    report.unexpected_causes = static_cast<std::uint16_t>(causes & ~armed);
    ++cycles_;
    return report;
}

}  // namespace attadipa::core
