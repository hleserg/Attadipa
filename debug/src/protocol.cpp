#include "attadipa/debug/protocol.h"

#include <cstring>

#include "attadipa/link/frame_codec.h"

namespace attadipa::debug {
namespace {

void put_u16(std::uint8_t* p, std::uint16_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
}

void put_u32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::uint16_t get_u16(const std::uint8_t* p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

std::uint32_t get_u32(const std::uint8_t* p)
{
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

// The envelope CRC covers the eight header bytes before it and then the body.
// Computed in two calls rather than by copying them together, because the body
// can be 182 bytes and a scratch copy per message is exactly the kind of buffer
// that has no home on an MCU.
std::uint16_t envelope_crc(const std::uint8_t* header8, const std::uint8_t* body,
                           std::size_t body_len)
{
    // CRC-16/CCITT over header then body. link::crc16_ccitt has no seeded form,
    // so the two are hashed into one buffer only when the body is small enough
    // to sit on the stack -- which it always is, since kMaxBody is 182.
    std::uint8_t scratch[8 + kMaxBody] = {};
    std::memcpy(scratch, header8, 8);
    if (body_len > 0 && body != nullptr) {
        std::memcpy(scratch + 8, body, body_len);
    }
    return link::crc16_ccitt(scratch, 8 + body_len);
}

}  // namespace

std::size_t encode_message(const Envelope& envelope, const std::uint8_t* body,
                           std::size_t body_len, std::uint8_t* out, std::size_t out_capacity)
{
    if (body_len > kMaxBody) {
        return 0;
    }
    if (out == nullptr || out_capacity < kEnvelopeBytes + body_len) {
        return 0;
    }

    out[0] = envelope.version;
    out[1] = envelope.cls;
    put_u16(out + 2, envelope.req_id);
    put_u16(out + 4, static_cast<std::uint16_t>(envelope.op));
    put_u16(out + 6, static_cast<std::uint16_t>(body_len));
    put_u16(out + 8, envelope_crc(out, body, body_len));

    if (body_len > 0 && body != nullptr) {
        std::memcpy(out + kEnvelopeBytes, body, body_len);
    }
    return kEnvelopeBytes + body_len;
}

bool decode_message(const std::uint8_t* payload, std::size_t payload_len, Envelope& envelope_out,
                    const std::uint8_t*& body_out)
{
    if (payload == nullptr || payload_len < kEnvelopeBytes) {
        return false;
    }

    const std::uint16_t body_len = get_u16(payload + 6);

    // The length is checked against what actually arrived before anything is
    // read past it -- the same discipline the framing applies to its own
    // length, and for the same reason.
    if (body_len > kMaxBody || kEnvelopeBytes + body_len != payload_len) {
        return false;
    }

    const std::uint8_t* body = payload + kEnvelopeBytes;
    if (get_u16(payload + 8) != envelope_crc(payload, body, body_len)) {
        return false;
    }

    envelope_out.version  = payload[0];
    envelope_out.cls      = payload[1];
    envelope_out.req_id   = get_u16(payload + 2);
    envelope_out.op       = static_cast<Opcode>(get_u16(payload + 4));
    envelope_out.body_len = body_len;
    body_out              = body;
    return true;
}

std::uint8_t bytes_per_pixel(PixelFormat format)
{
    switch (format) {
    case PixelFormat::Rgb888:
    case PixelFormat::Bgr888:
        return 3;
    case PixelFormat::Rgb565Le:
    case PixelFormat::Rgb565Be:
        return 2;
    case PixelFormat::Unknown:
        break;
    }
    return 0;
}

// --- Hello ---------------------------------------------------------------

std::size_t encode_hello(const HelloBody& in, std::uint8_t* out, std::size_t capacity)
{
    if (capacity < kHelloBodyBytes) {
        return 0;
    }
    out[0] = in.protocol_version;
    std::memcpy(out + 1, in.board_id, sizeof(in.board_id));
    std::memcpy(out + 1 + sizeof(in.board_id), in.build, sizeof(in.build));
    return kHelloBodyBytes;
}

bool decode_hello(const std::uint8_t* body, std::size_t len, HelloBody& out)
{
    if (body == nullptr || len < kHelloBodyBytes) {
        return false;
    }
    out.protocol_version = body[0];
    std::memcpy(out.board_id, body + 1, sizeof(out.board_id));
    std::memcpy(out.build, body + 1 + sizeof(out.board_id), sizeof(out.build));
    // A name that filled its field is still terminated, so that a caller can
    // print it without reading past the array.
    out.board_id[sizeof(out.board_id) - 1] = '\0';
    out.build[sizeof(out.build) - 1]       = '\0';
    return true;
}

// --- Capabilities ---------------------------------------------------------

std::size_t encode_capabilities(const CapabilitiesBody& in, std::uint8_t* out, std::size_t capacity)
{
    if (capacity < kCapabilitiesBodyBytes) {
        return 0;
    }
    put_u16(out + 0, in.width);
    put_u16(out + 2, in.height);
    out[4] = static_cast<std::uint8_t>(in.format);
    out[5] = static_cast<std::uint8_t>(in.orientation);
    out[6] = in.max_touch_points;
    out[7] = in.button_count;

    std::size_t at = 8;
    for (std::size_t i = 0; i < 4; ++i) {
        std::memcpy(out + at, in.buttons[i].id, sizeof(in.buttons[i].id));
        out[at + 16] = in.buttons[i].flags;
        at += 17;
    }
    put_u16(out + at, in.max_body);
    at += 2;
    put_u32(out + at, in.max_hold_ms);
    at += 4;
    put_u16(out + at, in.max_events_per_s);
    at += 2;
    return at;
}

bool decode_capabilities(const std::uint8_t* body, std::size_t len, CapabilitiesBody& out)
{
    if (body == nullptr || len < kCapabilitiesBodyBytes) {
        return false;
    }
    out.width            = get_u16(body + 0);
    out.height           = get_u16(body + 2);
    out.format           = static_cast<PixelFormat>(body[4]);
    out.orientation      = static_cast<Orientation>(body[5]);
    out.max_touch_points = body[6];
    // Clamped to what the struct holds. `button_count` arrives off the wire
    // and every consumer uses it as the loop bound over a fixed 4-slot array;
    // a device claiming five would walk past the end of a struct on the host.
    constexpr std::uint8_t kWireButtons =
        static_cast<std::uint8_t>(sizeof(out.buttons) / sizeof(out.buttons[0]));
    out.button_count     = body[7] > kWireButtons ? kWireButtons : body[7];

    std::size_t at = 8;
    for (std::size_t i = 0; i < 4; ++i) {
        std::memcpy(out.buttons[i].id, body + at, sizeof(out.buttons[i].id));
        out.buttons[i].id[sizeof(out.buttons[i].id) - 1] = '\0';
        out.buttons[i].flags                             = body[at + 16];
        at += 17;
    }
    out.max_body = get_u16(body + at);
    at += 2;
    out.max_hold_ms = get_u32(body + at);
    at += 4;
    out.max_events_per_s = get_u16(body + at);
    return true;
}

// --- ScreenInfo -----------------------------------------------------------

std::size_t encode_screen_info(const ScreenInfoBody& in, std::uint8_t* out, std::size_t capacity)
{
    if (capacity < kScreenInfoBodyBytes) {
        return 0;
    }
    put_u32(out + 0, in.frame_id);
    put_u16(out + 4, in.width);
    put_u16(out + 6, in.height);
    out[8] = static_cast<std::uint8_t>(in.format);
    out[9] = static_cast<std::uint8_t>(in.orientation);
    put_u32(out + 10, in.total_bytes);
    put_u32(out + 14, in.crc32);
    put_u32(out + 18, in.at_ms);
    return kScreenInfoBodyBytes;
}

bool decode_screen_info(const std::uint8_t* body, std::size_t len, ScreenInfoBody& out)
{
    if (body == nullptr || len < kScreenInfoBodyBytes) {
        return false;
    }
    out.frame_id    = get_u32(body + 0);
    out.width       = get_u16(body + 4);
    out.height      = get_u16(body + 6);
    out.format      = static_cast<PixelFormat>(body[8]);
    out.orientation = static_cast<Orientation>(body[9]);
    out.total_bytes = get_u32(body + 10);
    out.crc32       = get_u32(body + 14);
    out.at_ms       = get_u32(body + 18);
    return true;
}

// --- InputEvent -----------------------------------------------------------

std::size_t encode_input_event(const InputEventBody& in, std::uint8_t* out, std::size_t capacity)
{
    if (capacity < kInputEventBodyBytes) {
        return 0;
    }
    out[0] = in.type;
    out[1] = in.button;
    put_u16(out + 2, static_cast<std::uint16_t>(in.x));
    put_u16(out + 4, static_cast<std::uint16_t>(in.y));
    out[6] = in.touch_id;
    put_u32(out + 7, in.at_ms);
    return kInputEventBodyBytes;
}

bool decode_input_event(const std::uint8_t* body, std::size_t len, InputEventBody& out)
{
    if (body == nullptr || len < kInputEventBodyBytes) {
        return false;
    }
    out.type     = body[0];
    out.button   = body[1];
    out.x        = static_cast<std::int16_t>(get_u16(body + 2));
    out.y        = static_cast<std::int16_t>(get_u16(body + 4));
    out.touch_id = body[6];
    out.at_ms    = get_u32(body + 7);
    return true;
}

// --- CRC-32 ---------------------------------------------------------------

// Bitwise rather than table-driven. A 1 KiB table would be the largest constant
// in the debug subsystem, and this runs once per screenshot on a part where
// RESOURCE_BUDGET counts every kilobyte of flash.
//
// The cost is **not** microseconds, and an earlier version of this comment said
// it was. Eight iterations per byte over a 617 kB frame is 4.94 million inner
// loops: milliseconds on a desktop, and on an ESP32-S3 the dominant term in a
// capture that already blocks the interface. Recorded here rather than fixed
// because moving the CRC or the capture off the interface thread is a design
// change T-114 owns, and because a wrong number in a comment is how the next
// person decides not to measure.
std::uint32_t crc32(const std::uint8_t* data, std::size_t length, std::uint32_t seed)
{
    std::uint32_t crc = ~seed;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1u));
            crc                      = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

}  // namespace attadipa::debug
