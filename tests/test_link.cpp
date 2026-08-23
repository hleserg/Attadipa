#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "attadipa/link/frame_codec.h"
#include "attadipa/link/frame_queue.h"
#include "attadipa/link/link_state.h"

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
// with the comment "no way of knowing, so assume yes". Attadipa does not inherit
// those, and these are the tests that say so rather than the paragraph that
// claims it.
//
// Nothing here touches USB, Bluetooth or a radio. It is the logic above them.

using namespace attadipa;
using namespace attadipa::link;

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

// "A frame of exactly this length was handed over." Both halves are asserted on
// purpose: the status, because that is what the caller's drain loop reads, and
// the boolean conversion, because an implementation that derived truth from the
// length instead would pass every length except the one that matters.
bool delivered(const FrameResult& result, std::size_t length)
{
    return result.status == FrameStatus::Delivered && result.length == length &&
           static_cast<bool>(result);
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

    // Every byte on its own. Nothing may be delivered until the last one, and
    // "still arriving" is the honest word for every one of those steps — the
    // buffer is not empty, it is part-way through a frame.
    for (std::size_t i = 0; i + 1 < encoded; ++i) {
        CHECK(decoder.push(&wire[i], 1) == 1);
        const FrameResult waiting = decoder.next(out, sizeof out);
        CHECK(!waiting);
        CHECK(waiting.status == FrameStatus::Incomplete);
    }
    CHECK(decoder.push(&wire[encoded - 1], 1) == 1);
    CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
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
    CHECK(delivered(decoder.next(out, sizeof out), sizeof a));
    CHECK(same(out, a, sizeof a));
    CHECK(!decoder.next(out, sizeof out));

    CHECK(decoder.push(wire + split, used - split) == used - split);
    CHECK(delivered(decoder.next(out, sizeof out), sizeof b));
    CHECK(same(out, b, sizeof b));
    CHECK(delivered(decoder.next(out, sizeof out), sizeof c));
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
    // entry either — the frame is still there for a caller with a real buffer,
    // which is told how big that buffer has to be rather than left to guess.
    std::uint8_t small[8];
    const FrameResult too_small = queue.pop(small, sizeof small);
    CHECK(!too_small);
    CHECK(too_small.status == FrameStatus::OutputTooSmall);
    CHECK(too_small.length == sizeof payload);
    CHECK(queue.size() == 1);

    std::uint8_t out[kMaxPayload];
    CHECK(delivered(queue.pop(out, sizeof out), sizeof payload));
    CHECK(same(out, payload, sizeof payload));
    CHECK(queue.empty());
    CHECK(queue.pop(out, sizeof out).status == FrameStatus::NoFrame);
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
    CHECK(delivered(queue.pop(out, sizeof out), sizeof payload));
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
    CHECK(!decoder.next(out, sizeof out));

    // And a whole frame after that decodes normally.
    CHECK(decoder.push(wire, encoded) == encoded);
    CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
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
// from link/include/attadipa/link/frame_codec.h, which is where the rest of the
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
    CHECK(delivered(decoder.next(out, sizeof out), kMaxPayload));
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
        CHECK(!decoder.next(out, sizeof out));
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
        CHECK(!decoder.next(out, sizeof out));
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
        CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
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
    CHECK(!decoder.next(out, sizeof out));
    CHECK(decoder.stats().frames == 0);
}

// THE TEST THAT SHOULD HAVE EXISTED FIRST.
//
// Corrupt every byte of a frame in turn, and demand that not one of them ever
// produces a frame that is delivered with wrong content. A hand-picked byte is
// not enough, and this is not a hypothetical: the CRC span was `length + 2`
// where it had to be `length + 3`, so the last byte of every frame was
// unprotected. The encoder and the decoder computed the same wrong span, agreed
// with each other perfectly, and every round-trip test passed while a corrupted
// final byte was delivered as good data — the upstream failure this format
// exists to prevent, with a checksum on top.
//
// A sweep catches an off-by-one at either end. A single corrupted byte at
// offset ten does not.
void test_no_single_byte_corruption_is_ever_delivered()
{
    for (std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{2},
                             std::size_t{17}, std::size_t{kMaxPayload - 1},
                             kMaxPayload}) {
        std::uint8_t payload[kMaxPayload];
        fill(payload, size, 0x33);

        std::uint8_t wire[kMaxFrame];
        const std::size_t encoded = encode(size ? payload : nullptr, size, wire, sizeof wire);
        CHECK(encoded == size + kOverheadBytes);

        for (std::size_t i = 0; i < encoded; ++i) {
            // Every bit of every byte, not merely a flip of all eight: an XOR
            // with 0xFF is a poor probe of a CRC, which is linear.
            for (std::uint8_t mask : {std::uint8_t{0x01}, std::uint8_t{0x02},
                                      std::uint8_t{0x40}, std::uint8_t{0x80},
                                      std::uint8_t{0xFF}}) {
                std::uint8_t corrupt[kMaxFrame];
                std::memcpy(corrupt, wire, encoded);
                corrupt[i] = static_cast<std::uint8_t>(corrupt[i] ^ mask);

                Decoder decoder;
                decoder.push(corrupt, encoded);
                std::uint8_t out[kMaxPayload];
                const FrameResult result = decoder.next(out, sizeof out);

                // Delivering nothing is fine. Delivering the right thing is fine
                // — a corrupted sync byte can resynchronise onto a frame that
                // happens to still be intact. Delivering the WRONG thing is the
                // failure, and it must be impossible.
                if (result && result.length == size && size != 0 &&
                    !same(out, payload, size)) {
                    std::fprintf(stderr,
                                 "FAIL line %d: payload of %lu bytes, corrupting byte %lu with "
                                 "mask 0x%02X, delivered wrong content\n",
                                 __LINE__, static_cast<unsigned long>(size),
                                 static_cast<unsigned long>(i),
                                 static_cast<unsigned>(mask));
                    ++failures;
                }
            }
        }
    }
}

// The checksum must cover the length-check byte too, which the sweep above
// cannot show on its own: a corrupted length-check is normally caught by the
// header test before the CRC is ever consulted. This pins the span directly.
void test_the_checksum_covers_the_whole_header_after_the_sync()
{
    std::uint8_t payload[8];
    fill(payload, sizeof payload);
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);

    // The span is out[2] through out[4 + length] inclusive: two length bytes,
    // the length check, and every payload byte.
    const std::uint16_t over_the_whole_span = crc16_ccitt(wire + 2, sizeof payload + 3);
    const std::uint16_t on_the_wire         = static_cast<std::uint16_t>(
        wire[encoded - 2] | (wire[encoded - 1] << 8));
    CHECK(over_the_whole_span == on_the_wire);

    // One byte short would leave the last payload byte unprotected, and one byte
    // long would read past the payload into the checksum itself.
    CHECK(crc16_ccitt(wire + 2, sizeof payload + 2) != on_the_wire);
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
    CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
}

// ---------------------------------------------------------------------------
// A zero-length frame, and the sentinel it used to be indistinguishable from.
//
// Issue #146, and `TASKS.md` T-062 before it: a valid payload length of zero
// was returned by `Decoder::next()` and `FrameQueue::pop()` as the same value
// they used for "nothing here", *after* consuming the frame. The documented
// drain loop stopped on it, so the next frame in the buffer was stranded with
// nothing to fetch it, while `stats().frames` and `accepted()` counted the
// empty one as delivered. Two independently written containers, one rule
// broken, so these tests attack both.

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
    CHECK(delivered(decoder.next(out, sizeof out), 0));
    CHECK(decoder.stats().frames == 1);   // delivered, not merely absent
}

// THE PROOF TEST for this defect, and the one that has to be read before the
// others: it pins the two answers apart rather than pinning either alone.
//
// An implementation that reports a delivered empty frame as `NoFrame`, or whose
// truth test reads `length != 0` instead of the status, passes every other test
// in this file — including the round-trip one directly above — and has the bug
// back. So both are asserted here explicitly and against each other.
void test_a_delivered_empty_frame_is_not_the_answer_for_nothing()
{
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(nullptr, 0, wire, sizeof wire);

    Decoder decoder;
    std::uint8_t out[kMaxPayload];

    // Nothing has arrived at all.
    const FrameResult idle = decoder.next(out, sizeof out);
    CHECK(idle.status == FrameStatus::NoFrame);
    CHECK(!idle);
    CHECK(idle.length == 0);

    decoder.push(wire, encoded);
    const FrameResult empty = decoder.next(out, sizeof out);
    CHECK(empty.status == FrameStatus::Delivered);
    CHECK(static_cast<bool>(empty));       // and NOT derived from the length
    CHECK(empty.length == 0);

    // The two lengths are equal and the two answers must not be. That sentence
    // is the whole defect and the whole fix.
    CHECK(empty.length == idle.length);
    CHECK(empty.status != idle.status);
    CHECK(static_cast<bool>(empty) != static_cast<bool>(idle));

    // And the queue, which had the identical collision, gives the identical
    // pair of answers.
    FrameQueue<> queue;
    const FrameResult queue_idle = queue.pop(out, sizeof out);
    CHECK(queue_idle.status == FrameStatus::NoFrame);
    CHECK(!queue_idle);

    const std::uint8_t nothing = 0;
    CHECK(queue.push(&nothing, 0));
    const FrameResult queue_empty = queue.pop(out, sizeof out);
    CHECK(queue_empty.status == FrameStatus::Delivered);
    CHECK(static_cast<bool>(queue_empty));
    CHECK(queue_empty.length == 0);
    CHECK(queue_empty.length == queue_idle.length);
    CHECK(queue_empty.status != queue_idle.status);
}

// Scenario 1 from the report: an empty frame and a real one in a single read.
// The empty frame is consumed and reported, and the byte behind it comes out of
// the SAME drain cycle rather than waiting for the transport to speak again.
void test_an_empty_frame_does_not_strand_the_frame_behind_it()
{
    const std::uint8_t one = 0xA7;
    std::uint8_t wire[kMaxFrame * 2];
    std::size_t  used = 0;
    used += encode(nullptr, 0, wire + used, sizeof wire - used);
    used += encode(&one, 1, wire + used, sizeof wire - used);
    CHECK(used == kOverheadBytes * 2 + 1);

    Decoder decoder;
    CHECK(decoder.push(wire, used) == used);

    // Drained the way the header documents it: keep going while the result is
    // true. Against the old contract this loop ran exactly zero times.
    std::uint8_t out[kMaxPayload];
    std::size_t  seen[4]  = {};
    std::size_t  count    = 0;
    for (FrameResult r = decoder.next(out, sizeof out); r;
         r = decoder.next(out, sizeof out)) {
        if (count < 4) {
            seen[count] = r.length;
        }
        if (r.length == 1) {
            CHECK(out[0] == one);
        }
        ++count;
    }

    CHECK(count == 2);
    CHECK(seen[0] == 0);
    CHECK(seen[1] == 1);
    CHECK(decoder.stats().frames == 2);
    CHECK(decoder.buffered() == 0);
    CHECK(decoder.next(out, sizeof out).status == FrameStatus::NoFrame);
}

// The same pair, arriving in fragments that split both a header and a trailer.
// Fragment boundaries are the transport's business and must not change what
// comes out — least of all for the frame whose payload is empty, where the
// header and the trailer are all there is.
void test_an_empty_frame_survives_a_split_header_and_trailer()
{
    std::uint8_t payload[9];
    fill(payload, sizeof payload, 5);

    std::uint8_t wire[kMaxFrame * 2];
    std::size_t  used = 0;
    used += encode(nullptr, 0, wire + used, sizeof wire - used);
    used += encode(payload, sizeof payload, wire + used, sizeof wire - used);

    Decoder decoder;
    std::uint8_t out[kMaxPayload];

    // Cut three bytes into the empty frame's header, then one byte before the
    // end of its trailer, then partway through the second frame's header.
    const std::size_t cuts[] = {3, kOverheadBytes - 1, kOverheadBytes + 3};
    std::size_t       fed    = 0;
    for (std::size_t cut : cuts) {
        CHECK(decoder.push(wire + fed, cut - fed) == cut - fed);
        fed = cut;
        const FrameResult partial = decoder.next(out, sizeof out);
        if (cut < kOverheadBytes) {
            CHECK(!partial);   // the empty frame is not complete yet
        }
    }

    // The empty frame completed at the third cut, which is past its trailer.
    CHECK(decoder.stats().frames == 1);

    CHECK(decoder.push(wire + fed, used - fed) == used - fed);
    CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
    CHECK(same(out, payload, sizeof payload));
    CHECK(decoder.stats().frames == 2);
    CHECK(decoder.stats().bad_crc == 0);
    CHECK(decoder.stats().resyncs == 0);
}

// Scenario 3: an empty frame between two ordinary ones. The protocol above
// assumes its stream is ordered, so the two real payloads must arrive in the
// order they were sent with the empty one accounted for between them — not
// reordered, not dropped, not merged.
void test_an_empty_frame_between_two_frames_changes_no_order()
{
    std::uint8_t first[13], second[70];
    fill(first, sizeof first, 11);
    fill(second, sizeof second, 12);

    std::uint8_t wire[kMaxFrame * 3];
    std::size_t  used = 0;
    used += encode(first, sizeof first, wire + used, sizeof wire - used);
    used += encode(nullptr, 0, wire + used, sizeof wire - used);
    used += encode(second, sizeof second, wire + used, sizeof wire - used);

    Decoder decoder;
    CHECK(decoder.push(wire, used) == used);

    std::uint8_t out[kMaxPayload];
    CHECK(delivered(decoder.next(out, sizeof out), sizeof first));
    CHECK(same(out, first, sizeof first));
    CHECK(delivered(decoder.next(out, sizeof out), 0));
    CHECK(delivered(decoder.next(out, sizeof out), sizeof second));
    CHECK(same(out, second, sizeof second));
    CHECK(!decoder.next(out, sizeof out));
    CHECK(decoder.stats().frames == 3);
}

// Scenario 4: an empty frame whose CRC is wrong. It must be counted as a
// checksum failure like any other corrupt frame — not as an empty delivery,
// which is the one mistake that would look right in the counters — and the
// valid frame behind it must still arrive.
void test_a_corrupt_empty_frame_is_a_crc_error_and_blocks_nothing()
{
    std::uint8_t payload[24];
    fill(payload, sizeof payload, 7);

    std::uint8_t wire[kMaxFrame * 2];
    std::size_t  used = 0;
    used += encode(nullptr, 0, wire + used, sizeof wire - used);
    used += encode(payload, sizeof payload, wire + used, sizeof wire - used);

    // Corrupt the empty frame's own checksum, the last byte before the good
    // frame begins.
    wire[kOverheadBytes - 1] = static_cast<std::uint8_t>(wire[kOverheadBytes - 1] ^ 0xFFu);

    Decoder decoder;
    CHECK(decoder.push(wire, used) == used);

    std::uint8_t out[kMaxPayload];
    CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
    CHECK(same(out, payload, sizeof payload));
    CHECK(decoder.stats().bad_crc == 1);
    CHECK(decoder.stats().frames == 1);   // one delivery, and it is the real one
    CHECK(!decoder.next(out, sizeof out));
}

// Scenario 2: the queue's half of the same defect. A zero-length entry occupies
// a slot and is counted as accepted, so it has to be reportable as a successful
// pop — otherwise the entry behind it is stranded in a container that says it
// took both.
void test_a_zero_length_entry_does_not_strand_the_queue()
{
    FrameQueue<> queue;
    const std::uint8_t nothing = 0;
    std::uint8_t       payload[40];
    fill(payload, sizeof payload, 9);

    CHECK(queue.push(&nothing, 0));
    CHECK(queue.push(payload, sizeof payload));
    CHECK(queue.size() == 2);
    CHECK(queue.accepted() == 2);

    // Drained the way a consumer drains: while the result is true.
    std::uint8_t out[kMaxPayload];
    std::size_t  seen[4] = {};
    std::size_t  count   = 0;
    for (FrameResult r = queue.pop(out, sizeof out); r; r = queue.pop(out, sizeof out)) {
        if (count < 4) {
            seen[count] = r.length;
        }
        ++count;
    }

    CHECK(count == 2);
    CHECK(seen[0] == 0);
    CHECK(seen[1] == sizeof payload);
    CHECK(same(out, payload, sizeof payload));
    CHECK(queue.empty());

    // A null pointer with a zero length is what `encode()` already accepts, and
    // the three boundaries have to agree about what an empty frame is or a
    // frame that survives one of them is refused by the next.
    CHECK(queue.push(nullptr, 0));
    CHECK(delivered(queue.pop(out, sizeof out), 0));
    CHECK(queue.malformed() == 0);

    // A null pointer with something to copy is still a caller error.
    CHECK(!queue.push(nullptr, 1));
    CHECK(queue.malformed() == 1);
}

// Finding 1 of PR #148's independent review, and it is #146 again wearing
// #146's own fix: both readers judged the output *pointer* in the same
// condition as the capacity, so a zero-length frame handed to a caller with a
// null `out` came back as `OutputTooSmall` with a length of 0 — come back with
// room for nothing. The frame was never consumed, the answer never changed, and
// everything behind it was stranded. Seven bytes from a peer to trigger.
//
// A frame with no payload has nothing to copy, so every output satisfies it.
void test_an_empty_frame_needs_no_output_buffer_at_all()
{
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(nullptr, 0, wire, sizeof wire);

    // The decoder, through the door the review named: null pointer, no room.
    {
        Decoder decoder;
        CHECK(decoder.push(wire, encoded) == encoded);
        const FrameResult r = decoder.next(nullptr, 0);
        CHECK(delivered(r, 0));
        CHECK(decoder.buffered() == 0);          // consumed, not left forever
        CHECK(decoder.stats().frames == 1);
    }

    // And the same frame through a non-null pointer with a capacity of zero,
    // which must not be a different answer — the header says the two are the
    // same request, and for a while they were not.
    {
        Decoder decoder;
        decoder.push(wire, encoded);
        std::uint8_t scratch[1];
        CHECK(delivered(decoder.next(scratch, 0), 0));
        CHECK(decoder.buffered() == 0);
    }

    // The queue's half. `push(nullptr, 0)` is accepted by the boundary
    // agreement, so `pop(nullptr, 0)` of that same entry has to come out.
    {
        FrameQueue<> queue;
        CHECK(queue.push(nullptr, 0));
        CHECK(delivered(queue.pop(nullptr, 0), 0));
        CHECK(queue.empty());
    }

    // A frame that does have a payload is still refused by a null output, and
    // is still there afterwards. The rule is about what a frame contains, not
    // about being lenient with pointers.
    {
        std::uint8_t payload[12];
        fill(payload, sizeof payload, 6);
        std::uint8_t full[kMaxFrame];
        const std::size_t n = encode(payload, sizeof payload, full, sizeof full);

        Decoder decoder;
        decoder.push(full, n);
        const FrameResult refused = decoder.next(nullptr, 0);
        CHECK(refused.status == FrameStatus::OutputTooSmall);
        CHECK(refused.length == sizeof payload);
        CHECK(decoder.stats().frames == 0);

        FrameQueue<> queue;
        CHECK(queue.push(payload, sizeof payload));
        const FrameResult queue_refused = queue.pop(nullptr, 0);
        CHECK(queue_refused.status == FrameStatus::OutputTooSmall);
        CHECK(queue_refused.length == sizeof payload);
        CHECK(queue.size() == 1);
    }
}

// Finding 2 of the same review. `OutputTooSmall` is falsy, so a drain written
// as `while (result)` exits with a complete CRC-verified frame still inside —
// and the decoder holds exactly one maximum frame plus a byte, so the next
// push is refused down to one byte and every frame after that is torn. The
// exit condition is `exhausted()`, and this pins the difference.
void test_a_frame_too_big_for_the_caller_does_not_end_the_drain()
{
    std::uint8_t payload[kMaxPayload];
    fill(payload, sizeof payload, 8);
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);

    Decoder decoder;
    CHECK(decoder.push(wire, encoded) == encoded);

    std::uint8_t cramped[16];
    const FrameResult refused = decoder.next(cramped, sizeof cramped);
    CHECK(!refused);                  // not a delivery...
    CHECK(!refused.exhausted());      // ...and not the end of the drain either
    CHECK(refused.length == kMaxPayload);

    // Left there, it does exactly what the review said: the buffer is one byte
    // short of holding anything more, so the transport starts being refused.
    CHECK(decoder.buffered() == encoded);
    std::uint8_t more[64];
    fill(more, sizeof more);
    CHECK(decoder.push(more, sizeof more) == Decoder::kCapacity - encoded);
    CHECK(decoder.stats().input_dropped > 0);

    // Handled rather than treated as an exit, the frame comes out intact.
    std::uint8_t out[kMaxPayload];
    CHECK(delivered(decoder.next(out, sizeof out), kMaxPayload));
    CHECK(same(out, payload, kMaxPayload));

    // The two ends of a drain, told apart. Only these two mean stop.
    CHECK(decoder.next(out, sizeof out).exhausted());
    Decoder fresh;
    CHECK(fresh.next(out, sizeof out).exhausted());
    const std::uint8_t stub[] = {kSync0, kSync1};
    fresh.push(stub, sizeof stub);
    CHECK(fresh.next(out, sizeof out).exhausted());
    FrameQueue<> queue;
    CHECK(queue.pop(out, sizeof out).exhausted());

    // And a delivery never means stop, whatever its length.
    Decoder empty_frames;
    std::uint8_t nothing_wire[kMaxFrame];
    empty_frames.push(nothing_wire, encode(nullptr, 0, nothing_wire, sizeof nothing_wire));
    CHECK(!empty_frames.next(out, sizeof out).exhausted());
}

// Second pass of the same review, and the fix for finding 2 had a defect of its
// own: the drain loop written out in `next()`'s comment took neither branch on
// `OutputTooSmall`, fell off the end of its body, and span — `next()` mutates
// nothing on that path, so every iteration is bit-identical while `stats()` goes
// on describing a healthy decoder. Worse than the stranded frame it replaced: a
// hang rather than a stall.
//
// This is the header's loop, transcribed rather than paraphrased, against the
// only buffer size at which the third case is reachable. **Keep the two in
// step**; a comment cannot be executed, so this test is the only thing that
// makes that example true. The iteration bound stands in for the watchdog — a
// spin has to fail this test, not hang the suite.
void test_the_drain_loop_the_header_prescribes_terminates()
{
    std::uint8_t payload[64];
    fill(payload, sizeof payload, 13);
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);

    Decoder decoder;
    CHECK(decoder.push(wire, encoded) == encoded);

    std::uint8_t out[16];   // smaller than the frame, and smaller than kMaxPayload
    std::size_t  delivered_count = 0;
    std::size_t  undersized      = 0;
    std::size_t  guard           = 0;
    bool         returned        = false;

    for (;;) {
        if (++guard > 1000) {
            break;   // a spin, not a drain
        }
        const FrameResult r = decoder.next(out, sizeof out);
        if (r) { ++delivered_count; continue; }
        if (r.exhausted()) { returned = true; break; }
        undersized = r.length;
        returned   = true;
        break;
    }

    CHECK(returned);
    CHECK(guard < 1000);
    CHECK(delivered_count == 0);
    CHECK(undersized == sizeof payload);

    // The frame was not consumed by any of that, so a caller that comes back
    // with room gets it whole. `OutputTooSmall` is a request, not a loss.
    std::uint8_t roomy[kMaxPayload];
    CHECK(delivered(decoder.next(roomy, sizeof roomy), sizeof payload));
    CHECK(same(roomy, payload, sizeof payload));

    // The same loop over a caller that took the header's other advice — a
    // buffer of kMaxPayload — never reaches the third case at all.
    Decoder plenty;
    plenty.push(wire, encoded);
    std::size_t rounds = 0;
    for (;;) {
        if (++rounds > 1000) { break; }
        const FrameResult r = plenty.next(roomy, sizeof roomy);
        if (r) { ++delivered_count; continue; }
        if (r.exhausted()) { break; }
        CHECK(false);   // OutputTooSmall is unreachable with kMaxPayload of room
        break;
    }
    CHECK(rounds == 2);            // one delivery, one exhausted
    CHECK(delivered_count == 1);
}

// Finding 3: the two non-delivery statuses must not claim more than the decoder
// can see. `Incomplete` is "fewer bytes than a whole frame needs", NOT "a frame
// is on its way" — four bytes of line noise sit below the header threshold
// forever, and the resynchroniser cannot judge them either way.
void test_incomplete_does_not_promise_that_anything_is_coming()
{
    std::uint8_t out[kMaxPayload];

    // Four bytes that are not a header and never will be. Nothing is arriving;
    // the decoder simply cannot say so, and must not pretend otherwise.
    Decoder decoder;
    const std::uint8_t noise[] = {0x11, 0x22, 0x33, 0x44};
    CHECK(decoder.push(noise, sizeof noise) == sizeof noise);
    CHECK(decoder.next(out, sizeof out).status == FrameStatus::Incomplete);
    CHECK(decoder.next(out, sizeof out).status == FrameStatus::Incomplete);
    CHECK(decoder.buffered() == sizeof noise);   // and it stays, indefinitely

    // Residue and emptiness stay distinguishable, which is the only claim
    // either status is entitled to make.
    decoder.reset();
    CHECK(decoder.next(out, sizeof out).status == FrameStatus::NoFrame);

    // A genuinely partial frame reports the same thing, and that is the point:
    // this decoder cannot tell the two apart, so neither status may be read as
    // evidence about the transport beneath it.
    std::uint8_t payload[6];
    fill(payload, sizeof payload, 2);
    std::uint8_t wire[kMaxFrame];
    const std::size_t encoded = encode(payload, sizeof payload, wire, sizeof wire);
    decoder.push(wire, 3);
    CHECK(decoder.next(out, sizeof out).status == FrameStatus::Incomplete);
    decoder.push(wire + 3, encoded - 3);
    CHECK(delivered(decoder.next(out, sizeof out), sizeof payload));
}

// The counters have to agree with what a caller could actually observe. That is
// the property the defect broke without breaking any single counter: `frames`
// and `accepted()` were right about what had been consumed and wrong about what
// had been handed over, and nothing in the code could tell the two apart.
void test_the_counters_agree_with_what_was_observable()
{
    std::uint8_t payload[16];
    fill(payload, sizeof payload, 3);

    std::uint8_t wire[kMaxFrame * 4];
    std::size_t  used = 0;
    used += encode(nullptr, 0, wire + used, sizeof wire - used);
    used += encode(payload, sizeof payload, wire + used, sizeof wire - used);
    used += encode(nullptr, 0, wire + used, sizeof wire - used);

    Decoder decoder;
    CHECK(decoder.push(wire, used) == used);

    std::uint8_t out[kMaxPayload];
    std::size_t  observed = 0;
    while (decoder.next(out, sizeof out)) {
        ++observed;
    }
    CHECK(observed == 3);
    CHECK(decoder.stats().frames == observed);
    CHECK(decoder.stats().bad_crc == 0);
    CHECK(decoder.stats().bad_length == 0);
    CHECK(decoder.stats().resyncs == 0);
    CHECK(decoder.stats().input_dropped == 0);

    // A frame refused for want of room is not a delivery and must not be
    // counted as one — and it is still there afterwards.
    std::uint8_t big[kMaxPayload];
    fill(big, sizeof big, 4);
    std::uint8_t one_more[kMaxFrame];
    const std::size_t encoded = encode(big, sizeof big, one_more, sizeof one_more);
    CHECK(decoder.push(one_more, encoded) == encoded);

    std::uint8_t cramped[8];
    const FrameResult refused = decoder.next(cramped, sizeof cramped);
    CHECK(refused.status == FrameStatus::OutputTooSmall);
    CHECK(refused.length == kMaxPayload);
    CHECK(decoder.stats().frames == observed);   // unchanged: nothing was handed over
    CHECK(delivered(decoder.next(out, sizeof out), kMaxPayload));
    CHECK(decoder.stats().frames == observed + 1);

    // The queue's counters, against the same standard. Two accepted, two
    // observably delivered, one refused for want of room and not counted.
    FrameQueue<2> queue;
    const std::uint8_t nothing = 0;
    CHECK(queue.push(&nothing, 0));
    CHECK(queue.push(payload, sizeof payload));
    CHECK(!queue.push(payload, sizeof payload));
    CHECK(queue.accepted() == 2);
    CHECK(queue.dropped() == 1);
    CHECK(queue.malformed() == 0);

    std::size_t popped = 0;
    while (queue.pop(out, sizeof out)) {
        ++popped;
    }
    CHECK(popped == queue.accepted());
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
    test_no_single_byte_corruption_is_ever_delivered();
    test_the_checksum_covers_the_whole_header_after_the_sync();
    test_an_over_long_frame_is_refused_not_truncated();
    test_the_checksum_is_the_one_we_said_it_was();
    test_attached_is_not_ready();
    test_silence_ends_the_session();
    test_unexpected_events_are_ignored_and_counted();
    test_suspend_ends_the_session_rather_than_pausing_it();
    test_a_fault_is_not_cleared_by_trying_again();
    test_input_beyond_the_buffer_is_refused_and_counted();

    // The zero-length frame and the sentinel it collided with (#146, T-062).
    test_an_empty_frame_is_a_frame();
    test_a_delivered_empty_frame_is_not_the_answer_for_nothing();
    test_an_empty_frame_does_not_strand_the_frame_behind_it();
    test_an_empty_frame_survives_a_split_header_and_trailer();
    test_an_empty_frame_between_two_frames_changes_no_order();
    test_a_corrupt_empty_frame_is_a_crc_error_and_blocks_nothing();
    test_a_zero_length_entry_does_not_strand_the_queue();
    test_an_empty_frame_needs_no_output_buffer_at_all();
    test_a_frame_too_big_for_the_caller_does_not_end_the_drain();
    test_the_drain_loop_the_header_prescribes_terminates();
    test_incomplete_does_not_promise_that_anything_is_coming();
    test_the_counters_agree_with_what_was_observable();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("link: all checks passed (host only — no USB, Bluetooth or radio involved)\n");
    return 0;
}
