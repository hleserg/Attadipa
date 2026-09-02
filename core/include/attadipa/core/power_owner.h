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
// somebody else's lease by accident. Thirty-two bits, of which the slot takes
// four: the rest is a generation that is spent once and never comes round
// again, which is what makes "stale" a property of the handle rather than of
// how long ago it was cut (see PowerLeases::kGrantsPerSlot).
using LeaseId = std::uint32_t;

inline constexpr LeaseId kNoLease = 0;

enum class LeaseError : std::uint8_t {
    None,
    NoDomains,       // an empty lease holds nothing, and would never be released
    Exhausted,       // every slot is held. Recoverable: one release() clears it,
                     // and outstanding() == kCapacity names the leak
    NotHeld,         // an id that is not outstanding: unknown, stale, or already released
    Retired,         // every slot has spent its generations (kGrantsPerSlot).
                     // Permanent until reboot: no release() helps, and it can
                     // arrive with outstanding() == 0. Nothing granted, no count moved
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

    // A slot hands out this many generations and is then retired for the life
    // of the table: it is never reused and the generation never wraps. #367's
    // reproduction is why -- with a twelve-bit generation that wrapped, the
    // 4096th grant on one slot repeated the first one's handle and the first
    // consumer's stale `release()` freed the live lease. A wrap is the exact
    // collision the generation exists to prevent, so past the last generation
    // the answer is `Retired`, not a smaller version of the same bug. 2^28
    // grants per slot is 8.5 years at one lease per second on a slot that is
    // reused every time, and a reboot resets the table.
    //
    // The budget is a constructor argument for one reason: no host test can
    // drive 268 million grants, and a boundary that cannot be reached in a test
    // is a boundary nobody has checked. Production takes the default.
    static constexpr LeaseId kGrantsPerSlot = (LeaseId{1} << 28) - 1u;

    //
    // Clamped, not trusted: a budget past the encoding would let a generation
    // carry into the slot bits, and acquire() would hand back kNoLease with the
    // counters already raised -- invariant 1 broken by the argument that exists
    // to test the boundary.
    explicit PowerLeases(LeaseId grants_per_slot = kGrantsPerSlot)
        : grants_per_slot_(grants_per_slot < kGrantsPerSlot ? grants_per_slot
                                                            : kGrantsPerSlot) {}

    LeaseId grants_per_slot() const { return grants_per_slot_; }

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
    // room to double the capacity without changing the encoding; the other
    // twenty-eight are the generation, and a slot that has used all of them is
    // retired rather than wrapped -- kGrantsPerSlot says why.
    static constexpr std::uint16_t kSlotBits = 4;
    static constexpr LeaseId       kSlotMask = (LeaseId{1} << kSlotBits) - 1u;
    static_assert(kCapacity <= kSlotMask + 1, "a slot index must fit the handle");
    static_assert(kGrantsPerSlot <= (~LeaseId{0} >> kSlotBits),
                  "a generation must fit the handle beside its slot");

    struct Entry {
        LeaseId       generation = 0;  // zero means free
        std::uint16_t domains    = 0;
        MonotonicTime deadline{};
    };

    Entry         entries_[kCapacity]{};
    LeaseId       next_generation_[kCapacity]{};
    LeaseId       grants_per_slot_;
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
// What woke the device, in two halves that must not be added together.
//
// ADR-0016 §6 replaced the single-cause API with the bitmap, and the reason
// generalises past ESP-IDF: what the SoC reports and what the board concludes
// are different claims. Only `from_soc` can be checked against what this
// transaction armed, so only `from_soc` can show a source nobody armed.
//
// On the Waveshare board the power button is the case in point. It is not an
// SoC wake source at all: the AXP2101 latches an edge, and the firmware reads
// that register during a timer wake. Reporting it in `from_soc` would make
// every button press look like an unreconciled wake source.
struct WakeCauses {
    std::uint16_t from_soc = 0;  // what the silicon said woke it
    std::uint16_t derived  = 0;  // what the board concluded, from a register or a pin

    // Raw, board-shaped bits that `from_soc` could not name.
    //
    // A board maps the causes it knows into `WakeSource` and would otherwise
    // drop the rest, which turns "woke on something nobody armed" -- the exact
    // state this owner exists to detect -- into a zero indistinguishable from
    // "nothing was wrong". It is deliberately not a `WakeSource` mask: naming
    // it would mean inventing a name, and the value of this field is that it is
    // the hardware's own word, printable, and not translated by anyone.
    std::uint32_t unmapped_from_soc = 0;
};

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

    // Arm one wake source, or report that this board cannot.
    //
    // "Cannot" is the important half. A board that returns true for a source it
    // did not actually arm has manufactured exactly the state ADR-0016 exists to
    // prevent — software believing the hardware holds something it does not —
    // and it has done it in the one place nothing downstream can detect.
    virtual bool arm_wake(WakeSource source) = 0;
    virtual bool disarm_wake(WakeSource source) = 0;

    // Stop the CPU, and report every source that brought it back.
    //
    // Both halves are `WakeSource` bitmasks, and they are separate because only
    // one of them can be reconciled against what was armed. Zero causes with a
    // `true` return is legal and means the hardware could not say — the report
    // carries that rather than inventing a Timer.
    virtual bool sleep(PowerState state, WakeCauses& causes) = 0;
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
    // What refused the transition, in two words rather than one because the
    // two bit spaces overlap exactly -- `domain_bit(Display)` and
    // `wake_bit(Timer)` are both `0x0001` -- and because an unwind can fail on
    // a domain and on a source in the same call, so `outcome` cannot say which
    // kind a single word held.
    std::uint16_t blocked_by      = 0;  // PowerDomain bits
    std::uint16_t blocked_sources = 0;  // WakeSource bits

    // Causes the hardware reported that this transaction did not arm.
    //
    // Never masked away, because this is the direct reading of the state
    // POWER_OWNERSHIP.md found and nothing reconciled: the SoC holding a source
    // the software believes is not armed. Under this owner it should be
    // permanently zero, and a non-zero value is the evidence that something
    // outside the owner is still arming wake sources.
    std::uint16_t unexpected_causes = 0;

    // The same evidence for a cause the board has no name for. Non-zero means
    // the SoC reported a wake this project cannot yet map, and the raw value is
    // what a log has to print, because there is nothing else to say about it.
    std::uint32_t unmapped_causes = 0;

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

    // The board has re-established the hardware and read it back.
    //
    // The blunt way out of `Failed`, for a caller that re-initialised the
    // hardware itself. The ordinary way is `sleep()`, which retries the exact
    // steps that failed before it will refuse -- see `recover()`. Neither
    // happens on a timer and neither happens hopefully.
    void reinitialised();

    // Completed sleep episodes. The count a wake log is numbered by.
    std::uint32_t cycles() const { return cycles_; }

    SleepReport sleep(const SleepPlan& plan, MonotonicTime now);

private:
    bool unwind_suspend(std::uint16_t suspended, SleepReport& report);
    bool unwind_rails(std::uint16_t cut, SleepReport& report);
    bool unwind_wake(std::uint16_t armed, SleepReport& report);

    // Retry exactly the unwind steps that failed, and nothing else.
    //
    // Without this the latch has no consumer: one failed `resume(Display)`
    // leaves the panel dark, `availability()` Failed, and every later `sleep()`
    // refused before it touches hardware -- so the screen is never asked to
    // come back and the watch is dark until it is reset. Retrying is honest
    // here because each failed step recorded *which* step it was, so this
    // re-issues that one operation rather than re-deriving a teardown.
    bool recover(SleepReport& report);

    PowerHardware& hardware_;
    PowerLeases    leases_{};
    PowerState     state_          = PowerState::Active;
    std::uint32_t  cycles_         = 0;
    bool           hardware_known_ = true;

    // What an unwind left un-done, and what `recover()` retries. Domains for
    // the first two, `WakeSource` bits for the third.
    std::uint16_t failed_resume_  = 0;
    std::uint16_t failed_rail_    = 0;
    std::uint16_t failed_disarm_  = 0;
};

}  // namespace attadipa::core
