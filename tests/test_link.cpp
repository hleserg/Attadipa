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

void check_outcome(EventOutcome actual, EventOutcome expected, int line)
{
    if (actual != expected) {
        std::fprintf(stderr, "FAIL line %d: outcome is %s, expected %s\n", line,
                     to_string(actual), to_string(expected));
        ++failures;
    }
}

#define CHECK_OUTCOME(actual, expected) check_outcome((actual), (expected), __LINE__)

// For checks inside a loop over phases, where the line number alone does not
// say which case failed.
void check_in(bool condition, TransportPhase phase, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d [phase %s]: %s\n", line, core::to_string(phase),
                     what);
        ++failures;
    }
}

#define CHECK_IN(phase, cond) check_in((cond), (phase), #cond, __LINE__)

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
                const std::size_t delivered = decoder.next(out, sizeof out);

                // Delivering nothing is fine. Delivering the right thing is fine
                // — a corrupted sync byte can resynchronise onto a frame that
                // happens to still be intact. Delivering the WRONG thing is the
                // failure, and it must be impossible.
                if (delivered == size && size != 0 && !same(out, payload, size)) {
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

struct AttachCase {
    TransportPhase phase;
    EventOutcome   expected;
};

// Every phase appears in the table, and the check is by value rather than by
// row count: a table of the right length with one phase written twice would
// leave another untested and still look complete.
constexpr bool covers_every_phase(const AttachCase (&cases)[core::kTransportPhaseCount])
{
    for (std::uint8_t p = 0; p < core::kTransportPhaseCount; ++p) {
        bool found = false;
        for (const AttachCase& c : cases) {
            if (static_cast<std::uint8_t>(c.phase) == p) {
                found = true;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

// Drive a fresh link into `phase` using only public events, and assert it got
// there. A table that silently tested the wrong state would prove nothing.
void place_in(LinkState& link, TransportPhase phase, int line)
{
    switch (phase) {
        case TransportPhase::Absent:
            break;
        case TransportPhase::Attached:
            link.apply(LinkEvent::Attach, at(1));
            break;
        case TransportPhase::Connecting:
            link.apply(LinkEvent::Attach, at(1));
            link.apply(LinkEvent::PeerArriving, at(2));
            break;
        case TransportPhase::Ready:
            link.apply(LinkEvent::Attach, at(1));
            link.apply(LinkEvent::PeerArriving, at(2));
            link.apply(LinkEvent::PeerEstablished, at(3));
            break;
        case TransportPhase::Suspended:
            link.apply(LinkEvent::Attach, at(1));
            link.apply(LinkEvent::Suspend, at(2));
            break;
        case TransportPhase::Faulted:
            link.apply(LinkEvent::Attach, at(1));
            link.apply(LinkEvent::Fault, at(2), DisconnectReason::Fault);
            break;
    }
    check_phase(link.phase(), phase, line);
}

// What `Attach` means is a question per phase, and it was being answered by one
// negative guard for all six.
//
// The two refusals are not synonyms. `Redundant` is the claim that the link is
// already in the state the event asked for — uncounted, and a caller may read
// it as success. `Ignored` is the claim that the event does not apply here, and
// it is counted, because the rate of inapplicable callbacks is the diagnostic
// this machine exists to preserve. `phase_ != Absent -> Redundant` said the
// first for a faulted transport, which carries nothing however present its
// hardware is, and for a suspended one, which we quiesced on purpose.
void test_attach_is_classified_per_phase()
{
    constexpr AttachCase cases[] = {
        // Absent is the only one where there is work to do.
        {TransportPhase::Absent,     EventOutcome::Applied},
        // Attached is the state Attach asks for, and Connecting and Ready are
        // strictly downstream of it: the peripheral is there in all three, so
        // the request is already satisfied. Answering anything else would mean
        // dropping an arriving peer or a live session on behalf of an event
        // that asked for something already true.
        {TransportPhase::Attached,   EventOutcome::Redundant},
        {TransportPhase::Connecting, EventOutcome::Redundant},
        {TransportPhase::Ready,      EventOutcome::Redundant},
        // Suspended needs a Resume and Faulted needs a SubsystemRestart. In
        // neither is the attach satisfied, and in both the frequency of the
        // attempt is worth counting.
        {TransportPhase::Suspended,  EventOutcome::Ignored},
        {TransportPhase::Faulted,    EventOutcome::Ignored},
    };

    // Adding a phase to the enum without deciding what Attach does about it
    // fails to compile *here*. The production switch in link_state.cpp has no
    // such guard on purpose — a phase nobody has reasoned about falls to
    // Ignored, which is the safe half — so this table is the thing that says
    // somebody reasoned about it. The parameter type pins the length; the body
    // pins the coverage.
    static_assert(covers_every_phase(cases),
                  "every TransportPhase needs a decided Attach outcome");

    for (const AttachCase& c : cases) {
        LinkState link(LinkState::Config{TransportKind::Usb, Millis{5000}, true});
        place_in(link, c.phase, __LINE__);

        const std::uint32_t    ignored_before  = link.ignored_events();
        const std::uint32_t    epoch_before    = link.epoch();
        const std::uint32_t    sessions_before = link.sessions();
        const DisconnectReason reason_before   = link.last_disconnect();

        const EventOutcome outcome = link.apply(LinkEvent::Attach, at(100));
        if (outcome != c.expected) {
            std::fprintf(stderr, "FAIL line %d: Attach in %s gave %s, expected %s\n", __LINE__,
                         core::to_string(c.phase), to_string(outcome), to_string(c.expected));
            ++failures;
        }

        // Counted exactly when it was Ignored, and exactly once.
        const std::uint32_t counted = c.expected == EventOutcome::Ignored ? 1u : 0u;
        CHECK_IN(c.phase, link.ignored_events() == ignored_before + counted);

        // Attach moves the link out of Absent and out of nothing else.
        CHECK_IN(c.phase, link.phase() == (c.phase == TransportPhase::Absent
                                               ? TransportPhase::Attached
                                               : c.phase));

        // And it never touches the session accounting. Attaching is not a
        // session, so even the applied case leaves all three alone — though for
        // last_disconnect() the Absent row proves little, since a fresh link's
        // reason is None whatever enter() does with it. The case with teeth is
        // an Absent reached *by* a restart, and it is asserted in
        // test_attach_while_faulted_is_refused_and_counted_every_time.
        CHECK_IN(c.phase, link.epoch() == epoch_before);
        CHECK_IN(c.phase, link.sessions() == sessions_before);
        CHECK_IN(c.phase, link.last_disconnect() == reason_before);
    }
}

// A faulted link refuses an attach every time, and says so every time.
//
// The count is the point. A controller whose attach callback fires on every
// enumeration attempt against broken hardware produces exactly this shape, and
// under the old answer it produced nothing at all to look at: `Redundant` is
// not counted, so a retry storm and a quiet link read identically.
void test_attach_while_faulted_is_refused_and_counted_every_time()
{
    LinkState link(LinkState::Config{TransportKind::Usb, Millis{5000}, true});
    link.apply(LinkEvent::Attach, at(0));
    link.apply(LinkEvent::PeerArriving, at(10));
    link.apply(LinkEvent::PeerEstablished, at(20));
    CHECK(link.sessions() == 1);   // a real session, so the counters have something to keep

    link.apply(LinkEvent::Fault, at(30), DisconnectReason::Fault);
    CHECK_PHASE(link.phase(), TransportPhase::Faulted);

    const std::uint32_t epoch_faulted   = link.epoch();
    const std::uint32_t ignored_faulted = link.ignored_events();

    for (std::uint64_t i = 0; i < 5; ++i) {
        CHECK_OUTCOME(link.apply(LinkEvent::Attach, at(40 + i)), EventOutcome::Ignored);
        CHECK_PHASE(link.phase(), TransportPhase::Faulted);
    }
    CHECK(link.ignored_events() == ignored_faulted + 5);
    CHECK(!link.ready());

    // The fault survives intact: the attaches changed nothing except the count
    // of how many arrived.
    CHECK(link.epoch() == epoch_faulted);
    CHECK(link.sessions() == 1);
    CHECK(link.last_disconnect() == DisconnectReason::Fault);

    // The one way out, and afterwards an attach is real work again.
    CHECK_OUTCOME(link.apply(LinkEvent::SubsystemRestart, at(100)), EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Absent);
    CHECK_OUTCOME(link.apply(LinkEvent::Attach, at(110)), EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Attached);

    // The restart cleared the link, not the evidence about it — the five
    // refusals are still there to be read.
    CHECK(link.ignored_events() == ignored_faulted + 5);
    CHECK(link.sessions() == 1);

    // And the *applied* attach did not overwrite why the link went down. This
    // is the one place that can be asserted: the table's Absent row starts from
    // a fresh link whose reason is already None, so it would hold whatever
    // enter() did with it. Here Absent was reached by a restart, so a reason
    // assigned unconditionally would erase SubsystemRestart on the way back up
    // — the recovery deleting the record of what it recovered from.
    CHECK(link.last_disconnect() == DisconnectReason::SubsystemRestart);
}

// Suspended is a state we asked for, and an attach must not undo it by accident.
//
// The peripheral never went anywhere, which is exactly why the answer is not
// "already attached": the link is quiesced and carries nothing, and the way
// back is Resume — which lands in Attached with the peer still to arrive,
// because the peer was never told we went away. An Attach honoured here would
// route around that lifecycle, and a `Redundant` reported here would tell a
// caller the link was usable while it was not.
void test_attach_does_not_resurrect_a_suspended_link()
{
    LinkState link(LinkState::Config{TransportKind::Bluetooth, Millis{5000}, true});
    link.apply(LinkEvent::Attach, at(0));
    link.apply(LinkEvent::PeerArriving, at(10));
    link.apply(LinkEvent::PeerEstablished, at(20));
    link.apply(LinkEvent::Suspend, at(100));
    CHECK_PHASE(link.phase(), TransportPhase::Suspended);

    const std::uint32_t ignored_suspended = link.ignored_events();
    const std::uint32_t epoch_suspended   = link.epoch();

    CHECK_OUTCOME(link.apply(LinkEvent::Attach, at(110)), EventOutcome::Ignored);
    CHECK_PHASE(link.phase(), TransportPhase::Suspended);
    CHECK(link.ignored_events() == ignored_suspended + 1);
    CHECK(link.epoch() == epoch_suspended);
    CHECK(!link.ready());

    // Resume still lands where it always did, and the attach minted no session
    // on the way past.
    CHECK_OUTCOME(link.apply(LinkEvent::Resume, at(200)), EventOutcome::Applied);
    CHECK_PHASE(link.phase(), TransportPhase::Attached);
    CHECK(link.sessions() == 1);
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
    test_no_single_byte_corruption_is_ever_delivered();
    test_the_checksum_covers_the_whole_header_after_the_sync();
    test_an_over_long_frame_is_refused_not_truncated();
    test_the_checksum_is_the_one_we_said_it_was();
    test_attached_is_not_ready();
    test_silence_ends_the_session();
    test_unexpected_events_are_ignored_and_counted();
    test_suspend_ends_the_session_rather_than_pausing_it();
    test_a_fault_is_not_cleared_by_trying_again();
    test_attach_is_classified_per_phase();
    test_attach_while_faulted_is_refused_and_counted_every_time();
    test_attach_does_not_resurrect_a_suspended_link();
    test_input_beyond_the_buffer_is_refused_and_counted();
    test_an_empty_frame_is_a_frame();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("link: all checks passed (host only — no USB, Bluetooth or radio involved)\n");
    return 0;
}
