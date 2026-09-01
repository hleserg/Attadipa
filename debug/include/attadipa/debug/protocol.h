#pragma once

#include <cstddef>
#include <cstdint>

// The debug channel's wire format.
//
// **This is not a second protocol.** It is a message class inside the one the
// project already has:
//
//   link::frame_codec framing   sync F1 5E | len | len_check | payload | crc16
//     └─ ADR-0005 section 4 envelope   ver | class | req_id | op | body_len | crc16
//          └─ this file's bodies       fixed little-endian fields
//
// Three consequences of reusing the framing rather than inventing one, and all
// three are the reason the reuse is worth more than the saved code:
//
//   * **A text log sharing the stream cannot corrupt an image.** The decoder
//     resynchronises on F1 5E and rejects anything whose CRC does not match, and
//     0xF1 is not a byte ASCII log lines contain. A stray printf costs one
//     resync, counted in DecoderStats, not a torn screenshot.
//   * **A corrupted length is rejected at the header** instead of committing
//     the reader to hundreds of bytes of noise.
//   * **An over-long frame is an error, never a truncation.** Which matters
//     more here than on the node link: a truncated screenshot that reported
//     success would be an image with a plausible-looking bottom edge.
//
// ### Why the bodies are not TLV
//
// ADR-0005's body is `TLV*`, and the frame codec's own comment records that the
// envelope and its TLV body are **provisional pending the encoding benchmark
// (T-016)**. So this file deliberately does not settle that question for the
// node class. The debug class uses fixed little-endian bodies because its
// messages are fixed-shape and one of them is high-volume: a screenshot is
// thousands of chunks, and a tag and a length on every field of every chunk is
// overhead paid ten thousand times to describe a layout that never varies. If
// T-016 changes the node class body encoding, this class is unaffected — that
// is what `class` and `ver` are for.
//
// ### The payload bound is not raised
//
// `link::kMaxPayload` is 192 bytes and RESOURCE_BUDGET section 4 requires any
// pool to be sized to the maximum payload with the bound declared. It would
// have been easy to widen it for screenshots. It is not widened: images are
// chunked to fit, which costs frames and keeps every buffer in the system the
// size it was reviewed at.

namespace attadipa::debug {

// ---------------------------------------------------------------------------
// Envelope
// ---------------------------------------------------------------------------

// ADR-0005 section 4 lists six fields -- ver, class, req_id, op, body_len,
// crc16 -- which is ten bytes. Its diagram draws twelve column positions above
// them; the columns are a drafting artefact of the ASCII table, not two
// unnamed bytes. The fields are implemented, because a field list is a
// specification and a column count is a picture.
inline constexpr std::size_t kEnvelopeBytes = 10;

// ADR-0005 names `class` and assigns no values. These are the first assignment.
inline constexpr std::uint8_t kClassNode  = 0x01;  // reserved for ADR-0005 proper
inline constexpr std::uint8_t kClassDebug = 0x02;  // this file

// The debug class's own version, negotiated in Hello and independent of the
// node protocol's `proto_major` -- ADR-0005 section 5's point that version and
// capability set are orthogonal applies here too.
inline constexpr std::uint8_t kDebugProtocolVersion = 1;

enum class Opcode : std::uint16_t {
    // Requests
    Hello         = 0x0001,
    Capabilities  = 0x0002,
    ScreenRequest = 0x0010,
    InputEvent    = 0x0020,
    InputReset    = 0x0021,
    WaitStable    = 0x0030,
    TimeSync      = 0x0040,
    MeshConfigure = 0x0050,
    MeshSend      = 0x0051,
    MeshRoomSend  = 0x0052,
    MeshDisconnect= 0x0053,
    MeshForgetBond= 0x0054,

    // Responses
    HelloOk       = 0x8001,
    CapabilitiesOk= 0x8002,
    ScreenInfo    = 0x8010,
    ScreenData    = 0x8011,
    ScreenEnd     = 0x8012,
    InputOk       = 0x8020,
    StableOk      = 0x8030,
    TimeSyncOk    = 0x8040,
    MeshOk        = 0x8050,
    Error         = 0x80FF,
};

// Typed errors. ADR-0005 section 4: an unknown opcode is answered with a typed
// error and never ignored, because silence is indistinguishable from a lost
// frame and the host would retry forever.
enum class ErrorCode : std::uint16_t {
    None             = 0,
    UnknownOpcode    = 1,
    BadBody          = 2,
    Unsupported      = 3,   // understood, and this build cannot do it
    BadInput         = 4,   // impossible from the current input state
    TooManyTouches   = 5,   // a second finger; see core/input.h
    NoScreen         = 6,   // nothing has been rendered yet
    Busy             = 7,   // one of that kind is already running: a screen transfer, or a bond deletion
    RateLimited      = 8,
    VersionMismatch  = 9,
    QueueFull        = 10,  // the input queue overran; the event was dropped
    CaptureFailed    = 11,  // the renderer could not produce a frame -- typically out of memory
    ScreenGeometry   = 12,  // the active screen is not the panel size
    OperationFailed  = 13,  // valid request; the device-side operation failed
};

// Appended, never renumbered. A value on this wire is a fact somebody's log
// already contains, and the Python side mirrors these by hand
// (`tools/watch/protocol.py`) rather than generating them, so a moved number
// is a silently mistranslated error message rather than a build failure.
//
// `QueueFull` exists because `Busy` was answering for two unrelated things: a
// second screenshot arriving mid-transfer, and a swipe point that found the
// input queue full. A dropped gesture reporting itself as a screenshot
// collision sends the reader to the wrong subsystem.
//
// A forget-bond arriving over one already running is `Busy` rather than a
// fourteenth code, and that is the same line rather than a step back over it:
// `Busy` means "a request of this kind is already running, wait for it", which
// is one condition with two subjects, where the input queue overrunning was a
// second subsystem wearing the first one's name. What `Busy` must never absorb
// again is an operation that *failed* -- #381 found `OperationFailed` doing the
// mirror image of this, telling the operator a deletion had been refused when
// the request had not reached the store at all.
//
// `CaptureFailed` and `ScreenGeometry` exist for the same reason one layer up.
// Every way a capture can fail used to answer `NoScreen`, "nothing has been
// rendered yet" -- so an LVGL allocation failure, which is a real prospect
// because `lv_conf_simulator.h` fixes `LV_MEM_SIZE` at 1 MiB deliberately "so
// that the simulator can still run out of LVGL memory the way a watch would",
// told the operator to wait for a frame and take the picture again. Waiting
// does not fix either of these. `NoScreen` keeps its meaning and no shipped
// source returns it today: `lv_snapshot_take` re-renders, so it succeeds before
// the first `lv_timer_handler` too. A device source may still need it.

struct Envelope {
    std::uint8_t  version  = kDebugProtocolVersion;
    std::uint8_t  cls      = kClassDebug;
    std::uint16_t req_id   = 0;
    Opcode        op       = Opcode::Hello;
    std::uint16_t body_len = 0;
};

// Writes envelope + body into `out`. Returns bytes written, or 0 if `out` is
// too small or the body exceeds what one frame's payload can carry -- never a
// partial message, for the same reason frame_codec never writes a partial
// frame.
std::size_t encode_message(const Envelope& envelope, const std::uint8_t* body,
                           std::size_t body_len, std::uint8_t* out, std::size_t out_capacity);

// Reads one message out of a frame payload. Returns false on a short payload,
// a body length that disagrees with the payload, or a failed envelope CRC.
// `body_out` points into `payload`; it is not copied and not owned, so it is
// valid exactly as long as `payload` is. Storing it past the lifetime of the
// buffer it came from is the caller's bug, not a defect here -- CodeQL's
// cpp/stack-address-escape reads the assignment as an escape for this reason.
bool decode_message(const std::uint8_t* payload, std::size_t payload_len, Envelope& envelope_out,
                    const std::uint8_t*& body_out);

// The most body bytes one message can carry, given the framing.
inline constexpr std::size_t kMaxBody = 192 /* link::kMaxPayload */ - kEnvelopeBytes;

// ---------------------------------------------------------------------------
// Pixel formats and orientation
// ---------------------------------------------------------------------------

// What the bytes of a screenshot mean. Named explicitly including byte order,
// because "RGB565" alone is the single most common way a screenshot comes back
// looking almost right: the simulator's LVGL build stores RGB888 as B,G,R in
// memory, and a device panel will be 16-bit with an endianness decided by the
// SPI/QSPI driver rather than by the pixel format.
enum class PixelFormat : std::uint8_t {
    Unknown    = 0,
    Rgb888     = 1,  // three bytes per pixel, R then G then B
    Rgb565Le   = 2,  // two bytes per pixel, little-endian: low byte first
    Rgb565Be   = 3,  // two bytes per pixel, big-endian
    // Three bytes per pixel, B then G then R. Not a curiosity: this is how
    // LVGL stores LV_COLOR_FORMAT_RGB888 in memory (`lv_color_t` in
    // src/misc/lv_color.h), so it is what a snapshot of the simulator actually
    // contains. Naming it on the wire is the alternative to swapping every
    // pixel on the device and hoping both ends agree -- and a swapped channel
    // is the single most common way a screenshot comes back looking almost
    // right, which is exactly the failure the owner asked to be ruled out.
    Bgr888     = 4,
};

std::uint8_t bytes_per_pixel(PixelFormat format);

// The rotation the **host must apply, clockwise**, to make the received frame
// upright.
//
// The direction is defined here rather than derived, because "the framebuffer
// is rotated by N relative to the panel" and "rotate by N to display it" are
// opposite instructions that both read naturally. A driver that reports one
// while the host assumes the other produces an image that is upside down in
// exactly half of the cases, which is the kind of bug that survives review
// because the other half looks fine. `Deg90` means: turn the received image a
// quarter turn clockwise. Width and height swap for 90 and 270.
//
// Carried on the wire rather than assumed by the host, because it is a board
// fact and the host must not hold a table of board facts -- that is the
// architecture rule in CLAUDE.md applied to a tool: the host asks what the
// device can do, never which device it is.
enum class Orientation : std::uint8_t {
    Deg0   = 0,
    Deg90  = 1,
    Deg180 = 2,
    Deg270 = 3,
};

// ---------------------------------------------------------------------------
// Bodies
// ---------------------------------------------------------------------------

// Hello (request): version the host speaks.
// HelloOk (response): version the device speaks, plus who it is.
struct HelloBody {
    std::uint8_t protocol_version = kDebugProtocolVersion;
    char         board_id[24]     = {};  // BoardProfile::id, NUL-padded
    char         build[24]        = {};  // "simulator" or a firmware version
};

inline constexpr std::size_t kHelloBodyBytes = 1 + 24 + 24;

std::size_t encode_hello(const HelloBody& in, std::uint8_t* out, std::size_t capacity);
bool        decode_hello(const std::uint8_t* body, std::size_t len, HelloBody& out);

// One button, as the device declares it.
struct ButtonDescriptor {
    char         id[16] = {};  // stable, lowercase, what the host tool names
    std::uint8_t flags  = 0;   // bit 0: injectable. bit 1: role is UNKNOWN
};

inline constexpr std::uint8_t kButtonInjectable = 0x01;
inline constexpr std::uint8_t kButtonRoleUnknown = 0x02;

struct CapabilitiesBody {
    std::uint16_t     width            = 0;
    std::uint16_t     height           = 0;
    PixelFormat       format           = PixelFormat::Unknown;
    Orientation       orientation      = Orientation::Deg0;
    std::uint8_t      max_touch_points = 1;
    std::uint8_t      button_count     = 0;
    ButtonDescriptor  buttons[4]       = {};
    std::uint16_t     max_body         = kMaxBody;
    std::uint32_t     max_hold_ms      = 0;
    std::uint16_t     max_events_per_s = 0;
};

inline constexpr std::size_t kCapabilitiesBodyBytes = 2 + 2 + 1 + 1 + 1 + 1 + (4 * 17) + 2 + 4 + 2;

std::size_t encode_capabilities(const CapabilitiesBody& in, std::uint8_t* out, std::size_t capacity);
bool        decode_capabilities(const std::uint8_t* body, std::size_t len, CapabilitiesBody& out);

// ScreenInfo: the header of one image, sent once before the chunks.
struct ScreenInfoBody {
    std::uint32_t frame_id    = 0;
    std::uint16_t width       = 0;
    std::uint16_t height      = 0;
    PixelFormat   format      = PixelFormat::Unknown;
    Orientation   orientation = Orientation::Deg0;
    std::uint32_t total_bytes = 0;
    std::uint32_t crc32       = 0;  // over the whole image, not per chunk
    std::uint32_t at_ms       = 0;
};

inline constexpr std::size_t kScreenInfoBodyBytes = 4 + 2 + 2 + 1 + 1 + 4 + 4 + 4;

std::size_t encode_screen_info(const ScreenInfoBody& in, std::uint8_t* out, std::size_t capacity);
bool        decode_screen_info(const std::uint8_t* body, std::size_t len, ScreenInfoBody& out);

// InputEvent body. Mirrors core::InputEvent without depending on it, so the
// wire format is not hostage to a refactor of the internal type.
struct InputEventBody {
    std::uint8_t  type     = 0;  // core::InputEventType
    std::uint8_t  button   = 0;
    std::int16_t  x        = 0;
    std::int16_t  y        = 0;
    std::uint8_t  touch_id = 0;
    std::uint32_t at_ms    = 0;  // 0 = stamp on arrival
};

inline constexpr std::size_t kInputEventBodyBytes = 1 + 1 + 2 + 2 + 1 + 4;

std::size_t encode_input_event(const InputEventBody& in, std::uint8_t* out, std::size_t capacity);
bool        decode_input_event(const std::uint8_t* body, std::size_t len, InputEventBody& out);

// A deliberate wall-clock correction from the development host. UTC and the
// presentation offset are separate fields; `valid_for_ms` bounds both the
// sample's freshness and how long the current offset may be trusted offline.
struct TimeSyncBody {
    std::int64_t  utc_seconds             = 0;
    std::int16_t  timezone_offset_minutes = 0;
    std::uint32_t valid_for_ms             = 0;
    std::uint8_t  flags                    = 0;
};

inline constexpr std::uint8_t kTimeSyncAllowLargeCorrection = 0x01;
inline constexpr std::size_t kTimeSyncBodyBytes = 8 + 2 + 4 + 1;

std::size_t encode_time_sync(const TimeSyncBody& in, std::uint8_t* out,
                             std::size_t capacity);
bool decode_time_sync(const std::uint8_t* body, std::size_t len,
                      TimeSyncBody& out);

// ---------------------------------------------------------------------------
// Integrity
// ---------------------------------------------------------------------------

// CRC-32/ISO-HDLC, the one PNG and zlib use. A different algorithm from the
// framing's CRC-16 on purpose: the framing already proved each chunk arrived
// intact, and what this checks is that the chunks were *assembled* correctly --
// none missing, none doubled, none out of order. Two independent checks catch
// two independent failures; the same check twice catches one.
std::uint32_t crc32(const std::uint8_t* data, std::size_t length, std::uint32_t seed = 0);

}  // namespace attadipa::debug
