#include "attadipa/core/mesh_service.h"

#include <cstdint>

namespace attadipa::core {

const char* to_string(MeshDelivery delivery)
{
    switch (delivery) {
    case MeshDelivery::None:        return "none";
    case MeshDelivery::Queued:      return "queued";
    case MeshDelivery::Accepted:    return "accepted";
    case MeshDelivery::Confirmed:   return "confirmed";
    case MeshDelivery::Unconfirmed: return "unconfirmed";
    case MeshDelivery::Refused:     return "refused";
    case MeshDelivery::Unknown:     return "unknown";
    }
    // Not "unknown": that is a value of this enum now, and a fallback spelled
    // like a real state would report a corrupt byte as a legitimate verdict.
    return "invalid";
}

const char* to_string(MeshSendRefusal refusal)
{
    switch (refusal) {
    case MeshSendRefusal::None:                return "none";
    case MeshSendRefusal::LinkNotReady:        return "link not ready";
    case MeshSendRefusal::Busy:                return "busy";
    case MeshSendRefusal::EmptyBody:           return "empty body";
    case MeshSendRefusal::BodyTooLong:         return "body too long";
    case MeshSendRefusal::BodyNotUtf8:         return "body not utf-8";
    case MeshSendRefusal::TimestampOutOfRange: return "timestamp out of range";
    case MeshSendRefusal::RoomPasswordInvalid: return "room password invalid";
    case MeshSendRefusal::RingFull:            return "ring full";
    }
    return "invalid";
}

std::size_t utf8_prefix_length(std::string_view text, std::size_t limit)
{
    const std::size_t bound = limit < text.size() ? limit : text.size();
    // The smallest code point each width is allowed to encode. An over-long
    // form -- U+0000 written as two bytes, `/` written as three -- is the
    // classic way a filter is walked past, and it is not a character either
    // way, so it ends the prefix rather than being counted into it.
    static constexpr std::uint32_t kSmallest[5] = {0, 0, 0x80, 0x800, 0x10000};
    std::size_t at = 0;
    while (at < bound) {
        const auto lead = static_cast<unsigned char>(text[at]);
        std::size_t width = 0;
        std::uint32_t code = 0;
        if (lead < 0x80) {
            width = 1;
            code = lead;
        } else if ((lead & 0xE0) == 0xC0) {
            width = 2;
            code = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            width = 3;
            code = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            width = 4;
            code = lead & 0x07u;
        } else {
            // A continuation byte with nothing leading it, or a five- or
            // six-byte form that UTF-8 has not permitted since RFC 3629.
            return at;
        }
        // THE WHOLE POINT OF THE FUNCTION IS THIS LINE. A code point that
        // straddles the bound is not in the prefix at all -- the prefix ends
        // before it, so a caller that shortens to this length never emits half
        // a character.
        if (width > bound - at) return at;
        for (std::size_t i = 1; i < width; ++i) {
            const auto continuation = static_cast<unsigned char>(text[at + i]);
            if ((continuation & 0xC0) != 0x80) return at;
            code = (code << 6) | (continuation & 0x3Fu);
        }
        if (code < kSmallest[width]) return at;
        // Past the last code point, or inside the surrogate range, which UTF-8
        // never encodes -- those belong to UTF-16's pairing and are not
        // characters.
        if (code > 0x10FFFFu) return at;
        if (code >= 0xD800u && code <= 0xDFFFu) return at;
        at += width;
    }
    return at;
}

}  // namespace attadipa::core
