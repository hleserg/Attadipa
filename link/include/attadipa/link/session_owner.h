#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// One owner for a transport session, and one way to catch up on what was
// missed.
//
// `LinkState` next door answers "is the link up". This answers the question
// underneath it, which cost issue #317: *who is allowed to say so*. On the
// ESP32-S3 the NimBLE host runs in its own FreeRTOS task and the worker that
// drives the node protocol runs in another, on either core. Before this file
// existed, both of them read and wrote the same ten plain globals — connection
// handle, three GATT handles, MTU, a `gatt_ready` flag and a `write_in_flight`
// flag — with nothing between them. A disconnect callback could clear the
// connection handle between the worker's `gatt_ready` check and its write, and
// a write completion belonging to a dead connection could clear the in-flight
// flag of a live one.
//
// The fix is not a mutex per field. Independent atomics remove each individual
// data race and leave the expensive one: a *torn session*, where the fields are
// individually current and collectively describe no connection that ever
// existed. So the session is one record, copied in one piece, and every fact
// recorded about it names the generation it belongs to. A generation is
// allocated once per session and never reused; anything that arrives naming an
// older one is not applied, which is what makes a late callback harmless rather
// than a write to a handle that has been reissued.
//
// The second half is `reconcile()`, and it is there because the lifecycle used
// to travel as entries in the same bounded queue as bulk data frames. Dropping
// a data frame under backpressure is deliberate and safe — the node re-sends a
// contact record and the sync boundary still arrives. Dropping the *disconnect*
// is neither: the worker never resets the session, the next connection is
// established against a link model that thinks the previous peer is still
// there, and the transport stalls until a reboot. Here the lifecycle is not a
// message at all. It is state, and a worker that missed three transitions asks
// what happened rather than being told; `reconcile()` gives it the ordered list
// of what it still owes the link model, so a full queue can cost latency and
// can no longer cost a transition.
//
// Nothing in this header knows what BLE is. A connection handle is whatever
// small integer the transport uses to name a live link, and the file compiles
// and is tested on a host with no radio. The decision and its rejected
// alternatives are docs/adr/0015-transport-session-ownership.md.
//
// `LinkState::epoch()` is the same idea one layer up, and the two are not
// duplicates. That one stamps what the *link model* must discard on reconnect
// (ADR-0005 §5) and is owned entirely by the worker; this one is the number two
// execution contexts compare, and only it can be checked inside a callback.

namespace attadipa::link {

// BLE's `BLE_HS_CONN_HANDLE_NONE` happens to be this, and the one file that
// knows what NimBLE is static_asserts that they agree rather than assuming it.
inline constexpr std::uint16_t kNoSessionHandle = 0xFFFF;

// A session runs Arriving -> [Ready] -> Ended, once, and then the next session
// gets the next generation. The phase never moves backwards inside a
// generation, which is what lets a starved worker reconstruct what it missed
// from two numbers.
//
// `Ended` is also the state before anything has ever happened, at generation 0.
// That is not a special case to work around: a worker whose mark says
// (generation 0, Ended) is owed exactly nothing, which is true.
enum class SessionPhase : std::uint8_t {
    Arriving = 0,  // an advertisement was accepted; connecting and discovering
    Ready    = 1,  // the transport is usable and frames may be written
    Ended    = 2,  // over. Everything stamped with this generation is stale.
};

// The whole record, copied out in one piece under whatever lock the platform
// has. Reading a field from here rather than from the owner is the difference
// between a coherent session and six separate ones.
struct SessionSnapshot {
    std::uint32_t generation = 0;

    // Every lifecycle change ever recorded — stack ready, fault, arriving,
    // ready, ended. Compared against the worker's mark it says how many
    // transitions happened, and `reconcile()` subtracts the ones it replayed to
    // report how many were folded together.
    std::uint32_t transitions = 0;

    std::uint32_t faults = 0;

    // How many times the radio stack has synchronised, not whether it ever
    // did. A stack that resets and re-syncs raises a second edge here and the
    // worker is owed a second `StackReady` for it; a bool made the resync
    // invisible and nothing started scanning again. `stack_readies != 0` is
    // the "is the stack up" predicate it replaces.
    std::uint32_t stack_readies = 0;

    std::uint32_t write_completions = 0;
    std::int32_t  write_result = 0;

    // The generation whose write produced `write_result`. A completion outlives
    // its session -- the worker may not run again until the connection is gone
    // -- and acting on a stale one paces a transmitter that no longer exists,
    // or tears down whichever connection inherited the handle since.
    std::uint32_t write_generation = 0;

    // Diagnostics, carried here so one locked read answers every question.
    std::uint32_t dropped_frames = 0;
    std::uint32_t coalesced_lifecycle = 0;

    SessionPhase  phase = SessionPhase::Ended;

    // The phase the session was in when the last fault was raised, which is
    // what orders `Fault` against the session steps in `reconcile()`. The
    // reconnect that the `Disconnected` step performs is what clears a fault,
    // so a fault raised while a session was live is replayed before that
    // session's end and is meant to be cleared, and one raised after the
    // session had already ended is replayed after it and must survive.
    SessionPhase  fault_phase = SessionPhase::Ended;

    std::uint16_t connection = kNoSessionHandle;
    std::uint16_t mtu = 0;
    std::uint16_t service_start = 0;
    std::uint16_t service_end = 0;
    std::uint16_t rx_handle = 0;
    std::uint16_t tx_handle = 0;
    std::uint16_t cccd_handle = 0;
    bool write_in_flight = false;

    // Whether *this* generation ever reached `Ready`. A session that came and
    // went while the worker was starved is replayed faithfully because of this
    // bit, and a session that never established is not replayed as one — which
    // is the difference between an accurate session count and a `Ready` the
    // link never had.
    bool reached_ready = false;
};

// What the worker has already told the link model about. It is the worker's
// private business and lives in the worker's own stack, not in the record.
struct SessionMark {
    std::uint32_t generation = 0;
    std::uint32_t transitions = 0;
    std::uint32_t faults = 0;
    std::uint32_t write_completions = 0;
    std::uint32_t stack_readies = 0;
    SessionPhase  phase = SessionPhase::Ended;
};

// What the worker still owes the link model, in the order it must be said.
enum class SessionStep : std::uint8_t {
    StackReady,    // the radio stack synchronised: begin, and start scanning
    Fault,         // the transport itself failed
    PeerArriving,  // a session began
    Ready,         // it was established
    Disconnected,  // it ended
};

// Six is the ceiling and it is reachable: stack ready, a fault, the end of the
// session the worker last saw, and a whole further session that arrived,
// established and ended before the worker ran again.
inline constexpr std::size_t kMaxCatchUpSteps = 6;

struct SessionCatchUp {
    std::array<SessionStep, kMaxCatchUpSteps> steps{};
    std::uint8_t count = 0;

    // A write finished and the worker has not accounted for it. Separate from
    // the steps because it is not a lifecycle transition: it does not move the
    // link model, it releases the transmit slot and reports a status.
    bool write_completed = false;
    std::int32_t write_result = 0;
    std::uint32_t write_generation = 0;

    // Transitions that happened but are not individually replayable — the
    // second of two faults, a session the worker never learned had begun. Zero
    // on a worker that is keeping up, which is what makes it worth publishing.
    std::uint32_t coalesced = 0;
};

// The single writer. Not thread-safe by itself and deliberately so: it has no
// idea what a critical section is on the platform it is running on. The caller
// wraps every call in one lock — the *same* lock — and the guarantee this class
// offers is that no sequence of calls, in any order, from any number of
// contexts, can produce a torn session or let a stale generation write a live
// one.
class SessionOwner {
public:
    // ---- lifecycle, recorded by whichever context notices it first --------

    // The radio stack synchronised. Counted, not latched: a worker that never
    // received the notification still finds it here, and a stack that reset and
    // synchronised again is a second edge rather than a no-op.
    void stack_ready();

    // The transport itself failed. Not stamped with a generation, because the
    // two callers that matter — a stack reset and an address-configuration
    // failure — belong to no session. A caller that *does* hold a generation
    // checks `live()` first and does not report a fault for a session that has
    // already been replaced.
    void fault();

    // Begin the next session and return its generation. A generation is never
    // reused and never zero, so zero is a safe "not ours" answer everywhere.
    // A session still live when this is called is ended first, so the phase
    // inside a generation stays monotone whatever the transport does.
    std::uint32_t peer_arriving();

    // End a named session. Ignores any generation that is not the live one,
    // which is how a disconnect callback for a connection that has already been
    // replaced fails to tear down its replacement.
    bool ended(std::uint32_t generation);

    // End whatever is live, for the caller that has no handle to name — a
    // connect attempt that failed before a connection existed, or a local stop.
    void ended();

    // ---- facts about one session, each naming the session it belongs to ----

    bool connected(std::uint32_t generation, std::uint16_t connection);
    bool set_mtu(std::uint32_t generation, std::uint16_t mtu);
    bool set_service_range(std::uint32_t generation, std::uint16_t start,
                           std::uint16_t end);
    bool set_rx_handle(std::uint32_t generation, std::uint16_t handle);
    bool set_tx_handle(std::uint32_t generation, std::uint16_t handle);
    bool set_cccd_handle(std::uint32_t generation, std::uint16_t handle);
    bool ready(std::uint32_t generation);

    // Claim the transmit slot. False means there is nothing to write to — the
    // session is gone, or a write is already outstanding. Claiming *before* the
    // transport call is deliberate: a completion can arrive before the call
    // that caused it has returned.
    bool write_submitted(std::uint32_t generation);

    // Release it. Only the generation that claimed the slot can release it, so
    // a completion belonging to a connection that has since been torn down
    // cannot hand a live session's slot away — the failure that leaves a
    // transport permanently unable to send.
    bool write_completed(std::uint32_t generation, std::int32_t result);

    // ---- counters, published rather than inferred -------------------------

    // A data frame the provider never saw: no room in the queue, or the session
    // it belonged to had ended before the worker reached it. Backpressure and
    // staleness, not defects — and the count is how anyone would ever know.
    void frame_dropped() { state_.dropped_frames += 1; }
    void note_coalesced(std::uint32_t transitions);

    // ---- reads ------------------------------------------------------------

    SessionSnapshot snapshot() const { return state_; }
    std::uint32_t generation() const { return state_.generation; }
    std::uint16_t connection() const { return state_.connection; }

    // Whether this generation is the one that owns the transport right now.
    bool live(std::uint32_t generation) const;

private:
    void clear_session();

    SessionSnapshot state_{};
};

// The mark a worker holds once it has applied everything in the snapshot.
SessionMark mark_of(const SessionSnapshot& current);

// What `applied` still owes, given where the session actually is. Pure: no
// state, no allocation, no ordering assumptions beyond the ones the owner
// enforces. This is the whole of the lifecycle-delivery guarantee, and it is
// why the lifecycle no longer travels through a queue that is allowed to drop.
SessionCatchUp reconcile(const SessionMark& applied,
                         const SessionSnapshot& current);

const char* to_string(SessionPhase phase);
const char* to_string(SessionStep step);

}  // namespace attadipa::link
