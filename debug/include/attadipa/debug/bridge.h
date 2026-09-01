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
// Into `core::InputQueue`, which is where a touch controller and the board's
// buttons will push once T-114 gives them a driver. Nothing here reaches a
// widget, a screen or an event handler. A test that drove the interface through
// a private door would pass against code no finger can reach.
//
// The bridge is that queue's **only producer today**. The simulator's own
// finger is `lv_sdl_mouse_create()`, which reaches LVGL as its own indev and
// never enters the queue; the two coexist because LVGL runs each indev
// independently, not because they meet here. Said plainly because an earlier
// draft of this comment claimed they met, and a false invariant in a core
// header is what the next agent builds on.
//
// ### The frame buffer is the caller's
//
// A screenshot needs a consistent copy of one frame, and on a device that is
// **617 kB** on the Waveshare's 410x502 panel at RGB888, or 173 kB on the
// T-Watch's 240x240 -- half of each at RGB565. That is memory production must
// not spend, and it is the whole argument of this section, so the number
// belongs in it. So the bridge does not own
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

    // Why a capture returned false. Every value maps to a different thing for
    // the reader to go and look at, which is the whole point: rolled into one
    // answer they all read as "wait for a frame and try again", and only one of
    // them is fixed by waiting.
    enum class Failure : std::uint8_t {
        None = 0,          // the capture succeeded
        ShapeQuery,        // no buffer was passed; the geometry is filled and nothing copied
        BufferTooSmall,    // this build's frame buffer cannot hold this panel
        NotRendered,       // there is no frame yet -- the only one waiting fixes
        RendererFailed,    // the renderer could not produce one, typically out of memory
        GeometryMismatch,  // the active screen is not the panel size
    };

    // Copies the currently displayed frame into `out`.
    //
    // The copy is the point: it must be one consistent frame, taken at a moment
    // when the renderer is not partway through the next one. Returns false if
    // nothing has been rendered yet or the buffer is too small -- never a
    // partial image reported as success.
    //
    // **Two modes, and both are part of the contract.** Called with
    // `out == nullptr` or `capacity == 0` this is a *shape query*: fill
    // `width_out`, `height_out`, `format_out`, `orientation_out` and
    // `bytes_out`, copy nothing, and return false. An implementation that
    // returns early on a null buffer without filling them reports a 0x0 screen
    // in `Capabilities`, and every later command then fails with a coordinate
    // error that names nothing. `Bridge::handle_capabilities` uses this mode;
    // both shipped implementations honoured it by coincidence of authorship
    // until it was written down here.
    //
    // On every path the geometry outputs are filled before returning, and so is
    // `why_out` -- `Failure::None` exactly when the return is true.
    virtual bool capture(std::uint8_t* out, std::size_t capacity, std::uint16_t& width_out,
                         std::uint16_t& height_out, PixelFormat& format_out,
                         Orientation& orientation_out, std::size_t& bytes_out,
                         Failure& why_out) = 0;

    // Board and build identity for Hello, and the button list for Capabilities.
    virtual const char* board_id() const  = 0;
    virtual const char* build_id() const  = 0;
    // `buttons()` points at `button_count()` descriptors and never fewer.
    // A source may return nullptr, in which case the count is treated as zero.
    virtual std::uint8_t button_count() const = 0;
    virtual const ButtonDescriptor* buttons() const = 0;

    // True once the interface has settled: no input for at least `ms` -- a
    // duration, not a point in time -- **and** nothing animating, **and**
    // nothing still on its way in. The third is the one a source cannot
    // usually see and the bridge checks for it: an event sitting in
    // `core::InputQueue`, or in whatever the source pumps it into before the
    // interface reads, is input that has reached the device and not the
    // interface. `tap` sends down and up in about ten milliseconds and the
    // drain is one loop iteration away, so a `wait_stable` immediately after
    // an action used to be answered against the idle the *previous* step left
    // behind, say `ok`, and let the screenshot after it re-render a tree that
    // had never seen the tap. The caller
    // supplies the duration; `WaitStable` carries it in the body for exactly
    // this reason, because a source cannot know how long "settled" is for the
    // transition the host is waiting on.
    //
    // Both halves are required and the second is the one that is easy to omit.
    // An idle timer is stamped by input processing; an animation started by the
    // tap 300 ms ago touches nothing, so idleness alone would report a settled
    // interface with a transition still running under it, and the screenshot
    // after it would catch that transition halfway under a step that said
    // `ok`. A source that cannot see its own animations must answer `false`
    // rather than answer for the half it can see.
    //
    // **No default.** A source with no idle tracking must say so by writing
    // `return false;` and meaning it, because the reply carries one bit and
    // cannot distinguish "settled" from "not tracked". A default of `true` here
    // reaches every test double by inheritance and makes the step vacuous
    // wherever it is not overridden -- which is what happened, twice: first in
    // the host step that discarded the answer, then here.
    virtual bool stable_since(std::uint32_t ms) const = 0;
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

    // Events per second, counted over a fixed one-second window that restarts
    // on the first event after the previous one expired -- so a burst
    // straddling the boundary can briefly reach twice this, which is accepted
    // and named rather than smoothed. Injection is cheap,
    // but a client in a loop can outrun the interface's ability to drain the
    // queue, and the failure mode is a UI that looks hung.
    //
    // **This is above what the simulator's interface absorbs**, deliberately
    // and not by oversight. `sim/remote_input.cpp` dispatches at most sixteen
    // transitions per `lv_indev_read`, and LVGL reads an input device once per
    // `LV_DEF_REFR_PERIOD` -- 33 ms in that build -- so about 485 a second
    // reach a widget. A client between the two numbers is refused nothing and
    // sees its events counted and typed on the way in, rather than silently
    // outrunning the drain. The cap belongs to the transport; how fast an
    // interface consumes is the interface's own fact, and a source with a
    // slower one does not get a slower protocol.
    std::uint16_t max_events_per_s = 500;
};

enum class TimeSinkResult : std::uint8_t { Accepted, Rejected, Failed };

class TimeSink {
public:
    virtual ~TimeSink() = default;
    virtual TimeSinkResult synchronize(const TimeSyncBody& request) = 0;
};

// `Pending` is answered by `forget_bond` alone, and it means "the request was
// taken and the answer is not known yet". Every other operation here is
// answered by the sink synchronously, so `Accepted` still means what it says
// for them -- and for a forget-bond sink that really is synchronous, which is
// why this is a fourth value rather than a redefinition of `Accepted`.
enum class MeshSinkResult : std::uint8_t { Accepted, Rejected, Failed, Pending };

// The array extents below are a contract, not a constraint: an array parameter
// decays to a pointer, so nothing makes the compiler check that 6 or 32 bytes
// are really there. What does check it is the caller -- bridge.cpp refuses
// MeshSend under 15 body bytes and MeshRoomSend under 42 before either call,
// so the prefix and the room key are whole by the time they arrive. A sized
// type here would not add a check; the body is a pointer into the wire buffer,
// so it would only move a reinterpret_cast to the call site.
class MeshSink {
public:
    virtual ~MeshSink() = default;
    virtual MeshSinkResult configure(std::uint32_t passkey) = 0;
    virtual MeshSinkResult disconnect() = 0;
    // Forgets the one bond a repeat-pairing conflict recorded, and arms one
    // fresh pairing. `Rejected` means nothing conflicted, so there is no bond
    // this may touch -- see firmware/main/meshcore_bond_recovery.h for why the
    // operation takes no address. This is not a new privilege on the debug
    // channel: whoever reaches it already holds `configure`, which sets the
    // pairing passkey, and `disconnect`.
    //
    // `Pending` means the deletion has not happened yet and the bridge must not
    // answer -- #378, where the reply that said the bond was gone went out
    // before `ble_store_util_delete_peer()` had been called, and stayed a
    // success when the store then refused. A sink that answers `Pending` owes
    // exactly one `forget_bond_outcome()` other than `Pending` afterwards.
    virtual MeshSinkResult forget_bond() = 0;

    // The answer to the `Pending` forget-bond, or `Pending` while the operation
    // is still running. `Accepted` here -- and nowhere else -- means the bond is
    // actually gone.
    //
    // Consuming: an outcome is reported once. The bridge sends one terminal
    // response per request, so a second reader would either send a second one
    // or hand a stale answer to the next request.
    //
    // No default implementation. A sink that never answers `Pending` still has
    // to say so, by writing `return MeshSinkResult::Pending;` and meaning it: a
    // default would reach every test double by inheritance, and the value it
    // returned would be either a spurious success or a spurious failure for
    // whichever sink forgot to override it.
    virtual MeshSinkResult forget_bond_outcome() = 0;
    virtual MeshSinkResult send(const std::uint8_t peer_prefix[6],
                                const char* text, std::size_t text_length,
                                std::int64_t utc_seconds) = 0;
    virtual MeshSinkResult send_room(const std::uint8_t room[32],
                                     const char* password, std::size_t password_length,
                                     const char* text, std::size_t text_length,
                                     std::int64_t utc_seconds) = 0;
};

class Bridge {
public:
    // Where responses go. A raw callback rather than std::function: this
    // compiles for a part where the allocation behind a std::function is not
    // welcome, and the two arguments cost nothing to thread through.
    using Emit = void (*)(void* ctx, const std::uint8_t* payload, std::size_t length);

    Bridge(core::InputQueue& queue, core::InputState& state, ScreenSource& source,
           std::uint8_t* frame_buffer, std::size_t frame_capacity,
           TimeSink* time_sink = nullptr, MeshSink* mesh_sink = nullptr,
           BridgeLimits limits = BridgeLimits{});

    // Handles one de-framed message. Every request produces exactly one
    // terminal response, including a typed error -- ADR-0005 section 4: never
    // ignored.
    //
    // One request sends its response from `tick` instead of from here:
    // MeshForgetBond, whose answer is not known until another task has deleted
    // the bond (#378). It is still exactly one, against the same req_id, and
    // still bounded -- see `kForgetBondDeadlineMs` in bridge.cpp. A host that
    // calls `handle` and never calls `tick` will wait forever for that one
    // opcode and no other.
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

    // ### Thread affinity, for all of the above and below
    //
    // **One thread owns a `Bridge`.** `handle`, `pump`, `tick` and
    // `on_disconnect` all mutate `state_`, `out_`, `stats_` and the transfer
    // cursor, and none of them locks anything. On the desktop that is the
    // simulator's single loop and the question does not arise. On a device it
    // is a constraint: the transport's task and the interface's task are not
    // the same task, and a `Bridge` reached from both needs a queue in front of
    // it rather than a mutex inside it -- a mutex would put the interface's
    // task to sleep waiting on a transfer, which is the thing `pump` exists to
    // prevent. T-114 owns making that choice; this comment exists so it is a
    // choice rather than a discovery.
    //
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
    TimeSink*         time_sink_ = nullptr;
    MeshSink*         mesh_sink_ = nullptr;

    std::uint8_t* frame_buffer_   = nullptr;
    std::size_t   frame_capacity_ = 0;

    BridgeLimits limits_{};
    Stats  stats_{};

    // The one forget-bond whose answer has not arrived. One slot, not a table:
    // the sink behind it has one slot too (`ForgetBondOperation`), so a second
    // request is refused here rather than queued into a correlation this cannot
    // make. #344's rule against a lifecycle framework for one bootstrap reads
    // the same way for one operation.
    struct PendingForget {
        bool          active     = false;
        std::uint16_t req_id     = 0;
        std::uint32_t started_ms = 0;
    } forget_{};

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
