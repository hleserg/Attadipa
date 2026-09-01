#pragma once

#include <cstdint>

#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"
#include "attadipa/core/power_state.h"

// The one owner of sleep, rails and wake sources, and the leases consumers use
// instead of touching hardware.
//
// docs/adr/0016-one-power-owner.md, whose evidence is
// docs/research/POWER_OWNERSHIP.md. Three findings there are what this file is
// shaped by, and none of them is hypothetical:
//
//   * The armed wake plan was never reconciled with what the SoC actually
//     holds. `esp_sleep_disable_wakeup_source` appeared once in the whole tree
//     and two early-return paths left sources armed with no sleep entered. So
//     every step here is journaled, and an unwind un-does exactly the steps
//     that succeeded rather than re-deriving what "should" be undone.
//   * The wake cause was read from the single-cause API, whose own header warns
//     that simultaneous sources are lost, then corrected by re-reading a pin.
//     So `SleepReport::wake_causes` is a bitmask and can carry several.
//   * A rail that reads as free is not: ALDO2 on this board is a pull-up
//     holding `DSI_PWR_EN` high, and switching it off blanks the display
//     through a route that looks like a wiring fault. So core never names a
//     rail. It names a `PowerDomain`, and the board decides what that costs.
//
// Nothing in this file calls ESP-IDF, and it must stay that way: everything
// below is the mechanism, host-testable now, and `PowerHardware` is the seam
// the board implements. The board is where a GPIO number, an `esp_pm_lock` and
// an AXP2101 register live — ADR-0016 §1 and §5.
//
// Deep sleep is deliberately absent from the transaction's resume path. It is a
// reboot boundary, not a resume (§7), and it is out of scope until the shipping
// firmware has entered one.

namespace attadipa::core {

// A resource a consumer holds, and the unit a rail plan is written in.
//
// Not a rail, not a chip, not a bus. A consumer says "I am using the radio";
// which of DC1, ALDO2 or a GPIO that costs is the board's problem, and putting
// that mapping here is how `#ifdef BOARD_X` gets into `core/`.
enum class PowerDomain : std::uint8_t {
    Display,   // the panel and its backlight
    Radio,     // the LoRa transceiver
    NodeLink,  // BLE, and the link to an attached Attadipa node
    Gnss,      // the receiver
    Imu,       // the accelerometer, and whatever wakes on a wrist raise
};

inline constexpr std::uint8_t kPowerDomainCount =
    static_cast<std::uint8_t>(PowerDomain::Imu) + 1;

constexpr std::uint16_t domain_bit(PowerDomain domain)
{
    return static_cast<std::uint16_t>(1u << static_cast<std::uint16_t>(domain));
}

const char* to_string(PowerDomain domain);

// ---------------------------------------------------------------------------
// Leases.

// A grant. Zero is never one, so a default-constructed handle cannot release
// somebody else's lease by accident.
using LeaseId = std::uint16_t;

inline constexpr LeaseId kNoLease = 0;

enum class LeaseError : std::uint8_t {
    None,
    NoDomains,       // an empty lease holds nothing, and would never be released
    Exhausted,       // the table is full. Nothing was granted, no count moved
    NotHeld,         // an id that is not outstanding: unknown, stale, or already released
    HardwareFailed,  // the owner does not know what the hardware is doing (ADR-0016 §4)
};

const char* to_string(LeaseError error);

// A fixed-capacity, reference-counted table of who is using what.
//
// No heap and no task, because it is entered from a timer callback and from the
// path that is about to stop the CPU. Its three invariants are Zephyr v4.4.2's,
// verified at `671f64aa7992` and recorded in POWER_OWNERSHIP.md:
//
//   1. A refused acquire grants nothing and restores the count. Here it never
//      has to be restored: the slot is found before any counter moves.
//   2. A release below zero is a reported error, never a wrap. A handle carries
//      the generation of the slot it was cut from, so a double release, a stale
//      handle and a fabricated one are all `NotHeld` rather than a decrement.
//   3. A lease past its deadline is reported and **not** silently reclaimed.
//      A consumer that believes it holds hardware it does not is the Meshtastic
//      failure with its polarity reversed, and it is worse: nothing tells it.
class PowerLeases {
public:
    // Eight is not a budget, it is a ceiling: five domains, and a consumer that
    // wants a third simultaneous lease on one of them is a design error worth
    // reporting rather than absorbing.
    static constexpr std::uint8_t kCapacity = 8;

    // Grant a lease over every domain in `domains`, or grant nothing.
    LeaseId acquire(std::uint16_t domains, MonotonicTime deadline, LeaseError& why);

    bool release(LeaseId id, LeaseError& why);

    // The union of every domain currently held.
    std::uint16_t held() const;

    std::uint8_t holders(PowerDomain domain) const;

    std::uint8_t outstanding() const { return outstanding_; }

    // Domains held by a lease whose deadline has passed. Reported, never
    // reclaimed — see invariant 3.
    std::uint16_t overdue(MonotonicTime now) const;

private:
    // A handle is `slot | generation << kSlotBits`. Four bits of slot leaves
    // room to double the capacity without changing the encoding, and twelve
    // bits of generation is what makes a stale handle detectable rather than a
    // collision waiting for the 65536th acquire.
    static constexpr std::uint16_t kSlotBits = 4;
    static constexpr std::uint16_t kSlotMask = (1u << kSlotBits) - 1u;
    static_assert(kCapacity <= kSlotMask + 1, "a slot index must fit the handle");

    struct Entry {
        std::uint16_t generation = 0;  // zero means free
        std::uint16_t domains    = 0;
        MonotonicTime deadline{};
    };

    Entry         entries_[kCapacity]{};
    std::uint16_t next_generation_[kCapacity]{};
    std::uint8_t  holders_[kPowerDomainCount]{};
    std::uint8_t  outstanding_ = 0;
};

// ---------------------------------------------------------------------------
// The hardware seam.

// Everything the owner may do to a board, and nothing else may.
//
// Every operation returns whether its **postcondition** holds, not whether the
// call was issued. "Every shutdown callback was called" is not a postcondition
// (ADR-0016 §3): `uhrwerk-rs` `1986e3f` on this same board family shipped a
// teardown whose postcondition was never read back, and `11437824e0` is the
// correction.
class PowerHardware {
public:
    virtual ~PowerHardware() = default;

    // Quiesce the consumer on this domain, with its clock and its bus still up.
    // This is why rail gating comes after suspension and never before.
    virtual bool suspend(PowerDomain domain) = 0;
    virtual bool resume(PowerDomain domain) = 0;

    // The rail that feeds a domain. Cutting one is authorised by a measurement,
    // not by this interface existing — ADR-0016's Consequences.
    virtual bool set_rail(PowerDomain domain, bool on) = 0;

    virtual bool arm_wake(WakeSource source) = 0;
    virtual bool disarm_wake(WakeSource source) = 0;

    // Stop the CPU, and report every source that brought it back.
    //
    // `causes` is a `WakeSource` bitmask because a bitmap is what the SoC has:
    // `esp_sleep_get_wakeup_causes()` at ESP-IDF v5.5.5. An implementation that
    // can only name one cause sets one bit, and one that woke for two sets two.
    // Zero causes with a `true` return is legal and means the hardware could not
    // say — the report carries it rather than inventing a Timer.
    virtual bool sleep(PowerState state, std::uint16_t& causes) = 0;
};

// ---------------------------------------------------------------------------
// The transaction.

struct SleepPlan {
    PowerState    state         = PowerState::LightSleep;
    std::uint16_t wake_sources  = 0;  // WakeSource bits to arm
    std::uint16_t suspend       = 0;  // PowerDomain bits to quiesce
    std::uint16_t rails_off     = 0;  // PowerDomain bits whose rail to cut
};

enum class SleepOutcome : std::uint8_t {
    Woken,                      // slept, came back, hardware restored
    RefusedTransition,          // the state machine does not allow this move
    RefusedWakePlan,            // a source this state may not arm
    RefusedRailBeforeSuspend,   // a rail cut under a consumer nobody suspended
    RefusedLeaseHeld,           // a consumer is using what the plan would take
    RefusedHardwareFailed,      // a previous unwind failed; the board is unknown
    FailedSuspend,              // rolled back
    FailedRail,                 // rolled back
    FailedArm,                  // rolled back
    FailedSleep,                // never slept, or slept and reported failure; rolled back
};

const char* to_string(SleepOutcome outcome);

struct SleepReport {
    SleepOutcome  outcome        = SleepOutcome::RefusedTransition;
    std::uint16_t wake_causes    = 0;  // WakeSource bits; may be more than one
    std::uint16_t overdue_leases = 0;  // PowerDomain bits held past a deadline
    std::uint16_t blocked_by     = 0;  // the domains or sources that refused it

    // Causes the hardware reported that this transaction did not arm.
    //
    // Never masked away, because this is the direct reading of the state
    // POWER_OWNERSHIP.md found and nothing reconciled: the SoC holding a source
    // the software believes is not armed. Under this owner it should be
    // permanently zero, and a non-zero value is the evidence that something
    // outside the owner is still arming wake sources.
    std::uint16_t unexpected_causes = 0;

    // False once an unwind step failed. The board is then in a state nobody
    // read back, and ADR-0016 §4 is what happens next: `Failed`, never `Ready`.
    bool hardware_known = true;

    constexpr bool slept() const { return outcome == SleepOutcome::Woken; }
};

// The owner.
//
// One instance per board. It holds the lease table, the current state, and the
// only path into sleep. Everything it does to hardware goes through
// `PowerHardware`, and every step it takes is recorded so that the step can be
// un-taken — from the record, rather than by re-deriving it.
//
// **Not thread-safe, and deliberately so.** Every `acquire()`, `release()` and
// `sleep()` must happen on one task; on the Waveshare board that is the UI task
// that already owned `maybe_sleep()`. No lock is taken and none is needed while
// that holds. The first consumer that wants a lease from another task — the
// BLE transport is the obvious one, and issue #367 item 7 asks for it — brings
// the serialisation decision with it, and it is a larger decision than a mutex
// around this table: `sleep()` reads `held()` and then talks to hardware for as
// long as the sleep lasts, so a lease taken in between is a lease the sleeper
// never saw. Answering that needs the lease to participate in the sleep
// decision itself, which is a design this owner does not yet have and which
// nothing in the current firmware needs, because the only sleep plan that runs
// suspends the display alone and no cross-task lease intersects it.
class PowerOwner {
public:
    explicit PowerOwner(PowerHardware& hardware) : hardware_(hardware) {}

    LeaseId acquire(std::uint16_t domains, MonotonicTime deadline, LeaseError& why);
    bool    release(LeaseId id, LeaseError& why);

    const PowerLeases& leases() const { return leases_; }

    PowerState state() const { return state_; }

    // `Failed` once an unwind failed, and it stays there until the board says
    // it has re-initialised. Unpowered or unknown-state hardware is never
    // reported Active (ADR-0016 §4).
    Availability availability() const
    {
        return hardware_known_ ? Availability::Ready : Availability::Failed;
    }

    // The board has re-established the hardware and read it back. Only this
    // clears a failure; nothing does it on a timer, and nothing does it hopefully.
    void reinitialised();

    // Completed sleep episodes. The count a wake log is numbered by.
    std::uint32_t cycles() const { return cycles_; }

    SleepReport sleep(const SleepPlan& plan, MonotonicTime now);

private:
    bool unwind_suspend(std::uint16_t suspended, SleepReport& report);
    bool unwind_rails(std::uint16_t cut, SleepReport& report);
    bool unwind_wake(std::uint16_t armed, SleepReport& report);

    PowerHardware& hardware_;
    PowerLeases    leases_{};
    PowerState     state_          = PowerState::Active;
    std::uint32_t  cycles_         = 0;
    bool           hardware_known_ = true;
};

}  // namespace attadipa::core
