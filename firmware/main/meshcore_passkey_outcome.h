#pragma once

// Who is allowed to answer a passkey provisioning request, and when -- and
// nothing else.
//
// #416: `configure_meshcore_ble()` returned true the moment the request was
// queued, `BoardProvisioner::set_mesh_passkey()` turned that into a terminal
// `ProvisionOutcome::Accepted`, and the entry screen printed "the watch is set
// up" before `ble_sm_configure_static_passkey()` had been called at all. When
// the stack then refused, or when the NVS write that has to outlive the boot
// refused, the only place that said so was the serial log -- which the person
// holding the watch is not reading.
//
// Two tasks are involved and they are not the same task: the request arrives on
// the LVGL task (`ProvisioningEntry::accept()`, driven by a keypad callback)
// and the passkey is armed and stored on the `meshcore` worker (`xTaskCreate`
// in `meshcore_ble.cpp`). So the answer has to cross a task boundary, and the
// slot it crosses in has to be reserved before the request is queued.
//
// This is that slot, and it is `meshcore_forget_outcome.h` one screen further
// on: same reserve-before-post, same worker completion, same one-shot take, and
// dependency-free for the same reason -- `meshcore_ble.cpp` is ESP-IDF-only, no
// host test can reach it, and a rule tested through a copy is not tested
// (AGENTS.md, "a test of ... an isolated decision helper does not prove the
// production caller works"). `tests/test_meshcore_passkey.cpp` compiles this
// exact file.
//
// What it has that the forget-bond slot does not is a **ticket**. The bridge's
// forget-bond has one correlation at a time and says so; this screen can be
// cancelled and reopened by a person who is standing there, so a completion
// belonging to the session that was abandoned must not be able to finish the
// session that replaced it. The ticket is what makes "not mine" answerable
// rather than guessed at, and it is one word: ticket and state live in one
// atomic so that a reader can never see one of them from before a reservation
// and the other from after it.

#include <atomic>
#include <cstdint>

namespace attadipa::firmware {

enum class PasskeyOutcome : std::uint8_t {
    // No request is in flight and no answer is waiting to be read. Also what a
    // ticket that no longer owns the slot is told: its operation is over and
    // nobody is coming back to it.
    Idle,
    // Reserved, and the worker has not finished. The only state that refuses a
    // second reservation.
    InFlight,
    // `ble_sm_configure_static_passkey()` took it and, where the passkey had to
    // outlive the boot, `store_passkey()` returned true. This is the *only*
    // value that may become "the watch is set up".
    Armed,
    // The stack refused the passkey. Nothing is armed and the transport is
    // faulted; the passkey is not stored either, because the worker never
    // reaches the write.
    Refused,
    // Armed for this boot and not on flash: the NVS write refused. A power
    // cycle unprovisions the watch, which is not something a person can be
    // left to discover from a node that stops answering.
    NotStored,
};

// One operation at a time, with its answer and the ticket it belongs to.
//
// The request task reserves and the worker completes, so the two ends never
// write the same field twice; a lock-free word is enough and a mutex here would
// put one of them to sleep on the other.
class PasskeyOperation {
public:
    // Takes the slot and hands back the ticket the answer will be quoted under.
    // False -- and only false -- when a request really is still in flight; the
    // ticket is then untouched.
    //
    // An answer nobody collected does NOT refuse. The screen that asked can be
    // cancelled, and if an uncollected `Armed` sitting here kept refusing, the
    // watch could not be provisioned again until it rebooted -- a worse failure
    // than the one this file fixes. The stale answer is overwritten, and the
    // ticket moves on so that its worker cannot come back and answer for the
    // request that replaced it.
    bool reserve(std::uint32_t& ticket)
    {
        std::uint32_t seen = slot_.load(std::memory_order_acquire);
        for (;;) {
            if (state_of(seen) == PasskeyOutcome::InFlight) return false;
            const std::uint32_t taken = next_ticket(ticket_of(seen));
            if (slot_.compare_exchange_weak(seen,
                                            pack(taken, PasskeyOutcome::InFlight),
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire)) {
                ticket = taken;
                return true;
            }
        }
    }

    // The request never got queued. Gives the slot back rather than leaving it
    // in flight forever behind a request that does not exist. The ticket is
    // kept, so the next reservation still moves past it.
    void release(std::uint32_t ticket)
    {
        std::uint32_t expected = pack(ticket, PasskeyOutcome::InFlight);
        (void)slot_.compare_exchange_strong(expected,
                                            pack(ticket, PasskeyOutcome::Idle),
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
    }

    // The worker is done, and says which of the three ways it finished.
    // `Armed` means the stack took the passkey and flash holds it, and nothing
    // else -- not "the event was handled", not "the queue took it".
    //
    // A ticket of zero is the boot replay and the debug bridge: they post the
    // same Configure event with nobody waiting on an answer, and a completion
    // from them must not land in a slot the screen reserved. A ticket that no
    // longer owns the slot is dropped for the same reason.
    void complete(std::uint32_t ticket, PasskeyOutcome outcome)
    {
        if (ticket == 0 || outcome == PasskeyOutcome::Idle ||
            outcome == PasskeyOutcome::InFlight) {
            return;
        }
        std::uint32_t expected = pack(ticket, PasskeyOutcome::InFlight);
        (void)slot_.compare_exchange_strong(expected, pack(ticket, outcome),
                                            std::memory_order_acq_rel,
                                            std::memory_order_acquire);
    }

    // Reads this ticket's answer once. A finished outcome is consumed, so the
    // caller that shows the terminal screen is the only one that can see it and
    // cannot see it twice. `InFlight` is reported as it is; a ticket that is not
    // the slot's, and zero with it, is `Idle` -- there is no operation of yours
    // outstanding, and a screen told `InFlight` there would wait for an answer
    // that is never coming.
    PasskeyOutcome take(std::uint32_t ticket)
    {
        if (ticket == 0) return PasskeyOutcome::Idle;
        std::uint32_t seen = slot_.load(std::memory_order_acquire);
        if (ticket_of(seen) != ticket) return PasskeyOutcome::Idle;
        const PasskeyOutcome state = state_of(seen);
        if (state == PasskeyOutcome::Idle || state == PasskeyOutcome::InFlight) {
            return state;
        }
        if (slot_.compare_exchange_strong(seen, pack(ticket, PasskeyOutcome::Idle),
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            return state;
        }
        // A new request took the slot between the load and the exchange, so
        // that answer belonged to an operation nobody is waiting on any more.
        return PasskeyOutcome::Idle;
    }

private:
    // Three bits of state under twenty-nine of ticket. Two atomics would let a
    // reader pair a ticket with a state that was never true together.
    static constexpr std::uint32_t kStateBits = 3;
    static constexpr std::uint32_t kStateMask = (1U << kStateBits) - 1U;
    static constexpr std::uint32_t kTicketMax = (1U << (32 - kStateBits)) - 1U;

    static constexpr std::uint32_t pack(std::uint32_t ticket, PasskeyOutcome state)
    {
        return (ticket << kStateBits) | static_cast<std::uint32_t>(state);
    }
    static constexpr std::uint32_t ticket_of(std::uint32_t slot)
    {
        return slot >> kStateBits;
    }
    static constexpr PasskeyOutcome state_of(std::uint32_t slot)
    {
        return static_cast<PasskeyOutcome>(slot & kStateMask);
    }
    // Zero is never handed out: it is what "no request of mine" is spelled as,
    // in the board's own field and in `complete()` above.
    static constexpr std::uint32_t next_ticket(std::uint32_t ticket)
    {
        const std::uint32_t next = (ticket + 1U) & kTicketMax;
        return next == 0U ? 1U : next;
    }

    std::atomic<std::uint32_t> slot_{pack(0, PasskeyOutcome::Idle)};
};

}  // namespace attadipa::firmware
