#include "attadipa/link/frame_codec.h"

#include <cstring>

namespace attadipa::link {
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

    // The CRC covers the three header bytes after the sync pattern — the two
    // length bytes and the length check — and then the whole payload. Three,
    // not two: the span starts at out[2] and must reach out[4 + length], which
    // is `length + 3` bytes. Getting this off by one leaves the LAST byte of
    // every frame unprotected, and because the decoder computes the same span
    // the two agree with each other and every round-trip test passes while a
    // corrupted final byte is delivered as good data. That is precisely the
    // upstream failure this format exists to avoid, with a checksum on top.
    const std::uint16_t crc = crc16_ccitt(out + 2, length + 3);
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

FrameResult Decoder::next(std::uint8_t* out, std::size_t out_capacity)
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
            // Not enough to judge; wait for more. An empty buffer and a header
            // part-way in are reported apart, because they are different facts
            // about the link and a caller can act on the difference.
            return {size_ == 0 ? FrameStatus::NoFrame : FrameStatus::Incomplete, 0};
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
            return {FrameStatus::Incomplete, 0};  // a real frame, still arriving
        }

        // `declared + 3`, matching encode(): two length bytes, the length check,
        // and the whole payload. See the comment there.
        const std::uint16_t expected = crc16_ccitt(buffer_ + 2, declared + 3);
        const std::uint16_t actual   = static_cast<std::uint16_t>(
            buffer_[kHeaderBytes + declared] | (buffer_[kHeaderBytes + declared + 1] << 8));

        if (expected != actual) {
            ++stats_.bad_crc;
            ++stats_.resyncs;
            discard_front(1);
            continue;
        }

        // A caller whose buffer is too small gets nothing and the frame stays
        // queued, rather than a partial copy it might mistake for the whole. It
        // is told how much room the frame needs, which is the difference
        // between an error it can act on and a stall it cannot explain.
        if (out == nullptr || out_capacity < declared) {
            return {FrameStatus::OutputTooSmall, declared};
        }

        if (declared != 0) {
            std::memcpy(out, buffer_ + kHeaderBytes, declared);
        }
        discard_front(needed);
        ++stats_.frames;
        // `stats_.frames` says "delivered intact", and this is the line that
        // has to make that true. It is only true because the caller can tell
        // this apart from a non-delivery without looking at `declared`, which
        // is 0 for an empty frame and was 0 for "nothing ready" as well.
        return {FrameStatus::Delivered, declared};
    }
}

void Decoder::reset()
{
    size_ = 0;
    // The statistics deliberately survive. A reset is what happens when a link
    // is restarted, and the whole reason the counters exist is to say how often
    // that has been necessary.
}

}  // namespace attadipa::link
