#include "attadipa/link/session_owner.h"

namespace attadipa::link {
namespace {

// Position within a session's life. Only used to work out which transitions a
// worker has not yet been told about, so it has to agree with the order the
// owner enforces and with nothing else.
int rank(SessionPhase phase)
{
    switch (phase) {
    case SessionPhase::Arriving: return 0;
    case SessionPhase::Ready:    return 1;
    case SessionPhase::Ended:    return 2;
    }
    return 2;
}

}  // namespace

void SessionOwner::clear_session()
{
    state_.connection = kNoSessionHandle;
    state_.mtu = 0;
    state_.service_start = 0;
    state_.service_end = 0;
    state_.rx_handle = 0;
    state_.tx_handle = 0;
    state_.cccd_handle = 0;
    state_.write_in_flight = false;
}

bool SessionOwner::live(std::uint32_t generation) const
{
    return generation != 0 && generation == state_.generation &&
           state_.phase != SessionPhase::Ended;
}

void SessionOwner::stack_ready()
{
    state_.stack_readies += 1;
    state_.transitions += 1;
}

void SessionOwner::fault()
{
    state_.fault_phase = state_.phase;
    state_.faults += 1;
    state_.transitions += 1;
}

std::uint32_t SessionOwner::peer_arriving()
{
    if (state_.phase != SessionPhase::Ended) ended(state_.generation);
    state_.generation += 1;
    // Zero is the "not ours" answer everywhere else, so it is never a
    // generation. Thirty-two bits of sessions is not a wrap anyone will see,
    // and a wrap that reused zero would make every stale callback look current.
    if (state_.generation == 0) state_.generation = 1;
    state_.phase = SessionPhase::Arriving;
    state_.reached_ready = false;
    clear_session();
    state_.transitions += 1;
    return state_.generation;
}

bool SessionOwner::ended(std::uint32_t generation)
{
    if (!live(generation)) return false;
    state_.phase = SessionPhase::Ended;
    clear_session();
    state_.transitions += 1;
    return true;
}

void SessionOwner::ended()
{
    (void)ended(state_.generation);
}

bool SessionOwner::connected(std::uint32_t generation, std::uint16_t connection)
{
    if (!live(generation) || state_.phase != SessionPhase::Arriving) return false;
    state_.connection = connection;
    return true;
}

bool SessionOwner::set_mtu(std::uint32_t generation, std::uint16_t mtu)
{
    if (!live(generation)) return false;
    state_.mtu = mtu;
    return true;
}

bool SessionOwner::set_service_range(std::uint32_t generation,
                                     std::uint16_t start, std::uint16_t end)
{
    if (!live(generation)) return false;
    state_.service_start = start;
    state_.service_end = end;
    return true;
}

bool SessionOwner::set_rx_handle(std::uint32_t generation, std::uint16_t handle)
{
    if (!live(generation)) return false;
    state_.rx_handle = handle;
    return true;
}

bool SessionOwner::set_tx_handle(std::uint32_t generation, std::uint16_t handle)
{
    if (!live(generation)) return false;
    state_.tx_handle = handle;
    return true;
}

bool SessionOwner::set_cccd_handle(std::uint32_t generation, std::uint16_t handle)
{
    if (!live(generation)) return false;
    state_.cccd_handle = handle;
    return true;
}

bool SessionOwner::ready(std::uint32_t generation)
{
    if (!live(generation) || state_.phase != SessionPhase::Arriving) return false;
    state_.phase = SessionPhase::Ready;
    state_.reached_ready = true;
    state_.transitions += 1;
    return true;
}

bool SessionOwner::write_submitted(std::uint32_t generation)
{
    if (!live(generation) || state_.write_in_flight) return false;
    state_.write_in_flight = true;
    return true;
}

bool SessionOwner::write_completed(std::uint32_t generation,
                                   std::int32_t result)
{
    if (!live(generation) || !state_.write_in_flight) return false;
    state_.write_in_flight = false;
    state_.write_result = result;
    state_.write_generation = generation;
    state_.write_completions += 1;
    return true;
}

void SessionOwner::note_coalesced(std::uint32_t transitions)
{
    state_.coalesced_lifecycle += transitions;
}

SessionMark mark_of(const SessionSnapshot& current)
{
    SessionMark mark{};
    mark.generation = current.generation;
    mark.transitions = current.transitions;
    mark.faults = current.faults;
    mark.write_completions = current.write_completions;
    mark.phase = current.phase;
    mark.stack_readies = current.stack_readies;
    return mark;
}

SessionCatchUp reconcile(const SessionMark& applied,
                         const SessionSnapshot& current)
{
    SessionCatchUp out{};
    const auto emit = [&out](SessionStep step) {
        if (out.count < kMaxCatchUpSteps) {
            out.steps[out.count] = step;
            out.count += 1;
        }
    };

    if (current.stack_readies != applied.stack_readies) emit(SessionStep::StackReady);

    // Faults coalesce to one. A second fault before the worker has run adds
    // nothing the link model can act on — it is already faulted — and the count
    // that would be lost is reported as `coalesced` rather than silently.
    //
    // Where the one fault goes is decided by the phase it was raised in, and it
    // is not a detail: the `Disconnected` step ends by re-beginning the link so
    // a reconnect can establish, and that is what clears `Faulted`. A fault
    // raised while the session was live happened *before* that end and is meant
    // to be cleared by it. A fault raised once the session had already ended —
    // a scan that would not start, an address that would not configure —
    // happened after, and replaying it first lets the same catch-up erase it.
    const bool faulted = current.faults != applied.faults;
    const bool fault_outlives_session =
        faulted && current.fault_phase == SessionPhase::Ended;
    if (faulted && !fault_outlives_session) emit(SessionStep::Fault);

    // Where in the current generation the worker already is. A generation it
    // has never heard of starts before `Arriving`, which is what -1 means.
    int from = -1;
    if (applied.generation == current.generation) {
        from = rank(applied.phase);
    } else if (applied.phase != SessionPhase::Ended) {
        // The session the worker last knew about is gone and it was never told.
        // Any session between that one and this one is folded into this single
        // disconnect: the link model's only interest in a session it never
        // learned had begun is that it is over.
        emit(SessionStep::Disconnected);
    }

    const int to = rank(current.phase);
    if (from < 0) emit(SessionStep::PeerArriving);
    // `Ready` is replayed only for a generation that actually reached it. An
    // established session that ended before the worker ran is replayed in full;
    // one that never established is not turned into one, because a `Ready` the
    // link never had is the more expensive of the two mistakes.
    if (from < 1 && to >= 1 && current.reached_ready) emit(SessionStep::Ready);
    if (from < 2 && to >= 2) emit(SessionStep::Disconnected);

    if (fault_outlives_session) emit(SessionStep::Fault);

    if (current.write_completions != applied.write_completions) {
        // Only the worker submits, and it submits one at a time, so there is at
        // most one completion outstanding and no result can be overwritten
        // before it is read.
        out.write_completed = true;
        out.write_result = current.write_result;
        out.write_generation = current.write_generation;
    }

    const std::uint32_t happened = current.transitions - applied.transitions;
    if (happened > out.count) out.coalesced = happened - out.count;
    return out;
}

const char* to_string(SessionPhase phase)
{
    switch (phase) {
    case SessionPhase::Arriving: return "arriving";
    case SessionPhase::Ready:    return "ready";
    case SessionPhase::Ended:    return "ended";
    }
    return "unknown";
}

const char* to_string(SessionStep step)
{
    switch (step) {
    case SessionStep::StackReady:   return "stack-ready";
    case SessionStep::Fault:        return "fault";
    case SessionStep::PeerArriving: return "peer-arriving";
    case SessionStep::Ready:        return "ready";
    case SessionStep::Disconnected: return "disconnected";
    }
    return "unknown";
}

}  // namespace attadipa::link
