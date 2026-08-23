#pragma once

#include <cstddef>
#include <cstdint>

#include "attadipa/core/input.h"
#include "attadipa/debug/protocol.h"

// The device end of the debug channel.
//
// It owns no transport. Bytes arrive from somewhere -- a Unix socket in the
// simulator, USB-Serial/JTAG on a board that does not exist yet -- already
// de-framed by `link::Decoder`, and responses leave the same way. That is what
// makes the whole thing testable on a host with no device and no socket: the
// bridge is a pure function of (message in, state) to (messages out, input
// events queued).
//
// ### Where input goes
//
// Into `core::InputQueue`, which is the same queue the simulator's own SDL
// driver and, later, a touch controller push into. Nothing here reaches a
// widget, a screen or an event handler. A test that drove the interface through
// a private door would pass against code no finger can reach.
//
// ### The frame buffer is the caller's
//
// A screenshot needs a consistent copy of one frame, and on a device that is
// tens of kilobytes that production must not spend. So the bridge does not own
// one: the composition root passes a buffer in, and a build that does not want
// the feature does not allocate it. RESOURCE_BUDGET section 4's rule -- size
// the pool to the maximum and declare the bound -- is satisfied by the caller
// declaring it, at the one place that knows the panel size.
//
// ### Limits, and why each exists
//
// Section 10 of the request asks for caps on message size, event rate and hold
// duration, and for a safe reset. All four live here rather than in the host
// tool, because a limit enforced only by the well-behaved client is not a
// limit -- and the client that matters is the one that crashed halfway through
// a swipe.

namespace attadipa::debug {

// Where a rendered frame comes from.
//
// Deliberately narrow. The bridge must not know that LVGL exists, and the
// simulator must not know the protocol exists; this interface is the seam.
class ScreenSource {
public:
    virtual ~ScreenSource() = default;

    // Copies the currently displayed frame into `out`.
    //
    // The copy is the point: it must be one consistent frame, taken at a moment
    // when the renderer is not partway through the next one. Returns false if
    // nothing has been rendered yet or the buffer is too small -- never a
    // partial image reported as success.
    virtual bool capture(std::uint8_t* out, std::size_t capacity, std::uint16_t& width_out,
                         std::uint16_t& height_out, PixelFormat& format_out,
                         Orientation& orientation_out, std::size_t& bytes_out) = 0;

    // Board and build identity for Hello, and the button list for Capabilities.
    virtual const char* board_id() const  = 0;
    virtual const char* build_id() const  = 0;
    virtual std::uint8_t button_count() const = 0;
    virtual const ButtonDescriptor* buttons() const = 0;

    // True once the interface has been idle long enough to be worth
    // photographing. A source that cannot tell returns true immediately and
    // says so in `WaitStable`'s reply, rather than sleeping and pretending.
    virtual bool stable_since(std::uint32_t ms) const { (void)ms; return true; }
};

// The caps section 10 of the request asks for, at namespace scope rather than
// nested in Bridge. Not a style choice: a nested class's default member
// initialisers are not usable in a default argument of the enclosing class, so
// `Limits limits = {}` does not compile where it reads most naturally. Hoisting
// keeps the default and keeps the call site honest.
struct BridgeLimits {
    // A held button or finger is released after this long no matter what the
    // client does, so a crashed test cannot leave the watch with a button down
    // until someone notices.
    std::uint32_t max_hold_ms = 30000;

    // Events per second, counted over a sliding second. Injection is cheap,
    // but a client in a loop can outrun the interface's ability to drain the
    // queue, and the failure mode is a UI that looks hung.
    std::uint16_t max_events_per_s = 500;
};

class Bridge {
public:
    // Where responses go. A raw callback rather than std::function: this
    // compiles for a part where the allocation behind a std::function is not
    // welcome, and the two arguments cost nothing to thread through.
    using Emit = void (*)(void* ctx, const std::uint8_t* payload, std::size_t length);

    Bridge(core::InputQueue& queue, core::InputState& state, ScreenSource& source,
           std::uint8_t* frame_buffer, std::size_t frame_capacity, BridgeLimits limits = BridgeLimits{});

    // Handles one de-framed message. Every request produces at least one
    // response, including a typed error -- ADR-0005 section 4: never ignored.
    void handle(const std::uint8_t* payload, std::size_t length, std::uint32_t now_ms, Emit emit,
                void* ctx);

    // Sends the next screenshot chunk if a transfer is in progress. Returns
    // true while there is more to do.
    //
    // Chunk-at-a-time rather than a loop inside `handle`, so the caller keeps
    // control of how long it spends: on a device this runs from the same task
    // that services the interface, and a transfer that blocked until the last
    // of two thousand chunks had gone out is a watchdog reset with extra steps.
    bool pump(Emit emit, void* ctx);

    // Periodic housekeeping: expires anything held past `max_hold_ms`.
    void tick(std::uint32_t now_ms, Emit emit, void* ctx);

    // The transport dropped. Lifts everything the remote was holding, and
    // nothing a person is holding, then abandons any transfer in flight.
    void on_disconnect(std::uint32_t now_ms);

    bool transfer_in_progress() const { return transfer_.active; }

    // Diagnostics, counted rather than logged.
    struct Stats {
        std::uint32_t requests       = 0;
        std::uint32_t errors         = 0;
        std::uint32_t events_injected= 0;
        std::uint32_t events_refused = 0;
        std::uint32_t frames_sent    = 0;
        std::uint32_t holds_expired  = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void send(const Envelope& envelope, const std::uint8_t* body, std::size_t body_len, Emit emit,
              void* ctx);
    void send_error(std::uint16_t req_id, ErrorCode code, Emit emit, void* ctx);

    void handle_hello(std::uint16_t req_id, const std::uint8_t* body, std::size_t len, Emit emit,
                      void* ctx);
    void handle_capabilities(std::uint16_t req_id, Emit emit, void* ctx);
    void handle_screen(std::uint16_t req_id, std::uint32_t now_ms, Emit emit, void* ctx);
    void handle_input(std::uint16_t req_id, const std::uint8_t* body, std::size_t len,
                      std::uint32_t now_ms, Emit emit, void* ctx);
    void handle_input_reset(std::uint16_t req_id, std::uint32_t now_ms, Emit emit, void* ctx);

    bool rate_allows(std::uint32_t now_ms);

    core::InputQueue& queue_;
    core::InputState& state_;
    ScreenSource&     source_;

    std::uint8_t* frame_buffer_   = nullptr;
    std::size_t   frame_capacity_ = 0;

    BridgeLimits limits_{};
    Stats  stats_{};

    struct Transfer {
        bool          active   = false;
        std::uint16_t req_id   = 0;
        std::size_t   sent     = 0;
        std::size_t   total    = 0;
        std::uint32_t frame_id = 0;
    } transfer_{};

    // When each currently-held input went down, so `tick` can expire it.
    std::uint32_t button_down_at_[core::kMaxButtons] = {};
    std::uint32_t pointer_down_at_                   = 0;

    // Sliding-second event counter.
    std::uint32_t rate_window_start_ms_ = 0;
    std::uint16_t rate_window_count_    = 0;

    std::uint32_t next_frame_id_ = 1;
};

}  // namespace attadipa::debug
