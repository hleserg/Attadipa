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
// It is deliberately not a lifecycle framework. It knows three steps because
// this bootstrap has three; a fourth would be a line here, not a mechanism.

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
};

// `Ops` supplies, in acquisition order:
//
//   bool port_init()      -- nimble_port_init()
//   void configure_host() -- ble_hs_cfg and the bond store; infallible, and
//                            after port_init because it writes the host's
//                            configuration
//   bool queue_create()   -- the worker's event queue
//   bool worker_create()  -- the worker task
//   void host_start()     -- nimble_port_freertos_init(); the last step, and
//                            the only one that cannot fail
//
// and to undo them:
//
//   void queue_delete()
//   void port_deinit()    -- nimble_port_deinit()
//
// The published state a caller may observe is exactly the state this returns
// `Ok` for. The worker is created last of the fallible steps, so it never runs
// before the state it reads exists; and every failure after `port_init`
// succeeded gives NimBLE back, because ESP-IDF's own lifecycle pairs
// `nimble_port_init` with `nimble_port_deinit` and nothing else here can.
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
    ops.host_start();
    return BootResult::Ok;
}

}  // namespace attadipa::firmware
