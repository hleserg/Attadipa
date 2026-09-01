// The session-ownership seam behind issue #317.
//
// The defect was two FreeRTOS tasks writing one set of plain globals with
// nothing between them, and a lifecycle that travelled as entries in the same
// bounded queue as bulk data — so a contact burst could drop the disconnect and
// leave the transport unable to send until a reboot. Neither half can be tested
// on a host by staring at a BLE stack, which is exactly why the ownership rule
// and the catch-up rule were lifted out of firmware/main/meshcore_ble.cpp into
// link/session_owner.{h,cpp} where a test can drive them.
//
// What is proven here is what the seam claims: a generation that is no longer
// live cannot write, fault, or release the transmit slot of the one that is;
// and a worker that missed any number of transitions is told the truth about
// where the session got to, in an order a link model accepts.
//
// What is *not* proven here is anything about a radio. The stress case at the
// bottom runs the two roles in two real threads behind one mutex, which is the
// same discipline the firmware applies with a portMUX — it will catch a hidden
// unsynchronised read inside the class, and it says nothing about NimBLE
// callback timing on the board. The hardware stress this issue asks for is
// NOT EXECUTED — HARDWARE REQUIRED.

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "attadipa/link/meshcore_companion.h"
#include "attadipa/link/session_owner.h"
#include "meshcore_boot.h"
#include "meshcore_node_pin.h"
#include "meshcore_bond_recovery.h"
#include "meshcore_forget_outcome.h"
#include "meshcore_write_outcome.h"

namespace {

using attadipa::link::kMaxCatchUpSteps;
using attadipa::link::kNoSessionHandle;
using attadipa::link::mark_of;
using attadipa::link::reconcile;
using attadipa::link::SessionCatchUp;
using attadipa::link::SessionMark;
using attadipa::link::SessionOwner;
using attadipa::link::SessionSnapshot;
using attadipa::link::SessionPhase;
using attadipa::link::SessionStep;
using attadipa::firmware::classify_write_failure;
using attadipa::firmware::kRefusedNodeCooldownMs;
using attadipa::firmware::PinnedSession;
using attadipa::firmware::PinOutcome;
using attadipa::firmware::refusal_active;
using attadipa::firmware::settle_node_pin;
using attadipa::firmware::WriteOutcome;

int failures = 0;

#define CHECK(value) check((value), #value, __LINE__)

void check(bool value, const char* expression, int line)
{
    if (!value) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}

std::vector<SessionStep> steps_of(const SessionCatchUp& catch_up)
{
    std::vector<SessionStep> out;
    for (std::uint8_t i = 0; i < catch_up.count; ++i) out.push_back(catch_up.steps[i]);
    return out;
}

// Bring a session all the way up, the way the GATT callbacks do: connect,
// negotiate, discover, subscribe. Returns the generation.
attadipa::core::MonotonicTime at(std::uint64_t ms)
{
    return attadipa::core::MonotonicTime{ms};
}

std::uint32_t establish(SessionOwner& owner, std::uint16_t connection,
                        std::uint16_t mtu = 247)
{
    const std::uint32_t generation = owner.peer_arriving();
    CHECK(owner.connected(generation, connection));
    CHECK(owner.set_mtu(generation, mtu));
    CHECK(owner.set_service_range(generation, 10, 20));
    CHECK(owner.set_rx_handle(generation, 12));
    CHECK(owner.set_tx_handle(generation, 14));
    CHECK(owner.set_cccd_handle(generation, 15));
    CHECK(owner.ready(generation));
    return generation;
}

// ---------------------------------------------------------------------------

// The stack coming up is the one lifecycle fact that legitimately repeats: the
// ESP32 host resets, re-syncs, and everything the worker did for the first sync
// -- begin the link, start scanning -- has to happen again. While this was a
// latched bool the second sync produced no step at all, so nothing ever
// rescanned and the transport stayed dark until a reboot.
void a_stack_that_resyncs_is_replayed_a_second_time()
{
    SessionOwner owner;
    SessionMark applied{};

    owner.stack_ready();
    CHECK(steps_of(reconcile(applied, owner.snapshot())) ==
          std::vector<SessionStep>{SessionStep::StackReady});
    applied = mark_of(owner.snapshot());
    CHECK(steps_of(reconcile(applied, owner.snapshot())).empty());

    owner.stack_ready();
    const auto again = reconcile(applied, owner.snapshot());
    CHECK(steps_of(again) == std::vector<SessionStep>{SessionStep::StackReady});
    CHECK(again.coalesced == 0);
}

// Where the fault goes in the replay decides whether it survives it. The
// `Disconnected` step re-begins the link so a reconnect can establish, and that
// is what clears `Faulted` -- so a fault raised after the session had already
// ended has to be replayed after the disconnect, or the same catch-up erases
// it and the link model reports a health it does not have.
void a_fault_raised_after_the_session_ended_is_replayed_after_it()
{
    SessionOwner owner;
    owner.stack_ready();
    const std::uint32_t generation = establish(owner, 7);
    SessionMark applied = mark_of(owner.snapshot());

    // The bench case: the write fails, the peer is still there, the disconnect
    // follows. That fault belongs before the end it preceded, and being cleared
    // by the reconnect is the point -- it is the 2026-08-27 fix.
    owner.fault();
    CHECK(owner.ended(generation));
    CHECK(steps_of(reconcile(applied, owner.snapshot())) ==
          (std::vector<SessionStep>{SessionStep::Fault, SessionStep::Disconnected}));

    // The other way round: the session ends first, and only then does
    // something fail with no session left to belong to -- a scan that would not
    // start, or the NimBLE host resetting under a live connection. Replaying
    // that fault first would let the disconnect's reconnect wipe it.
    SessionOwner second_owner;
    second_owner.stack_ready();
    const std::uint32_t second = establish(second_owner, 8);
    applied = mark_of(second_owner.snapshot());
    CHECK(second_owner.ended(second));
    second_owner.fault();
    CHECK(steps_of(reconcile(applied, second_owner.snapshot())) ==
          (std::vector<SessionStep>{SessionStep::Disconnected, SessionStep::Fault}));
}

// A completion the worker reads after the session that submitted it is gone
// says nothing about the session that is live now. It has to carry the
// generation it belongs to, or the caller pauses a transmitter that no longer
// exists and terminates a connection whose write never failed.
// `StackReady` is dispatched as begin-and-rescan, so it supersedes any fault
// raised before it. Replaying the older fault behind it faults the transport
// the same catch-up just re-armed, and only `begin()` clears `Faulted` -- so a
// scan that then finds the node connects, subscribes and carries nothing,
// with no local way out while the peer stays idle.
void a_fault_the_stack_resync_supersedes_is_not_replayed()
{
    SessionOwner owner;
    owner.stack_ready();
    const std::uint32_t generation = establish(owner, 7);
    const SessionMark applied = mark_of(owner.snapshot());

    // The host reset under a live session and NimBLE synchronised again before
    // the worker ran: both fit inside one write-pacing delay.
    CHECK(owner.ended(generation));
    owner.fault();
    owner.stack_ready();

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) ==
          (std::vector<SessionStep>{SessionStep::StackReady, SessionStep::Disconnected}));
    // Superseded, not silently dropped: three transitions happened and two were
    // replayed, and the difference is published rather than lost.
    CHECK(catch_up.coalesced == 1);
}

void a_write_result_carries_the_generation_that_produced_it()
{
    SessionOwner owner;
    const std::uint32_t first = establish(owner, 7);
    SessionMark applied = mark_of(owner.snapshot());

    CHECK(owner.write_submitted(first));
    CHECK(owner.write_completed(first, -1));

    // The worker did not run. The session ended and the next one established.
    CHECK(owner.ended(first));
    const std::uint32_t second = establish(owner, 8);
    CHECK(second != first);

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(catch_up.write_completed);
    CHECK(catch_up.write_result == -1);
    CHECK(catch_up.write_generation == first);
    CHECK(catch_up.write_generation != second);
    // Which is what the caller checks against: this result is not the live
    // session's, so nothing about the live session may be done on its account.
    CHECK(!owner.live(catch_up.write_generation));
}

void generations_are_allocated_once_and_are_never_zero()
{
    SessionOwner owner;
    CHECK(!owner.live(0));
    const std::uint32_t first = owner.peer_arriving();
    CHECK(first != 0);
    CHECK(owner.live(first));

    // A session still live when the next advertisement is accepted is ended
    // first, so a generation's phase never moves backwards.
    const std::uint32_t second = owner.peer_arriving();
    CHECK(second == first + 1);
    CHECK(!owner.live(first));
    CHECK(owner.live(second));
    CHECK(!owner.live(0));
}

// The issue's first interleaving: the peer goes away between the worker's
// guard and its write. Before the seam existed the worker had already read
// `gatt_ready`, `connection` and `rx_handle` as three separate globals and
// wrote to whatever was left of them.
void a_disconnect_between_the_guard_and_the_write_refuses_the_write()
{
    SessionOwner owner;
    const std::uint32_t generation = establish(owner, 7);

    const auto guarded = owner.snapshot();
    CHECK(guarded.phase == SessionPhase::Ready);
    CHECK(guarded.connection == 7);
    CHECK(guarded.rx_handle == 12);

    CHECK(owner.ended(generation));

    // Same snapshot, decision already made — and the commit refuses, because
    // the generation it was taken from no longer owns the transport.
    CHECK(!owner.write_submitted(guarded.generation));
    CHECK(!owner.snapshot().write_in_flight);
}

// The second: the write was submitted and the link dropped before it could
// finish. The slot must not be left claimed, or nothing is ever sent again.
void a_disconnect_after_the_submit_releases_the_slot()
{
    SessionOwner owner;
    const std::uint32_t generation = establish(owner, 7);
    CHECK(owner.write_submitted(generation));
    CHECK(owner.snapshot().write_in_flight);

    CHECK(owner.ended(generation));
    CHECK(!owner.snapshot().write_in_flight);

    // And the completion that arrives afterwards belongs to nobody.
    CHECK(!owner.write_completed(generation, 0));
}

// The third, and the one that produced a permanently stalled transport: a write
// completion from a connection that has since been replaced must not hand the
// live session's transmit slot away.
void a_stale_completion_cannot_release_a_live_sessions_slot()
{
    SessionOwner owner;
    const std::uint32_t first = establish(owner, 7);
    CHECK(owner.write_submitted(first));
    CHECK(owner.ended(first));

    const std::uint32_t second = establish(owner, 9);
    CHECK(owner.write_submitted(second));
    CHECK(owner.snapshot().write_in_flight);

    CHECK(!owner.write_completed(first, 0));
    CHECK(owner.snapshot().write_in_flight);
    CHECK(owner.snapshot().write_completions == 0);

    CHECK(owner.write_completed(second, 0));
    CHECK(!owner.snapshot().write_in_flight);
    CHECK(owner.snapshot().write_completions == 1);
}

// The fourth: a discovery or MTU callback from the previous connection lands
// after the next one has started, and must not overwrite its handles.
void a_previous_generation_cannot_write_the_current_ones_handles()
{
    SessionOwner owner;
    const std::uint32_t first = establish(owner, 7, 247);
    CHECK(owner.ended(first));

    const std::uint32_t second = owner.peer_arriving();
    CHECK(owner.connected(second, 9));
    CHECK(owner.set_mtu(second, 185));

    CHECK(!owner.set_mtu(first, 23));
    CHECK(!owner.set_rx_handle(first, 99));
    CHECK(!owner.set_tx_handle(first, 99));
    CHECK(!owner.set_cccd_handle(first, 99));
    CHECK(!owner.set_service_range(first, 1, 2));
    CHECK(!owner.ready(first));
    CHECK(!owner.connected(first, 11));

    const auto session = owner.snapshot();
    CHECK(session.generation == second);
    CHECK(session.mtu == 185);
    CHECK(session.rx_handle == 0);
    CHECK(session.tx_handle == 0);
    CHECK(session.cccd_handle == 0);
    CHECK(session.connection == 9);
    CHECK(session.phase == SessionPhase::Arriving);
}

// A session that ends leaves nothing behind for the next one to trip over.
void ending_a_session_clears_everything_stamped_with_it()
{
    SessionOwner owner;
    const std::uint32_t generation = establish(owner, 7);
    CHECK(owner.write_submitted(generation));
    CHECK(owner.ended(generation));

    const auto session = owner.snapshot();
    CHECK(session.phase == SessionPhase::Ended);
    CHECK(session.connection == kNoSessionHandle);
    CHECK(session.mtu == 0);
    CHECK(session.rx_handle == 0);
    CHECK(session.tx_handle == 0);
    CHECK(session.cccd_handle == 0);
    CHECK(session.service_start == 0);
    CHECK(session.service_end == 0);
    CHECK(!session.write_in_flight);

    // And ending it twice changes nothing, which duplicate disconnect callbacks
    // make ordinary rather than exotic.
    CHECK(!owner.ended(generation));
    CHECK(owner.snapshot().transitions == session.transitions);
}

// ---------------------------------------------------------------------------
// The catch-up. This is the half that replaces a lifecycle queue.

void a_worker_that_keeps_up_is_told_each_transition_once()
{
    SessionOwner owner;
    SessionMark applied{};

    CHECK(steps_of(reconcile(applied, owner.snapshot())).empty());

    owner.stack_ready();
    auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) == std::vector<SessionStep>{SessionStep::StackReady});
    CHECK(catch_up.coalesced == 0);
    applied = mark_of(owner.snapshot());

    const std::uint32_t generation = owner.peer_arriving();
    catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) == std::vector<SessionStep>{SessionStep::PeerArriving});
    applied = mark_of(owner.snapshot());

    CHECK(owner.ready(generation));
    catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) == std::vector<SessionStep>{SessionStep::Ready});
    applied = mark_of(owner.snapshot());

    CHECK(owner.ended(generation));
    catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) == std::vector<SessionStep>{SessionStep::Disconnected});
    CHECK(catch_up.coalesced == 0);
    applied = mark_of(owner.snapshot());

    CHECK(steps_of(reconcile(applied, owner.snapshot())).empty());
}

// The fifth interleaving, and the one the issue's second comment is about: a
// contact burst fills the data queue, sessions come and go, and the worker is
// still told exactly what it needs to reach the state the transport is in.
void a_starved_worker_is_told_where_the_session_actually_got_to()
{
    SessionOwner owner;
    SessionMark applied{};

    owner.stack_ready();
    const std::uint32_t first = establish(owner, 7);
    applied = mark_of(owner.snapshot());
    // The worker has now applied StackReady, PeerArriving and Ready.

    CHECK(owner.ended(first));
    const std::uint32_t second = establish(owner, 8);
    CHECK(owner.ended(second));
    const std::uint32_t third = establish(owner, 9);
    (void)third;

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) ==
          (std::vector<SessionStep>{SessionStep::Disconnected,
                                    SessionStep::PeerArriving,
                                    SessionStep::Ready}));
    // The middle session began, established and ended without the worker ever
    // hearing of it. Those three transitions are reported rather than lost.
    CHECK(catch_up.coalesced == 3);
    CHECK(catch_up.count == 3);
}

// A session that never established is not replayed as one. A `Ready` the link
// never had is the more expensive of the two possible mistakes: it is what
// makes a status lie about a transport that cannot carry anything.
void a_session_that_never_established_is_not_replayed_as_ready()
{
    SessionOwner owner;
    SessionMark applied{};

    const std::uint32_t generation = owner.peer_arriving();
    CHECK(owner.connected(generation, 7));
    CHECK(owner.ended(generation));

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) ==
          (std::vector<SessionStep>{SessionStep::PeerArriving,
                                    SessionStep::Disconnected}));
}

// A session that did establish and then ended before the worker ran is replayed
// in full, so the link model's session count stays honest.
void a_completed_session_is_replayed_in_full()
{
    SessionOwner owner;
    SessionMark applied{};

    const std::uint32_t generation = establish(owner, 7);
    CHECK(owner.ended(generation));

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) ==
          (std::vector<SessionStep>{SessionStep::PeerArriving,
                                    SessionStep::Ready,
                                    SessionStep::Disconnected}));
    CHECK(catch_up.coalesced == 0);
}

// Faults coalesce, and the count that would otherwise be lost is published.
void faults_coalesce_and_are_counted()
{
    SessionOwner owner;
    SessionMark applied{};

    owner.fault();
    owner.fault();
    owner.fault();

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(steps_of(catch_up) == std::vector<SessionStep>{SessionStep::Fault});
    CHECK(catch_up.coalesced == 2);
}

// The ceiling is reachable, so it is asserted rather than assumed. An
// undersized array would drop a step in silence, which is the failure mode the
// whole file exists to remove.
void the_longest_possible_catch_up_still_fits()
{
    SessionOwner owner;
    const std::uint32_t first = establish(owner, 7);
    SessionMark applied = mark_of(owner.snapshot());
    // The worker knew about a live, established session and nothing else: it
    // has not seen the stack come up, because it has not run since before sync.
    applied.stack_readies = 0;
    applied.transitions = 0;

    owner.stack_ready();
    owner.fault();
    CHECK(owner.ended(first));
    const std::uint32_t second = establish(owner, 8);
    CHECK(owner.ended(second));

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(catch_up.count == 6);
    CHECK(catch_up.count <= kMaxCatchUpSteps);
    CHECK(steps_of(catch_up) ==
          (std::vector<SessionStep>{SessionStep::StackReady,
                                    SessionStep::Fault,
                                    SessionStep::Disconnected,
                                    SessionStep::PeerArriving,
                                    SessionStep::Ready,
                                    SessionStep::Disconnected}));
}

void a_write_result_reaches_the_worker_without_a_queue()
{
    SessionOwner owner;
    SessionMark applied{};
    const std::uint32_t generation = establish(owner, 7);
    applied = mark_of(owner.snapshot());

    CHECK(owner.write_submitted(generation));
    CHECK(owner.write_completed(generation, -7));

    const auto catch_up = reconcile(applied, owner.snapshot());
    CHECK(catch_up.write_completed);
    CHECK(catch_up.write_result == -7);
    CHECK(catch_up.count == 0);

    applied = mark_of(owner.snapshot());
    CHECK(!reconcile(applied, owner.snapshot()).write_completed);
}

// ---------------------------------------------------------------------------
// The two roles, in two real threads, behind the one lock the firmware uses.
//
// Every assertion here holds under any interleaving, so it cannot be flaky: an
// illegal step order or a torn snapshot is a failure whenever it happens, and
// the end state is checked only after both threads have stopped.

// What the worker's link model would do with the steps it is given. It refuses
// anything a real link model would refuse, which is the point.
class LinkModel {
public:
    enum class Phase { Down, Up, Established };

    void apply(SessionStep step)
    {
        switch (step) {
        case SessionStep::StackReady:
        case SessionStep::Fault:
            return;
        case SessionStep::PeerArriving:
            if (phase_ != Phase::Down) ++illegal;
            phase_ = Phase::Up;
            ++arrivals;
            return;
        case SessionStep::Ready:
            if (phase_ != Phase::Up) ++illegal;
            phase_ = Phase::Established;
            ++establishments;
            return;
        case SessionStep::Disconnected:
            if (phase_ == Phase::Down) ++illegal;
            phase_ = Phase::Down;
            ++departures;
            return;
        }
    }

    Phase phase() const { return phase_; }

    int illegal = 0;
    int arrivals = 0;
    int establishments = 0;
    int departures = 0;

private:
    Phase phase_ = Phase::Down;
};

// A transmit that fails immediately fails for one of two reasons, and #335 is
// what happened when they were treated alike: an ordinary peer disappearance
// took the same permanent path as a broken radio stack, and the transport could
// not reconnect until the device was configured again. `pump_tx()` cannot be
// host-compiled, so the rule it switches on lives in a header that can be --
// this test compiles the same `meshcore_write_outcome.h` the firmware does.
void the_transmit_classifier_names_only_a_broken_subsystem()
{
    // Session-scoped, every one of them. BLE_HS_ENOTCONN is the peer going away
    // in the window `pump_tx()` documents; BLE_HS_ENOMEM is the host out of
    // mbufs, which this transport has twice ruled is backpressure rather than a
    // broken subsystem; EINVAL and EMSGSIZE are a malformed request. None is a
    // reason to stop scanning for the rest of the boot.
    CHECK(classify_write_failure(7) == WriteOutcome::Recycle);   // ENOTCONN
    CHECK(classify_write_failure(6) == WriteOutcome::Recycle);   // ENOMEM
    CHECK(classify_write_failure(3) == WriteOutcome::Recycle);   // EINVAL
    CHECK(classify_write_failure(4) == WriteOutcome::Recycle);   // EMSGSIZE
    CHECK(classify_write_failure(13) == WriteOutcome::Recycle);  // ETIMEOUT
    CHECK(classify_write_failure(-7) == WriteOutcome::Recycle);

    // The stack itself, and only the stack itself. `meshcore_ble.cpp`
    // static_asserts both against BLE_HS_EOS and BLE_HS_ECONTROLLER rather than
    // repeating the numbers, so an upstream renumber is a build failure.
    CHECK(attadipa::firmware::kWriteStatusStackFailure == 11);
    CHECK(attadipa::firmware::kWriteStatusControllerFailure == 12);
    CHECK(classify_write_failure(11) == WriteOutcome::Fault);
    CHECK(classify_write_failure(12) == WriteOutcome::Fault);
}

// And what each answer costs, in the record the worker actually reads. The two
// arms differ in exactly one thing: whether `fault()` is raised. That one step
// is what reaches the link model and refuses the session that follows, so it is
// the whole of #335 expressed at this seam.
void a_recycled_write_ends_one_generation_a_fatal_one_faults_the_transport()
{
    // Recycle. The completion is recorded, the generation ends once, and
    // nothing in the catch-up tells the link model the transport is broken.
    {
        SessionOwner owner;
        SessionMark applied{};
        const std::uint32_t first = establish(owner, 7);
        applied = mark_of(owner.snapshot());

        CHECK(owner.write_submitted(first));
        CHECK(owner.write_completed(first, 7));
        CHECK(classify_write_failure(7) == WriteOutcome::Recycle);
        owner.frame_dropped();  // the frame pump_tx() popped is gone; counted

        auto catch_up = reconcile(applied, owner.snapshot());
        CHECK(catch_up.write_completed);
        CHECK(catch_up.write_result == 7);
        CHECK(catch_up.write_generation == first);
        CHECK(catch_up.count == 0);
        CHECK(owner.snapshot().dropped_frames == 1);
        applied = mark_of(owner.snapshot());

        // handle_write_result() terminates; the GAP disconnect ends it. Once.
        CHECK(owner.ended(first));
        CHECK(!owner.ended(first));
        catch_up = reconcile(applied, owner.snapshot());
        CHECK(steps_of(catch_up) ==
              std::vector<SessionStep>{SessionStep::Disconnected});
        applied = mark_of(owner.snapshot());

        // start_scan() runs, the node advertises again, and the next session
        // establishes -- which is the recovery #335 says was unreachable.
        const std::uint32_t second = establish(owner, 9);
        CHECK(second != first);
        catch_up = reconcile(applied, owner.snapshot());
        CHECK(steps_of(catch_up) ==
              (std::vector<SessionStep>{SessionStep::PeerArriving,
                                        SessionStep::Ready}));
        CHECK(owner.snapshot().phase == SessionPhase::Ready);

        // The dead session's immediate result arriving late cannot recycle the
        // live one: the generation guard refuses it before the classifier is
        // ever consulted.
        CHECK(owner.write_submitted(second));
        CHECK(!owner.write_completed(first, 7));
        CHECK(owner.snapshot().write_in_flight);
        CHECK(owner.snapshot().write_completions == 1);
    }

    // Fault. `disconnect_fault()` raises fault(), and the Fault step the worker
    // then owes the link model is what suppresses the reconnect. Fail-closed,
    // and visible -- not a retry loop.
    {
        SessionOwner owner;
        SessionMark applied{};
        const std::uint32_t generation = establish(owner, 7);
        applied = mark_of(owner.snapshot());

        CHECK(owner.write_submitted(generation));
        CHECK(owner.write_completed(generation, 11));  // BLE_HS_EOS
        CHECK(classify_write_failure(11) == WriteOutcome::Fault);
        owner.fault();
        CHECK(owner.ended(generation));

        const auto catch_up = reconcile(applied, owner.snapshot());
        CHECK(catch_up.write_completed);
        CHECK(catch_up.write_result == 11);
        CHECK(steps_of(catch_up) ==
              (std::vector<SessionStep>{SessionStep::Fault,
                                        SessionStep::Disconnected}));
    }
}

void two_tasks_cannot_tear_a_session_or_lose_a_transition()
{
    constexpr int kSessions = 400;

    SessionOwner owner;
    std::mutex lock;
    std::atomic_bool host_done{false};
    std::atomic_int torn{0};
    std::atomic_int double_claims{0};

    std::thread host([&] {
        for (int i = 0; i < kSessions; ++i) {
            std::uint32_t generation = 0;
            {
                std::lock_guard<std::mutex> held(lock);
                generation = owner.peer_arriving();
            }
            {
                std::lock_guard<std::mutex> held(lock);
                (void)owner.connected(generation, static_cast<std::uint16_t>(i & 0x7F));
            }
            {
                std::lock_guard<std::mutex> held(lock);
                (void)owner.set_mtu(generation, 247);
            }
            {
                std::lock_guard<std::mutex> held(lock);
                (void)owner.set_rx_handle(generation, 12);
            }
            // Only now is the session usable. A worker that sees Ready must see
            // all four of the facts above, or the snapshot is torn.
            {
                std::lock_guard<std::mutex> held(lock);
                (void)owner.ready(generation);
            }
            if ((i % 3) == 0) {
                std::lock_guard<std::mutex> held(lock);
                (void)owner.write_completed(generation, 0);
            }
            if ((i % 7) == 0) {
                std::lock_guard<std::mutex> held(lock);
                owner.fault();
            }
            {
                std::lock_guard<std::mutex> held(lock);
                (void)owner.ended(generation);
            }
        }
        host_done.store(true);
    });

    LinkModel model;
    SessionMark applied{};
    std::uint32_t outstanding = 0;

    const auto pass = [&] {
        attadipa::link::SessionSnapshot session{};
        SessionCatchUp catch_up{};
        {
            std::lock_guard<std::mutex> held(lock);
            session = owner.snapshot();
            catch_up = reconcile(applied, session);
            owner.note_coalesced(catch_up.coalesced);
        }
        applied = mark_of(session);

        if (session.phase == SessionPhase::Ready &&
            (session.connection == kNoSessionHandle || session.mtu == 0 ||
             session.rx_handle == 0)) {
            torn.fetch_add(1);
        }

        for (std::uint8_t i = 0; i < catch_up.count; ++i) {
            model.apply(catch_up.steps[i]);
            if (catch_up.steps[i] == SessionStep::Disconnected) outstanding = 0;
        }
        if (catch_up.write_completed) outstanding = 0;

        // Submit against the snapshot, commit against the record — the same
        // conditional commit pump_tx() makes.
        if (session.phase == SessionPhase::Ready && !session.write_in_flight) {
            bool claimed = false;
            {
                std::lock_guard<std::mutex> held(lock);
                claimed = owner.write_submitted(session.generation);
            }
            if (claimed) {
                if (outstanding != 0) double_claims.fetch_add(1);
                outstanding = session.generation;
            }
        }
    };

    while (!host_done.load()) pass();
    host.join();
    // One last pass with nothing else running: the model must now agree with
    // the record, whatever it missed on the way.
    pass();

    CHECK(model.illegal == 0);
    CHECK(torn.load() == 0);
    CHECK(double_claims.load() == 0);
    CHECK(model.phase() == LinkModel::Phase::Down);
    CHECK(owner.snapshot().phase == SessionPhase::Ended);
    CHECK(model.arrivals == model.departures);
    CHECK(model.establishments <= model.arrivals);
    // The worker cannot have been told about more sessions than there were.
    CHECK(model.arrivals <= kSessions);
    CHECK(model.arrivals >= 1);
}

// ---------------------------------------------------------------------------
// #325 -- which bond an owner may forget.
//
// The seam is firmware/main/meshcore_bond_recovery.h, compiled here rather than
// copied. What it has to hold is that a radio peer can provoke the event but
// never choose the consequence.

void nothing_is_forgotten_until_a_conflict_is_recorded()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    BondIdentity taken{};
    CHECK(!recovery.recovery_required());
    CHECK(!recovery.take_forget(taken));
    CHECK(!taken.valid);
}

void a_forget_consumes_the_record_so_a_second_one_is_refused()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    BondIdentity peer{};
    peer.address = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    peer.type = 0;
    peer.valid = true;
    recovery.record(peer);

    BondIdentity taken{};
    CHECK(recovery.take_forget(taken));
    CHECK(taken.valid);
    CHECK(taken.address == peer.address);
    CHECK(taken.type == peer.type);

    // One conflict, one deletion, one armed pairing. Without a fresh conflict
    // the operation is refused, so it can never walk the bond store.
    BondIdentity again{};
    CHECK(!recovery.recovery_required());
    CHECK(!recovery.take_forget(again));
    CHECK(!again.valid);
}

void a_second_peer_cannot_displace_the_bond_the_owner_was_told_about()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    BondIdentity legitimate{};
    legitimate.address = {1, 1, 1, 1, 1, 1};
    legitimate.valid = true;
    BondIdentity intruder{};
    intruder.address = {9, 9, 9, 9, 9, 9};
    intruder.valid = true;

    recovery.record(legitimate);
    // Any peer in range can send a Pairing Request. If the newest one won, it
    // would be aiming the owner's next forget at a bond of its choosing.
    recovery.record(intruder);

    BondIdentity taken{};
    CHECK(recovery.take_forget(taken));
    CHECK(taken.address == legitimate.address);
}

void an_unidentifiable_peer_offers_no_bond_to_forget()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    // ble_gap_conn_find() failed, so there is no identity address. Fail closed:
    // the transport stays faulted and no bond is nominated for deletion.
    recovery.record(BondIdentity{});
    CHECK(!recovery.recovery_required());
}

void encryption_coming_up_retires_the_conflict()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    BondIdentity peer{};
    peer.address = {2, 2, 2, 2, 2, 2};
    peer.valid = true;
    recovery.record(peer);
    CHECK(recovery.recovery_required());

    // The fresh pairing after a forget completes, or the peer produced the key
    // after all. Either way the stale bond is no longer why the link is down,
    // so it stops being offered as a reason.
    recovery.pairing_succeeded();
    CHECK(!recovery.recovery_required());
    BondIdentity taken{};
    CHECK(!recovery.take_forget(taken));
}

void a_key_missing_failure_records_the_bond_and_shares_the_one_slot()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    BondIdentity node{};
    node.address = {3, 3, 3, 3, 3, 3};
    node.valid = true;

    // This is the trigger the watch actually reaches. It is a central, and
    // NimBLE raises BLE_GAP_EVENT_REPEAT_PAIRING only from ble_sm_pair_req_rx()
    // -- the inbound Pairing Request. What a central sees when the node has
    // been reflashed is the encryption refused with `PIN or Key Missing`.
    recovery.record(node);
    CHECK(recovery.recovery_required());

    // Both entry points write the same single slot, so a peer that can provoke
    // the other event cannot add a second nomination beside this one.
    BondIdentity intruder{};
    intruder.address = {9, 9, 9, 9, 9, 9};
    intruder.valid = true;
    recovery.record(intruder);

    BondIdentity taken{};
    CHECK(recovery.take_forget(taken));
    CHECK(taken.address == node.address);
}

void a_refused_deletion_puts_the_bond_back_so_the_owner_can_retry()
{
    using attadipa::firmware::BondIdentity;
    using attadipa::firmware::BondRecovery;
    BondRecovery recovery;
    BondIdentity peer{};
    peer.address = {4, 4, 4, 4, 4, 4};
    peer.valid = true;
    recovery.record(peer);

    BondIdentity taken{};
    CHECK(recovery.take_forget(taken));
    CHECK(!recovery.recovery_required());

    // ble_store_util_delete_peer() answered non-zero -- a damaged or full NVS,
    // which is exactly the state #325's erase-the-NVS workaround implies. The
    // operator was already told the bond was gone, because the wire answer went
    // out when the request was accepted. Putting the record back is what makes
    // running the command again the fix rather than a refusal.
    recovery.record(taken);
    BondIdentity retried{};
    CHECK(recovery.take_forget(retried));
    CHECK(retried.address == peer.address);
}

// ---------------------------------------------------------------------------
// #378 -- who may answer a forget-bond, and when.
//
// The seam is firmware/main/meshcore_forget_outcome.h, compiled here rather
// than copied: the request task and the mesh worker are two tasks, and the rule
// that keeps one answer per request is the reservation below.

void a_forget_bond_is_answered_only_after_the_store_says_the_bond_is_gone()
{
    using attadipa::firmware::ForgetBondOperation;
    using attadipa::firmware::ForgetOutcome;
    ForgetBondOperation op;

    CHECK(op.take() == ForgetOutcome::Idle);
    CHECK(op.reserve());
    // The whole of #378 in one line: between here and complete() the old code
    // had already told the operator the bond was gone.
    CHECK(op.take() == ForgetOutcome::InFlight);

    op.complete(ForgetOutcome::Deleted);
    CHECK(op.take() == ForgetOutcome::Deleted);
    // Read once. A second reader would either send a second terminal response
    // or hand this answer to the request after it.
    CHECK(op.take() == ForgetOutcome::Idle);
}

void a_deletion_the_store_refused_is_not_a_success_and_never_becomes_one()
{
    using attadipa::firmware::ForgetBondOperation;
    using attadipa::firmware::ForgetOutcome;
    ForgetBondOperation op;

    CHECK(op.reserve());
    op.complete(ForgetOutcome::Refused);
    CHECK(op.take() == ForgetOutcome::Refused);
    CHECK(op.take() == ForgetOutcome::Idle);
}

void a_record_that_was_already_gone_is_not_a_refused_deletion()
{
    using attadipa::firmware::ForgetBondOperation;
    using attadipa::firmware::ForgetOutcome;
    ForgetBondOperation op;

    CHECK(op.reserve());
    // The worker found no conflict record. Nothing was deleted and no bond is
    // left behind, which is a different sentence for the operator than a store
    // that refused -- and it shared `Refused`'s value until #381, so
    // `mesh-forget-bond` answered "the bond is still on the watch" about a bond
    // that had never been recorded.
    op.complete(ForgetOutcome::Nothing);
    CHECK(op.take() == ForgetOutcome::Nothing);
    CHECK(op.take() == ForgetOutcome::Idle);

    // Consumed like any other terminal answer, so the slot is free again.
    CHECK(op.reserve());
}

void two_forget_bonds_cannot_both_be_answered_by_one_deletion()
{
    using attadipa::firmware::ForgetBondOperation;
    ForgetBondOperation op;

    CHECK(op.reserve());
    // The second request arrives while the worker still has the first. Refused
    // where the caller can be told, rather than queued behind a correlation
    // that does not exist: one deletion, one answer.
    CHECK(!op.reserve());
}

void a_request_that_was_never_queued_gives_the_slot_back()
{
    using attadipa::firmware::ForgetBondOperation;
    using attadipa::firmware::ForgetOutcome;
    ForgetBondOperation op;

    CHECK(op.reserve());
    // post() refused -- a full event queue. The worker will never see this one,
    // so nothing will ever complete it, and a slot left in flight behind a
    // request that does not exist refuses every later forget-bond forever.
    op.release();
    CHECK(op.take() == ForgetOutcome::Idle);
    CHECK(op.reserve());
}

void an_answer_nobody_collected_does_not_wedge_the_next_request()
{
    using attadipa::firmware::ForgetBondOperation;
    using attadipa::firmware::ForgetOutcome;
    ForgetBondOperation op;

    CHECK(op.reserve());
    op.complete(ForgetOutcome::Deleted);
    // The host disconnected: the answer is sitting here and the request it
    // belonged to is gone. If that refused the next reservation, forget-bond
    // would be dead until the watch rebooted -- a worse failure than the one
    // this file fixes.
    CHECK(op.reserve());
    // AND THE RESERVATION REPLACED THE ANSWER, rather than merely being
    // permitted. A `reserve()` that overwrote only from `Idle` would return
    // true here and leave `Deleted` in the slot, so the next `tick` would
    // answer this request with the previous one's outcome -- #378 again, and
    // with the whole of this change in place. Asserting `reserve()` alone
    // cannot see that, because the `complete()` below overwrites the stale
    // answer before anything reads it.
    CHECK(op.take() == ForgetOutcome::InFlight);
    op.complete(ForgetOutcome::Refused);
    CHECK(op.take() == ForgetOutcome::Refused);
}

// ---------------------------------------------------------------------------
// #344 -- the bootstrap is a transaction.
//
// firmware/main/meshcore_boot.h holds the acquisition order and the rollback,
// and start_meshcore_ble() instantiates that same template with the real
// ESP-IDF calls. What is driven here is therefore the production sequence with
// failures injected, not a second copy of it.

struct RecordingBootOps {
    bool port_ok = true;
    bool queue_ok = true;
    bool worker_ok = true;
    std::vector<std::string> calls;

    bool port_init()
    {
        calls.push_back("port_init");
        return port_ok;
    }
    void configure_host() { calls.push_back("configure_host"); }
    bool queue_create()
    {
        calls.push_back("queue_create");
        return queue_ok;
    }
    bool worker_create()
    {
        calls.push_back("worker_create");
        return worker_ok;
    }
    void host_start() { calls.push_back("host_start"); }
    void queue_delete() { calls.push_back("queue_delete"); }
    void port_deinit() { calls.push_back("port_deinit"); }

    bool did(const char* what) const
    {
        return std::find(calls.begin(), calls.end(), std::string(what)) !=
               calls.end();
    }
};

void a_successful_boot_starts_the_host_last()
{
    using attadipa::firmware::BootResult;
    RecordingBootOps ops;
    CHECK(attadipa::firmware::boot_meshcore(ops) == BootResult::Ok);
    // The worker is created before the host task and after everything it
    // reads. Nothing is released.
    CHECK(ops.calls == std::vector<std::string>({"port_init", "configure_host",
                                                 "queue_create",
                                                 "worker_create",
                                                 "host_start"}));
}

void a_failed_port_init_publishes_nothing_and_releases_nothing()
{
    using attadipa::firmware::BootResult;
    RecordingBootOps ops;
    ops.port_ok = false;
    CHECK(attadipa::firmware::boot_meshcore(ops) == BootResult::PortInitFailed);
    // This is the case the issue is about: the queue and the worker used to be
    // created *before* nimble_port_init, so this failure left a task polling a
    // queue nothing would ever post to for the rest of the boot.
    CHECK(!ops.did("queue_create"));
    CHECK(!ops.did("worker_create"));
    CHECK(!ops.did("host_start"));
    // Nothing was acquired, so nimble_port_deinit would be undoing a failure.
    CHECK(!ops.did("port_deinit"));
}

void a_failed_queue_gives_nimble_back()
{
    using attadipa::firmware::BootResult;
    RecordingBootOps ops;
    ops.queue_ok = false;
    CHECK(attadipa::firmware::boot_meshcore(ops) == BootResult::QueueFailed);
    CHECK(!ops.did("worker_create"));
    CHECK(!ops.did("host_start"));
    CHECK(ops.did("port_deinit"));
    // There is no queue to delete: the one thing that could have been acquired
    // was not.
    CHECK(!ops.did("queue_delete"));
}

void a_failed_worker_releases_in_reverse_acquisition_order()
{
    using attadipa::firmware::BootResult;
    RecordingBootOps ops;
    ops.worker_ok = false;
    CHECK(attadipa::firmware::boot_meshcore(ops) == BootResult::WorkerFailed);
    CHECK(!ops.did("host_start"));
    const auto queue = std::find(ops.calls.begin(), ops.calls.end(),
                                 std::string("queue_delete"));
    const auto port = std::find(ops.calls.begin(), ops.calls.end(),
                                std::string("port_deinit"));
    CHECK(queue != ops.calls.end());
    CHECK(port != ops.calls.end());
    // The queue is what the worker would have read, so it goes back first.
    CHECK(queue < port);
}

// ---------------------------------------------------------------------------
// #345 -- a reconfiguration cannot be applied to the session it reconfigures.
//
// `reconcile()`, `SessionOwner` and `MeshCoreCompanion` below are the shipping
// ones. What is modelled is the worker's dispatch, because that lives in an
// ESP-IDF-only translation unit: `apply_steps` is the step-to-provider mapping
// of `meshcore_ble.cpp`'s `apply_lifecycle()`, and `configure` is its
// `EventKind::Configure` arm. The physical case -- a real `mesh-configure` over
// a live BLE connection -- is NOT EXECUTED - HARDWARE REQUIRED.

void apply_steps(SessionMark& applied, SessionOwner& owner,
                 attadipa::link::MeshCoreCompanion& provider,
                 bool reconnect_allowed)
{
    const SessionSnapshot session = owner.snapshot();
    const SessionCatchUp catch_up = reconcile(applied, session);
    applied = attadipa::link::mark_of(session);
    for (std::uint8_t i = 0; i < catch_up.count; ++i) {
        switch (catch_up.steps[i]) {
        case SessionStep::StackReady:  provider.begin(at(0)); break;
        case SessionStep::Fault:       provider.fault(at(0)); break;
        case SessionStep::PeerArriving: provider.peer_arriving(at(0)); break;
        case SessionStep::Ready:       provider.connected(at(0)); break;
        case SessionStep::Disconnected:
            provider.disconnected(at(0));
            if (reconnect_allowed) provider.begin(at(0));
            break;
        }
    }
}

// Did the provider queue a CMD_APP_START? It is the first frame of the
// Companion handshake and the thing that stops arriving when the provider is
// reset behind a live session -- opcode 1, sixteen bytes
// (tests/test_meshcore_companion.cpp, connect_and_handshake).
bool took_app_start(attadipa::link::MeshCoreCompanion& provider)
{
    attadipa::link::MeshCoreFrame frame{};
    return provider.next_tx(frame) && frame.size == 16 && frame.bytes[0] == 1;
}

void a_reconfigure_over_a_live_session_recycles_it_rather_than_stranding_the_provider()
{
    SessionOwner owner;
    attadipa::link::MeshCoreCompanion provider;
    SessionMark applied{};

    owner.stack_ready();
    apply_steps(applied, owner, provider, true);
    const std::uint32_t first = establish(owner, 7);
    apply_steps(applied, owner, provider, true);
    CHECK(took_app_start(provider));
    CHECK(owner.snapshot().phase == SessionPhase::Ready);

    // The shipping Configure arm: a live connection is ended, and nothing else
    // is touched. provider.begin() used to run here instead, which is the
    // defect -- it is asserted below.
    CHECK(owner.snapshot().connection != attadipa::link::kNoSessionHandle);
    CHECK(owner.ended(first));

    // The disconnect path, then a fresh generation, exactly as the radio would
    // produce them.
    apply_steps(applied, owner, provider, true);
    const std::uint32_t second = establish(owner, 8);
    CHECK(second != first);
    apply_steps(applied, owner, provider, true);

    // One CMD_APP_START on the new session, and the two machines agree.
    CHECK(took_app_start(provider));
    CHECK(owner.snapshot().phase == SessionPhase::Ready);
    CHECK(provider.status().transport == attadipa::core::TransportPhase::Ready);
}

void resetting_the_provider_under_a_live_session_is_what_wedged_it()
{
    SessionOwner owner;
    attadipa::link::MeshCoreCompanion provider;
    SessionMark applied{};

    owner.stack_ready();
    apply_steps(applied, owner, provider, true);
    (void)establish(owner, 7);
    apply_steps(applied, owner, provider, true);
    CHECK(took_app_start(provider));

    // What Configure used to do, unconditionally.
    provider.begin(at(1));

    // The owner never moved, so reconcile has no Ready to replay: the provider
    // is never told it is connected again and no CMD_APP_START is queued. This
    // is the state neither machine allows on its own -- transport Ready,
    // provider back to Attached -- and it lasted until something disconnected.
    apply_steps(applied, owner, provider, true);
    CHECK(owner.snapshot().phase == SessionPhase::Ready);
    CHECK(provider.status().transport != attadipa::core::TransportPhase::Ready);
    CHECK(!took_app_start(provider));
}

// ---------------------------------------------------------------------------
// Which node this watch talks to (#304).
//
// The rule lives in firmware/main/meshcore_node_pin.h for the reason the boot
// sequence above does: `settle_node_identity()` is ESP-IDF-only, so the four
// outcomes, the cooldown and the millisecond wrap are unreachable from a host
// unless the decision itself is compiled here. These cases drive the shipping
// header, not a copy of it.
//
// What is *not* proven here is that a real node is refused on a real radio.
// That is NOT EXECUTED — HARDWARE REQUIRED.

struct FakePin {
    // What the provider would answer.
    bool has_node = true;
    bool has_pin = false;
    bool wrong = false;
    attadipa::core::MeshPeerId node{};
    attadipa::core::MeshPeerId pin{};
    // What the transport would answer, and what it recorded.
    PinnedSession live{};
    bool store_ok = true;
    bool stored = false;
    bool adopted = false;
    std::uint64_t cooled = 0;
    int disconnected = -1;
    int session_reads = 0;

    bool node_id(attadipa::core::MeshPeerId& out) const
    {
        if (!has_node) return false;
        out = node;
        return true;
    }
    bool pinned(attadipa::core::MeshPeerId& out) const
    {
        if (!has_pin) return false;
        out = pin;
        return true;
    }
    bool wrong_node() const { return wrong; }
    bool store(const attadipa::core::MeshPeerId&)
    {
        stored = store_ok;
        return store_ok;
    }
    void adopt(const attadipa::core::MeshPeerId&) { adopted = true; }
    PinnedSession session()
    {
        ++session_reads;
        return live;
    }
    void cool_down(std::uint64_t addr) { cooled = addr; }
    void disconnect(std::uint16_t connection)
    {
        disconnected = static_cast<int>(connection);
    }
};

attadipa::core::MeshPeerId node_key_of(std::uint8_t first)
{
    attadipa::core::MeshPeerId id{};
    id.public_key[0] = first;
    return id;
}

void an_unpinned_watch_adopts_the_node_that_identified_itself()
{
    FakePin ops;
    ops.node = node_key_of(0x5C);
    attadipa::core::MeshPeerId seen{};
    attadipa::core::MeshPeerId expected{};
    CHECK(settle_node_pin(ops, seen, expected) == PinOutcome::Adopted);
    CHECK(ops.stored);
    CHECK(ops.adopted);
    // Adoption asks nothing about the connection: the key was genuinely read,
    // and a watch that forgot it because the link dropped afterwards would be
    // unpinned for no reason.
    CHECK(ops.session_reads == 0);
    CHECK(ops.cooled == 0);
    CHECK(ops.disconnected == -1);
    CHECK(seen == ops.node);
}

void a_pin_that_could_not_be_written_leaves_the_watch_unpinned()
{
    FakePin ops;
    ops.node = node_key_of(0x5C);
    ops.store_ok = false;
    attadipa::core::MeshPeerId seen{};
    attadipa::core::MeshPeerId expected{};
    CHECK(settle_node_pin(ops, seen, expected) == PinOutcome::AdoptFailed);
    CHECK(!ops.stored);
    // Not handed to the provider either. A watch that took the pin in RAM only
    // would report a pin it loses on the next boot, and would stop adopting.
    CHECK(!ops.adopted);
}

void the_pinned_node_is_left_alone()
{
    FakePin ops;
    ops.node = node_key_of(0x5C);
    ops.has_pin = true;
    ops.pin = node_key_of(0x5C);
    ops.wrong = false;
    attadipa::core::MeshPeerId seen{};
    attadipa::core::MeshPeerId expected{};
    CHECK(settle_node_pin(ops, seen, expected) == PinOutcome::Pinned);
    CHECK(!ops.stored);
    CHECK(!ops.adopted);
    CHECK(ops.disconnected == -1);
    CHECK(expected == ops.pin);
}

void another_node_is_cooled_down_and_disconnected_and_nothing_is_written()
{
    FakePin ops;
    ops.node = node_key_of(0x04);
    ops.has_pin = true;
    ops.pin = node_key_of(0x5C);
    ops.wrong = true;
    ops.live = {true, 0x0100F7F3336B9B61ULL, 42, true};
    attadipa::core::MeshPeerId seen{};
    attadipa::core::MeshPeerId expected{};
    CHECK(settle_node_pin(ops, seen, expected) == PinOutcome::Refused);
    CHECK(ops.cooled == 0x0100F7F3336B9B61ULL);
    CHECK(ops.disconnected == 42);
    // This path writes no pin and deletes no bond; what it does is stop the
    // traffic, which is the whole of #304's second half. It does not follow
    // that the bond survived -- where a passkey is armed the pairing that
    // preceded this decision has already evicted the pinned node's bond. See
    // `firmware/main/meshcore_node_pin.h` -- "IT DOES NOT UNDO THE BOND".
    CHECK(!ops.stored);
    CHECK(!ops.adopted);
}

void a_refusal_for_a_session_that_is_over_touches_nothing()
{
    // The defect this exists to keep out: the wrong node drops right after its
    // SELF_INFO, the pinned node connects, and the worker then cools down the
    // *pinned* node's address and terminates its connection.
    FakePin ops;
    ops.node = node_key_of(0x04);
    ops.has_pin = true;
    ops.pin = node_key_of(0x5C);
    ops.wrong = true;
    ops.live = {false, 0x0100AAAAAAAAAAAAULL, 7, true};
    attadipa::core::MeshPeerId seen{};
    attadipa::core::MeshPeerId expected{};
    CHECK(settle_node_pin(ops, seen, expected) == PinOutcome::SessionOver);
    CHECK(ops.cooled == 0);
    CHECK(ops.disconnected == -1);
}

void a_handshake_with_no_key_settles_nothing()
{
    FakePin ops;
    ops.has_node = false;
    attadipa::core::MeshPeerId seen{};
    attadipa::core::MeshPeerId expected{};
    CHECK(settle_node_pin(ops, seen, expected) == PinOutcome::NoIdentity);
    CHECK(!ops.stored);
    CHECK(ops.session_reads == 0);
}

void a_refusal_outlives_a_millisecond_wrap_by_expiring_rather_than_by_lasting()
{
    // esp_timer's millisecond count wraps every 49 days. A refusal recorded
    // just before the wrap must expire just after it, not run for another 49.
    const std::uint32_t before_wrap = 0xFFFFFFF0U;
    const std::uint32_t until = before_wrap + kRefusedNodeCooldownMs;  // wraps
    CHECK(until < before_wrap);
    CHECK(refusal_active(until, before_wrap));
    CHECK(refusal_active(until, 0x00000005U));
    CHECK(!refusal_active(until, until));
    CHECK(!refusal_active(until, until + 1U));
    // And the ordinary case, away from the boundary, both ways.
    CHECK(refusal_active(60000U, 1U));
    CHECK(!refusal_active(60000U, 60001U));
    // A counter that has run for 25 days is not "before" a refusal recorded at
    // 5 seconds: half the range apart is the one distance the sign cannot tell
    // apart, and the rule is that a stale refusal expires rather than sticks.
    CHECK(!refusal_active(5000U, 5000U + 0x80000000U));
}

void two_wrong_nodes_in_range_stop_the_loop_one_slot_could_not()
{
    // The regression this exists to keep out. `refused_addr` was one slot,
    // overwritten by every refusal: with two wrong nodes in range, refusing B
    // freed A and refusing A freed B, so the scan walked A -> B -> A with no
    // gap. Each turn is a fresh connection and a fresh pairing, and with one
    // bond slot the pinned node's bond dies on the first turn and cannot
    // survive the loop -- worse than the behaviour the pin replaced.
    using attadipa::firmware::RefusalState;
    using attadipa::firmware::after_refusal;
    using attadipa::firmware::connect_is_refused;

    constexpr std::uint64_t kA = 0x0100AAAAAAAAAAAAULL;
    constexpr std::uint64_t kB = 0x0100BBBBBBBBBBBBULL;
    constexpr std::uint64_t kPinned = 0x0100CCCCCCCCCCCCULL;

    // One stranger: skipped by address, and the pinned node is not delayed at
    // all. This is the case a global hold-off would have broken.
    const RefusalState one = after_refusal(RefusalState{}, kA, 1000U);
    CHECK(connect_is_refused(one, kA, 1000U));
    CHECK(!connect_is_refused(one, kPinned, 1000U));

    // Two: the second refusal inside the first one's window holds everything.
    const RefusalState two = after_refusal(one, kB, 2000U);
    CHECK(connect_is_refused(two, kA, 2000U));
    CHECK(connect_is_refused(two, kB, 2000U));
    CHECK(connect_is_refused(two, kPinned, 2000U));
    // A third stranger raises nothing further: it is never connected, so it is
    // never refused.
    CHECK(connect_is_refused(two, 0x0100DDDDDDDDDDDDULL, 3000U));
    // And it is a hold, not a stop: one cooldown from the second refusal.
    CHECK(connect_is_refused(two, kPinned, 2000U + kRefusedNodeCooldownMs - 1U));
    CHECK(!connect_is_refused(two, kPinned, 2000U + kRefusedNodeCooldownMs));
    CHECK(!connect_is_refused(two, kA, 2000U + kRefusedNodeCooldownMs));

    // Refusing the same node twice is not two nodes. It re-arms its own slot
    // and leaves the pinned node free -- which is what happens when an address
    // does stay put and the node keeps advertising.
    const RefusalState again = after_refusal(one, kA, 2000U);
    CHECK(!connect_is_refused(again, kPinned, 2000U));
    CHECK(connect_is_refused(again, kA, 2000U + kRefusedNodeCooldownMs - 1U));
    CHECK(!connect_is_refused(again, kA, 2000U + kRefusedNodeCooldownMs));

    // Nor is a second address after the first has lapsed: the slot moves, the
    // floor stays down. Otherwise a rotating address would hold the radio shut
    // for good, one refusal at a time.
    const std::uint32_t after = 1000U + kRefusedNodeCooldownMs;
    const RefusalState later = after_refusal(one, kB, after);
    CHECK(connect_is_refused(later, kB, after));
    CHECK(!connect_is_refused(later, kPinned, after));
}

}  // namespace

int main()
{
    generations_are_allocated_once_and_are_never_zero();
    a_disconnect_between_the_guard_and_the_write_refuses_the_write();
    a_disconnect_after_the_submit_releases_the_slot();
    a_stale_completion_cannot_release_a_live_sessions_slot();
    a_previous_generation_cannot_write_the_current_ones_handles();
    ending_a_session_clears_everything_stamped_with_it();
    a_worker_that_keeps_up_is_told_each_transition_once();
    a_starved_worker_is_told_where_the_session_actually_got_to();
    a_session_that_never_established_is_not_replayed_as_ready();
    a_completed_session_is_replayed_in_full();
    faults_coalesce_and_are_counted();
    the_longest_possible_catch_up_still_fits();
    a_stack_that_resyncs_is_replayed_a_second_time();
    a_fault_raised_after_the_session_ended_is_replayed_after_it();
    a_fault_the_stack_resync_supersedes_is_not_replayed();
    a_write_result_carries_the_generation_that_produced_it();
    a_write_result_reaches_the_worker_without_a_queue();
    the_transmit_classifier_names_only_a_broken_subsystem();
    a_recycled_write_ends_one_generation_a_fatal_one_faults_the_transport();
    two_tasks_cannot_tear_a_session_or_lose_a_transition();
    nothing_is_forgotten_until_a_conflict_is_recorded();
    a_forget_consumes_the_record_so_a_second_one_is_refused();
    a_second_peer_cannot_displace_the_bond_the_owner_was_told_about();
    an_unidentifiable_peer_offers_no_bond_to_forget();
    encryption_coming_up_retires_the_conflict();
    a_key_missing_failure_records_the_bond_and_shares_the_one_slot();
    a_refused_deletion_puts_the_bond_back_so_the_owner_can_retry();
    a_forget_bond_is_answered_only_after_the_store_says_the_bond_is_gone();
    a_deletion_the_store_refused_is_not_a_success_and_never_becomes_one();
    a_record_that_was_already_gone_is_not_a_refused_deletion();
    two_forget_bonds_cannot_both_be_answered_by_one_deletion();
    a_request_that_was_never_queued_gives_the_slot_back();
    an_answer_nobody_collected_does_not_wedge_the_next_request();
    a_successful_boot_starts_the_host_last();
    a_failed_port_init_publishes_nothing_and_releases_nothing();
    a_failed_queue_gives_nimble_back();
    a_failed_worker_releases_in_reverse_acquisition_order();
    a_reconfigure_over_a_live_session_recycles_it_rather_than_stranding_the_provider();
    resetting_the_provider_under_a_live_session_is_what_wedged_it();
    an_unpinned_watch_adopts_the_node_that_identified_itself();
    a_pin_that_could_not_be_written_leaves_the_watch_unpinned();
    the_pinned_node_is_left_alone();
    another_node_is_cooled_down_and_disconnected_and_nothing_is_written();
    a_refusal_for_a_session_that_is_over_touches_nothing();
    a_handshake_with_no_key_settles_nothing();
    a_refusal_outlives_a_millisecond_wrap_by_expiring_rather_than_by_lasting();
    two_wrong_nodes_in_range_stop_the_loop_one_slot_could_not();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("session owner: all checks passed\n");
    return 0;
}
