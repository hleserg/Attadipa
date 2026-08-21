#include <cstdio>
#include <cstring>

#include "firefly/link/frame_codec.h"
#include "firefly/link/frame_queue.h"
#include "firefly/link/link_state.h"

// Host tests for the transport framing, the bounded queue above it, and the
// session state machine above that.
//
// The test list is not invented here. It is the owner's §6 — fragmented input,
// partial writes, a full queue, a disconnect mid-frame, a reconnect, a large
// payload, a malformed frame — one test function per item, named so that the
// coverage can be audited against the brief rather than taken on trust.
//
// The second half is different in kind: those tests exist because
// docs/upstream/meshcore-1.17-review.md records the exact defects in the
// upstream serial transport, verified at source. An over-long frame truncated
// to the maximum and delivered as if complete; a stream with no checksum and no
// resynchronisation; a connection predicate that returns true unconditionally
// with the comment "no way of knowing, so assume yes". Firefly does not inherit
// those, and these are the tests that say so rather than the paragraph that
// claims it.
//
// Nothing here touches USB, Bluetooth or a radio. It is the logic above them.

using namespace firefly;
using namespace firefly::link;

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

void check_phase(TransportPhase actual, TransportPhase expected, int line)
{
    if (actual != expected) {
        std::fprintf(stderr, "FAIL line %d: phase is %s, expected %s\n", line,
                     core::to_string(actual), core::to_string(expected));
        ++failures;
    }
}

#define CHECK_PHASE(actual, expected) check_phase((actual), (expected), __LINE__)

MonotonicTime at(std::uint64_t ms) { return MonotonicTime{ms}; }

// A payload with no accidental structure: every byte different from its
// neighbours, and containing the sync pattern on purpose so that a decoder
// which searches the payload for a header has somewhere to go wrong.
void fill(std::uint8_t* out, std::size_t length, std::uint8_t seed = 0)
{
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = static_cast<std::uint8_t>((i * 7u + seed) & 0xFFu);
    }
    if (length >= 4) {
        out[1] = kSync0;
        out[2] = kSync1;
    }
}

bool same(const std::uint8_t* a, const std::uint8_t* b, std::size_t length)
{
    return std::memcmp(a, b, length) == 0;
}

// ---------------------------------------------------------------------------
// The owner's §6 list, in order.

// A transport delivers whatever the hardware happened to have. One byte at a
// time is the worst case and it is not a rare one — it is what a USB CDC read
// returns when the host is polling faster than the device is writing.
void test_fragmented_input()
{
    std::uint8_t payload[64];
    fill(payload, sizeof payload);

    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);
    CHECK(encoded == sizeof payload + kOverheadBytes);

    Decoder decoder;
    std::uint8_t out[kMaxPayload];

    // Every byte on its own. Nothing may be delivered until the last one.
    for (std::size_t i = 0; i + 1 < encoded; ++i) {
        CHECK(decoder.push(&wire[i], 1) == 1);
        CHECK(decoder.next(out, sizeof out) == 0);
    }
    CHECK(decoder.push(&wire[encoded - 1], 1) == 1);
    CHECK(decoder.next(out, sizeof out) == sizeof payload);
    CHECK(same(out, payload, sizeof payload));
    CHECK(decoder.stats().frames == 1);
    CHECK(decoder.stats().bad_crc == 0);
}

// The mirror image: several frames arriving in one read, including a boundary
// that falls in the middle of a header.
void test_several_frames_in_one_read()
{
    std::uint8_t a[16], b[100], c[1];
    fill(a, sizeof a, 1);
    fill(b, sizeof b, 2);
    fill(c, sizeof c, 3);

    std::uint8_t wire[kMaxFrame * 3];
    std::size_t  used = 0;
    used += encode(a, sizeof a, wire + used, sizeof wire - used);
    used += encode(b, sizeof b, wire + used, sizeof wire - used);
    used += encode(c, sizeof c, wire + used, sizeof wire - used);

    Decoder decoder;
    std::uint8_t out[kMaxPayload];

    // Split at an awkward place: three bytes into the second frame's header.
    const std::size_t split = sizeof a + kOverheadBytes + 3;
    CHECK(decoder.push(wire, split) == split);
    CHECK(decoder.next(out, sizeof out) == sizeof a);
    CHECK(same(out, a, sizeof a));
    CHECK(decoder.next(out, sizeof out) == 0);

    CHECK(decoder.push(wire + split, used - split) == used - split);
    CHECK(decoder.next(out, sizeof out) == sizeof b);
    CHECK(same(out, b, sizeof b));
    CHECK(decoder.next(out, sizeof out) == sizeof c);
    CHECK(same(out, c, sizeof c));
    CHECK(decoder.stats().frames == 3);
}

// A write that the transport only partly accepted. The rule is whole-frame-or-
// nothing: half a frame on the wire is a resynchronisation for the peer, and a
// queue that reports success for a partial write has lied to the layer above.
void test_partial_writes()
{
    FrameQueue<> queue;
    std::uint8_t payload[32];
    fill(payload, sizeof payload);

    CHECK(queue.push(payload, sizeof payload));
    CHECK(queue.size() == 1);

    // A reader with nowhere to put it takes nothing, and does not consume the
    // entry either — the frame is still there for a caller with a real buffer.
    std::uint8_t small[8];
    CHECK(queue.pop(small, sizeof small) == 0);
    CHECK(queue.size() == 1);

    std::uint8_t out[kMaxPayload];
    CHECK(queue.pop(out, sizeof out) == sizeof payload);
    CHECK(same(out, payload, sizeof payload));
    CHECK(queue.empty());
}

// Bounded, and it says how much it lost. A queue that silently overwrites is a
// queue whose depth nobody can tune, because nobody can tell it was too small.
void test_queue_full()
{
    FrameQueue<4> queue;
    std::uint8_t payload[16];
    fill(payload, sizeof payload);

    for (int i = 0; i < 4; ++i) {
        CHECK(queue.push(payload, sizeof payload));
    }
    CHECK(queue.full());
    CHECK(!queue.writable());

    CHECK(!queue.push(payload, sizeof payload));
    CHECK(!queue.push(payload, sizeof payload));
    CHECK(queue.dropped() == 2);
    CHECK(queue.accepted() == 4);
    CHECK(queue.size() == 4);

    // Draining one makes room for exactly one.
    std::uint8_t out[kMaxPayload];
    CHECK(queue.pop(out, sizeof out) == sizeof payload);
    CHECK(queue.writable());
    CHECK(queue.push(payload, sizeof payload));
    CHECK(queue.full());
}

// The peer vanishes with half a frame in the buffer. The next session must not
// begin by decoding the tail of the last one against the head of the new — that
// is how a reconnect delivers a frame nobody ever sent.
void test_disconnect_during_a_frame()
{
    std::uint8_t payload[80];
    fill(payload, sizeof payload);

    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);

    Decoder decoder;
    CHECK(decoder.push(wire, encoded / 2) == encoded / 2);
    CHECK(decoder.buffered() > 0);

    decoder.reset();
    CHECK(decoder.buffered() == 0);

    // The rest of the old frame arrives after the reset. It is a fragment with
    // no header, so it must be discarded rather than assembled into anything.
    std::uint8_t out[kMaxPayload];
    decoder.push(wire + encoded / 2, encoded - encoded / 2);
    CHECK(decoder.next(out, sizeof out) == 0);

    // And a whole frame after that decodes normally.
    CHECK(decoder.push(wire, encoded) == encoded);
    CHECK(decoder.next(out, sizeof out) == sizeof payload);
}

// A reconnect is a new session, not a continuation. The epoch is what lets the
// layer above tell "the same peer, still there" from "a peer again", and it is
// the thing a protocol that keeps sequence numbers has to look at.
void test_reconnect_is_a_new_session()
{
    LinkState link(LinkState::Config{TransportKind::Usb, Millis{5000}, true});

    CHECK_PHASE(link.phase(), TransportPhase::Absent);
    CHECK(link.apply(LinkEvent::Attach, at(0)) == EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Attached);

    CHECK(link.apply(LinkEvent::PeerArriving, at(10)) == EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Connecting);
    CHECK(link.apply(LinkEvent::PeerEstablished, at(20)) == EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Ready);
    CHECK(link.ready());

    const std::uint32_t first_epoch = link.epoch();
    CHECK(link.sessions() == 1);

    link.apply(LinkEvent::PeerGone, at(1000), DisconnectReason::PeerClosed);
    CHECK(!link.ready());
    CHECK(link.last_disconnect() == DisconnectReason::PeerClosed);

    link.apply(LinkEvent::PeerArriving, at(2000));
    link.apply(LinkEvent::PeerEstablished, at(2100));
    CHECK(link.ready());
    CHECK(link.sessions() == 2);
    CHECK(link.epoch() != first_epoch);
}

// kMaxPayload exactly, and one byte past it. The boundary is where framing bugs
// live, and the size is not a magic number — it is the transport's own limit
// from link/include/firefly/link/frame_codec.h, which is where the rest of the
// system reads it too.
void test_large_payload()
{
    std::uint8_t payload[kMaxPayload];
    fill(payload, sizeof payload);

    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, kMaxPayload, wire, sizeof wire);
    CHECK(encoded == kMaxFrame);

    Decoder decoder;
    CHECK(decoder.push(wire, encoded) == encoded);
    std::uint8_t out[kMaxPayload];
    CHECK(decoder.next(out, sizeof out) == kMaxPayload);
    CHECK(same(out, payload, kMaxPayload));

    // One byte too many is refused at the encoder, and refused as a failure
    // rather than as a shorter frame.
    std::uint8_t oversize[kMaxPayload + 1];
    fill(oversize, sizeof oversize);
    std::uint8_t scratch[kMaxFrame * 2];
    CHECK(encode(oversize, sizeof oversize, scratch, sizeof scratch) == 0);

    // And a caller whose output buffer is too small gets nothing rather than a
    // truncated frame.
    CHECK(encode(payload, kMaxPayload, scratch, kMaxFrame - 1) == 0);
    FrameQueue<> queue;
    CHECK(!queue.push(oversize, sizeof oversize));
    CHECK(queue.malformed() == 1);
    CHECK(queue.accepted() == 0);
}

// Garbage in, silence out — and then recovery. A decoder that cannot resync is
// a decoder that one corrupt byte disables until the next reboot.
void test_malformed_frame()
{
    std::uint8_t payload[48];
    fill(payload, sizeof payload);
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);

    std::uint8_t out[kMaxPayload];

    // 1. A corrupted payload byte fails the checksum and is not delivered.
    {
        std::uint8_t corrupt[kMaxFrame];
        std::memcpy(corrupt, wire, encoded);
        corrupt[kHeaderBytes + 10] ^= 0xFFu;

        Decoder decoder;
        decoder.push(corrupt, encoded);
        CHECK(decoder.next(out, sizeof out) == 0);
        CHECK(decoder.stats().bad_crc == 1);
        CHECK(decoder.stats().frames == 0);
    }

    // 2. A header claiming an impossible length is rejected on the header
    //    rather than after waiting for bytes that will never come.
    {
        std::uint8_t lying[kMaxFrame];
        std::memcpy(lying, wire, encoded);
        lying[2] = 0xFFu;  // length field
        lying[3] = 0xFFu;

        Decoder decoder;
        decoder.push(lying, encoded);
        decoder.next(out, sizeof out);
        CHECK(decoder.stats().bad_length + decoder.stats().resyncs > 0);
        CHECK(decoder.stats().frames == 0);
    }

    // 3. Noise before a good frame. The frame still arrives, and the decoder
    //    counts what it threw away.
    {
        const std::uint8_t noise[] = {0x00, 0xF1, 0xF1, 0x5E, 0x01, 0xAA, 0xF1, 0x99};
        Decoder decoder;
        decoder.push(noise, sizeof noise);
        decoder.push(wire, encoded);
        CHECK(decoder.next(out, sizeof out) == sizeof payload);
        CHECK(same(out, payload, sizeof payload));
        CHECK(decoder.stats().resyncs > 0);
    }
}

// ---------------------------------------------------------------------------
// The defects docs/upstream/meshcore-1.17-review.md found upstream, held
// against our own implementation. Each of these is a bug that exists in code
// shipping today on these boards.

// Upstream truncates an over-long frame to the maximum and delivers it as if it
// were complete, which corrupts a message rather than dropping it — the failure
// mode a protocol cannot recover from, because the receiver believes it.
void test_an_over_long_frame_is_refused_not_truncated()
{
    // Hand-build a header claiming more than the maximum. The encoder will not
    // produce one, which is the point: this is what a peer running different
    // firmware, or a corrupted length field, puts on the wire.
    std::uint8_t frame[kMaxFrame + 64] = {};
    frame[0] = kSync0;
    frame[1] = kSync1;
    const std::uint16_t claimed = static_cast<std::uint16_t>(kMaxPayload + 32);
    frame[2] = static_cast<std::uint8_t>(claimed & 0xFFu);
    frame[3] = static_cast<std::uint8_t>(claimed >> 8);
    frame[4] = static_cast<std::uint8_t>(~(frame[2] ^ frame[3]));

    Decoder decoder;
    decoder.push(frame, sizeof frame);

    std::uint8_t out[kMaxPayload];
    const std::size_t delivered = decoder.next(out, sizeof out);
    CHECK(delivered == 0);
    CHECK(decoder.stats().frames == 0);
}

// Upstream's framing has no checksum at all, so a corrupted byte is delivered
// as data. Ours has CRC-16/CCITT, and this pins the polynomial rather than
// merely the presence of a field: a checksum that disagrees with the peer's is
// a link that never carries anything.
void test_the_checksum_is_the_one_we_said_it_was()
{
    // Two well-known vectors for CRC-16/CCITT-FALSE, init 0xFFFF.
    const std::uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK(crc16_ccitt(digits, sizeof digits) == 0x29B1);

    // A single byte, computed independently rather than read off this
    // implementation — a test that asks the code what the code says would pass
    // against any polynomial at all.
    const std::uint8_t a = 'A';
    CHECK(crc16_ccitt(&a, 1) == 0xB915);

    // And it is order-sensitive, which a sum is not. This is the property that
    // makes it worth the flash.
    const std::uint8_t forward[]  = {0x01, 0x02, 0x03, 0x04};
    const std::uint8_t backward[] = {0x04, 0x03, 0x02, 0x01};
    CHECK(crc16_ccitt(forward, 4) != crc16_ccitt(backward, 4));
}

// The upstream defect that started this: `isConnected()` returning true
// unconditionally, with the comment "no way of knowing, so assume yes". The
// owner's §6 says it in the other direction — physical presence and a ready
// session are different facts — so a phase that conflates them is the bug.
void test_attached_is_not_ready()
{
    LinkState link(LinkState::Config{TransportKind::Usb, Millis{5000}, true});

    link.apply(LinkEvent::Attach, at(0));
    CHECK_PHASE(link.phase(), TransportPhase::Attached);
    CHECK(!link.ready());   // enumerated, powered, and nobody is listening

    link.apply(LinkEvent::PeerArriving, at(1));
    CHECK(!link.ready());   // enumerating is not enumerated

    link.apply(LinkEvent::PeerEstablished, at(2));
    CHECK(link.ready());

    // A transport with no connection signal — a UART — reaches Ready by a
    // different route, and it is worth being precise about which. Not "ready
    // once attached": a UART with nothing on the other end is a powered pin.
    // Data is the proof, because on that transport data is the only proof
    // there is.
    LinkState uart(LinkState::Config{TransportKind::Uart, Millis{5000}, false});
    uart.apply(LinkEvent::Attach, at(0));
    CHECK(!uart.ready());
    CHECK(uart.apply(LinkEvent::PeerData, at(1)) == EventOutcome::Applied);
    CHECK(uart.ready());

    // And the same event on a transport that does have a connection signal
    // must not promote it, or a stray byte would declare a session and
    // "enumerated" would be confused with "ready" all over again.
    LinkState usb(LinkState::Config{TransportKind::Usb, Millis{5000}, true});
    usb.apply(LinkEvent::Attach, at(0));
    CHECK(usb.apply(LinkEvent::PeerData, at(1)) == EventOutcome::Ignored);
    CHECK(!usb.ready());
}

// A peer that has stopped answering is not connected, whatever the stack says.
// Data is the only proof of liveness there is.
void test_silence_ends_the_session()
{
    LinkState link(LinkState::Config{TransportKind::Bluetooth, Millis{5000}, true});
    link.apply(LinkEvent::Attach, at(0));
    link.apply(LinkEvent::PeerArriving, at(10));
    link.apply(LinkEvent::PeerEstablished, at(20));
    CHECK(link.ready());

    link.apply(LinkEvent::PeerData, at(1000));
    link.tick(at(5999));
    CHECK(link.ready());   // 4999 ms since the last frame

    link.tick(at(6001));
    CHECK(!link.ready());
    CHECK(link.last_disconnect() == DisconnectReason::LivenessTimeout);
}

// §6 again: survive a stack restart and an unexpected callback. Both arrive in
// the field and neither is a programming error, so neither may be an assertion.
void test_unexpected_events_are_ignored_and_counted()
{
    LinkState link(LinkState::Config{TransportKind::Bluetooth, Millis{5000}, true});

    // Data before anything is attached. It cannot be acted on and it must not
    // fabricate a session out of nothing.
    CHECK(link.apply(LinkEvent::PeerData, at(0)) == EventOutcome::Ignored);
    CHECK_PHASE(link.phase(), TransportPhase::Absent);
    CHECK(link.ignored_events() == 1);

    link.apply(LinkEvent::Attach, at(1));
    CHECK(link.apply(LinkEvent::Attach, at(2)) == EventOutcome::Redundant);

    // A restart of the stack below invalidates the session even though nothing
    // said the peer left.
    link.apply(LinkEvent::PeerArriving, at(10));
    link.apply(LinkEvent::PeerEstablished, at(20));
    const std::uint32_t before = link.epoch();
    link.apply(LinkEvent::SubsystemRestart, at(30));
    CHECK(!link.ready());
    CHECK(link.last_disconnect() == DisconnectReason::SubsystemRestart);
    CHECK(link.epoch() != before);
}

// Suspend is deliberate and ours, and it is still the end of a session.
//
// The tempting design is to come back into Ready — a resume that costs a
// reconnect makes sleep expensive. It is the wrong one: the peer was never told
// we quiesced, so it may have timed out, closed, or been unplugged while we
// were down, and resuming into Ready means asserting a session the other end
// has forgotten. The link comes back Attached and the peer arrives again.
//
// The cost is real and is not hidden: a sleep state that suspends a live
// transport pays a reconnect, so the power manager has to decide that is worth
// it rather than discover it. Recorded as an open question for the power work —
// what a light sleep short enough to keep a USB host happy actually costs is a
// measurement nobody has taken.
void test_suspend_ends_the_session_rather_than_pausing_it()
{
    LinkState link(LinkState::Config{TransportKind::Usb, Millis{5000}, true});
    link.apply(LinkEvent::Attach, at(0));
    link.apply(LinkEvent::PeerArriving, at(10));
    link.apply(LinkEvent::PeerEstablished, at(20));
    const std::uint32_t epoch_when_ready = link.epoch();

    CHECK(link.apply(LinkEvent::Suspend, at(100)) == EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Suspended);
    CHECK(!link.ready());
    CHECK(link.epoch() != epoch_when_ready);   // leaving Ready ended the session

    CHECK(link.apply(LinkEvent::Suspend, at(110)) == EventOutcome::Redundant);

    CHECK(link.apply(LinkEvent::Resume, at(200)) == EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Attached);
    CHECK(!link.ready());

    // The peer arrives again, and it is a second session.
    link.apply(LinkEvent::PeerArriving, at(300));
    link.apply(LinkEvent::PeerEstablished, at(310));
    CHECK(link.ready());
    CHECK(link.sessions() == 2);

    // Suspending something that was never there is not a state change.
    LinkState absent;
    CHECK(absent.apply(LinkEvent::Suspend, at(0)) == EventOutcome::Ignored);
}

// A fault needs a reset rather than a retry, and the distinction has to survive
// contact with a caller that retries anyway.
void test_a_fault_is_not_cleared_by_trying_again()
{
    LinkState link(LinkState::Config{TransportKind::Usb, Millis{5000}, true});
    link.apply(LinkEvent::Attach, at(0));
    link.apply(LinkEvent::Fault, at(10), DisconnectReason::Fault);
    CHECK_PHASE(link.phase(), TransportPhase::Faulted);

    CHECK(link.apply(LinkEvent::PeerArriving, at(20)) == EventOutcome::Ignored);
    CHECK(link.apply(LinkEvent::PeerEstablished, at(30)) == EventOutcome::Ignored);
    CHECK_PHASE(link.phase(), TransportPhase::Faulted);

    // A reset clears the link, not the device's history. The epoch moves
    // forward rather than to zero — anything holding a stale epoch must not be
    // able to match it again — and the session and ignored-event counters
    // survive, because a restart is precisely what somebody reading them is
    // trying to understand.
    const std::uint32_t epoch_before = link.epoch();
    link.reset();
    CHECK_PHASE(link.phase(), TransportPhase::Absent);
    CHECK(link.epoch() > epoch_before);
    CHECK(link.last_disconnect() == DisconnectReason::SubsystemRestart);
}

// The decoder's buffer is finite, and what it does when a peer fills it faster
// than anybody drains it is a design decision rather than an accident.
void test_input_beyond_the_buffer_is_refused_and_counted()
{
    Decoder decoder;
    std::uint8_t noise[Decoder::kCapacity * 2];
    for (std::size_t i = 0; i < sizeof noise; ++i) {
        noise[i] = static_cast<std::uint8_t>(i & 0xFFu);
    }

    const std::size_t taken = decoder.push(noise, sizeof noise);
    CHECK(taken < sizeof noise);
    CHECK(decoder.stats().input_dropped > 0);

    // And it recovers: after draining, a real frame decodes.
    decoder.reset();
    std::uint8_t payload[24];
    fill(payload, sizeof payload);
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);
    decoder.push(wire, encoded);
    std::uint8_t out[kMaxPayload];
    CHECK(decoder.next(out, sizeof out) == sizeof payload);
}

// A zero-length frame is a legitimate thing to send — a keepalive is exactly
// that — and it must survive the round trip as an empty frame rather than as
// nothing at all.
void test_an_empty_frame_is_a_frame()
{
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(nullptr, 0, wire, sizeof wire);
    CHECK(encoded == kOverheadBytes);

    Decoder decoder;
    decoder.push(wire, encoded);
    std::uint8_t out[kMaxPayload];
    CHECK(decoder.next(out, sizeof out) == 0);
    CHECK(decoder.stats().frames == 1);   // delivered, not merely absent
}

}  // namespace

int main()
{
    // The owner's §6 list.
    test_fragmented_input();
    test_several_frames_in_one_read();
    test_partial_writes();
    test_queue_full();
    test_disconnect_during_a_frame();
    test_reconnect_is_a_new_session();
    test_large_payload();
    test_malformed_frame();

    // The upstream defects, held against our own code.
    test_an_over_long_frame_is_refused_not_truncated();
    test_the_checksum_is_the_one_we_said_it_was();
    test_attached_is_not_ready();
    test_silence_ends_the_session();
    test_unexpected_events_are_ignored_and_counted();
    test_suspend_ends_the_session_rather_than_pausing_it();
    test_a_fault_is_not_cleared_by_trying_again();
    test_input_beyond_the_buffer_is_refused_and_counted();
    test_an_empty_frame_is_a_frame();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("link: all checks passed (host only — no USB, Bluetooth or radio involved)\n");
    return 0;
}
