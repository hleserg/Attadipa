#pragma once

// Who is allowed to answer a forget-bond, and when -- and nothing else.
//
// #378: `meshcore_ble_forget_bond()` returned `ESP_OK` the moment the request
// was queued, the bridge turned that into a terminal `MeshOk`, and the CLI
// printed `{"forgotten": true}` before `ble_store_util_delete_peer()` had been
// called at all. When the store then refused, the code said so only to the
// console -- its own comment conceded "the operator has already been told the
// bond is gone".
//
// Two tasks are involved and they are not the same task: the request arrives on
// the LVGL task (`watch_control.cpp` polls the bridge from an `lv_timer`) and
// the deletion happens on the `meshcore` worker (`xTaskCreate` in
// `meshcore_ble.cpp`). So the answer has to cross a task boundary, and the slot
// it crosses in has to be reserved before the request is queued -- otherwise two
// back-to-back requests both get told they succeeded and only one deletion
// happened.
//
// This is that slot. It is deliberately tiny, deliberately dependency-free, and
// deliberately not inside `meshcore_ble.cpp`: that translation unit is
// ESP-IDF-only and no host test can reach it, and a rule tested through a copy
// is not tested (AGENTS.md, "a test of ... an isolated decision helper does not
// prove the production caller works"). So it lives here, includes nothing but
// `<atomic>`, and `tests/test_session_owner.cpp` compiles this exact file --
// the pattern `meshcore_write_outcome.h` already uses.

#include <atomic>
#include <cstdint>

namespace attadipa::firmware {

enum class ForgetOutcome : std::uint8_t {
    // No request is in flight and no answer is waiting to be read.
    Idle,
    // Reserved, and the worker has not finished. The only state that refuses a
    // second reservation.
    InFlight,
    // `ble_store_util_delete_peer()` returned 0. This is the *only* value that
    // may become a terminal `MeshOk`.
    Deleted,
    // The worker finished and the bond is still there: the store refused, or
    // the conflict record was gone by the time the worker looked.
    Refused,
};

// One operation at a time, with its answer.
//
// The request task reserves and the worker completes, so the two ends never
// touch the same field twice; a lock-free slot is enough and a mutex here would
// put one of them to sleep on the other.
class ForgetBondOperation {
public:
    // Takes the slot. False -- and only false -- when a request really is still
    // in flight.
    //
    // An answer nobody collected does NOT refuse: the bridge drops its half of
    // the correlation when the host disconnects or its deadline expires, and if
    // an uncollected `Deleted` sitting here kept refusing, forget-bond would be
    // dead until the watch rebooted. That is a worse failure than the bug this
    // file fixes, so the stale answer is overwritten by the request that
    // replaces it.
    bool reserve()
    {
        for (;;) {
            ForgetOutcome expected = state_.load(std::memory_order_acquire);
            if (expected == ForgetOutcome::InFlight) return false;
            if (state_.compare_exchange_weak(expected, ForgetOutcome::InFlight,
                                             std::memory_order_acq_rel,
                                             std::memory_order_acquire)) {
                return true;
            }
        }
    }

    // The request never got queued. Gives the slot back rather than leaving it
    // in flight forever behind a request that does not exist.
    void release() { state_.store(ForgetOutcome::Idle, std::memory_order_release); }

    // The worker is done. `deleted` is `ble_store_util_delete_peer() == 0` and
    // nothing else -- not "the event was handled", not "the record was taken".
    void complete(bool deleted)
    {
        state_.store(deleted ? ForgetOutcome::Deleted : ForgetOutcome::Refused,
                     std::memory_order_release);
    }

    // Reads an answer once. A finished outcome is consumed, so the caller that
    // sends the terminal response is the only one that can see it and cannot
    // send it twice. `Idle` and `InFlight` are reported as they are.
    ForgetOutcome take()
    {
        ForgetOutcome seen = state_.load(std::memory_order_acquire);
        if (seen != ForgetOutcome::Deleted && seen != ForgetOutcome::Refused) {
            return seen;
        }
        if (state_.compare_exchange_strong(seen, ForgetOutcome::Idle,
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
            return seen;
        }
        // A new request took the slot between the load and the exchange, so
        // that answer belonged to an operation nobody is waiting on any more.
        return ForgetOutcome::InFlight;
    }

private:
    std::atomic<ForgetOutcome> state_{ForgetOutcome::Idle};
};

}  // namespace attadipa::firmware
