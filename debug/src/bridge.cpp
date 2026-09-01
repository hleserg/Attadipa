#include "attadipa/debug/bridge.h"

#include <cstring>

namespace attadipa::debug {
namespace {

// The largest image bytes one ScreenData message can carry: a full body minus
// the four-byte offset that says where it belongs.
constexpr std::size_t kChunkBytes = kMaxBody - 4;

void put_u32(std::uint8_t* p, std::uint32_t v)
{
    p[0] = static_cast<std::uint8_t>(v & 0xFFu);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

}  // namespace

Bridge::Bridge(core::InputQueue& queue, core::InputState& state, ScreenSource& source,
               std::uint8_t* frame_buffer, std::size_t frame_capacity,
               TimeSink* time_sink, MeshSink* mesh_sink, BridgeLimits limits)
    : queue_(queue), state_(state), source_(source), time_sink_(time_sink),
      mesh_sink_(mesh_sink),
      frame_buffer_(frame_buffer),
      frame_capacity_(frame_capacity), limits_(limits)
{
}

void Bridge::send(const Envelope& envelope, const std::uint8_t* body, std::size_t body_len,
                  Emit emit, void* ctx)
{
    std::uint8_t out[kEnvelopeBytes + kMaxBody] = {};
    const std::size_t written = encode_message(envelope, body, body_len, out, sizeof(out));
    if (written > 0 && emit != nullptr) {
        emit(ctx, out, written);
    }
}

void Bridge::send_error(std::uint16_t req_id, ErrorCode code, Emit emit, void* ctx)
{
    ++stats_.errors;
    Envelope envelope;
    envelope.req_id = req_id;
    envelope.op     = Opcode::Error;

    std::uint8_t body[2];
    body[0] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(code) & 0xFFu);
    body[1] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(code) >> 8) & 0xFFu);
    send(envelope, body, sizeof(body), emit, ctx);
}

void Bridge::handle(const std::uint8_t* payload, std::size_t length, std::uint32_t now_ms,
                    Emit emit, void* ctx)
{
    Envelope            envelope;
    const std::uint8_t* body = nullptr;

    // A message that fails to decode gets no reply, and that is deliberate: the
    // req_id is one of the fields that could not be trusted, so there is no
    // correlation to answer on. The framing already counted the corruption.
    if (!decode_message(payload, length, envelope, body)) {
        ++stats_.errors;
        return;
    }

    ++stats_.requests;

    // A foreign class is answered *in our class*, and with `UnknownOpcode` when
    // what we did not recognise was the class rather than the opcode. Both are
    // deliberate for the only feeder that exists -- the debug socket carries
    // nothing else, so a wrong class is a malformed debug message, and an
    // answer in a class the caller may not parse is worse than an imprecise
    // code. Worth saying because this file opens by arguing that the debug
    // channel is a message class inside the one the project already has: the
    // day a second class shares a transport, this needs a code of its own.
    if (envelope.cls != kClassDebug) {
        send_error(envelope.req_id, ErrorCode::UnknownOpcode, emit, ctx);
        return;
    }
    // `Hello` is outside the version gate, and that is the design rather than an
    // exception to it: ADR-0005 section 5 negotiates the protocol version *in*
    // the HELLO exchange, so a handshake a version gate can refuse is a
    // negotiation that cannot happen. With the gate over everything, the
    // leniency `handle_hello` and `client.py` both describe was unreachable
    // prose -- a host one version behind was refused on its first request and
    // never learned the board id or the build string, which is the line
    // `CLAUDE.md` leans on to stop a simulator screenshot being filed as a
    // hardware result.
    //
    // What this costs, said plainly: `HelloBody`'s shape has to stay decodable
    // across versions or the exemption buys nothing -- a changed layout answers
    // `BadBody` instead, which is at least an honest failure at the right step.
    // Everything after the handshake stays gated, so a host that has been told
    // the version and carries on anyway is refused per request.
    if (envelope.version != kDebugProtocolVersion && envelope.op != Opcode::Hello) {
        send_error(envelope.req_id, ErrorCode::VersionMismatch, emit, ctx);
        return;
    }

    switch (envelope.op) {
    case Opcode::Hello:
        handle_hello(envelope.req_id, body, envelope.body_len, emit, ctx);
        return;
    case Opcode::Capabilities:
        handle_capabilities(envelope.req_id, emit, ctx);
        return;
    case Opcode::ScreenRequest:
        handle_screen(envelope.req_id, now_ms, emit, ctx);
        return;
    case Opcode::InputEvent:
        handle_input(envelope.req_id, body, envelope.body_len, now_ms, emit, ctx);
        return;
    case Opcode::InputReset:
        handle_input_reset(envelope.req_id, now_ms, emit, ctx);
        return;
    case Opcode::WaitStable: {
        // The body is the quiet period the host wants, in milliseconds. It used
        // to be empty, and this call used to pass `now_ms` -- the monotonic tick
        // -- as the duration, which asks "has the interface been idle for as
        // long as the process has been running". That is true until the first
        // input and false ever after, however long you wait.
        if (body == nullptr || envelope.body_len < 2) {
            send_error(envelope.req_id, ErrorCode::BadInput, emit, ctx);
            return;
        }
        const std::uint32_t quiet_ms =
            static_cast<std::uint32_t>(body[0]) | (static_cast<std::uint32_t>(body[1]) << 8);
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op     = Opcode::StableOk;
        // The queue is asked **first**, and it is asked here rather than in the
        // source. A source answers about what the interface has already
        // processed -- an idle timer and a running animation are both stamped
        // by processing -- so neither term can see an event that reached the
        // device and has not been drained yet. That event is input, and it is
        // the input the caller is waiting on: `tap` sends down and up in about
        // ten milliseconds, and the drain is one loop iteration away, so
        // `wait_stable` immediately afterwards used to answer against the idle
        // the *previous* step left behind and say `ok`. The screenshot after it
        // then re-rendered a tree that had never seen the tap. Not a rare race:
        // the window is a whole read period wide and the round trips inside it
        // are shorter than that.
        //
        // `queue_.size()` is the half the bridge can see without knowing what a
        // display is, which is why it lives here. The other half -- events the
        // simulator has pumped into LVGL's transition FIFO but LVGL's read
        // timer has not consumed -- belongs to whoever owns that FIFO, and
        // `LvglScreenSource::stable_since` carries it as a third term.
        const bool         queued = queue_.size() != 0;
        const std::uint8_t ready  = (!queued && source_.stable_since(quiet_ms)) ? 1u : 0u;
        send(reply, &ready, 1, emit, ctx);
        return;
    }
    case Opcode::TimeSync: {
        if (time_sink_ == nullptr) {
            send_error(envelope.req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
        TimeSyncBody request;
        if (!decode_time_sync(body, envelope.body_len, request)) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        const TimeSinkResult result = time_sink_->synchronize(request);
        if (result != TimeSinkResult::Accepted) {
            send_error(envelope.req_id,
                       result == TimeSinkResult::Rejected
                           ? ErrorCode::BadInput
                           : ErrorCode::OperationFailed,
                       emit, ctx);
            return;
        }
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op = Opcode::TimeSyncOk;
        send(reply, nullptr, 0, emit, ctx);
        return;
    }
    case Opcode::MeshConfigure: {
        if (mesh_sink_ == nullptr) {
            send_error(envelope.req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
        if (envelope.body_len != 4) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        const std::uint32_t passkey =
            static_cast<std::uint32_t>(body[0]) |
            (static_cast<std::uint32_t>(body[1]) << 8) |
            (static_cast<std::uint32_t>(body[2]) << 16) |
            (static_cast<std::uint32_t>(body[3]) << 24);
        const MeshSinkResult result = mesh_sink_->configure(passkey);
        if (result != MeshSinkResult::Accepted) {
            send_error(envelope.req_id,
                       result == MeshSinkResult::Rejected
                           ? ErrorCode::BadInput
                           : ErrorCode::OperationFailed,
                       emit, ctx);
            return;
        }
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op = Opcode::MeshOk;
        send(reply, nullptr, 0, emit, ctx);
        return;
    }
    case Opcode::MeshDisconnect: {
        if (mesh_sink_ == nullptr) {
            send_error(envelope.req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
        if (envelope.body_len != 0) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        const MeshSinkResult result = mesh_sink_->disconnect();
        if (result != MeshSinkResult::Accepted) {
            send_error(envelope.req_id, ErrorCode::OperationFailed, emit, ctx);
            return;
        }
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op = Opcode::MeshOk;
        send(reply, nullptr, 0, emit, ctx);
        return;
    }
    case Opcode::MeshForgetBond: {
        if (mesh_sink_ == nullptr) {
            send_error(envelope.req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
        if (envelope.body_len != 0) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        const MeshSinkResult result = mesh_sink_->forget_bond();
        if (result != MeshSinkResult::Accepted) {
            send_error(envelope.req_id,
                       result == MeshSinkResult::Rejected
                           ? ErrorCode::BadInput
                           : ErrorCode::OperationFailed,
                       emit, ctx);
            return;
        }
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op = Opcode::MeshOk;
        send(reply, nullptr, 0, emit, ctx);
        return;
    }
    case Opcode::MeshSend: {
        constexpr std::size_t kHeader = 6 + 8;
        if (mesh_sink_ == nullptr) {
            send_error(envelope.req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
        if (body == nullptr || envelope.body_len <= kHeader) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        std::uint64_t raw_timestamp = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            raw_timestamp |= static_cast<std::uint64_t>(body[6 + i]) << (8 * i);
        }
        const MeshSinkResult result = mesh_sink_->send(
            body, reinterpret_cast<const char*>(body + kHeader),
            envelope.body_len - kHeader,
            static_cast<std::int64_t>(raw_timestamp));
        if (result != MeshSinkResult::Accepted) {
            send_error(envelope.req_id,
                       result == MeshSinkResult::Rejected
                           ? ErrorCode::BadInput
                           : ErrorCode::OperationFailed,
                       emit, ctx);
            return;
        }
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op = Opcode::MeshOk;
        send(reply, nullptr, 0, emit, ctx);
        return;
    }
    case Opcode::MeshRoomSend: {
        constexpr std::size_t kRoomBytes = 32;
        constexpr std::size_t kFixedBytes = kRoomBytes + 1 + 8;
        if (mesh_sink_ == nullptr) {
            send_error(envelope.req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
        if (body == nullptr || envelope.body_len <= kFixedBytes) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        const std::size_t password_length = body[kRoomBytes];
        const std::size_t header = kFixedBytes + password_length;
        if (password_length == 0 || password_length > 15 ||
            envelope.body_len <= header) {
            send_error(envelope.req_id, ErrorCode::BadBody, emit, ctx);
            return;
        }
        std::uint64_t raw_timestamp = 0;
        for (std::size_t i = 0; i < 8; ++i) {
            raw_timestamp |= static_cast<std::uint64_t>(body[kRoomBytes + 1 + password_length + i]) << (8 * i);
        }
        const MeshSinkResult result = mesh_sink_->send_room(
            body, reinterpret_cast<const char*>(body + kRoomBytes + 1), password_length,
            reinterpret_cast<const char*>(body + header), envelope.body_len - header,
            static_cast<std::int64_t>(raw_timestamp));
        if (result != MeshSinkResult::Accepted) {
            send_error(envelope.req_id,
                       result == MeshSinkResult::Rejected
                           ? ErrorCode::BadInput
                           : ErrorCode::OperationFailed,
                       emit, ctx);
            return;
        }
        Envelope reply;
        reply.req_id = envelope.req_id;
        reply.op = Opcode::MeshOk;
        send(reply, nullptr, 0, emit, ctx);
        return;
    }
    default:
        send_error(envelope.req_id, ErrorCode::UnknownOpcode, emit, ctx);
        return;
    }
}

void Bridge::handle_hello(std::uint16_t req_id, const std::uint8_t* body, std::size_t len,
                          Emit emit, void* ctx)
{
    HelloBody incoming;
    if (!decode_hello(body, len, incoming)) {
        send_error(req_id, ErrorCode::BadBody, emit, ctx);
        return;
    }
    // The version is reported, not enforced to be equal: ADR-0005 section 5
    // keeps version and capability set orthogonal, and a host one minor behind
    // should learn what it is talking to rather than be hung up on. This is
    // reachable because `handle` exempts `Hello` from the envelope gate -- see
    // the comment there for why that exemption is the ADR and not a hole in it.
    HelloBody reply;
    reply.protocol_version = kDebugProtocolVersion;
    std::strncpy(reply.board_id, source_.board_id(), sizeof(reply.board_id) - 1);
    std::strncpy(reply.build, source_.build_id(), sizeof(reply.build) - 1);

    std::uint8_t      out[kHelloBodyBytes] = {};
    const std::size_t n                    = encode_hello(reply, out, sizeof(out));

    Envelope envelope;
    envelope.req_id = req_id;
    envelope.op     = Opcode::HelloOk;
    send(envelope, out, n, emit, ctx);
}

void Bridge::handle_capabilities(std::uint16_t req_id, Emit emit, void* ctx)
{
    CapabilitiesBody caps;

    std::uint16_t w = 0;
    std::uint16_t h = 0;
    PixelFormat   f = PixelFormat::Unknown;
    Orientation   o = Orientation::Deg0;
    std::size_t   n = 0;

    // Asked of the source rather than held here, because the panel geometry is
    // a board fact and this class is not allowed to hold a table of board
    // facts. Capture into a zero-capacity buffer: the source fills the
    // metadata and refuses the copy, which is exactly the query we want.
    ScreenSource::Failure why = ScreenSource::Failure::None;
    (void)source_.capture(nullptr, 0, w, h, f, o, n, why);

    caps.width            = w;
    caps.height           = h;
    caps.format           = f;
    caps.orientation      = o;
    caps.max_touch_points = core::kMaxTouchPoints;
    caps.max_body         = static_cast<std::uint16_t>(kMaxBody);
    caps.max_hold_ms      = limits_.max_hold_ms;
    caps.max_events_per_s = limits_.max_events_per_s;

    constexpr std::uint8_t kWireButtons =
        static_cast<std::uint8_t>(sizeof(caps.buttons) / sizeof(caps.buttons[0]));
    const std::uint8_t count = source_.buttons() == nullptr ? 0 : source_.button_count();
    caps.button_count        = count > kWireButtons ? kWireButtons : count;
    const ButtonDescriptor* src = source_.buttons();
    for (std::uint8_t i = 0; i < caps.button_count && src != nullptr; ++i) {
        caps.buttons[i] = src[i];
    }

    std::uint8_t      out[kCapabilitiesBodyBytes] = {};
    const std::size_t written                     = encode_capabilities(caps, out, sizeof(out));

    Envelope envelope;
    envelope.req_id = req_id;
    envelope.op     = Opcode::CapabilitiesOk;
    send(envelope, out, written, emit, ctx);
}

void Bridge::handle_screen(std::uint16_t req_id, std::uint32_t now_ms, Emit emit, void* ctx)
{
    if (transfer_.active) {
        // One at a time. Two interleaved transfers on one req_id space would
        // be indistinguishable to the host from one transfer with duplicated
        // chunks, which is the failure a CRC catches and a protocol should
        // prevent.
        send_error(req_id, ErrorCode::Busy, emit, ctx);
        return;
    }
    if (frame_buffer_ == nullptr || frame_capacity_ == 0) {
        // A build without the frame buffer. Says so instead of returning an
        // empty image: "unsupported in this build" and "the screen is black"
        // must never look the same to a test.
        send_error(req_id, ErrorCode::Unsupported, emit, ctx);
        return;
    }

    std::uint16_t w = 0;
    std::uint16_t h = 0;
    PixelFormat   f = PixelFormat::Unknown;
    Orientation   o = Orientation::Deg0;
    std::size_t   n = 0;

    // Ask the shape first, so "this build's buffer is too small for this panel"
    // is answerable as itself. Rolled into NoScreen it read as "nothing has
    // been rendered yet", which sends the reader to look at the interface when
    // the fault is a number in the composition root.
    ScreenSource::Failure why = ScreenSource::Failure::None;
    (void)source_.capture(nullptr, 0, w, h, f, o, n, why);
    if (n > frame_capacity_) {
        send_error(req_id, ErrorCode::Unsupported, emit, ctx);
        return;
    }

    w   = 0;
    h   = 0;
    f   = PixelFormat::Unknown;
    o   = Orientation::Deg0;
    n   = 0;
    why = ScreenSource::Failure::None;
    if (!source_.capture(frame_buffer_, frame_capacity_, w, h, f, o, n, why) || n == 0) {
        // Each failure names a different place to look. Answering all of them
        // with NoScreen -- "nothing has been rendered yet" -- told the operator
        // to wait for a frame, which fixes exactly one of them.
        ErrorCode code = ErrorCode::NoScreen;
        switch (why) {
        case ScreenSource::Failure::BufferTooSmall:
            // Pre-screened above, so this is a source disagreeing with its own
            // shape query rather than the ordinary path. Still its own answer.
            code = ErrorCode::Unsupported;
            break;
        case ScreenSource::Failure::RendererFailed:
            code = ErrorCode::CaptureFailed;
            break;
        case ScreenSource::Failure::GeometryMismatch:
            code = ErrorCode::ScreenGeometry;
            break;
        case ScreenSource::Failure::None:
        case ScreenSource::Failure::ShapeQuery:
        case ScreenSource::Failure::NotRendered:
            break;
        }
        send_error(req_id, code, emit, ctx);
        return;
    }

    ScreenInfoBody info;
    info.frame_id    = next_frame_id_++;
    info.width       = w;
    info.height      = h;
    info.format      = f;
    info.orientation = o;
    info.total_bytes = static_cast<std::uint32_t>(n);
    info.crc32       = crc32(frame_buffer_, n);
    info.at_ms       = now_ms;

    std::uint8_t      out[kScreenInfoBodyBytes] = {};
    const std::size_t written                   = encode_screen_info(info, out, sizeof(out));

    Envelope envelope;
    envelope.req_id = req_id;
    envelope.op     = Opcode::ScreenInfo;
    send(envelope, out, written, emit, ctx);

    transfer_.active   = true;
    transfer_.req_id   = req_id;
    transfer_.sent     = 0;
    transfer_.total    = n;
    transfer_.frame_id = info.frame_id;
}

bool Bridge::pump(Emit emit, void* ctx)
{
    if (!transfer_.active) {
        return false;
    }

    if (transfer_.sent >= transfer_.total) {
        std::uint8_t body[4];
        put_u32(body, transfer_.frame_id);

        Envelope envelope;
        envelope.req_id = transfer_.req_id;
        envelope.op     = Opcode::ScreenEnd;
        send(envelope, body, sizeof(body), emit, ctx);

        transfer_.active = false;
        ++stats_.frames_sent;
        return false;
    }

    const std::size_t remaining = transfer_.total - transfer_.sent;
    const std::size_t take      = remaining < kChunkBytes ? remaining : kChunkBytes;

    std::uint8_t body[4 + kChunkBytes];
    put_u32(body, static_cast<std::uint32_t>(transfer_.sent));
    std::memcpy(body + 4, frame_buffer_ + transfer_.sent, take);

    Envelope envelope;
    envelope.req_id = transfer_.req_id;
    envelope.op     = Opcode::ScreenData;
    send(envelope, body, 4 + take, emit, ctx);

    transfer_.sent += take;
    return true;
}

bool Bridge::rate_allows(std::uint32_t now_ms)
{
    if (now_ms - rate_window_start_ms_ >= 1000u) {
        rate_window_start_ms_ = now_ms;
        rate_window_count_    = 0;
    }
    if (rate_window_count_ >= limits_.max_events_per_s) {
        return false;
    }
    ++rate_window_count_;
    return true;
}

void Bridge::handle_input(std::uint16_t req_id, const std::uint8_t* body, std::size_t len,
                          std::uint32_t now_ms, Emit emit, void* ctx)
{
    InputEventBody incoming;
    if (!decode_input_event(body, len, incoming)) {
        send_error(req_id, ErrorCode::BadBody, emit, ctx);
        return;
    }
    if (incoming.type > static_cast<std::uint8_t>(core::InputEventType::PointerUp)) {
        send_error(req_id, ErrorCode::BadBody, emit, ctx);
        return;
    }
    if (!rate_allows(now_ms)) {
        ++stats_.events_refused;
        send_error(req_id, ErrorCode::RateLimited, emit, ctx);
        return;
    }

    core::InputEvent event;
    event.type     = static_cast<core::InputEventType>(incoming.type);
    event.origin   = core::InputOrigin::Remote;
    event.button   = incoming.button;
    event.x        = incoming.x;
    event.y        = incoming.y;
    event.touch_id = incoming.touch_id;
    // A client that sends 0 means "now". A client that sends its own timestamp
    // is replaying a recorded gesture and its intervals are the point, so they
    // are kept.
    event.at_ms = incoming.at_ms == 0 ? now_ms : incoming.at_ms;

    if (event.type == core::InputEventType::PointerDown ||
        event.type == core::InputEventType::PointerMove ||
        event.type == core::InputEventType::PointerUp) {
        std::uint16_t width = 0;
        std::uint16_t height = 0;
        PixelFormat format = PixelFormat::Unknown;
        Orientation orientation = Orientation::Deg0;
        std::size_t bytes = 0;
        ScreenSource::Failure why = ScreenSource::Failure::None;
        (void)source_.capture(nullptr, 0, width, height, format, orientation, bytes, why);
        if (event.x < 0 || event.y < 0 || event.x >= width || event.y >= height) {
            ++stats_.events_refused;
            send_error(req_id, ErrorCode::BadInput, emit, ctx);
            return;
        }
    }

    // The state machine decides whether this event is possible before it is
    // queued, so an impossible one is an answerable error rather than a
    // confusing screen. A second finger is separated out because "your stack
    // is single-touch" is a different fact from "that press made no sense".
    if (event.type == core::InputEventType::PointerDown &&
        event.touch_id >= core::kMaxTouchPoints) {
        ++stats_.events_refused;
        send_error(req_id, ErrorCode::TooManyTouches, emit, ctx);
        return;
    }
    // A button the board marks as not injectable is refused **here**, not by
    // the host tool. The T-Watch's BOOT is a boot-mode strap read at reset: it
    // produces no software event on real hardware, so simulating one would
    // manufacture an input the interface can never actually receive. The
    // header two files up argues that a limit only the well-behaved client
    // enforces is not a limit; this is that limit.
    //
    // Only a button that **exists** and is not injectable is refused here. An
    // index past the profile falls through to the state machine below and
    // comes back as `BadInput`, because "there is no button 2" and "button 1
    // is a service key" are different facts -- the same distinction `Busy` was
    // failing to make, and it would be a poor joke to fix that one and break
    // this one in the same commit.
    if (event.type == core::InputEventType::ButtonDown ||
        event.type == core::InputEventType::ButtonUp) {
        const ButtonDescriptor* descriptors = source_.buttons();
        if (descriptors != nullptr && event.button < source_.button_count() &&
            (descriptors[event.button].flags & kButtonInjectable) == 0) {
            ++stats_.events_refused;
            send_error(req_id, ErrorCode::Unsupported, emit, ctx);
            return;
        }
    }

    // Button ownership is enforced by `InputState` for every producer. Pointer
    // ownership remains here because physical touch separately remembers
    // whether its own down was accepted before it can send a move or release.
    if ((event.type == core::InputEventType::PointerUp ||
         event.type == core::InputEventType::PointerMove) &&
        state_.pointer_down() && state_.pointer_origin() != core::InputOrigin::Remote) {
        ++stats_.events_refused;
        send_error(req_id, ErrorCode::BadInput, emit, ctx);
        return;
    }

    // Read before `apply`, for the PointerMove rollback below.
    const std::int16_t before_x = state_.pointer_x();
    const std::int16_t before_y = state_.pointer_y();

    if (!state_.apply(event, source_.button_count())) {
        ++stats_.events_refused;
        send_error(req_id, ErrorCode::BadInput, emit, ctx);
        return;
    }
    if (!queue_.push(event)) {
        // The queue overran. The state machine has already recorded the press,
        // so it is rolled back rather than left claiming something is held that
        // the interface will never see go down.
        // Every transition is rolled back, releases included. An un-rolled-back
        // ButtonUp left the state machine believing a held button was free
        // while the widget under it was still pressed -- and with the hold
        // forgotten, the expiry that exists to rescue exactly that case had
        // nothing left to fire on.
        core::InputEvent undo = event;
        // Every pointer transition writes `pointer_x_`/`pointer_y_` from its own
        // event -- `core/src/input.cpp` does it in all three cases -- so undoing
        // one by type alone leaves the coordinates of the *refused* event
        // behind. That is not cosmetic: `release_all` and `tick` lift "where it
        // was last seen" from exactly that field, so a refused release at
        // (300,400) over a finger held at (10,10) ends with LVGL getting a
        // release at (300,400) and firing whatever sits there. The rollback
        // therefore restores the coordinates as well as the type, which is what
        // "every transition is rolled back" has to mean.
        undo.x = before_x;
        undo.y = before_y;
        switch (event.type) {
        case core::InputEventType::ButtonDown:
            undo.type = core::InputEventType::ButtonUp;
            (void)state_.apply(undo, source_.button_count());
            break;
        case core::InputEventType::ButtonUp:
            undo.type = core::InputEventType::ButtonDown;
            (void)state_.apply(undo, source_.button_count());
            break;
        case core::InputEventType::PointerDown:
            undo.type = core::InputEventType::PointerUp;
            (void)state_.apply(undo, source_.button_count());
            break;
        case core::InputEventType::PointerUp:
            undo.type = core::InputEventType::PointerDown;
            (void)state_.apply(undo, source_.button_count());
            break;
        case core::InputEventType::PointerMove:
            // A move has no inverse type, so it is undone by moving back --
            // which the shared coordinate restore above already does.
            (void)state_.apply(undo, source_.button_count());
            break;
        default:
            break;
        }
        ++stats_.events_refused;
        // Its own code. `Busy` means a screen transfer is already running; a
        // dropped swipe point answering with it sent the reader to the wrong
        // subsystem entirely.
        send_error(req_id, ErrorCode::QueueFull, emit, ctx);
        return;
    }

    // Deliberately `now_ms`, not `event.at_ms`, and the two disagree on
    // purpose. The queued event keeps the client's timestamp because a gesture
    // recogniser needs the finger's own intervals. This is the safety timer
    // that guarantees a crashed client cannot leave a button held, and keying
    // it on a number the client chose means a client replaying a recording
    // from another epoch expires its own hold on the very next tick -- or,
    // with a timestamp in the future, never.
    if (event.type == core::InputEventType::ButtonDown && event.button < core::kMaxButtons) {
        button_down_at_[event.button] = now_ms;
    }
    if (event.type == core::InputEventType::PointerDown) {
        pointer_down_at_ = now_ms;
    }

    ++stats_.events_injected;

    Envelope envelope;
    envelope.req_id = req_id;
    envelope.op     = Opcode::InputOk;
    send(envelope, nullptr, 0, emit, ctx);
}

void Bridge::handle_input_reset(std::uint16_t req_id, std::uint32_t now_ms, Emit emit, void* ctx)
{
    const std::uint8_t released = state_.release_all(core::InputOrigin::Remote, now_ms, queue_);

    // Two numbers, because one cannot tell the two failures apart. `released`
    // counts what reached the queue; `still_held` is what `release_all`
    // deliberately left held because the queue refused it (`input.h`, above
    // `release_all`). A lone `0` means both "nothing was stuck" and "everything
    // still is", and a lone `1` reads as complete when a button went out and
    // the finger did not -- and this command is advertised as the escape hatch
    // for exactly the stalled interface that produces those.
    //
    // Still `InputOk` rather than `QueueFull`: the release is not lost, the
    // hold expiry retries it, and a command that half-succeeded is not a
    // refused one. The caller is told enough to say which happened.
    const std::uint8_t body[2] = {released, state_.held_count(core::InputOrigin::Remote)};

    Envelope envelope;
    envelope.req_id = req_id;
    envelope.op     = Opcode::InputOk;
    send(envelope, body, sizeof(body), emit, ctx);
}

void Bridge::tick(std::uint32_t now_ms, Emit emit, void* ctx)
{
    (void)emit;
    (void)ctx;

    // Push first, mutate second -- the order `core::InputState::release_all`
    // establishes and argues for at length. `apply()` runs first in an `&&`,
    // so the obvious spelling clears the hold and *then* discovers the queue
    // was full: the release never reaches LVGL, the widget stays pressed, and
    // because the state now says nothing is held, no later tick, no
    // `input reset` and no disconnect will ever try again. The 30-second
    // escape hatch would have fired exactly once and done nothing.
    //
    // Discarding `apply()`'s result is safe here: the guards above have
    // already established the input is held, by Remote, at an index below
    // `kMaxButtons`, so it cannot fail.

    for (std::uint8_t i = 0; i < core::kMaxButtons; ++i) {
        if (state_.button_down(i) && state_.button_origin(i) == core::InputOrigin::Remote &&
            now_ms - button_down_at_[i] > limits_.max_hold_ms) {
            core::InputEvent up;
            up.type   = core::InputEventType::ButtonUp;
            up.origin = core::InputOrigin::Remote;
            up.button = i;
            up.at_ms  = now_ms;
            if (queue_.push(up)) {
                (void)state_.apply(up, core::kMaxButtons);
                ++stats_.holds_expired;
            }
        }
    }

    if (state_.pointer_down() && state_.pointer_origin() == core::InputOrigin::Remote &&
        now_ms - pointer_down_at_ > limits_.max_hold_ms) {
        core::InputEvent up;
        up.type   = core::InputEventType::PointerUp;
        up.origin = core::InputOrigin::Remote;
        up.x        = state_.pointer_x();
        up.y        = state_.pointer_y();
        // Not 0. `apply` refuses a `PointerUp` whose id does not match the
        // finger that is down (`core/src/input.cpp`), and a literal zero is
        // right only while `kMaxTouchPoints == 1`. Raise that constant with a
        // zero here and the expiry inverts: the push succeeds, LVGL is told a
        // finger it does not know about was lifted, `apply` fails, the real
        // pointer stays held, and this branch fires again every tick until the
        // queue is full -- the failure that was fixed for buttons above.
        // `release_all` has always done this correctly; this was the one site
        // that did not.
        up.touch_id = state_.pointer_touch_id();
        up.at_ms    = now_ms;
        if (queue_.push(up)) {
            // Unlike the button loop, the result is *not* discarded. The
            // argument that made discarding safe there is about `kMaxButtons`
            // and does not carry to a pointer whose id has to match.
            if (state_.apply(up, core::kMaxButtons)) {
                ++stats_.holds_expired;
            }
        }
    }
}

void Bridge::on_disconnect(std::uint32_t now_ms)
{
    (void)state_.release_all(core::InputOrigin::Remote, now_ms, queue_);
    transfer_.active = false;
}

}  // namespace attadipa::debug
