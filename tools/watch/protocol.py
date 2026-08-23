"""The debug channel's wire format, on the host side.

This is the Python half of `link/frame_codec.h` and `debug/protocol.h`, and it
is deliberately a *reimplementation* rather than a binding. Two independent
implementations of one format catch the class of bug a shared one cannot: an
encoder and decoder that agree with each other while both being wrong. The
device's own comment about its CRC span makes the same point -- get the span off
by one at both ends and every round trip passes while the last byte of every
frame goes unprotected.

`selftest.py` holds them against fixed byte vectors for that reason, not only
against each other.

Nothing here talks to a socket or a serial port; `client.py` does. Keeping the
format pure is what lets the tests run with no device, no simulator and no
network.
"""

from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field
from enum import IntEnum

# --- framing (link/frame_codec.h) -----------------------------------------

SYNC0 = 0xF1
SYNC1 = 0x5E
HEADER_BYTES = 5
TRAILER_BYTES = 2
OVERHEAD_BYTES = HEADER_BYTES + TRAILER_BYTES
MAX_PAYLOAD = 192
MAX_FRAME = MAX_PAYLOAD + OVERHEAD_BYTES


def length_check(length: int) -> int:
    """One byte derived from the length, salted.

    The salt is what stops an all-zero or all-0xFF stream -- a stuck bus, an
    unpowered device -- from producing a self-consistent header.
    """
    return ((length & 0xFF) ^ (length >> 8) ^ 0x5A) & 0xFF


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def frame_encode(payload: bytes) -> bytes:
    """Wrap one payload in a frame. Raises rather than truncating."""
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload of {len(payload)} exceeds {MAX_PAYLOAD}")
    header = bytes([SYNC0, SYNC1, len(payload) & 0xFF, len(payload) >> 8,
                    length_check(len(payload))])
    # The CRC covers the two length bytes, the length check and the payload --
    # header[2:] and then the payload. Not header[3:]: getting this off by one
    # leaves the last byte unprotected at both ends, which is invisible to a
    # round-trip test.
    crc = crc16_ccitt(header[2:] + payload)
    return header + payload + bytes([crc & 0xFF, crc >> 8])


@dataclass
class DecoderStats:
    frames: int = 0
    resyncs: int = 0
    bad_length: int = 0
    bad_crc: int = 0


class FrameDecoder:
    """A resynchronising decoder over a byte stream.

    Fragment-agnostic on purpose: feed it one byte or ten thousand and the
    frames that come out are the same. That property is what makes a text log
    sharing the stream harmless -- a stray line costs some resyncs, counted, and
    never a corrupted image.
    """

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.stats = DecoderStats()

    def push(self, data: bytes) -> None:
        self._buffer.extend(data)

    def __iter__(self):
        while True:
            frame = self.next()
            if frame is None:
                return
            yield frame

    def next(self) -> bytes | None:
        while True:
            # Find a sync pattern, discarding anything before it.
            start = self._buffer.find(bytes([SYNC0, SYNC1]))
            if start < 0:
                # Keep the last byte: a sync split across two reads must not be
                # lost. This is the fragment-boundary guarantee.
                dropped = max(0, len(self._buffer) - 1)
                if dropped:
                    self.stats.resyncs += dropped
                    del self._buffer[:dropped]
                return None
            if start > 0:
                self.stats.resyncs += start
                del self._buffer[:start]

            if len(self._buffer) < HEADER_BYTES:
                return None

            length = self._buffer[2] | (self._buffer[3] << 8)
            if length > MAX_PAYLOAD or self._buffer[4] != length_check(length):
                # An impossible length is rejected at the header, before the
                # decoder commits to reading a payload that may not exist. Drop
                # one byte and look for the next sync -- never trust the length.
                self.stats.bad_length += 1
                self.stats.resyncs += 1
                del self._buffer[:1]
                continue

            total = length + OVERHEAD_BYTES
            if len(self._buffer) < total:
                return None

            payload = bytes(self._buffer[HEADER_BYTES:HEADER_BYTES + length])
            got = self._buffer[HEADER_BYTES + length] | (self._buffer[HEADER_BYTES + length + 1] << 8)
            want = crc16_ccitt(bytes(self._buffer[2:HEADER_BYTES]) + payload)
            if got != want:
                self.stats.bad_crc += 1
                self.stats.resyncs += 1
                del self._buffer[:1]
                continue

            del self._buffer[:total]
            self.stats.frames += 1
            return payload


# --- envelope (docs/adr/0005-node-protocol.md section 4) -------------------

ENVELOPE_BYTES = 10
CLASS_NODE = 0x01
CLASS_DEBUG = 0x02
PROTOCOL_VERSION = 1
MAX_BODY = MAX_PAYLOAD - ENVELOPE_BYTES


class Op(IntEnum):
    HELLO = 0x0001
    CAPABILITIES = 0x0002
    SCREEN_REQUEST = 0x0010
    INPUT_EVENT = 0x0020
    INPUT_RESET = 0x0021
    WAIT_STABLE = 0x0030

    HELLO_OK = 0x8001
    CAPABILITIES_OK = 0x8002
    SCREEN_INFO = 0x8010
    SCREEN_DATA = 0x8011
    SCREEN_END = 0x8012
    INPUT_OK = 0x8020
    STABLE_OK = 0x8030
    ERROR = 0x80FF


class ErrorCode(IntEnum):
    NONE = 0
    UNKNOWN_OPCODE = 1
    BAD_BODY = 2
    UNSUPPORTED = 3
    BAD_INPUT = 4
    TOO_MANY_TOUCHES = 5
    NO_SCREEN = 6
    BUSY = 7
    RATE_LIMITED = 8
    VERSION_MISMATCH = 9


ERROR_TEXT = {
    ErrorCode.UNKNOWN_OPCODE: "the device does not know that command",
    ErrorCode.BAD_BODY: "the device could not read the command's arguments",
    ErrorCode.UNSUPPORTED: "this build of the firmware cannot do that",
    ErrorCode.BAD_INPUT: "that input is impossible from the current state "
                         "(a release with nothing held, a button this board does not have)",
    ErrorCode.TOO_MANY_TOUCHES: "this stack is single-touch",
    ErrorCode.NO_SCREEN: "nothing has been rendered yet",
    ErrorCode.BUSY: "a screenshot is already in progress",
    ErrorCode.RATE_LIMITED: "too many input events per second",
    ErrorCode.VERSION_MISMATCH: "the device speaks a different protocol version",
}


class ProtocolError(RuntimeError):
    """A typed error from the device, or a malformed reply."""

    def __init__(self, message: str, code: ErrorCode | None = None) -> None:
        super().__init__(message)
        self.code = code


@dataclass
class Envelope:
    op: Op
    req_id: int = 0
    version: int = PROTOCOL_VERSION
    cls: int = CLASS_DEBUG
    body: bytes = b""


def envelope_encode(envelope: Envelope) -> bytes:
    if len(envelope.body) > MAX_BODY:
        raise ValueError(f"body of {len(envelope.body)} exceeds {MAX_BODY}")
    header = struct.pack("<BBHHH", envelope.version, envelope.cls, envelope.req_id,
                         int(envelope.op), len(envelope.body))
    crc = crc16_ccitt(header + envelope.body)
    return header + struct.pack("<H", crc) + envelope.body


def envelope_decode(payload: bytes) -> Envelope:
    if len(payload) < ENVELOPE_BYTES:
        raise ProtocolError(f"a {len(payload)}-byte payload is shorter than an envelope")
    version, cls, req_id, op, body_len = struct.unpack("<BBHHH", payload[:8])
    (crc,) = struct.unpack("<H", payload[8:10])
    if body_len > MAX_BODY or ENVELOPE_BYTES + body_len != len(payload):
        raise ProtocolError(
            f"the envelope declares a {body_len}-byte body but {len(payload) - ENVELOPE_BYTES} arrived")
    body = payload[ENVELOPE_BYTES:]
    if crc16_ccitt(payload[:8] + body) != crc:
        raise ProtocolError("the envelope's checksum does not match")
    try:
        op_enum = Op(op)
    except ValueError as exc:
        raise ProtocolError(f"unknown opcode 0x{op:04X}") from exc
    return Envelope(op=op_enum, req_id=req_id, version=version, cls=cls, body=body)


# --- bodies ---------------------------------------------------------------

HELLO_BYTES = 1 + 24 + 24
SCREEN_INFO_BYTES = 4 + 2 + 2 + 1 + 1 + 4 + 4 + 4
INPUT_EVENT_BYTES = 1 + 1 + 2 + 2 + 1 + 4
BUTTON_BYTES = 17
CAPABILITIES_BYTES = 2 + 2 + 1 + 1 + 1 + 1 + 4 * BUTTON_BYTES + 2 + 4 + 2

BUTTON_INJECTABLE = 0x01
BUTTON_ROLE_UNKNOWN = 0x02


class PixelFormat(IntEnum):
    UNKNOWN = 0
    RGB888 = 1
    RGB565_LE = 2
    RGB565_BE = 3
    BGR888 = 4


BYTES_PER_PIXEL = {
    PixelFormat.RGB888: 3,
    PixelFormat.BGR888: 3,
    PixelFormat.RGB565_LE: 2,
    PixelFormat.RGB565_BE: 2,
}


class Orientation(IntEnum):
    DEG0 = 0
    DEG90 = 1
    DEG180 = 2
    DEG270 = 3


class EventType(IntEnum):
    BUTTON_DOWN = 0
    BUTTON_UP = 1
    POINTER_DOWN = 2
    POINTER_MOVE = 3
    POINTER_UP = 4


def _fixed(text: str, size: int) -> bytes:
    raw = text.encode("utf-8")[: size - 1]
    return raw + b"\0" * (size - len(raw))


def _unfixed(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", "replace")


@dataclass
class Hello:
    protocol_version: int = PROTOCOL_VERSION
    board_id: str = ""
    build: str = ""

    def encode(self) -> bytes:
        return bytes([self.protocol_version]) + _fixed(self.board_id, 24) + _fixed(self.build, 24)

    @staticmethod
    def decode(body: bytes) -> "Hello":
        if len(body) < HELLO_BYTES:
            raise ProtocolError("a short hello body")
        return Hello(body[0], _unfixed(body[1:25]), _unfixed(body[25:49]))


@dataclass
class Button:
    id: str = ""
    injectable: bool = True
    role_known: bool = True


@dataclass
class Capabilities:
    width: int = 0
    height: int = 0
    format: PixelFormat = PixelFormat.UNKNOWN
    orientation: Orientation = Orientation.DEG0
    max_touch_points: int = 1
    buttons: list[Button] = field(default_factory=list)
    max_body: int = MAX_BODY
    max_hold_ms: int = 0
    max_events_per_s: int = 0

    @staticmethod
    def decode(body: bytes) -> "Capabilities":
        if len(body) < CAPABILITIES_BYTES:
            raise ProtocolError("a short capabilities body")
        width, height, fmt, orientation, touches, count = struct.unpack("<HHBBBB", body[:8])
        buttons = []
        at = 8
        for index in range(4):
            raw = body[at:at + BUTTON_BYTES]
            if index < count:
                buttons.append(Button(
                    id=_unfixed(raw[:16]),
                    injectable=bool(raw[16] & BUTTON_INJECTABLE),
                    role_known=not (raw[16] & BUTTON_ROLE_UNKNOWN),
                ))
            at += BUTTON_BYTES
        max_body, max_hold, max_rate = struct.unpack("<HIH", body[at:at + 8])
        return Capabilities(width, height, PixelFormat(fmt), Orientation(orientation),
                            touches, buttons, max_body, max_hold, max_rate)


@dataclass
class ScreenInfo:
    frame_id: int = 0
    width: int = 0
    height: int = 0
    format: PixelFormat = PixelFormat.UNKNOWN
    orientation: Orientation = Orientation.DEG0
    total_bytes: int = 0
    crc32: int = 0
    at_ms: int = 0

    @staticmethod
    def decode(body: bytes) -> "ScreenInfo":
        if len(body) < SCREEN_INFO_BYTES:
            raise ProtocolError("a short screen-info body")
        frame_id, width, height, fmt, orientation, total, crc, at_ms = struct.unpack(
            "<IHHBBIII", body[:SCREEN_INFO_BYTES])
        return ScreenInfo(frame_id, width, height, PixelFormat(fmt), Orientation(orientation),
                          total, crc, at_ms)


def input_event_encode(event_type: EventType, *, button: int = 0, x: int = 0, y: int = 0,
                       touch_id: int = 0, at_ms: int = 0) -> bytes:
    return struct.pack("<BBhhBI", int(event_type), button, x, y, touch_id, at_ms)


# --- pixels ---------------------------------------------------------------

def to_rgb(data: bytes, width: int, height: int, fmt: PixelFormat) -> bytes:
    """Convert the device's pixels into tightly packed 8-bit RGB.

    The formats are distinguished by byte order as well as by channel order,
    because "RGB565" alone is the single most common way a screenshot comes back
    looking almost right. `BGR888` is not an oddity: it is how LVGL stores
    LV_COLOR_FORMAT_RGB888 in memory, so it is what a snapshot actually
    contains.

    RGB565 is expanded by replicating the high bits into the low ones
    (`(v << 3) | (v >> 2)` for the five-bit channels), so that full-scale stays
    full-scale. Shifting alone would turn 0xFF into 0xF8 and make every white in
    a test pattern slightly grey.
    """
    pixels = width * height
    stride = BYTES_PER_PIXEL.get(fmt)
    if stride is None:
        raise ProtocolError(f"the device reported pixel format {fmt!r}, which this tool cannot read")
    if len(data) != pixels * stride:
        raise ProtocolError(
            f"{len(data)} bytes for a {width}x{height} image in {fmt.name} "
            f"(expected {pixels * stride})")

    if fmt is PixelFormat.RGB888:
        return data
    if fmt is PixelFormat.BGR888:
        out = bytearray(data)
        out[0::3], out[2::3] = data[2::3], data[0::3]
        return bytes(out)

    out = bytearray(pixels * 3)
    big_endian = fmt is PixelFormat.RGB565_BE
    for i in range(pixels):
        lo, hi = data[i * 2], data[i * 2 + 1]
        value = (lo << 8) | hi if big_endian else (hi << 8) | lo
        r5 = (value >> 11) & 0x1F
        g6 = (value >> 5) & 0x3F
        b5 = value & 0x1F
        out[i * 3 + 0] = (r5 << 3) | (r5 >> 2)
        out[i * 3 + 1] = (g6 << 2) | (g6 >> 4)
        out[i * 3 + 2] = (b5 << 3) | (b5 >> 2)
    return bytes(out)


def apply_orientation(rgb: bytes, width: int, height: int,
                      orientation: Orientation) -> tuple[bytes, int, int]:
    """Rotate the image so that the PNG is the right way up on a wrist.

    The rotation is the device's, reported on the wire, because it is a board
    fact -- a display driver that rotates its framebuffer is the only thing that
    knows. A host tool holding a table of which board rotates by how much would
    be exactly the "an application learns which device it is" failure the
    architecture forbids, moved into a script.

    **The angle is what the host must apply, clockwise**, and this definition is
    chosen rather than derived. "The framebuffer is rotated by N relative to the
    panel" and "rotate by N to display it" are opposite instructions that both
    read naturally, so one of the two had to be written down or every device
    driver would guess. `DEG90` means: turn the received image a quarter turn
    clockwise and it is upright. Width and height swap for 90 and 270.
    """
    if orientation is Orientation.DEG0:
        return rgb, width, height

    rows = [rgb[y * width * 3:(y + 1) * width * 3] for y in range(height)]
    if orientation is Orientation.DEG180:
        out = bytearray()
        for row in reversed(rows):
            for x in range(width - 1, -1, -1):
                out += row[x * 3:x * 3 + 3]
        return bytes(out), width, height

    # 90 and 270. The output is transposed, so width and height swap.
    out = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            pixel = rows[y][x * 3:x * 3 + 3]
            if orientation is Orientation.DEG90:
                nx, ny = height - 1 - y, x
            else:
                nx, ny = y, width - 1 - x
            at = (ny * height + nx) * 3
            out[at:at + 3] = pixel
    return bytes(out), height, width


def write_png(path, rgb: bytes, width: int, height: int) -> None:
    """A real PNG, compressed.

    zlib is in the standard library, so unlike the simulator's C++ writer -- which
    emits stored blocks to avoid a build dependency -- this one has no reason not
    to deflate properly. A screenshot of the Waveshare panel is 617 kB raw.
    """
    if len(rgb) != width * height * 3:
        raise ValueError("pixel buffer does not match the stated size")

    raw = bytearray()
    for y in range(height):
        raw.append(0)  # filter type 0, none
        raw += rgb[y * width * 3:(y + 1) * width * 3]

    def chunk(tag: bytes, payload: bytes) -> bytes:
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
           + chunk(b"IEND", b""))
    with open(path, "wb") as handle:
        handle.write(png)


def crc32_of(data: bytes) -> int:
    """The image checksum, matching `attadipa::debug::crc32`.

    CRC-32/ISO-HDLC -- reflected polynomial 0xEDB88320, initial 0xFFFFFFFF,
    final complement -- which is exactly what `zlib.crc32` computes and what PNG
    itself uses. The device implements it bitwise to avoid carrying a 1 KiB
    table in flash; here there is no reason not to use the C one.
    """
    return zlib.crc32(data) & 0xFFFFFFFF
