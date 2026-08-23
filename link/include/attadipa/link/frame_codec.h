#pragma once

#include <cstddef>
#include <cstdint>

// Framing for a byte stream, and the properties MeshCore's USB framing does not
// have.
//
// Read from upstream source at `d929643`, not from a bug report
// (docs/upstream/meshcore-1.17-review.md §2). MeshCore's serial framing is a
// start byte plus a 16-bit little-endian length, and then the payload:
//
//   * **no checksum** — a corrupted payload is delivered as if correct;
//   * **no escaping and no resync marker** — after a torn frame the parser
//     resynchronises on the next byte that happens to equal `'<'`, which is
//     0x3C, a byte that occurs freely inside binary payloads. So
//     "resynchronisation" is a coin flip that can land mid-payload and
//     manufacture another false frame;
//   * **an over-long frame is silently truncated** to MAX_FRAME_SIZE and handed
//     up as complete, so a corrupted length produces a plausible short frame
//     rather than an error.
//
// Attadipa's node link is the same kind of link — a small MCU streaming
// structured records to a host that can stall — so these are requirements from
// the first line rather than lessons for later (TASKS T-044).
//
//   sync0 sync1 | len_lo len_hi | len_check | payload[len] | crc_lo crc_hi
//     0xF1  0x5E                   xor+salt                   CRC-16/CCITT
//
// Three deliberate properties:
//
//   1. **The length is checked before it is trusted.** `len_check` is one byte
//      derived from the length, so a corrupted length is rejected at the header
//      instead of committing the decoder to reading hundreds of bytes of noise.
//   2. **The CRC covers the length and the payload.** A torn frame is
//      *detected*, which is the difference between a resync and a guess.
//   3. **An over-long length is an error, never a truncation.** There is no
//      code path in this file that shortens a frame and reports success.
//
// This layer knows nothing about what the payload means. ADR-0005's envelope
// and its TLV body sit inside `payload` and are still provisional pending the
// encoding benchmark (T-016) — which is exactly why the framing beneath them
// could be written now: it survives any answer.

namespace attadipa::link {

inline constexpr std::uint8_t kSync0 = 0xF1;
inline constexpr std::uint8_t kSync1 = 0x5E;

// Header is sync(2) + length(2) + length check(1); trailer is the CRC.
inline constexpr std::size_t kHeaderBytes  = 5;
inline constexpr std::size_t kTrailerBytes = 2;
inline constexpr std::size_t kOverheadBytes = kHeaderBytes + kTrailerBytes;

// The largest payload one frame may carry.
//
// Derived, not copied. ADR-0005 §4's envelope is 12 bytes, and MeshCore's
// companion frames cap at 176 — so 192 carries a full companion frame inside a
// Attadipa envelope with room left, and is a round number of cache lines' worth
// on the part. RESOURCE_BUDGET §4 requires that any pool be sized to the
// maximum payload and that the bound be declared; this is that declaration, and
// it is one constant rather than a number repeated in six files.
inline constexpr std::size_t kMaxPayload = 192;
inline constexpr std::size_t kMaxFrame   = kMaxPayload + kOverheadBytes;

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t length);

// Write one frame. Returns the number of bytes written, or 0 if the payload is
// too long or the output is too small — never a partial frame, because a
// partial frame on the wire is indistinguishable from a torn one.
//
// A length of zero is a legal frame and encodes to `kOverheadBytes`. The return
// value is a bare size here and stays one deliberately: the smallest frame this
// function can write is seven bytes, so 0 is outside its valid range and cannot
// collide with a real answer. That is not true of the two readers below, which
// is why they do not return a bare size — see `FrameStatus`.
std::size_t encode(const std::uint8_t* payload, std::size_t length, std::uint8_t* out,
                   std::size_t out_capacity);

// Why a frame is handed over as a status *and* a length, rather than a length
// alone.
//
// A payload of zero bytes is a valid payload. `encode()` writes a frame for it,
// the decoder verifies its CRC and hands it over like any other, and
// `tests/test_link.cpp` has asserted that round trip since the framing was
// written. So a reader that returns the length and reserves 0 for "nothing
// ready" has given one value two meanings — and the drain loop its own
// documentation prescribes, *call until it returns 0*, stops on a frame that
// has already been consumed. Whatever was behind it stays in the buffer
// with nothing to fetch it, and the diagnostics have counted the empty frame as
// delivered intact. A peer does not have to be friendly to produce one: a
// zero-length header is self-consistent and its CRC is valid.
//
// That was the same defect in two independently written containers — the
// decoder and the queue — which is why the answer is one shared type rather
// than a check at each call site. Reported as issue #146, listed under
// `TASKS.md` T-062 before that, and the general rule is in
// `docs/architecture/ARCHITECTURE.md` §5: a value inside a type's valid domain
// may never also mean "there is no value". Either the status travels separately
// from the payload, or the sentinel is refused at the door.
//
// Refusing it at the door was the other option and was not taken. This layer
// knows nothing about what a payload means — it says so at the top of this file
// — ADR-0005 reserves no empty control frame either way, and rejecting zero
// length would have inverted an existing framing test and changed what goes on
// the wire with no compatibility evidence for the change.
enum class FrameStatus : std::uint8_t {
    // A whole frame was copied out. `length` is its payload length, and 0 is a
    // legitimate value for it: an empty frame that really arrived.
    Delivered,
    // This reader holds nothing at all.
    //
    // It is a fact about *this object*, and the comment says so because the
    // tempting reading is wider and wrong: an empty decoder buffer says nothing
    // about what is sitting in the USB or UART FIFO underneath it. Do not gate
    // a sleep decision or arm a link timeout on this alone.
    NoFrame,
    // Fewer bytes are held than any whole frame needs, so nothing can be judged
    // yet. Decoders only — a queue holds whole frames or nothing.
    //
    // Not a promise that a frame is on its way. The resynchroniser discards a
    // byte at a time and stops once fewer than `kHeaderBytes` remain, so up to
    // four bytes of pure line noise sit here permanently and report this
    // forever. "More bytes are needed before anything can be said" is the whole
    // claim; who will send them, and whether they exist, is not knowable here.
    Incomplete,
    // A whole frame is ready and the caller's buffer cannot hold it. `length`
    // is that frame's payload length — what the caller has to come back with —
    // and the frame stays exactly where it was.
    //
    // **Not an end-of-drain condition.** The frame is still queued and will
    // block this reader until somebody takes it; in the decoder, whose buffer
    // holds exactly one maximum frame plus a byte, it also starts tearing
    // everything that arrives behind it. See the drain loop on `next()`.
    //
    // Unreachable from the decoder for any caller whose buffer is `kMaxPayload`
    // bytes, which is why that bound is a public constant.
    OutputTooSmall,
};

// A frame handed over, or the reason there is none.
//
// Two words wide and trivially copyable, so it costs what returning the length
// cost. `operator bool` reads the *status*: it is true for a delivery and for
// nothing else, so a caller cannot stop on a frame that was consumed, and
// cannot quietly restore the old behaviour by testing the length instead.
struct FrameResult {
    FrameStatus status = FrameStatus::NoFrame;
    std::size_t length = 0;

    // A frame was handed over. `length` may be 0 and that is still a delivery.
    explicit operator bool() const { return status == FrameStatus::Delivered; }

    // Nothing more can be had from this reader until more bytes arrive. This,
    // not `!result`, is the end of a drain.
    //
    // `OutputTooSmall` is deliberately excluded. It is falsy — it is not a
    // delivery — but it is a caller error with a frame behind it, and a loop
    // that exits on it leaves that frame stranded. That is the defect this
    // whole type exists to remove, and writing the exit condition as `!result`
    // would reintroduce it one layer up.
    bool exhausted() const
    {
        return status == FrameStatus::NoFrame || status == FrameStatus::Incomplete;
    }
};

// What went wrong, counted rather than logged. A count that only ever rises is
// the difference between "the link is flaky" and a bug report.
struct DecoderStats {
    std::uint32_t frames        = 0;  // delivered intact
    std::uint32_t resyncs       = 0;  // a byte discarded looking for a header
    std::uint32_t bad_length    = 0;  // header arrived with an impossible length
    std::uint32_t bad_crc       = 0;  // whole frame arrived corrupt
    std::uint32_t input_dropped = 0;  // bytes refused because the buffer was full
};

// A resynchronising decoder over a byte stream.
//
// Holds one frame's worth of bytes and no more. It allocates nothing, blocks on
// nothing and is safe to feed one byte at a time or two hundred — the fragment
// boundaries of the underlying transport are not allowed to change what comes
// out, which is the property `push()` and `next()` exist to guarantee.
class Decoder {
public:
    // One byte of slack beyond a maximum frame, so that a complete frame plus
    // the first byte of the next one can be held while the first is drained.
    static constexpr std::size_t kCapacity = kMaxFrame + 1;

    // Feed bytes. Returns how many were accepted; anything refused is counted
    // in `stats().input_dropped` rather than silently lost, because a transport
    // that overruns us is a fact worth having.
    std::size_t push(const std::uint8_t* data, std::size_t length);

    // Take the next complete frame, if there is one.
    //
    // Copies into `out` and reports what happened. One push may complete
    // several frames, so this is called in a loop — and the loop has exactly
    // one correct exit, which is `exhausted()` and not `!result`:
    //
    //     for (;;) {
    //         const FrameResult r = decoder.next(out, sizeof out);
    //         if (r) { deliver(out, r.length); continue; }  // r.length may be 0
    //         if (r.exhausted()) { break; }                 // NoFrame/Incomplete
    //         // OutputTooSmall: the frame is still queued, `r.length` says how
    //         // big it is, and leaving it there blocks this decoder.
    //     }
    //
    // A `Delivered` result whose `length` is 0 is an empty frame that really
    // arrived: a reason to keep draining, not to stop. Making that sayable is
    // what the return type is for. Give `out` `kMaxPayload` bytes and the third
    // case cannot occur at all.
    //
    // `out` may be null only for a frame with no payload, where there is
    // nothing to copy and the caller has therefore received all of it. For any
    // other frame a null `out` is a buffer that cannot hold it, reported as
    // `OutputTooSmall` with the frame kept.
    FrameResult next(std::uint8_t* out, std::size_t out_capacity);

    void reset();

    const DecoderStats& stats() const { return stats_; }
    std::size_t         buffered() const { return size_; }

private:
    void discard_front(std::size_t count);

    std::uint8_t  buffer_[kCapacity] = {};
    std::size_t   size_              = 0;
    DecoderStats  stats_{};
};

}  // namespace attadipa::link
