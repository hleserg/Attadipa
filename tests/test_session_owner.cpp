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

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "attadipa/link/session_owner.h"

namespace {

using attadipa::link::kMaxCatchUpSteps;
using attadipa::link::kNoSessionHandle;
using attadipa::link::mark_of;
using attadipa::link::reconcile;
using attadipa::link::SessionCatchUp;
using attadipa::link::SessionMark;
using attadipa::link::SessionOwner;
using attadipa::link::SessionPhase;
using attadipa::link::SessionStep;

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
    a_write_result_carries_the_generation_that_produced_it();
    a_write_result_reaches_the_worker_without_a_queue();
    two_tasks_cannot_tear_a_session_or_lose_a_transition();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("session owner: all checks passed\n");
    return 0;
}
