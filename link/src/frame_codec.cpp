#include "firefly/link/frame_codec.h"

#include <cstring>

namespace firefly::link {
namespace {

// One byte derived from the length, so a corrupted length is caught before the
// decoder commits to reading a payload that may not exist. The salt stops the
// common all-zero and all-0xFF cases from producing a self-consistent header
// out of a stuck bus.
constexpr std::uint8_t length_check(std::uint16_t length)
{
    return static_cast<std::uint8_t>((length & 0xFF) ^ (length >> 8) ^ 0x5A);
}

}  // namespace

std::uint16_t crc16_ccitt(const std::uint8_t* data, std::size_t length)
{
    // CRC-16/CCITT-FALSE: polynomial 0x1021, initial value 0xFFFF, no final
    // xor, no reflection. Bitwise rather than table-driven on purpose — 256
    // entries of flash to save a few hundred cycles on a 192-byte frame is a
    // bad trade on a part where flash is shared with the fonts.
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                                 : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::size_t encode(const std::uint8_t* payload, std::size_t length, std::uint8_t* out,
                   std::size_t out_capacity)
{
    // Refusing is the only honest failure here. Writing a shortened frame and
    // returning its length would put a truncated payload on the wire with a
    // valid CRC over it — upstream's bug, with the checksum added.
    if (length > kMaxPayload) {
        return 0;
    }
    if (out == nullptr || out_capacity < length + kOverheadBytes) {
        return 0;
    }
    if (payload == nullptr && length != 0) {
        return 0;
    }

    const std::uint16_t len = static_cast<std::uint16_t>(length);

    out[0] = kSync0;
    out[1] = kSync1;
    out[2] = static_cast<std::uint8_t>(len & 0xFF);
    out[3] = static_cast<std::uint8_t>(len >> 8);
    out[4] = length_check(len);
    if (length != 0) {
        std::memcpy(out + kHeaderBytes, payload, length);
    }

    // The CRC covers the length bytes as well as the payload, so a header that
    // survived its own check but was corrupted anyway still fails here.
    const std::uint16_t crc = crc16_ccitt(out + 2, length + 2);
    out[kHeaderBytes + length]     = static_cast<std::uint8_t>(crc & 0xFF);
    out[kHeaderBytes + length + 1] = static_cast<std::uint8_t>(crc >> 8);

    return length + kOverheadBytes;
}

std::size_t Decoder::push(const std::uint8_t* data, std::size_t length)
{
    if (data == nullptr || length == 0) {
        return 0;
    }
    const std::size_t room     = kCapacity - size_;
    const std::size_t accepted = length < room ? length : room;
    if (accepted != 0) {
        std::memcpy(buffer_ + size_, data, accepted);
        size_ += accepted;
    }
    if (accepted < length) {
        stats_.input_dropped += static_cast<std::uint32_t>(length - accepted);
    }
    return accepted;
}

void Decoder::discard_front(std::size_t count)
{
    if (count >= size_) {
        size_ = 0;
        return;
    }
    std::memmove(buffer_, buffer_ + count, size_ - count);
    size_ -= count;
}

std::size_t Decoder::next(std::uint8_t* out, std::size_t out_capacity)
{
    // The loop is the resynchronisation. Every failure discards exactly one
    // byte and tries again from the next, so a torn frame costs at most one
    // frame's worth of rescanning and cannot cause the decoder to skip past a
    // real header that happened to follow the damage.
    //
    // That is the difference from scanning forward to the next occurrence of a
    // magic byte: this cannot land inside a payload and invent a frame, because
    // the length check and then the CRC both have to agree before anything is
    // emitted.
    for (;;) {
        if (size_ < kHeaderBytes) {
            return 0;  // not enough to judge; wait for more
        }

        const bool header_ok =
            buffer_[0] == kSync0 && buffer_[1] == kSync1 &&
            buffer_[4] == length_check(static_cast<std::uint16_t>(buffer_[2] |
                                                                 (buffer_[3] << 8)));

        const std::size_t declared =
            static_cast<std::size_t>(buffer_[2] | (buffer_[3] << 8));

        if (!header_ok) {
            ++stats_.resyncs;
            discard_front(1);
            continue;
        }

        // An impossible length is an error, and it is counted as its own kind
        // of error rather than folded into "bad frame" — a link that produces
        // these is failing differently from one that produces CRC errors, and
        // the two have different causes.
        if (declared > kMaxPayload) {
            ++stats_.bad_length;
            ++stats_.resyncs;
            discard_front(1);
            continue;
        }

        const std::size_t needed = kHeaderBytes + declared + kTrailerBytes;
        if (size_ < needed) {
            return 0;  // a real frame, still arriving
        }

        const std::uint16_t expected = crc16_ccitt(buffer_ + 2, declared + 2);
        const std::uint16_t actual   = static_cast<std::uint16_t>(
            buffer_[kHeaderBytes + declared] | (buffer_[kHeaderBytes + declared + 1] << 8));

        if (expected != actual) {
            ++stats_.bad_crc;
            ++stats_.resyncs;
            discard_front(1);
            continue;
        }

        // A caller whose buffer is too small gets nothing and the frame stays
        // queued, rather than a partial copy it might mistake for the whole.
        if (out == nullptr || out_capacity < declared) {
            return 0;
        }

        if (declared != 0) {
            std::memcpy(out, buffer_ + kHeaderBytes, declared);
        }
        discard_front(needed);
        ++stats_.frames;
        return declared;
    }
}

void Decoder::reset()
{
    size_ = 0;
    // The statistics deliberately survive. A reset is what happens when a link
    // is restarted, and the whole reason the counters exist is to say how often
    // that has been necessary.
}

}  // namespace firefly::link
