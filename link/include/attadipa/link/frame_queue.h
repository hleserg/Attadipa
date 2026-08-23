#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "attadipa/link/frame_codec.h"

// A bounded queue of whole frames, with the drops counted.
//
// The shape is taken from the one place MeshCore gets this right: its ESP32
// BLE interface has fixed-depth send and receive queues and logs an explicit
// overflow (`"writeFrame(), send_queue is full!"`). Its USB path has neither,
// and `isWriteBusy()` returns false unconditionally — so `CMD_GET_CONTACTS`
// streams frames back to back into a 256-byte ring with no gate at all, and a
// stalled host tears a frame in half (docs/upstream/meshcore-1.17-review.md §2).
// The same codebase does it right on one transport and not at all on the other,
// which is what makes it a lesson rather than a limitation.
//
// Two rules, and both are RESOURCE_BUDGET §4's:
//
//   * **whole frame or nothing.** There is no partial push. A frame that does
//     not fit is refused and counted, so backpressure is a return value the
//     caller has to look at rather than a state it has to infer;
//   * **a declared maximum and a defined behaviour on reaching it.** Refuse the
//     newest, because dropping the oldest silently reorders a stream that the
//     protocol above assumes is ordered.
//
// Storage is a fixed array — no allocation, ever. On a long-uptime device the
// number that fails is not free heap but largest free block, and a queue that
// allocates per frame is how that number goes down and never comes back.

namespace attadipa::link {

// Four slots per direction, and the derivation rather than the number:
// kMaxPayload is 192 bytes, so a four-deep queue is 768 bytes per direction and
// 1.5 KB for a bidirectional interface. That is the depth MeshCore's BLE
// interface chose and survives with in the field, and it is small enough that
// four interfaces cost 6 KB of the 512 KB of internal SRAM. It is a starting
// point with a stated basis; `dropped()` is what turns it into a measured one.
inline constexpr std::size_t kDefaultQueueDepth = 4;

template <std::size_t Depth = kDefaultQueueDepth>
class FrameQueue {
public:
    static constexpr std::size_t depth = Depth;

    bool empty() const { return count_ == 0; }
    bool full() const { return count_ == Depth; }
    std::size_t size() const { return count_; }

    // Backpressure, as a value. This is what `isWriteBusy()` should have
    // returned, and the reason it could not is that nothing upstream owned a
    // queue to ask.
    bool writable() const { return count_ < Depth; }

    // Enqueue a whole frame. Returns false and counts a drop if it does not fit
    // or is too long — never a partial enqueue.
    //
    // A length of zero is accepted, and a null pointer is a caller error only
    // when there is something to copy. That is `encode()`'s rule, byte for
    // byte, and the two agreeing is the point: a zero-length frame means the
    // same thing at the encoder, at the decoder and here, so a frame that
    // survives one boundary cannot be refused by the next.
    bool push(const std::uint8_t* data, std::size_t length)
    {
        if (length > kMaxPayload || (data == nullptr && length != 0)) {
            ++malformed_;
            return false;
        }
        if (count_ == Depth) {
            ++dropped_;
            return false;
        }
        const std::size_t slot = (head_ + count_) % Depth;
        if (length != 0) {
            std::memcpy(payload_[slot], data, length);
        }
        length_[slot] = length;
        ++count_;
        ++accepted_;
        return true;
    }

    // Take the oldest frame, or say why there is none.
    //
    // The status is separate from the length for the reason `FrameStatus`
    // gives, and this container is half of that reason: it accepts a
    // zero-length frame, so returning a length alone would report a successful
    // pop with the same value as an empty queue, and a consumer draining until
    // zero would leave whatever was behind it stranded while `accepted()` went
    // on counting it as taken. `Delivered` with a `length` of 0 is a successful
    // pop of an empty frame.
    //
    // `Incomplete` never comes out of here: a queue holds whole frames or
    // nothing, which is the rule at the top of this file. A frame the caller's
    // buffer cannot hold stays put and is reported with the room it needs.
    FrameResult pop(std::uint8_t* out, std::size_t out_capacity)
    {
        if (count_ == 0) {
            return {FrameStatus::NoFrame, 0};
        }
        const std::size_t length = length_[head_];
        if (out == nullptr || out_capacity < length) {
            return {FrameStatus::OutputTooSmall, length};
        }
        if (length != 0) {
            std::memcpy(out, payload_[head_], length);
        }
        head_ = (head_ + 1) % Depth;
        --count_;
        return {FrameStatus::Delivered, length};
    }

    // How many frames were refused because the queue was full. A count that
    // rises in the field is the signal to change the depth — and the signal is
    // the point, because a depth chosen without one is a guess that nobody can
    // ever correct.
    std::uint32_t dropped() const { return dropped_; }
    std::uint32_t malformed() const { return malformed_; }
    std::uint32_t accepted() const { return accepted_; }

    // Empties the queue. Counters survive, for the same reason the decoder's do.
    void clear()
    {
        head_  = 0;
        count_ = 0;
    }

private:
    std::uint8_t  payload_[Depth][kMaxPayload] = {};
    std::size_t   length_[Depth]               = {};
    std::size_t   head_                        = 0;
    std::size_t   count_                       = 0;
    std::uint32_t dropped_                     = 0;
    std::uint32_t malformed_                   = 0;
    std::uint32_t accepted_                    = 0;
};

}  // namespace attadipa::link
