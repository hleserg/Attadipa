#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "firefly/link/frame_codec.h"

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

namespace firefly::link {

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
    bool push(const std::uint8_t* data, std::size_t length)
    {
        if (length > kMaxPayload || data == nullptr) {
            ++malformed_;
            return false;
        }
        if (count_ == Depth) {
            ++dropped_;
            return false;
        }
        const std::size_t slot = (head_ + count_) % Depth;
        std::memcpy(payload_[slot], data, length);
        length_[slot] = length;
        ++count_;
        ++accepted_;
        return true;
    }

    // Take the oldest frame. Returns its length, or 0 if the queue is empty or
    // the caller's buffer is too small — in which case the frame stays put.
    std::size_t pop(std::uint8_t* out, std::size_t out_capacity)
    {
        if (count_ == 0 || out == nullptr) {
            return 0;
        }
        const std::size_t length = length_[head_];
        if (out_capacity < length) {
            return 0;
        }
        if (length != 0) {
            std::memcpy(out, payload_[head_], length);
        }
        head_ = (head_ + 1) % Depth;
        --count_;
        return length;
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

}  // namespace firefly::link
