#pragma once

// The order MeshCore's BLE bootstrap acquires things in, and the order it gives
// them back. Nothing else.
//
// Third file on the `meshcore_write_outcome.h` pattern, and here for the same
// reason: `meshcore_ble.cpp` is ESP-IDF-only, so a host test cannot reach the
// bootstrap, and a rule tested through a copy is not tested (AGENTS.md, "a test
// of ... an isolated decision helper does not prove the production caller
// works"). What is here is therefore not a helper the production code mirrors
// -- it *is* the production sequence. `start_meshcore_ble()` instantiates it
// with the real ESP-IDF calls, `tests/test_session_owner.cpp` instantiates it
// with fakes that can be made to fail, and there is one sequence between them.
//
// It is deliberately not a lifecycle framework. It knows the steps this
// bootstrap has; another would be a line here, not a mechanism.

#include <cstdint>

namespace attadipa::firmware {

enum class BootResult : std::uint8_t {
    Ok,
    // The BLE host and controller could not be brought up. Nothing was
    // acquired, so nothing is released.
    PortInitFailed,
    // The worker's event queue could not be allocated.
    QueueFailed,
    // The worker task could not be created.
    WorkerFailed,
    // The NimBLE host task could not be created, so there is nothing to run
    // `nimble_port_run()` and no BLE would ever have happened.
    HostFailed,
};

// `Ops` supplies, in acquisition order:
//
//   bool port_init()       -- nimble_port_init()
//   void configure_host()  -- ble_hs_cfg and the bond store; infallible, and
//                             after port_init because it writes the host's
//                             configuration
//   bool queue_create()    -- the worker's event queue
//   bool worker_create()   -- the worker task, which starts *gated*: created,
//                             but blocked before it has touched anything
//   bool host_start()      -- the NimBLE host task
//   void worker_release()  -- open the gate; the last step, and the only one
//                             that cannot fail
//
// and to undo them:
//
//   void worker_abort()    -- end the gated worker without it ever running
//   void queue_delete()
//   void port_deinit()     -- nimble_port_deinit()
//
// The published state a caller may observe is exactly the state this returns
// `Ok` for. The worker does not run before the state it reads exists, because
// it does not run at all until `worker_release()`; and every failure after
// `port_init` succeeded gives NimBLE back, because ESP-IDF's own lifecycle
// pairs `nimble_port_init` with `nimble_port_deinit` and nothing else here can.
//
// WHY THE WORKER IS ACQUIRED BEFORE THE HOST AND THEN HELD SHUT, rather than
// simply created after the host succeeds. Everything already acquired has to be
// releasable by whichever later step fails, and the host task is the one
// acquisition this bootstrap cannot release: stopping it means
// `nimble_port_stop()`, which
// pends `BLE_NPL_TIME_FOREVER` on a semaphore only the host's own run loop
// releases, and which refuses with `BLE_HS_EALREADY` until that loop has
// processed the start event -- precisely the window a rollback would use it in
// (pinned esp-nimble `685675c0`, `porting/nimble/src/nimble_port.c` and
// `nimble/host/src/ble_hs_stop.c`). So the host goes last of the fallible
// steps, and the worker -- which this repository does own, and which has no
// stop path -- is created before it with its hands tied. A gated worker told
// to abort has read no queue and called into no stack, so releasing the rest
// needs no hand-off barrier: there is nothing it could still be inside.
template <typename Ops>
BootResult boot_meshcore(Ops& ops)
{
    if (!ops.port_init()) return BootResult::PortInitFailed;
    ops.configure_host();
    if (!ops.queue_create()) {
        ops.port_deinit();
        return BootResult::QueueFailed;
    }
    if (!ops.worker_create()) {
        // Reverse acquisition order, and it matters: the queue is what the
        // worker would have read, so it goes first.
        ops.queue_delete();
        ops.port_deinit();
        return BootResult::WorkerFailed;
    }
    if (!ops.host_start()) {
        ops.worker_abort();
        ops.queue_delete();
        ops.port_deinit();
        return BootResult::HostFailed;
    }
    ops.worker_release();
    return BootResult::Ok;
}

}  // namespace attadipa::firmware
