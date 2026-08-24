#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "attadipa/debug/bridge.h"
#include "attadipa/debug/protocol.h"
#include "attadipa/link/frame_codec.h"

// Host tests for the debug channel: the wire format, and the state machine
// behind it.
//
// None of this needs a device, a socket or LVGL, which is the reason the bridge
// takes its transport and its frame source as seams rather than owning them.
// What that buys is that the failure modes worth testing are testable: a
// corrupted envelope, a length that disagrees with what arrived, a screenshot
// requested twice, a client that disconnects mid-swipe, a second finger on a
// single-touch stack, a button held past the cap.
//
// The tests that matter most are the ones asserting a *typed error* comes back.
// ADR-0005 §4 requires that an unknown opcode is answered rather than ignored,
// and the reason is visible from the host side: silence and a lost frame look
// identical, so a client that gets neither an answer nor an error retries until
// it times out and then reports the wrong cause.

using namespace attadipa;
using namespace attadipa::debug;

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

// --- a screen source that renders nothing but knows its own shape ----------

class FakeScreen : public ScreenSource {
public:
    FakeScreen(std::uint16_t w, std::uint16_t h, PixelFormat f) : w_(w), h_(h), f_(f)
    {
        image_.resize(static_cast<std::size_t>(w) * h * bytes_per_pixel(f));
        // An asymmetric fill, so that a test which reassembled the chunks in
        // the wrong order fails instead of comparing a field of zeroes to a
        // field of zeroes.
        for (std::size_t i = 0; i < image_.size(); ++i) {
            image_[i] = static_cast<std::uint8_t>((i * 7u + 11u) & 0xFFu);
        }
        std::strncpy(buttons_[0].id, "power", sizeof(buttons_[0].id) - 1);
        buttons_[0].flags = kButtonInjectable;
        std::strncpy(buttons_[1].id, "side", sizeof(buttons_[1].id) - 1);
        buttons_[1].flags = kButtonInjectable | kButtonRoleUnknown;
    }

    bool capture(std::uint8_t* out, std::size_t capacity, std::uint16_t& width_out,
                 std::uint16_t& height_out, PixelFormat& format_out, Orientation& orientation_out,
                 std::size_t& bytes_out, Failure& why_out) override
    {
        width_out       = w_;
        height_out      = h_;
        format_out      = f_;
        orientation_out = Orientation::Deg0;
        bytes_out       = understated_ != 0 ? understated_ : image_.size();
        why_out         = Failure::None;

        if (fail_capture_ != Failure::None) {
            bytes_out = 0;
            why_out   = fail_capture_;
            return false;
        }
        // A metadata-only query: the caller passed no buffer, so the shape is
        // filled and the copy refused. This is how Capabilities asks.
        if (out == nullptr || capacity == 0) {
            why_out = Failure::ShapeQuery;
            return false;
        }
        if (capacity < image_.size() || understated_ != 0) {
            // The second arm is the contradiction being modelled: the shape
            // query said `understated_` bytes, the caller sized its buffer for
            // that, and the copy now wants more. A real source does this by
            // being wrong, not by being asked.
            why_out = Failure::BufferTooSmall;
            return false;
        }
        std::memcpy(out, image_.data(), image_.size());
        return true;
    }

    // Report a shape smaller than the image actually is, so the bridge's
    // pre-screen passes and the copy then refuses. That is the only way to reach
    // the `BufferTooSmall` arm of the failure mapping -- a source disagreeing
    // with its own shape query -- and without it that arm has never run.
    void understate_shape(std::size_t bytes) { understated_ = bytes; }

    // No idle tracking of its own -- the rig sets the answer. Spelled out
    // rather than inherited: the base class used to default this to `true`, and
    // a double that inherits "yes, settled" turns every assertion resting on it
    // into a pass. That is how the vacuous step survived a review.
    bool stable_since(std::uint32_t ms) const override { return idle_ms_ >= ms; }
    void set_idle(std::uint32_t ms) { idle_ms_ = ms; }

    const char*             board_id() const override { return "waveshare-amoled-206"; }
    const char*             build_id() const override { return "test"; }
    std::uint8_t            button_count() const override { return 2; }
    const ButtonDescriptor* buttons() const override { return buttons_; }

    const std::vector<std::uint8_t>& image() const { return image_; }
    void fail_capture(Failure why) { fail_capture_ = why; }

    std::uint32_t idle_ms_ = 0;

    // Every button was injectable until now, which is exactly why the refusal
    // path had never existed: the board profile that motivates the flag
    // (T-Watch BOOT) was not represented anywhere a test could see it.
    void set_injectable(std::uint8_t index, bool on)
    {
        if (index >= 2) {
            return;
        }
        if (on) {
            buttons_[index].flags |= kButtonInjectable;
        } else {
            buttons_[index].flags = static_cast<std::uint8_t>(buttons_[index].flags &
                                                              ~kButtonInjectable);
        }
    }

private:
    std::uint16_t             w_;
    std::uint16_t             h_;
    PixelFormat               f_;
    std::vector<std::uint8_t> image_;
    ButtonDescriptor          buttons_[2] = {};
    Failure                   fail_capture_ = Failure::None;
    std::size_t               understated_  = 0;
};

// --- collecting what the bridge emits -------------------------------------

struct Collector {
    std::vector<std::vector<std::uint8_t>> messages;

    static void emit(void* ctx, const std::uint8_t* payload, std::size_t length)
    {
        auto* self = static_cast<Collector*>(ctx);
        self->messages.emplace_back(payload, payload + length);
    }

    void clear() { messages.clear(); }

    bool last_is(Opcode op) const
    {
        if (messages.empty()) {
            return false;
        }
        Envelope            e;
        const std::uint8_t* body = nullptr;
        if (!decode_message(messages.back().data(), messages.back().size(), e, body)) {
            return false;
        }
        return e.op == op;
    }

    ErrorCode last_error() const
    {
        if (messages.empty()) {
            return ErrorCode::None;
        }
        Envelope            e;
        const std::uint8_t* body = nullptr;
        if (!decode_message(messages.back().data(), messages.back().size(), e, body)) {
            return ErrorCode::None;
        }
        if (e.op != Opcode::Error || e.body_len < 2) {
            return ErrorCode::None;
        }
        return static_cast<ErrorCode>(static_cast<std::uint16_t>(body[0] | (body[1] << 8)));
    }
};

std::vector<std::uint8_t> request(Opcode op, std::uint16_t req_id, const std::uint8_t* body = nullptr,
                                  std::size_t body_len = 0)
{
    Envelope e;
    e.op     = op;
    e.req_id = req_id;

    std::vector<std::uint8_t> out(kEnvelopeBytes + kMaxBody);
    const std::size_t n = encode_message(e, body, body_len, out.data(), out.size());
    out.resize(n);
    return out;
}

// --- the wire format ------------------------------------------------------

void an_envelope_survives_a_round_trip()
{
    const std::uint8_t body[] = {1, 2, 3, 4, 5};

    Envelope out;
    out.req_id = 0xBEEF;
    out.op     = Opcode::InputEvent;

    std::uint8_t buffer[64] = {};
    const std::size_t n = encode_message(out, body, sizeof(body), buffer, sizeof(buffer));
    CHECK(n == kEnvelopeBytes + sizeof(body));

    Envelope            back;
    const std::uint8_t* body_back = nullptr;
    CHECK(decode_message(buffer, n, back, body_back));
    CHECK(back.req_id == 0xBEEF);
    CHECK(back.op == Opcode::InputEvent);
    CHECK(back.body_len == sizeof(body));
    CHECK(back.cls == kClassDebug);
    CHECK(std::memcmp(body_back, body, sizeof(body)) == 0);
}

void a_flipped_bit_anywhere_is_rejected()
{
    const std::uint8_t body[] = {9, 8, 7, 6};
    Envelope           out;
    out.op = Opcode::Hello;

    std::uint8_t buffer[64] = {};
    const std::size_t n = encode_message(out, body, sizeof(body), buffer, sizeof(buffer));

    // Every byte, one at a time. A CRC that covered only the header would pass
    // the body bytes, and a screenshot chunk is all body.
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t corrupted[64] = {};
        std::memcpy(corrupted, buffer, n);
        corrupted[i] ^= 0x40u;

        Envelope            back;
        const std::uint8_t* body_back = nullptr;
        // Corrupting the length byte changes the expected size, which the
        // length check rejects before the CRC is even consulted. Either way it
        // must not decode.
        CHECK(!decode_message(corrupted, n, back, body_back));
    }
}

void a_length_that_disagrees_with_the_payload_is_rejected()
{
    const std::uint8_t body[] = {1, 2, 3};
    Envelope           out;
    out.op = Opcode::Hello;

    std::uint8_t buffer[64] = {};
    const std::size_t n = encode_message(out, body, sizeof(body), buffer, sizeof(buffer));

    Envelope            back;
    const std::uint8_t* body_back = nullptr;
    // One byte short, and one byte long. Both are a payload that does not match
    // its declared body length; neither may be read as a shorter valid message.
    CHECK(!decode_message(buffer, n - 1, back, body_back));
    CHECK(!decode_message(buffer, n + 1, back, body_back));
    CHECK(!decode_message(buffer, kEnvelopeBytes - 1, back, body_back));
}

void a_body_too_large_for_a_frame_is_refused_not_truncated()
{
    std::vector<std::uint8_t> huge(kMaxBody + 1, 0xAB);
    Envelope                  out;
    std::vector<std::uint8_t> buffer(1024);
    CHECK(encode_message(out, huge.data(), huge.size(), buffer.data(), buffer.size()) == 0);
}

void an_output_buffer_too_small_yields_nothing()
{
    const std::uint8_t body[] = {1, 2, 3};
    Envelope           out;
    std::uint8_t       tiny[4] = {};
    CHECK(encode_message(out, body, sizeof(body), tiny, sizeof(tiny)) == 0);
}

void the_message_fits_inside_one_frame()
{
    // The whole point of chunking rather than raising kMaxPayload: a maximal
    // debug message still fits the framing the node link was reviewed at.
    std::vector<std::uint8_t> body(kMaxBody, 0x5A);
    Envelope                  out;
    std::uint8_t              message[kEnvelopeBytes + kMaxBody] = {};
    const std::size_t n = encode_message(out, body.data(), body.size(), message, sizeof(message));
    CHECK(n == link::kMaxPayload);

    std::uint8_t frame[link::kMaxFrame] = {};
    CHECK(link::encode(message, n, frame, sizeof(frame)) == link::kMaxFrame);
}

// The literals below are asserted identically in tools/watch/selftest.py.
//
// Two independent implementations of one format catch the bug a shared one
// cannot: an encoder and a decoder that agree with each other while both being
// wrong. Round-trip tests on either side pass in that case; a fixed byte string
// does not. If either implementation drifts, one of the two suites fails and
// the hex says where.
void the_wire_bytes_are_pinned_to_a_literal()
{
    // frame_encode(b"hello") == f15e05005f68656c6c6f750f
    const std::uint8_t payload[] = {'h', 'e', 'l', 'l', 'o'};
    const std::uint8_t expected[] = {0xF1, 0x5E, 0x05, 0x00, 0x5F, 'h',  'e',
                                     'l',  'l',  'o',  0x75, 0x0F};

    std::uint8_t frame[link::kMaxFrame] = {};
    const std::size_t n = link::encode(payload, sizeof(payload), frame, sizeof(frame));
    CHECK(n == sizeof(expected));
    CHECK(std::memcmp(frame, expected, sizeof(expected)) == 0);

    // The algorithm's own published check value, so the CRC is pinned to the
    // standard and not merely to this project's other copy of it.
    const char* check_string = "123456789";
    CHECK(link::crc16_ccitt(reinterpret_cast<const std::uint8_t*>(check_string), 9) == 0x29B1);

    // The salt, which is what stops a stuck bus from producing a self-consistent
    // header out of all-zeroes or all-ones.
    std::uint8_t empty_frame[link::kMaxFrame] = {};
    CHECK(link::encode(nullptr, 0, empty_frame, sizeof(empty_frame)) == link::kOverheadBytes);
    CHECK(empty_frame[4] == 0x5A);
}

// One whole message per body kind, envelope included, byte for byte.
//
// `the_wire_bytes_are_pinned_to_a_literal` above pins `link::frame_codec` --
// the framing, which this channel did not write. The envelope, the bodies and
// the two numbering tables were round-tripped independently on each side and
// compared nowhere, while four documents said the two implementations were
// "pinned to the same bytes". They were pinned to each other, which is the one
// thing a fixed literal exists to rule out: a mistake made identically on both
// sides -- a swapped field, a wrong width -- passes every round trip.
//
// The same hex appears in `tools/watch/selftest.py`. Neither side generated it
// from the other at review time: it was produced here and then decoded by the
// Python implementation, which arrived at the same field values through its own
// `struct` formats. Every field is a distinct value on purpose, so a transposed
// pair cannot survive -- `x` is negative, `width` and `height` differ.
const char* const kHelloOkHex =
    "01023412018031007b28017761766573686172652d616d6f6c65642d3230360000"
    "000073696d20302e302e31000000000000000000000000000000";
const char* const kScreenInfoHex =
    "01027856108016008e4b443322119a01f6010201144b06002639f4cb4e61bc00";
const char* const kInputEventHex =
    "0102bc9a20000b00a1a00301feff2c0107feff0000";

std::vector<std::uint8_t> from_hex(const char* hex)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; hex[i] != '\0'; i += 2) {
        auto nibble = [](char c) -> int {
            return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
        };
        out.push_back(static_cast<std::uint8_t>(nibble(hex[i]) * 16 + nibble(hex[i + 1])));
    }
    return out;
}

void whole_messages_are_pinned_to_literals_the_other_implementation_also_holds()
{
    std::uint8_t msg[512] = {};

    HelloBody hello;
    hello.protocol_version = kDebugProtocolVersion;
    std::strncpy(hello.board_id, "waveshare-amoled-206", sizeof(hello.board_id) - 1);
    std::strncpy(hello.build, "sim 0.0.1", sizeof(hello.build) - 1);
    std::uint8_t hello_body[kHelloBodyBytes] = {};
    const std::size_t hn = encode_hello(hello, hello_body, sizeof(hello_body));
    Envelope he;
    he.op     = Opcode::HelloOk;
    he.req_id = 0x1234;
    const std::size_t hlen     = encode_message(he, hello_body, hn, msg, sizeof(msg));
    const auto        expected_hello = from_hex(kHelloOkHex);
    CHECK(hlen == expected_hello.size());
    CHECK(std::memcmp(msg, expected_hello.data(), expected_hello.size()) == 0);

    ScreenInfoBody info;
    info.frame_id    = 0x11223344;
    info.width       = 410;
    info.height      = 502;
    info.format      = PixelFormat::Rgb565Le;
    info.orientation = Orientation::Deg90;
    info.total_bytes = 0x00064B14;
    info.crc32       = 0xCBF43926;
    info.at_ms       = 0x00BC614E;
    std::uint8_t info_body[kScreenInfoBodyBytes] = {};
    const std::size_t sn = encode_screen_info(info, info_body, sizeof(info_body));
    Envelope se;
    se.op     = Opcode::ScreenInfo;
    se.req_id = 0x5678;
    const std::size_t slen          = encode_message(se, info_body, sn, msg, sizeof(msg));
    const auto        expected_info = from_hex(kScreenInfoHex);
    CHECK(slen == expected_info.size());
    CHECK(std::memcmp(msg, expected_info.data(), expected_info.size()) == 0);

    InputEventBody event;
    event.type     = 3;
    event.button   = 1;
    event.x        = -2;
    event.y        = 300;
    event.touch_id = 7;
    event.at_ms    = 0x0000FFFE;
    std::uint8_t event_body[kInputEventBodyBytes] = {};
    const std::size_t in = encode_input_event(event, event_body, sizeof(event_body));
    Envelope ee;
    ee.op     = Opcode::InputEvent;
    ee.req_id = 0x9ABC;
    const std::size_t elen           = encode_message(ee, event_body, in, msg, sizeof(msg));
    const auto        expected_event = from_hex(kInputEventHex);
    CHECK(elen == expected_event.size());
    CHECK(std::memcmp(msg, expected_event.data(), expected_event.size()) == 0);

    // And the other direction, so a decoder that drifted alone is caught too.
    Envelope            back;
    const std::uint8_t* body = nullptr;
    CHECK(decode_message(expected_event.data(), expected_event.size(), back, body));
    InputEventBody decoded;
    CHECK(decode_input_event(body, back.body_len, decoded));
    CHECK(decoded.x == -2);  // the sign, which is the field a width bug eats
    CHECK(decoded.y == 300);
    CHECK(decoded.touch_id == 7);
}

void the_two_numbering_tables_are_spelled_out()
{
    // `protocol.h` says these are appended and never renumbered, because the
    // Python side mirrors them by hand and a moved number is a silently
    // mistranslated error message rather than a build failure. Writing the
    // values out is what turns that from a wish into a check -- swapping
    // `QueueFull` and `CaptureFailed` used to leave every suite green while an
    // operator read "the renderer could not produce a frame" for a swipe point
    // the input queue dropped.
    CHECK(static_cast<std::uint16_t>(Opcode::Hello) == 0x0001);
    CHECK(static_cast<std::uint16_t>(Opcode::Capabilities) == 0x0002);
    CHECK(static_cast<std::uint16_t>(Opcode::ScreenRequest) == 0x0010);
    CHECK(static_cast<std::uint16_t>(Opcode::InputEvent) == 0x0020);
    CHECK(static_cast<std::uint16_t>(Opcode::InputReset) == 0x0021);
    CHECK(static_cast<std::uint16_t>(Opcode::WaitStable) == 0x0030);
    CHECK(static_cast<std::uint16_t>(Opcode::HelloOk) == 0x8001);
    CHECK(static_cast<std::uint16_t>(Opcode::CapabilitiesOk) == 0x8002);
    CHECK(static_cast<std::uint16_t>(Opcode::ScreenInfo) == 0x8010);
    CHECK(static_cast<std::uint16_t>(Opcode::ScreenData) == 0x8011);
    CHECK(static_cast<std::uint16_t>(Opcode::ScreenEnd) == 0x8012);
    CHECK(static_cast<std::uint16_t>(Opcode::InputOk) == 0x8020);
    CHECK(static_cast<std::uint16_t>(Opcode::StableOk) == 0x8030);
    CHECK(static_cast<std::uint16_t>(Opcode::Error) == 0x80FF);

    CHECK(static_cast<std::uint16_t>(ErrorCode::None) == 0);
    CHECK(static_cast<std::uint16_t>(ErrorCode::UnknownOpcode) == 1);
    CHECK(static_cast<std::uint16_t>(ErrorCode::BadBody) == 2);
    CHECK(static_cast<std::uint16_t>(ErrorCode::Unsupported) == 3);
    CHECK(static_cast<std::uint16_t>(ErrorCode::BadInput) == 4);
    CHECK(static_cast<std::uint16_t>(ErrorCode::TooManyTouches) == 5);
    CHECK(static_cast<std::uint16_t>(ErrorCode::NoScreen) == 6);
    CHECK(static_cast<std::uint16_t>(ErrorCode::Busy) == 7);
    CHECK(static_cast<std::uint16_t>(ErrorCode::RateLimited) == 8);
    CHECK(static_cast<std::uint16_t>(ErrorCode::VersionMismatch) == 9);
    CHECK(static_cast<std::uint16_t>(ErrorCode::QueueFull) == 10);
    CHECK(static_cast<std::uint16_t>(ErrorCode::CaptureFailed) == 11);
    CHECK(static_cast<std::uint16_t>(ErrorCode::ScreenGeometry) == 12);
}

void crc32_matches_the_published_vector()
{
    const char* check_string = "123456789";
    CHECK(crc32(reinterpret_cast<const std::uint8_t*>(check_string), 9) == 0xCBF43926u);
    CHECK(crc32(nullptr, 0) == 0u);
}

void the_bodies_survive_a_round_trip()
{
    HelloBody hello;
    std::strncpy(hello.board_id, "t-watch-s3-plus", sizeof(hello.board_id) - 1);
    std::strncpy(hello.build, "simulator", sizeof(hello.build) - 1);

    std::uint8_t buffer[kHelloBodyBytes] = {};
    CHECK(encode_hello(hello, buffer, sizeof(buffer)) == kHelloBodyBytes);

    HelloBody back;
    CHECK(decode_hello(buffer, kHelloBodyBytes, back));
    CHECK(std::strcmp(back.board_id, "t-watch-s3-plus") == 0);
    CHECK(std::strcmp(back.build, "simulator") == 0);
    CHECK(!decode_hello(buffer, kHelloBodyBytes - 1, back));

    ScreenInfoBody info;
    info.frame_id    = 7;
    info.width       = 410;
    info.height      = 502;
    info.format      = PixelFormat::Rgb565Le;
    info.orientation = Orientation::Deg270;
    info.total_bytes = 410u * 502u * 2u;
    info.crc32       = 0xDEADBEEF;
    info.at_ms       = 999;

    std::uint8_t info_buffer[kScreenInfoBodyBytes] = {};
    CHECK(encode_screen_info(info, info_buffer, sizeof(info_buffer)) == kScreenInfoBodyBytes);

    ScreenInfoBody info_back;
    CHECK(decode_screen_info(info_buffer, kScreenInfoBodyBytes, info_back));
    CHECK(info_back.width == 410);
    CHECK(info_back.height == 502);
    CHECK(info_back.format == PixelFormat::Rgb565Le);
    CHECK(info_back.orientation == Orientation::Deg270);
    CHECK(info_back.total_bytes == 410u * 502u * 2u);
    CHECK(info_back.crc32 == 0xDEADBEEFu);

    InputEventBody event;
    event.type     = 3;
    event.x        = -12;  // negative coordinates survive the u16 transport
    event.y        = 1000;
    event.touch_id = 0;
    event.at_ms    = 123456;

    std::uint8_t event_buffer[kInputEventBodyBytes] = {};
    CHECK(encode_input_event(event, event_buffer, sizeof(event_buffer)) == kInputEventBodyBytes);

    InputEventBody event_back;
    CHECK(decode_input_event(event_buffer, kInputEventBodyBytes, event_back));
    CHECK(event_back.x == -12);
    CHECK(event_back.y == 1000);
    CHECK(event_back.at_ms == 123456u);
}

void bytes_per_pixel_is_defined_for_every_format()
{
    CHECK(bytes_per_pixel(PixelFormat::Rgb888) == 3);
    CHECK(bytes_per_pixel(PixelFormat::Bgr888) == 3);
    CHECK(bytes_per_pixel(PixelFormat::Rgb565Le) == 2);
    CHECK(bytes_per_pixel(PixelFormat::Rgb565Be) == 2);
    CHECK(bytes_per_pixel(PixelFormat::Unknown) == 0);
}

// --- the bridge -----------------------------------------------------------

struct Rig {
    FakeScreen                screen;
    core::InputQueue          queue;
    core::InputState          state;
    std::vector<std::uint8_t> frame;
    Bridge                    bridge;
    Collector                 sink;

    Rig(std::uint16_t w = 40, std::uint16_t h = 30, PixelFormat f = PixelFormat::Rgb888,
        bool with_buffer = true)
        : screen(w, h, f), frame(with_buffer ? screen.image().size() : 0),
          bridge(queue, state, screen, with_buffer ? frame.data() : nullptr, frame.size())
    {
    }

    void send(const std::vector<std::uint8_t>& message, std::uint32_t now = 0)
    {
        bridge.handle(message.data(), message.size(), now, &Collector::emit, &sink);
    }
};

void an_unknown_opcode_is_answered_with_a_typed_error()
{
    Rig rig;
    rig.send(request(static_cast<Opcode>(0x4242), 1));
    CHECK(rig.sink.messages.size() == 1);
    CHECK(rig.sink.last_is(Opcode::Error));
    CHECK(rig.sink.last_error() == ErrorCode::UnknownOpcode);
}

void a_wrong_version_is_answered_rather_than_ignored()
{
    Rig      rig;
    Envelope e;
    e.version = kDebugProtocolVersion + 1;
    e.op      = Opcode::Capabilities;
    e.req_id  = 5;

    std::vector<std::uint8_t> message(kEnvelopeBytes);
    message.resize(encode_message(e, nullptr, 0, message.data(), message.size()));
    rig.send(message);
    CHECK(rig.sink.last_error() == ErrorCode::VersionMismatch);
}

void a_handshake_at_the_wrong_version_still_says_what_this_device_is()
{
    // The half the gate used to make unreachable. ADR-0005 section 5 negotiates
    // the version inside the HELLO exchange, so refusing HELLO on a version
    // mismatch refuses the only message that could have resolved it -- and the
    // host never learns the board id or the build string, which is what tells a
    // reader a screenshot came from a simulator.
    Rig       rig;
    HelloBody hello;
    hello.protocol_version = kDebugProtocolVersion + 1;

    std::uint8_t body[kHelloBodyBytes] = {};
    encode_hello(hello, body, sizeof(body));

    Envelope e;
    e.version = kDebugProtocolVersion + 1;
    e.op      = Opcode::Hello;
    e.req_id  = 6;

    std::vector<std::uint8_t> message(kEnvelopeBytes + sizeof(body));
    message.resize(encode_message(e, body, sizeof(body), message.data(), message.size()));
    rig.send(message);

    CHECK(rig.sink.last_is(Opcode::HelloOk));

    Envelope            reply;
    const std::uint8_t* reply_body = nullptr;
    CHECK(decode_message(rig.sink.messages.back().data(), rig.sink.messages.back().size(), reply,
                         reply_body));
    HelloBody back;
    CHECK(decode_hello(reply_body, reply.body_len, back));
    // Its own version, not the caller's -- the host is told what it is talking
    // to and decides. `client.py` prints exactly this comparison.
    CHECK(back.protocol_version == kDebugProtocolVersion);
    CHECK(std::strcmp(back.board_id, "waveshare-amoled-206") == 0);

    // And the leniency stops at the handshake: the next request at that version
    // is still refused, so a host that was told and carried on is not indulged.
    rig.sink.clear();
    Envelope after;
    after.version = kDebugProtocolVersion + 1;
    after.op      = Opcode::Capabilities;
    after.req_id  = 7;
    std::vector<std::uint8_t> next(kEnvelopeBytes);
    next.resize(encode_message(after, nullptr, 0, next.data(), next.size()));
    rig.send(next);
    CHECK(rig.sink.last_error() == ErrorCode::VersionMismatch);
}

void a_message_from_another_class_is_not_ours_to_execute()
{
    Rig      rig;
    Envelope e;
    e.cls    = kClassNode;
    e.op     = Opcode::InputReset;
    e.req_id = 9;

    std::vector<std::uint8_t> message(kEnvelopeBytes);
    message.resize(encode_message(e, nullptr, 0, message.data(), message.size()));
    rig.send(message);
    CHECK(rig.sink.last_error() == ErrorCode::UnknownOpcode);
}

void an_undecodable_message_gets_no_reply_at_all()
{
    Rig                       rig;
    std::vector<std::uint8_t> junk = {0x01, 0x02, 0x03};
    rig.send(junk);
    // No reply, on purpose: req_id was one of the fields that could not be
    // trusted, so there is nothing to correlate an answer to.
    CHECK(rig.sink.messages.empty());
}

void hello_reports_the_board_and_the_version()
{
    Rig       rig;
    HelloBody hello;
    hello.protocol_version = kDebugProtocolVersion;

    std::uint8_t body[kHelloBodyBytes] = {};
    encode_hello(hello, body, sizeof(body));
    rig.send(request(Opcode::Hello, 3, body, sizeof(body)));

    CHECK(rig.sink.last_is(Opcode::HelloOk));

    Envelope            e;
    const std::uint8_t* reply_body = nullptr;
    CHECK(decode_message(rig.sink.messages.back().data(), rig.sink.messages.back().size(), e,
                         reply_body));
    CHECK(e.req_id == 3);

    HelloBody back;
    CHECK(decode_hello(reply_body, e.body_len, back));
    CHECK(std::strcmp(back.board_id, "waveshare-amoled-206") == 0);
    CHECK(back.protocol_version == kDebugProtocolVersion);
}

void hello_with_a_short_body_is_a_typed_error()
{
    Rig                rig;
    const std::uint8_t stub[2] = {1, 0};
    rig.send(request(Opcode::Hello, 4, stub, sizeof(stub)));
    CHECK(rig.sink.last_error() == ErrorCode::BadBody);
}

void capabilities_report_the_panel_and_the_buttons()
{
    Rig rig(410, 502, PixelFormat::Rgb888);
    rig.send(request(Opcode::Capabilities, 11));
    CHECK(rig.sink.last_is(Opcode::CapabilitiesOk));

    Envelope            e;
    const std::uint8_t* body = nullptr;
    decode_message(rig.sink.messages.back().data(), rig.sink.messages.back().size(), e, body);

    CapabilitiesBody caps;
    CHECK(decode_capabilities(body, e.body_len, caps));
    CHECK(caps.width == 410);
    CHECK(caps.height == 502);
    CHECK(caps.format == PixelFormat::Rgb888);
    CHECK(caps.button_count == 2);
    CHECK(std::strcmp(caps.buttons[0].id, "power") == 0);
    CHECK(std::strcmp(caps.buttons[1].id, "side") == 0);
    // The honesty flag: this board's second button has no established role.
    CHECK((caps.buttons[1].flags & kButtonRoleUnknown) != 0);
    // Single touch, and the number comes from the stack rather than a guess.
    CHECK(caps.max_touch_points == core::kMaxTouchPoints);
    CHECK(caps.max_touch_points == 1);
}

// The whole screenshot path, reassembled the way the host tool does it.
void a_screenshot_arrives_whole_and_in_order()
{
    Rig rig(40, 30, PixelFormat::Rgb888);
    rig.send(request(Opcode::ScreenRequest, 21));
    CHECK(rig.sink.last_is(Opcode::ScreenInfo));

    Envelope            e;
    const std::uint8_t* body = nullptr;
    decode_message(rig.sink.messages.back().data(), rig.sink.messages.back().size(), e, body);

    ScreenInfoBody info;
    CHECK(decode_screen_info(body, e.body_len, info));
    CHECK(info.width == 40);
    CHECK(info.height == 30);
    CHECK(info.total_bytes == 40u * 30u * 3u);

    std::vector<std::uint8_t> assembled(info.total_bytes, 0);
    std::vector<bool>         written(info.total_bytes, false);

    rig.sink.clear();
    int guard = 0;
    while (rig.bridge.pump(&Collector::emit, &rig.sink)) {
        CHECK(++guard < 100000);
    }
    // The final pump emits ScreenEnd.
    CHECK(rig.sink.last_is(Opcode::ScreenEnd));

    std::size_t chunks = 0;
    for (const auto& message : rig.sink.messages) {
        Envelope            me;
        const std::uint8_t* mb = nullptr;
        CHECK(decode_message(message.data(), message.size(), me, mb));
        CHECK(me.req_id == 21);
        if (me.op != Opcode::ScreenData) {
            continue;
        }
        ++chunks;
        const std::uint32_t offset = static_cast<std::uint32_t>(mb[0]) |
                                     (static_cast<std::uint32_t>(mb[1]) << 8) |
                                     (static_cast<std::uint32_t>(mb[2]) << 16) |
                                     (static_cast<std::uint32_t>(mb[3]) << 24);
        const std::size_t take = me.body_len - 4;
        CHECK(offset + take <= assembled.size());
        for (std::size_t i = 0; i < take; ++i) {
            // Each byte written exactly once: a doubled chunk is a bug the
            // image CRC would catch, and this says which chunk.
            CHECK(!written[offset + i]);
            written[offset + i]  = true;
            assembled[offset + i] = mb[4 + i];
        }
    }
    CHECK(chunks > 1);
    for (std::size_t i = 0; i < written.size(); ++i) {
        CHECK(written[i]);
    }
    CHECK(assembled == rig.screen.image());
    CHECK(crc32(assembled.data(), assembled.size()) == info.crc32);
    CHECK(rig.bridge.stats().frames_sent == 1);
}

void a_second_screenshot_while_one_is_running_is_refused()
{
    Rig rig;
    rig.send(request(Opcode::ScreenRequest, 1));
    CHECK(rig.bridge.transfer_in_progress());
    rig.sink.clear();
    rig.send(request(Opcode::ScreenRequest, 2));
    CHECK(rig.sink.last_error() == ErrorCode::Busy);
}

void a_build_without_a_frame_buffer_says_so()
{
    Rig rig(40, 30, PixelFormat::Rgb888, /*with_buffer=*/false);
    rig.send(request(Opcode::ScreenRequest, 1));
    // Not an empty image. "This build cannot" and "the screen is black" must
    // never look the same to a test.
    CHECK(rig.sink.last_error() == ErrorCode::Unsupported);
}

void nothing_rendered_yet_is_a_typed_error()
{
    Rig rig;
    rig.screen.fail_capture(ScreenSource::Failure::NotRendered);
    rig.send(request(Opcode::ScreenRequest, 1));
    CHECK(rig.sink.last_error() == ErrorCode::NoScreen);
    CHECK(!rig.bridge.transfer_in_progress());
}

// The three ways a capture fails that waiting does not fix. They used to be one
// answer -- NoScreen, "nothing has been rendered yet" -- which sent the reader
// to look at the interface when LVGL was out of memory or the composition root
// had built a screen the wrong size.
// The bridge pre-screens the shape and answers `Unsupported` before the copy, so
// the `BufferTooSmall` arm of the failure mapping is only reached by a source
// that disagrees with its own shape query. It is an arm for a source bug, and a
// source bug is exactly the thing no other test can stand in for.
void a_source_that_contradicts_its_own_shape_query_still_says_unsupported()
{
    Rig rig;
    // Small enough to pass the pre-screen against the bridge's buffer, then the
    // copy refuses because the real image is larger.
    rig.screen.understate_shape(16);
    rig.send(request(Opcode::ScreenRequest, 1));
    CHECK(rig.sink.last_error() == ErrorCode::Unsupported);
    // Not NoScreen. That conflation is the whole reason the failure is typed.
    CHECK(rig.sink.last_error() != ErrorCode::NoScreen);
    CHECK(!rig.bridge.transfer_in_progress());
}

void a_capture_that_waiting_will_not_fix_says_which_one_it_is()
{
    {
        Rig rig;
        rig.screen.fail_capture(ScreenSource::Failure::RendererFailed);
        rig.send(request(Opcode::ScreenRequest, 1));
        CHECK(rig.sink.last_error() == ErrorCode::CaptureFailed);
        CHECK(!rig.bridge.transfer_in_progress());
    }
    {
        Rig rig;
        rig.screen.fail_capture(ScreenSource::Failure::GeometryMismatch);
        rig.send(request(Opcode::ScreenRequest, 2));
        CHECK(rig.sink.last_error() == ErrorCode::ScreenGeometry);
        CHECK(!rig.bridge.transfer_in_progress());
    }
    // And the one that waiting *does* fix keeps its own code, so the
    // troubleshooting table's advice stays true of exactly the case it is true
    // of.
    {
        Rig rig;
        rig.screen.fail_capture(ScreenSource::Failure::NotRendered);
        rig.send(request(Opcode::ScreenRequest, 3));
        CHECK(rig.sink.last_error() == ErrorCode::NoScreen);
    }
}

std::vector<std::uint8_t> input_request(core::InputEventType type, std::uint16_t req_id,
                                        std::int16_t x = 0, std::int16_t y = 0,
                                        std::uint8_t button = 0, std::uint8_t touch_id = 0,
                                        std::uint32_t at = 0)
{
    InputEventBody event;
    event.type     = static_cast<std::uint8_t>(type);
    event.button   = button;
    event.x        = x;
    event.y        = y;
    event.touch_id = touch_id;
    event.at_ms    = at;

    std::uint8_t body[kInputEventBodyBytes] = {};
    encode_input_event(event, body, sizeof(body));
    return request(Opcode::InputEvent, req_id, body, sizeof(body));
}

void an_injected_tap_reaches_the_same_queue_a_finger_would()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 1, 100, 120), 500);
    CHECK(rig.sink.last_is(Opcode::InputOk));
    rig.send(input_request(core::InputEventType::PointerUp, 2, 100, 120), 560);
    CHECK(rig.sink.last_is(Opcode::InputOk));

    core::InputEvent down;
    CHECK(rig.queue.pop(down));
    CHECK(down.type == core::InputEventType::PointerDown);
    CHECK(down.x == 100);
    CHECK(down.y == 120);
    // Marked Remote so a disconnect can clean up after it, and for no other
    // reason: nothing above the queue is allowed to branch on this.
    CHECK(down.origin == core::InputOrigin::Remote);
    CHECK(down.at_ms == 500);

    core::InputEvent up;
    CHECK(rig.queue.pop(up));
    CHECK(up.type == core::InputEventType::PointerUp);
    CHECK(up.at_ms == 560);
    CHECK(rig.queue.empty());
}

void a_clients_own_timestamps_are_kept()
{
    Rig rig;
    // A replayed gesture carries its own intervals, and those intervals are
    // what decide whether the recogniser sees a fling or a drag.
    rig.send(input_request(core::InputEventType::PointerDown, 1, 10, 10, 0, 0, 7777), 100);
    core::InputEvent down;
    CHECK(rig.queue.pop(down));
    CHECK(down.at_ms == 7777);
}

void a_second_finger_is_named_as_a_stack_limit()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 1, 10, 10, 0, 0));
    rig.sink.clear();
    rig.send(input_request(core::InputEventType::PointerDown, 2, 90, 90, 0, /*touch_id=*/1));
    CHECK(rig.sink.last_error() == ErrorCode::TooManyTouches);
}

void an_impossible_press_is_a_different_error_from_a_second_finger()
{
    Rig rig;
    // Button 2 on a two-button board.
    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/2));
    CHECK(rig.sink.last_error() == ErrorCode::BadInput);
    rig.sink.clear();
    // A release of something never held.
    rig.send(input_request(core::InputEventType::ButtonUp, 2, 0, 0, 0));
    CHECK(rig.sink.last_error() == ErrorCode::BadInput);
}

void a_malformed_input_body_is_rejected()
{
    Rig                rig;
    const std::uint8_t stub[3] = {0, 0, 0};
    rig.send(request(Opcode::InputEvent, 1, stub, sizeof(stub)));
    CHECK(rig.sink.last_error() == ErrorCode::BadBody);

    rig.sink.clear();
    InputEventBody bad;
    bad.type = 99;  // not an InputEventType
    std::uint8_t body[kInputEventBodyBytes] = {};
    encode_input_event(bad, body, sizeof(body));
    rig.send(request(Opcode::InputEvent, 2, body, sizeof(body)));
    CHECK(rig.sink.last_error() == ErrorCode::BadBody);
}

void the_event_rate_is_capped()
{
    Rig rig;
    // The cap is enforced on the device, because a limit only the well-behaved
    // client respects is not a limit -- and the client that matters is the one
    // that crashed mid-swipe.
    std::uint16_t ok = 0;
    for (std::uint16_t i = 0; i < 600; ++i) {
        rig.sink.clear();
        // Alternate down/up so the state machine stays happy and the rate
        // limiter is the only thing that can refuse.
        const auto type = (i % 2 == 0) ? core::InputEventType::ButtonDown
                                       : core::InputEventType::ButtonUp;
        rig.send(input_request(type, i, 0, 0, 0), 1000);
        rig.queue.clear();
        if (rig.sink.last_is(Opcode::InputOk)) {
            ++ok;
        }
    }
    CHECK(ok == 500);
    CHECK(rig.sink.last_error() == ErrorCode::RateLimited);

    // A second later the window rolls over and the client is served again.
    rig.sink.clear();
    rig.send(input_request(core::InputEventType::ButtonDown, 999, 0, 0, 0), 2500);
    CHECK(rig.sink.last_is(Opcode::InputOk));
}

void a_finger_held_past_the_cap_is_lifted_with_the_id_it_went_down_with()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 0, 10, 20, 0), 1000);
    CHECK(rig.state.pointer_down());
    CHECK(rig.state.pointer_touch_id() == 0);
    rig.queue.clear();

    rig.bridge.tick(1000 + 30001, &Collector::emit, &rig.sink);
    CHECK(!rig.state.pointer_down());
    CHECK(rig.bridge.stats().holds_expired == 1);

    core::InputEvent up;
    CHECK(rig.queue.pop(up));
    CHECK(up.type == core::InputEventType::PointerUp);
    // The point of the test: the release carries the id of the finger that is
    // down, read from the state rather than assumed to be zero. `apply` refuses
    // a `PointerUp` whose id does not match, so a literal zero is correct only
    // while `kMaxTouchPoints == 1` -- and `core/input.h` says the field exists
    // so it survives to a device where it is not. This asserts the value is
    // *sourced*. The mismatching case cannot be built through the public API
    // today: `apply` refuses any id at or above the constant before the
    // comparison is reached.
    CHECK(up.touch_id == rig.state.pointer_touch_id());

    // And it expires exactly once. The failure guarded against is the opposite
    // one: `apply` fails, the pointer stays held, and this branch fires again
    // on every tick until the queue is full.
    rig.bridge.tick(1000 + 60000, &Collector::emit, &rig.sink);
    CHECK(rig.bridge.stats().holds_expired == 1);
}

void a_button_held_past_the_cap_is_released_by_the_device()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, 0), 1000);
    CHECK(rig.state.button_down(0));
    rig.queue.clear();

    rig.bridge.tick(1000 + 29000, &Collector::emit, &rig.sink);
    CHECK(rig.state.button_down(0));

    rig.bridge.tick(1000 + 30001, &Collector::emit, &rig.sink);
    CHECK(!rig.state.button_down(0));
    CHECK(rig.bridge.stats().holds_expired == 1);

    core::InputEvent up;
    CHECK(rig.queue.pop(up));
    CHECK(up.type == core::InputEventType::ButtonUp);
}

void a_disconnect_lifts_what_the_remote_was_holding()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 1, 55, 66), 10);
    rig.send(input_request(core::InputEventType::ButtonDown, 2, 0, 0, 1), 11);
    rig.queue.clear();
    rig.send(request(Opcode::ScreenRequest, 3));
    CHECK(rig.bridge.transfer_in_progress());

    rig.bridge.on_disconnect(99);

    CHECK(!rig.state.pointer_down());
    CHECK(!rig.state.button_down(1));
    CHECK(!rig.bridge.transfer_in_progress());

    // And the interface is told, so a widget that thinks it is being pressed
    // stops thinking so.
    bool saw_pointer_up = false;
    bool saw_button_up  = false;
    core::InputEvent event;
    while (rig.queue.pop(event)) {
        if (event.type == core::InputEventType::PointerUp) {
            saw_pointer_up = true;
            CHECK(event.x == 55);
            CHECK(event.y == 66);
        }
        if (event.type == core::InputEventType::ButtonUp) {
            saw_button_up = true;
        }
    }
    CHECK(saw_pointer_up);
    CHECK(saw_button_up);
}

// Reads the two bytes `InputOk` carries for an `InputReset`: what was released,
// and what is still held. Nothing else carries a two-byte body, so it is local.
std::pair<int, int> input_reset_reply(const Rig& rig)
{
    Envelope            e;
    const std::uint8_t* body = nullptr;
    const auto&         last = rig.sink.messages.back();
    if (!decode_message(last.data(), last.size(), e, body) || body == nullptr ||
        e.body_len < 2) {
        return {-1, -1};
    }
    return {body[0], body[1]};
}

void input_reset_is_available_as_a_command()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 1, 5, 5), 10);
    rig.queue.clear();
    rig.sink.clear();

    rig.send(request(Opcode::InputReset, 2), 20);
    CHECK(rig.sink.last_is(Opcode::InputOk));
    CHECK(!rig.state.pointer_down());

    core::InputEvent up;
    CHECK(rig.queue.pop(up));
    CHECK(up.type == core::InputEventType::PointerUp);

    // Idempotent: resetting a clean state is not an error.
    rig.sink.clear();
    rig.send(request(Opcode::InputReset, 3), 30);
    CHECK(rig.sink.last_is(Opcode::InputOk));
    CHECK(input_reset_reply(rig).first == 0);
    CHECK(input_reset_reply(rig).second == 0);
}

void input_reset_says_what_it_could_not_release()
{
    // The state this command exists for. `release_all` marks an input released
    // only if its event reached the queue, so on a stalled interface it writes
    // nothing and keeps everything held -- deliberately, because the widget
    // under each one still believes it is pressed. The count alone cannot say
    // so: a released of 0 is also what a clean device answers.
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 1, 5, 5), 10);
    rig.send(input_request(core::InputEventType::ButtonDown, 2, 0, 0, 1), 20);
    CHECK(rig.state.pointer_down());
    CHECK(rig.state.button_down(1));

    // Fill the queue so that no release can be written.
    rig.queue.clear();
    core::InputEvent filler;
    filler.type   = core::InputEventType::PointerMove;
    filler.origin = core::InputOrigin::Remote;
    while (rig.queue.push(filler)) {
    }
    const std::size_t full = rig.queue.size();

    rig.sink.clear();
    rig.send(request(Opcode::InputReset, 3), 30);
    CHECK(rig.sink.last_is(Opcode::InputOk));

    const auto answer = input_reset_reply(rig);
    CHECK(answer.first == 0);   // nothing reached the interface
    CHECK(answer.second == 2);  // and both inputs are still held

    // The state was not falsified to make the answer tidy: the finger and the
    // button are still down, so the hold expiry has something to retry.
    CHECK(rig.state.pointer_down());
    CHECK(rig.state.button_down(1));
    CHECK(rig.queue.size() == full);

    // Drain, and the same command now succeeds and says so.
    rig.queue.clear();
    rig.sink.clear();
    rig.send(request(Opcode::InputReset, 4), 40);
    const auto second = input_reset_reply(rig);
    CHECK(second.first == 2);
    CHECK(second.second == 0);
    CHECK(!rig.state.pointer_down());
    CHECK(!rig.state.button_down(1));
}

void a_persons_finger_is_not_counted_as_something_this_connection_holds()
{
    // `held_count`'s origin filter is what the whole `still_held` path rests
    // on, and that path is control flow now: a scenario step fails on it and
    // the tool exits non-zero. Drop `button_origin_[i] == origin` and every
    // other test stays green, while `input-reset` starts reporting a full queue
    // -- *"the interface may be stalled"* -- every time a person has a finger on
    // a button. That is the case `InputOrigin` exists to protect, reported as
    // the one failure it is not.
    Rig rig;
    core::InputEvent physical;
    physical.type   = core::InputEventType::ButtonDown;
    physical.origin = core::InputOrigin::Physical;
    physical.button = 0;
    CHECK(rig.state.apply(physical, 2));
    rig.queue.clear();

    rig.send(request(Opcode::InputReset, 1), 10);
    CHECK(rig.sink.last_is(Opcode::InputOk));
    const auto answer = input_reset_reply(rig);
    CHECK(answer.first == 0);   // nothing of ours was held
    CHECK(answer.second == 0);  // and the person's button is not ours to count
    CHECK(rig.state.button_down(0));  // still theirs, and still down
}

void a_physical_press_survives_a_remote_disconnect()
{
    Rig rig;
    // A person is holding button 0 while the debug client drops.
    core::InputEvent physical;
    physical.type   = core::InputEventType::ButtonDown;
    physical.origin = core::InputOrigin::Physical;
    physical.button = 0;
    CHECK(rig.state.apply(physical, 2));

    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, 1), 10);
    rig.queue.clear();

    rig.bridge.on_disconnect(50);
    CHECK(rig.state.button_down(0));   // still held by the person
    CHECK(!rig.state.button_down(1));  // released for the remote
}

}  // namespace


// --- what the independent review of #121 found, each closed and pinned ------

void a_button_the_board_will_not_simulate_is_refused_by_the_device()
{
    Rig rig;
    rig.screen.set_injectable(1, false);

    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/1));

    // Unsupported, not BadInput: the press is possible, the board declines it.
    CHECK(rig.sink.last_error() == ErrorCode::Unsupported);
    CHECK(!rig.state.button_down(1));
    CHECK(rig.queue.empty());
    CHECK(rig.bridge.stats().events_refused == 1);

    // And the refusal is the device's, not the host tool's -- nothing about
    // this test goes near tools/watch.
    rig.sink.clear();
    rig.send(input_request(core::InputEventType::ButtonDown, 2, 0, 0, /*button=*/0));
    CHECK(rig.sink.last_is(Opcode::InputOk));
}

void an_out_of_range_button_is_still_a_different_error_from_a_refused_one()
{
    Rig rig;
    rig.screen.set_injectable(1, false);

    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/5));
    CHECK(rig.sink.last_error() == ErrorCode::BadInput);

    rig.sink.clear();
    rig.send(input_request(core::InputEventType::ButtonDown, 2, 0, 0, /*button=*/1));
    CHECK(rig.sink.last_error() == ErrorCode::Unsupported);
}

// Fills the queue to capacity so the next push must fail, and says so: a test
// that meant to exercise the full-queue branch and quietly did not is worse
// than no test, because it reads as coverage of exactly the case it missed.
void fill_queue(core::InputQueue& queue)
{
    core::InputEvent filler;
    filler.type   = core::InputEventType::ButtonDown;
    filler.origin = core::InputOrigin::Physical;
    while (queue.push(filler)) {
    }
    CHECK(queue.size() == core::InputQueue::kCapacity);
    CHECK(!queue.push(filler));
}

void a_hold_that_cannot_be_released_stays_held_for_the_next_tick()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/0), 100);
    CHECK(rig.state.button_down(0));

    // The interface has stalled: nothing is draining, and the queue is full.
    fill_queue(rig.queue);

    rig.bridge.tick(100 + 60000, &Collector::emit, &rig.sink);

    // The release could not be delivered, so the button is **still held**.
    // Clearing the flag here would have stranded a pressed widget with nothing
    // left in the system able to lift it -- the 30-second escape hatch firing
    // once and doing nothing.
    CHECK(rig.state.button_down(0));
    CHECK(rig.bridge.stats().holds_expired == 0);

    // Drained, the very next tick delivers it.
    core::InputEvent scratch;
    while (rig.queue.pop(scratch)) {
    }
    rig.bridge.tick(100 + 60001, &Collector::emit, &rig.sink);
    CHECK(!rig.state.button_down(0));
    CHECK(rig.bridge.stats().holds_expired == 1);
}

void a_pointer_hold_that_cannot_be_released_also_stays_held()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::PointerDown, 1, 10, 20), 100);
    CHECK(rig.state.pointer_down());

    fill_queue(rig.queue);
    rig.bridge.tick(100 + 60000, &Collector::emit, &rig.sink);
    CHECK(rig.state.pointer_down());
    CHECK(rig.bridge.stats().holds_expired == 0);
}

void a_release_that_could_not_be_queued_leaves_the_input_held()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/0));
    CHECK(rig.state.button_down(0));

    fill_queue(rig.queue);
    rig.sink.clear();
    rig.send(input_request(core::InputEventType::ButtonUp, 2, 0, 0, /*button=*/0));

    // Rolled back: the state must not claim the button came up when the event
    // that says so never reached the interface.
    CHECK(rig.sink.last_error() == ErrorCode::QueueFull);
    CHECK(rig.state.button_down(0));
}

// A refused pointer event must leave the coordinates exactly where they were.
// Both halves of this were live defects and neither had a test: the `PointerMove`
// case fell through `default:` and did not roll back at all, and the `PointerUp`
// case rolled the *type* back while re-pressing at the refused release's
// coordinate. `release_all` and `Bridge::tick` lift "where it was last seen"
// from this field, so either one ends with LVGL getting a release at a point the
// finger never reached, firing whatever widget is there.
void a_refused_pointer_event_does_not_move_the_finger()
{
    {
        Rig rig;
        rig.send(input_request(core::InputEventType::PointerDown, 1, 10, 10));
        CHECK(rig.state.pointer_down());

        fill_queue(rig.queue);
        rig.sink.clear();
        rig.send(input_request(core::InputEventType::PointerMove, 2, 300, 400));

        CHECK(rig.sink.last_error() == ErrorCode::QueueFull);
        CHECK(rig.state.pointer_down());
        CHECK(rig.state.pointer_x() == 10);
        CHECK(rig.state.pointer_y() == 10);
    }
    {
        Rig rig;
        rig.send(input_request(core::InputEventType::PointerDown, 1, 10, 10));
        CHECK(rig.state.pointer_down());

        fill_queue(rig.queue);
        rig.sink.clear();
        rig.send(input_request(core::InputEventType::PointerUp, 2, 300, 400));

        // Still held -- the release never reached the interface -- and still at
        // the coordinate the finger is actually at.
        CHECK(rig.sink.last_error() == ErrorCode::QueueFull);
        CHECK(rig.state.pointer_down());
        CHECK(rig.state.pointer_x() == 10);
        CHECK(rig.state.pointer_y() == 10);
    }
}

void an_overrun_is_not_reported_as_a_screenshot_collision()
{
    Rig rig;
    fill_queue(rig.queue);
    rig.send(input_request(core::InputEventType::PointerDown, 1, 5, 5));

    // QueueFull, not Busy. A dropped swipe point answering "a screen transfer
    // is already in progress" sends the reader to the wrong subsystem.
    CHECK(rig.sink.last_error() == ErrorCode::QueueFull);
    CHECK(rig.sink.last_error() != ErrorCode::Busy);
}

void the_remote_may_not_lift_a_hold_a_person_owns()
{
    Rig rig;

    core::InputEvent physical;
    physical.type   = core::InputEventType::ButtonDown;
    physical.origin = core::InputOrigin::Physical;
    physical.button = 0;
    CHECK(rig.state.apply(physical, 2));
    CHECK(rig.state.button_down(0));

    rig.send(input_request(core::InputEventType::ButtonUp, 1, 0, 0, /*button=*/0));
    CHECK(rig.sink.last_error() == ErrorCode::BadInput);
    CHECK(rig.state.button_down(0));

    // The same for a finger.
    core::InputEvent finger;
    finger.type   = core::InputEventType::PointerDown;
    finger.origin = core::InputOrigin::Physical;
    finger.x      = 7;
    finger.y      = 9;
    CHECK(rig.state.apply(finger, 2));

    rig.sink.clear();
    rig.send(input_request(core::InputEventType::PointerUp, 2, 7, 9));
    CHECK(rig.sink.last_error() == ErrorCode::BadInput);
    CHECK(rig.state.pointer_down());
}

void the_hold_watchdog_ignores_the_clients_clock()
{
    Rig rig;

    // A client replaying a recording sends timestamps from its own epoch. The
    // queued event keeps them -- a gesture recogniser needs the intervals --
    // but the safety timer must not, or a recording from a large clock expires
    // its own hold on the very next tick, and one from the future never does.
    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/0,
                           /*touch_id=*/0, /*at_ms=*/900000),
             /*now=*/100);
    CHECK(rig.state.button_down(0));

    rig.bridge.tick(200, &Collector::emit, &rig.sink);
    CHECK(rig.state.button_down(0));
    CHECK(rig.bridge.stats().holds_expired == 0);

    // And it does still expire, on the device's own clock.
    rig.bridge.tick(100 + 30001, &Collector::emit, &rig.sink);
    CHECK(!rig.state.button_down(0));
}

void a_frame_too_large_for_the_buffer_says_so_as_itself()
{
    // A build whose buffer is smaller than the panel. Not "nothing has been
    // rendered yet" -- the screen is fine and a number in the composition root
    // is wrong, which is a different place to go looking.
    Rig rig(40, 30, PixelFormat::Rgb888);
    std::vector<std::uint8_t> tiny(16);
    Bridge                    small(rig.queue, rig.state, rig.screen, tiny.data(), tiny.size());
    Collector                 sink;
    const auto                message = request(Opcode::ScreenRequest, 1);
    small.handle(message.data(), message.size(), 0, &Collector::emit, &sink);
    CHECK(sink.last_error() == ErrorCode::Unsupported);
    CHECK(sink.last_error() != ErrorCode::NoScreen);
}

void a_decoded_capabilities_body_can_never_claim_more_buttons_than_it_holds()
{
    CapabilitiesBody in;
    in.button_count = 2;
    std::uint8_t raw[kCapabilitiesBodyBytes] = {};
    const std::size_t written = encode_capabilities(in, raw, sizeof(raw));
    CHECK(written == kCapabilitiesBodyBytes);

    // A device claiming five buttons; the struct holds four.
    raw[7] = 5;
    CapabilitiesBody out;
    CHECK(decode_capabilities(raw, written, out));
    CHECK(out.button_count == 4);
}

void a_flush_is_counted_like_an_overrun()
{
    core::InputQueue queue;
    core::InputEvent event;
    event.type = core::InputEventType::ButtonDown;
    CHECK(queue.push(event));
    CHECK(queue.push(event));
    queue.clear();

    // pushed == popped + flushed + size, at every moment. `dropped` is not a
    // term -- a refused push never entered the ring. This case has `dropped`
    // at 0, so it cannot tell the two spellings apart; the overrun test in
    // `test_input.cpp` is where the sum is evaluated with `dropped` non-zero.
    const auto& s = queue.stats();
    CHECK(s.flushed == 2);
    CHECK(s.dropped == 0);
    CHECK(s.pushed == s.popped + s.flushed + queue.size());
}

// --- wait_stable: the duration has to travel, and be the one measured ------

// Reads the single byte `StableOk` carries. Nothing else does, so it is local.
int stable_reply(const Rig& rig)
{
    Envelope            e;
    const std::uint8_t* body = nullptr;
    const auto&         last = rig.sink.messages.back();
    if (!decode_message(last.data(), last.size(), e, body) || body == nullptr ||
        e.body_len < 1) {
        return -1;
    }
    return body[0];
}

void a_stability_question_with_no_duration_in_it_is_refused()
{
    Rig rig;
    rig.screen.set_idle(50);
    rig.send(request(Opcode::WaitStable, 1), 10000);

    // An empty body used to be the whole message, and the device answered
    // `stable_since(now_ms)` -- "idle for as long as the process has run",
    // which is true until the first input and false ever after, however long
    // the host waits. The duration is the caller's to choose and must be on
    // the wire; a message without one is not answerable.
    CHECK(rig.sink.last_is(Opcode::Error));
    CHECK(rig.sink.last_error() == ErrorCode::BadInput);
}

void the_stability_answer_is_measured_against_the_duration_that_was_asked_for()
{
    Rig rig;
    rig.screen.set_idle(400);

    const std::uint8_t three_hundred[2] = {0x2C, 0x01};
    rig.send(request(Opcode::WaitStable, 1, three_hundred, sizeof(three_hundred)), 10000);
    CHECK(rig.sink.last_is(Opcode::StableOk));
    CHECK(stable_reply(rig) == 1);

    // The same instant, the same idle time, a longer question: the opposite
    // answer. That is the property the old code could not have -- it ignored
    // the body, so no two questions could ever differ.
    rig.sink.messages.clear();
    const std::uint8_t one_second[2] = {0xE8, 0x03};
    rig.send(request(Opcode::WaitStable, 2, one_second, sizeof(one_second)), 10000);
    CHECK(rig.sink.last_is(Opcode::StableOk));
    CHECK(stable_reply(rig) == 0);
}

void an_event_still_in_the_queue_is_not_a_settled_interface()
{
    Rig rig;
    // The screen says settled, loudly: four hundred milliseconds idle and
    // nothing animating. This is exactly the state a preceding screenshot
    // leaves behind -- half a second of no input on the Waveshare.
    rig.screen.set_idle(400);

    core::InputEvent tap{};
    tap.type   = core::InputEventType::PointerDown;
    tap.origin = core::InputOrigin::Remote;
    CHECK(rig.queue.push(tap));

    const std::uint8_t three_hundred[2] = {0x2C, 0x01};
    rig.send(request(Opcode::WaitStable, 1, three_hundred, sizeof(three_hundred)), 10000);
    CHECK(rig.sink.last_is(Opcode::StableOk));
    // Not settled, because the event the caller is waiting on has reached the
    // device and not the interface. The source cannot know that -- `FakeScreen`
    // has no queue, which is why the neighbouring stability test cannot cover
    // this and a reader must not take it as covering it.
    CHECK(stable_reply(rig) == 0);

    // Drained: the same instant, the same idle, the answer the screen gives.
    core::InputEvent out;
    CHECK(rig.queue.pop(out));
    rig.sink.messages.clear();
    rig.send(request(Opcode::WaitStable, 2, three_hundred, sizeof(three_hundred)), 10000);
    CHECK(rig.sink.last_is(Opcode::StableOk));
    CHECK(stable_reply(rig) == 1);
}

void a_hold_the_client_left_behind_is_still_released_once_it_is_gone()
{
    Rig rig;
    rig.send(input_request(core::InputEventType::ButtonDown, 1, 0, 0, /*button=*/0),
             /*now=*/1000);
    CHECK(rig.state.button_down(0));

    // The queue fills while the hold is live. Not contrived: `remote_input.cpp`
    // stops draining by design once its transition FIFO is full.
    fill_queue(rig.queue);
    rig.bridge.on_disconnect(1500);

    // `release_all` could not queue the release and kept the hold rather than
    // lose it -- the deliberate half. Nothing about that is repaired by the
    // client leaving; the retry is `tick`, and this is the case it exists for.
    CHECK(rig.state.button_down(0));

    core::InputEvent drained;
    while (rig.queue.pop(drained)) {
    }

    rig.bridge.tick(1000 + 30001, &Collector::emit, &rig.sink);
    CHECK(!rig.state.button_down(0));
    CHECK(rig.bridge.stats().holds_expired == 1);

    core::InputEvent up;
    CHECK(rig.queue.pop(up));
    CHECK(up.type == core::InputEventType::ButtonUp);
    CHECK(up.button == 0);

    // Two tests already covered the halves -- disconnect with a drainable
    // queue, and a full queue with a live client. Their intersection is the
    // one that stayed broken, and in the simulator it stayed broken twice
    // over: `DebugServer::poll` returned above its own `tick` as soon as
    // `client_fd_ < 0`, so on the only path that matters the retry never ran.
}


int main()
{
    an_envelope_survives_a_round_trip();
    a_flipped_bit_anywhere_is_rejected();
    a_length_that_disagrees_with_the_payload_is_rejected();
    a_body_too_large_for_a_frame_is_refused_not_truncated();
    an_output_buffer_too_small_yields_nothing();
    the_message_fits_inside_one_frame();
    the_wire_bytes_are_pinned_to_a_literal();
    whole_messages_are_pinned_to_literals_the_other_implementation_also_holds();
    the_two_numbering_tables_are_spelled_out();
    crc32_matches_the_published_vector();
    the_bodies_survive_a_round_trip();
    bytes_per_pixel_is_defined_for_every_format();

    an_unknown_opcode_is_answered_with_a_typed_error();
    a_wrong_version_is_answered_rather_than_ignored();
    a_handshake_at_the_wrong_version_still_says_what_this_device_is();
    a_message_from_another_class_is_not_ours_to_execute();
    an_undecodable_message_gets_no_reply_at_all();

    hello_reports_the_board_and_the_version();
    hello_with_a_short_body_is_a_typed_error();
    capabilities_report_the_panel_and_the_buttons();

    a_screenshot_arrives_whole_and_in_order();
    a_second_screenshot_while_one_is_running_is_refused();
    a_build_without_a_frame_buffer_says_so();
    nothing_rendered_yet_is_a_typed_error();
    a_capture_that_waiting_will_not_fix_says_which_one_it_is();
    a_source_that_contradicts_its_own_shape_query_still_says_unsupported();

    an_injected_tap_reaches_the_same_queue_a_finger_would();
    a_clients_own_timestamps_are_kept();
    a_second_finger_is_named_as_a_stack_limit();
    an_impossible_press_is_a_different_error_from_a_second_finger();
    a_malformed_input_body_is_rejected();
    the_event_rate_is_capped();

    a_button_held_past_the_cap_is_released_by_the_device();
    a_finger_held_past_the_cap_is_lifted_with_the_id_it_went_down_with();
    a_disconnect_lifts_what_the_remote_was_holding();
    input_reset_is_available_as_a_command();
    input_reset_says_what_it_could_not_release();
    a_persons_finger_is_not_counted_as_something_this_connection_holds();
    a_physical_press_survives_a_remote_disconnect();

    a_button_the_board_will_not_simulate_is_refused_by_the_device();
    an_out_of_range_button_is_still_a_different_error_from_a_refused_one();
    a_hold_that_cannot_be_released_stays_held_for_the_next_tick();
    a_pointer_hold_that_cannot_be_released_also_stays_held();
    a_release_that_could_not_be_queued_leaves_the_input_held();
    a_refused_pointer_event_does_not_move_the_finger();
    an_overrun_is_not_reported_as_a_screenshot_collision();
    the_remote_may_not_lift_a_hold_a_person_owns();
    the_hold_watchdog_ignores_the_clients_clock();
    a_frame_too_large_for_the_buffer_says_so_as_itself();
    a_decoded_capabilities_body_can_never_claim_more_buttons_than_it_holds();
    a_flush_is_counted_like_an_overrun();
    a_stability_question_with_no_duration_in_it_is_refused();
    the_stability_answer_is_measured_against_the_duration_that_was_asked_for();
    an_event_still_in_the_queue_is_not_a_settled_interface();
    a_hold_the_client_left_behind_is_still_released_once_it_is_gone();

    if (failures != 0) {
        std::fprintf(stderr, "%d debug-channel check(s) failed\n", failures);
        return 1;
    }
    std::printf("debug channel: all checks passed\n");
    return 0;
}
