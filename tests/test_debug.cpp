#include <cstdio>
#include <cstring>
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
                 std::size_t& bytes_out) override
    {
        width_out       = w_;
        height_out      = h_;
        format_out      = f_;
        orientation_out = Orientation::Deg0;
        bytes_out       = image_.size();

        if (fail_capture_) {
            bytes_out = 0;
            return false;
        }
        // A metadata-only query: the caller passed no buffer, so the shape is
        // filled and the copy refused. This is how Capabilities asks.
        if (out == nullptr || capacity == 0) {
            return false;
        }
        if (capacity < image_.size()) {
            return false;
        }
        std::memcpy(out, image_.data(), image_.size());
        return true;
    }

    const char*             board_id() const override { return "waveshare-amoled-206"; }
    const char*             build_id() const override { return "test"; }
    std::uint8_t            button_count() const override { return 2; }
    const ButtonDescriptor* buttons() const override { return buttons_; }

    const std::vector<std::uint8_t>& image() const { return image_; }
    void fail_capture(bool on) { fail_capture_ = on; }

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
    bool                      fail_capture_ = false;
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
    e.op      = Opcode::Hello;
    e.req_id  = 5;

    std::vector<std::uint8_t> message(kEnvelopeBytes);
    message.resize(encode_message(e, nullptr, 0, message.data(), message.size()));
    rig.send(message);
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
    rig.screen.fail_capture(true);
    rig.send(request(Opcode::ScreenRequest, 1));
    CHECK(rig.sink.last_error() == ErrorCode::NoScreen);
    CHECK(!rig.bridge.transfer_in_progress());
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

    // pushed == popped + dropped + flushed + size, at every moment.
    const auto& s = queue.stats();
    CHECK(s.flushed == 2);
    CHECK(s.pushed == s.popped + s.dropped + s.flushed + queue.size());
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
    crc32_matches_the_published_vector();
    the_bodies_survive_a_round_trip();
    bytes_per_pixel_is_defined_for_every_format();

    an_unknown_opcode_is_answered_with_a_typed_error();
    a_wrong_version_is_answered_rather_than_ignored();
    a_message_from_another_class_is_not_ours_to_execute();
    an_undecodable_message_gets_no_reply_at_all();

    hello_reports_the_board_and_the_version();
    hello_with_a_short_body_is_a_typed_error();
    capabilities_report_the_panel_and_the_buttons();

    a_screenshot_arrives_whole_and_in_order();
    a_second_screenshot_while_one_is_running_is_refused();
    a_build_without_a_frame_buffer_says_so();
    nothing_rendered_yet_is_a_typed_error();

    an_injected_tap_reaches_the_same_queue_a_finger_would();
    a_clients_own_timestamps_are_kept();
    a_second_finger_is_named_as_a_stack_limit();
    an_impossible_press_is_a_different_error_from_a_second_finger();
    a_malformed_input_body_is_rejected();
    the_event_rate_is_capped();

    a_button_held_past_the_cap_is_released_by_the_device();
    a_disconnect_lifts_what_the_remote_was_holding();
    input_reset_is_available_as_a_command();
    a_physical_press_survives_a_remote_disconnect();

    a_button_the_board_will_not_simulate_is_refused_by_the_device();
    an_out_of_range_button_is_still_a_different_error_from_a_refused_one();
    a_hold_that_cannot_be_released_stays_held_for_the_next_tick();
    a_pointer_hold_that_cannot_be_released_also_stays_held();
    a_release_that_could_not_be_queued_leaves_the_input_held();
    an_overrun_is_not_reported_as_a_screenshot_collision();
    the_remote_may_not_lift_a_hold_a_person_owns();
    the_hold_watchdog_ignores_the_clients_clock();
    a_frame_too_large_for_the_buffer_says_so_as_itself();
    a_decoded_capabilities_body_can_never_claim_more_buttons_than_it_holds();
    a_flush_is_counted_like_an_overrun();

    if (failures != 0) {
        std::fprintf(stderr, "%d debug-channel check(s) failed\n", failures);
        return 1;
    }
    std::printf("debug channel: all checks passed\n");
    return 0;
}
