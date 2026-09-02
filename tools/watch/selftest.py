"""Host tests for the remote-control tool.

Two kinds, and the second is the one that matters.

**Round trips** prove this module is self-consistent. That is worth little on
its own: an encoder and a decoder that share a mistake agree with each other
perfectly. The device's own source makes the point about its CRC span -- get it
off by one at both ends and every round trip passes while the last byte of every
frame goes unprotected.

**Fixed byte vectors** are the ones that catch it. The literals below were taken
from the C++ implementation and are asserted identically in
`tests/test_debug.cpp`, so the two independent implementations are pinned to the
same bytes rather than to each other. If either drifts, one of the two suites
fails and the hex string says where.

  python3 tools/watch/selftest.py
"""

from __future__ import annotations

import io
import os
import struct
import sys
import tempfile
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from watch import protocol as p  # noqa: E402

failures: list[str] = []


def check(condition: bool, what: str) -> None:
    if not condition:
        failures.append(what)


def check_raises(exception, what: str, call) -> None:
    try:
        call()
    except exception:
        return
    except Exception as exc:  # noqa: BLE001
        failures.append(f"{what}: raised {type(exc).__name__} instead of {exception.__name__}")
        return
    failures.append(f"{what}: did not raise")


# --- fixed vectors, shared with tests/test_debug.cpp -----------------------

# frame_encode(b"hello")
FRAME_HELLO = bytes.fromhex("f15e05005f68656c6c6f750f")
# crc16_ccitt(b"123456789") -- the algorithm's published check value
CRC16_CHECK = 0x29B1
# crc32(b"123456789")
CRC32_CHECK = 0xCBF43926

# One whole message per body kind, envelope included.
#
# The three above are `link::frame_codec`, which is the framing and not this
# protocol -- so until these existed, four documents claimed the two
# implementations were "pinned to the same bytes" while the envelope, the
# bodies and the two enum tables were only ever compared to *each other*, and
# only in the simulator-gated job. A mistake made identically on both sides --
# a swapped field, a wrong width -- round-tripped green on both.
#
# Every field below is a distinct value on purpose, so a transposed pair cannot
# survive: `x` is negative so the sign crosses the wire, `width` and `height`
# differ, `frame_id` is `0x11223344` and the session `HelloOk` echoes is
# `0xA5B6C7D8`. All three begin with the version byte, so a version bump
# re-pins all three: v2 added the session to Hello.
MSG_HELLO_OK = bytes.fromhex(
    "020234120180350016bb027761766573686172652d616d6f6c65642d3230360000"
    "000073696d20302e302e31000000000000000000000000000000d8c7b6a5")
MSG_SCREEN_INFO = bytes.fromhex(
    "0202785610801600af50443322119a01f6010201144b06002639f4cb4e61bc00")
MSG_INPUT_EVENT = bytes.fromhex(
    "0202bc9a20000b00a1d20301feff2c0107feff0000")

# The two numbering tables, spelled out rather than read from the enums, so a
# renumber fails here instead of silently mistranslating an operator's error
# message. `protocol.h` says these are appended and never renumbered; this is
# what makes that a check rather than a wish.
OPCODE_VALUES = {
    "HELLO": 0x0001, "CAPABILITIES": 0x0002, "SCREEN_REQUEST": 0x0010,
    "INPUT_EVENT": 0x0020, "INPUT_RESET": 0x0021, "WAIT_STABLE": 0x0030,
    "TIME_SYNC": 0x0040,
    "MESH_CONFIGURE": 0x0050, "MESH_SEND": 0x0051, "MESH_ROOM_SEND": 0x0052,
    "MESH_DISCONNECT": 0x0053, "MESH_FORGET_BOND": 0x0054,
    "HELLO_OK": 0x8001, "CAPABILITIES_OK": 0x8002, "SCREEN_INFO": 0x8010,
    "SCREEN_DATA": 0x8011, "SCREEN_END": 0x8012, "INPUT_OK": 0x8020,
    "STABLE_OK": 0x8030, "TIME_SYNC_OK": 0x8040, "MESH_OK": 0x8050,
    "ERROR": 0x80FF,
}
ERROR_VALUES = {
    "NONE": 0, "UNKNOWN_OPCODE": 1, "BAD_BODY": 2, "UNSUPPORTED": 3,
    "BAD_INPUT": 4, "TOO_MANY_TOUCHES": 5, "NO_SCREEN": 6, "BUSY": 7,
    "RATE_LIMITED": 8, "VERSION_MISMATCH": 9, "QUEUE_FULL": 10,
    "CAPTURE_FAILED": 11, "SCREEN_GEOMETRY": 12, "OPERATION_FAILED": 13,
}


def fixed_vectors() -> None:
    check(p.crc16_ccitt(b"123456789") == CRC16_CHECK,
          f"CRC-16/CCITT-FALSE check value is 0x{CRC16_CHECK:04X}")
    check(p.crc32_of(b"123456789") == CRC32_CHECK,
          f"CRC-32 check value is 0x{CRC32_CHECK:08X}")
    check(p.frame_encode(b"hello") == FRAME_HELLO,
          f"a frame around b'hello' is {FRAME_HELLO.hex()}")
    check(p.length_check(0) == 0x5A, "the length check is salted, so 0 does not map to 0")
    check(p.length_check(0xFFFF) == 0x5A, "0xFFFF does not map to 0 either")

    # --- the protocol itself, not the framing around it --------------------

    hello = p.Envelope(op=p.Op.HELLO_OK, req_id=0x1234,
                       body=p.Hello(protocol_version=p.PROTOCOL_VERSION,
                                    board_id="waveshare-amoled-206",
                                    build="sim 0.0.1", session=0xA5B6C7D8).encode())
    check(p.envelope_encode(hello) == MSG_HELLO_OK,
          "a whole HelloOk message is the literal the C++ suite asserts")

    event = p.Envelope(op=p.Op.INPUT_EVENT, req_id=0x9ABC,
                       body=p.input_event_encode(p.EventType(3), button=1, x=-2, y=300,
                                                 touch_id=7, at_ms=0x0000FFFE))
    check(p.envelope_encode(event) == MSG_INPUT_EVENT,
          "and so is a whole InputEvent, negative coordinate included")

    # No Python encoder for ScreenInfo -- the device sends it and the host reads
    # it -- so this direction pins the decode instead. Same literal, opposite
    # end, which is the point: the bytes are the contract, not either side.
    info_env = p.envelope_decode(MSG_SCREEN_INFO)
    check(info_env.op is p.Op.SCREEN_INFO and info_env.req_id == 0x5678,
          "the ScreenInfo literal decodes to the opcode and request id it was built with")
    info = p.ScreenInfo.decode(info_env.body)
    check((info.frame_id, info.width, info.height, info.total_bytes, info.crc32, info.at_ms)
          == (0x11223344, 410, 502, 0x00064B14, 0xCBF43926, 0x00BC614E),
          "and to every field, with none of them transposable")
    check(info.format is p.PixelFormat.RGB565_LE and info.orientation is p.Orientation.DEG90,
          "including the two single-byte enums, which sit next to each other")

    for name, value in OPCODE_VALUES.items():
        check(int(getattr(p.Op, name)) == value, f"Op.{name} is 0x{value:04X}")
    check(len(list(p.Op)) == len(OPCODE_VALUES),
          f"and there are exactly {len(OPCODE_VALUES)} opcodes -- a new one has to be pinned here too")
    for name, value in ERROR_VALUES.items():
        check(int(getattr(p.ErrorCode, name)) == value, f"ErrorCode.{name} is {value}")
    check(len(list(p.ErrorCode)) == len(ERROR_VALUES),
          f"and exactly {len(ERROR_VALUES)} error codes")


# --- framing ---------------------------------------------------------------

def framing_round_trip() -> None:
    for payload in (b"", b"x", b"a" * p.MAX_PAYLOAD, bytes(range(256))[:100]):
        decoder = p.FrameDecoder()
        decoder.push(p.frame_encode(payload))
        check(decoder.next() == payload, f"a {len(payload)}-byte payload survives framing")
    check_raises(ValueError, "an over-long payload is refused, not truncated",
                 lambda: p.frame_encode(b"a" * (p.MAX_PAYLOAD + 1)))


def framing_survives_any_fragmentation() -> None:
    stream = b"".join(p.frame_encode(bytes([i]) * (i + 1)) for i in range(5))
    for size in (1, 2, 3, 7, 64, len(stream)):
        decoder = p.FrameDecoder()
        got = []
        for at in range(0, len(stream), size):
            decoder.push(stream[at:at + size])
            got.extend(decoder)
        check(len(got) == 5, f"five frames arrive whatever the read size ({size})")
        check(got[3] == bytes([3]) * 4, f"and in order, at read size {size}")


def framing_rejects_a_corrupted_byte() -> None:
    good = p.frame_encode(b"the quick brown fox")
    for index in range(len(good)):
        broken = bytearray(good)
        broken[index] ^= 0x40
        decoder = p.FrameDecoder()
        decoder.push(bytes(broken))
        # It may resynchronise into nothing, or reject; what it must never do is
        # hand back the corrupted payload as if it were good.
        out = decoder.next()
        check(out != b"the quick brown fox" or index < 2,
              f"a bit flipped at byte {index} is not delivered as good data")


def framing_resynchronises_after_a_text_log() -> None:
    # The whole reason a log can share this stream. 0xF1 does not occur in ASCII,
    # so the noise costs resyncs and never a corrupted frame.
    noise = b"I (1234) main_task: Calling app_main()\n"
    decoder = p.FrameDecoder()
    decoder.push(noise + p.frame_encode(b"payload") + noise)
    check(decoder.next() == b"payload", "a frame after a log line is found intact")
    check(decoder.stats.resyncs == len(noise), "and the discarded bytes are counted")


def framing_rejects_an_impossible_length() -> None:
    broken = bytearray(p.frame_encode(b"hello"))
    broken[2] = 0xFF  # a length of 0x00FF, and the check byte no longer matches
    decoder = p.FrameDecoder()
    decoder.push(bytes(broken))
    check(decoder.next() is None, "a corrupted length is rejected at the header")
    check(decoder.stats.bad_length >= 1, "and counted")


def framing_never_loses_a_split_sync() -> None:
    frame = p.frame_encode(b"split")
    decoder = p.FrameDecoder()
    decoder.push(frame[:1])            # 0xF1 alone
    check(decoder.next() is None, "half a sync pattern yields nothing yet")
    decoder.push(frame[1:])
    check(decoder.next() == b"split", "and the frame arrives once the rest does")


# --- envelope --------------------------------------------------------------

def envelope_round_trip() -> None:
    envelope = p.Envelope(op=p.Op.SCREEN_REQUEST, req_id=0xBEEF, body=b"\x01\x02\x03")
    back = p.envelope_decode(p.envelope_encode(envelope))
    check(back.op is p.Op.SCREEN_REQUEST and back.req_id == 0xBEEF and back.body == b"\x01\x02\x03",
          "an envelope survives a round trip")
    check(len(p.envelope_encode(p.Envelope(op=p.Op.HELLO))) == p.ENVELOPE_BYTES,
          "an empty body encodes to exactly the envelope")


def envelope_rejects_corruption() -> None:
    good = p.envelope_encode(p.Envelope(op=p.Op.INPUT_EVENT, req_id=1, body=b"abcd"))
    for index in range(len(good)):
        broken = bytearray(good)
        broken[index] ^= 0x08
        check_raises(p.ProtocolError, f"a bit flipped at envelope byte {index} is rejected",
                     lambda b=bytes(broken): p.envelope_decode(b))


def envelope_rejects_a_length_disagreement() -> None:
    good = p.envelope_encode(p.Envelope(op=p.Op.HELLO, body=b"abc"))
    check_raises(p.ProtocolError, "one byte short is rejected",
                 lambda: p.envelope_decode(good[:-1]))
    check_raises(p.ProtocolError, "one byte long is rejected",
                 lambda: p.envelope_decode(good + b"\0"))
    check_raises(p.ProtocolError, "shorter than an envelope is rejected",
                 lambda: p.envelope_decode(b"\x01\x02"))
    check_raises(ValueError, "an over-long body is refused",
                 lambda: p.envelope_encode(p.Envelope(op=p.Op.HELLO, body=b"x" * (p.MAX_BODY + 1))))


def a_debug_message_fits_one_frame() -> None:
    biggest = p.envelope_encode(p.Envelope(op=p.Op.SCREEN_DATA, body=b"x" * p.MAX_BODY))
    check(len(biggest) == p.MAX_PAYLOAD,
          "a maximal message is exactly one payload -- which is why kMaxPayload is not raised")
    check(len(p.frame_encode(biggest)) == p.MAX_FRAME, "and exactly one frame")


def bodies_round_trip() -> None:
    hello = p.Hello(board_id="waveshare-amoled-206", build="sim 0.0.1")
    back = p.Hello.decode(hello.encode())
    check(back.board_id == "waveshare-amoled-206" and back.build == "sim 0.0.1",
          "a hello body survives a round trip")

    # A name that exactly fills its field is still terminated.
    long_name = p.Hello.decode(p.Hello(board_id="x" * 40).encode())
    check(len(long_name.board_id) == 23, "an over-long name is truncated, not overrun")

    event = p.input_event_encode(p.EventType.POINTER_MOVE, x=-12, y=1000, at_ms=123456)
    check(len(event) == p.INPUT_EVENT_BYTES, "an input event is a fixed size")

    sync = p.time_sync_encode(1787687654, 300, 86400000,
                              allow_large_correction=True)
    check(sync == bytes.fromhex("e6f28d6a000000002c01005c260501"),
          "TimeSync matches the fixed C++ byte vector")
    check(len(sync) == p.TIME_SYNC_BYTES, "a TimeSync body is a fixed size")


# --- pixels ----------------------------------------------------------------

def rgb888_and_bgr888() -> None:
    rgb = bytes([10, 20, 30, 40, 50, 60])
    check(p.to_rgb(rgb, 2, 1, p.PixelFormat.RGB888) == rgb, "RGB888 passes through")
    # BGR888 is what LVGL actually stores. Getting this wrong is the classic
    # "the screenshot looks almost right" failure.
    check(p.to_rgb(rgb, 2, 1, p.PixelFormat.BGR888) == bytes([30, 20, 10, 60, 50, 40]),
          "BGR888 has its red and blue swapped back")


def rgb565_expands_to_full_scale() -> None:
    # White in RGB565 is 0xFFFF. It must come out 255,255,255 -- not 248,252,248,
    # which is what a plain shift gives and what makes every white slightly grey.
    white_le = bytes([0xFF, 0xFF])
    check(p.to_rgb(white_le, 1, 1, p.PixelFormat.RGB565_LE) == bytes([255, 255, 255]),
          "full-scale RGB565 white expands to full-scale RGB888")
    check(p.to_rgb(bytes([0x00, 0x00]), 1, 1, p.PixelFormat.RGB565_LE) == bytes([0, 0, 0]),
          "and black stays black")

    # Pure red is 0xF800. Little-endian on the wire is 00 F8; big-endian F8 00.
    check(p.to_rgb(bytes([0x00, 0xF8]), 1, 1, p.PixelFormat.RGB565_LE) == bytes([255, 0, 0]),
          "RGB565 little-endian red")
    check(p.to_rgb(bytes([0xF8, 0x00]), 1, 1, p.PixelFormat.RGB565_BE) == bytes([255, 0, 0]),
          "RGB565 big-endian red -- the same colour, the other byte order")
    # The byte orders must disagree, or the test above proves nothing.
    check(p.to_rgb(bytes([0x00, 0xF8]), 1, 1, p.PixelFormat.RGB565_BE) != bytes([255, 0, 0]),
          "and reading one as the other does NOT give red")


def a_wrong_length_is_refused() -> None:
    check_raises(p.ProtocolError, "too few bytes for the stated size is refused",
                 lambda: p.to_rgb(b"\x00" * 5, 2, 1, p.PixelFormat.RGB888))
    check_raises(p.ProtocolError, "an unknown pixel format is refused, not guessed",
                 lambda: p.to_rgb(b"\x00", 1, 1, p.PixelFormat.UNKNOWN))


def orientation_turns_the_right_way() -> None:
    # A 2x1 image: red then green. Rotating it must move the pixels, and
    # rotating it back must restore them.
    red, green = bytes([255, 0, 0]), bytes([0, 255, 0])
    image = red + green

    same, w, h = p.apply_orientation(image, 2, 1, p.Orientation.DEG0)
    check(same == image and (w, h) == (2, 1), "no rotation changes nothing")

    flipped, w, h = p.apply_orientation(image, 2, 1, p.Orientation.DEG180)
    check(flipped == green + red and (w, h) == (2, 1), "180 degrees reverses a row")

    # DEG90 means "turn the received image a quarter turn clockwise". A row
    # [red][green] turned clockwise becomes a column with red on top.
    turned, w, h = p.apply_orientation(image, 2, 1, p.Orientation.DEG90)
    check((w, h) == (1, 2), "90 degrees swaps width and height")
    check(turned == red + green, "and clockwise puts the left pixel on top")

    other, w, h = p.apply_orientation(image, 2, 1, p.Orientation.DEG270)
    check((w, h) == (1, 2) and other == green + red,
          "270 degrees turns the other way -- which is how a mirrored or "
          "wrongly-signed driver is caught, since the two must differ")

    # 90 then 270 is the identity. If both were wrong the same way, this passes
    # -- which is why the fixed expectations above exist as well.
    once, w1, h1 = p.apply_orientation(image, 2, 1, p.Orientation.DEG90)
    twice, w2, h2 = p.apply_orientation(once, w1, h1, p.Orientation.DEG270)
    check(twice == image and (w2, h2) == (2, 1), "90 then 270 is the identity")


def png_is_a_real_png() -> None:
    width, height = 3, 2
    rgb = bytes(range(width * height * 3))
    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "shot.png")
        p.write_png(path, rgb, width, height)
        raw = Path(path).read_bytes()

    check(raw[:8] == b"\x89PNG\r\n\x1a\n", "the signature is a PNG signature")
    check(raw[12:16] == b"IHDR" and raw[-8:-4] == b"IEND", "the chunks are where they belong")

    # Decode it back without an image library: one IDAT, filter 0 on every row.
    at, idat = 8, b""
    while at < len(raw):
        length = int.from_bytes(raw[at:at + 4], "big")
        tag = raw[at + 4:at + 8]
        if tag == b"IDAT":
            idat += raw[at + 8:at + 8 + length]
        at += 12 + length
    decoded = zlib.decompress(idat)
    rows = [decoded[y * (width * 3 + 1):(y + 1) * (width * 3 + 1)] for y in range(height)]
    check(all(row[0] == 0 for row in rows), "every row uses filter 0")
    check(b"".join(row[1:] for row in rows) == rgb, "and the pixels come back unchanged")

    check_raises(ValueError, "a buffer that does not match the size is refused",
                 lambda: p.write_png("/dev/null", b"\0" * 5, 2, 1))


def a_screenshot_survives_the_whole_chain() -> None:
    """565 on the wire, rotated, to a PNG and back -- the path a device will use.

    The simulator is BGR888 and unrotated, so this is the only place the 565 and
    rotation legs are exercised together. Marked here rather than left to a
    device that does not exist yet.
    """
    width, height = 4, 3
    # Distinct per pixel, so a transposition that got the stride wrong shows up.
    wire = bytearray()
    for i in range(width * height):
        value = (((i * 2) & 0x1F) << 11) | (((i * 3) & 0x3F) << 5) | ((i * 5) & 0x1F)
        wire += bytes([value & 0xFF, value >> 8])

    rgb = p.to_rgb(bytes(wire), width, height, p.PixelFormat.RGB565_LE)
    check(len(rgb) == width * height * 3, "565 expands to three bytes a pixel")

    turned, w, h = p.apply_orientation(rgb, width, height, p.Orientation.DEG90)
    back, w2, h2 = p.apply_orientation(turned, w, h, p.Orientation.DEG270)
    check(back == rgb and (w2, h2) == (width, height), "and the rotation is reversible")

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "chain.png")
        p.write_png(path, turned, w, h)
        check(os.path.getsize(path) > 0, "a rotated frame writes a non-empty PNG")


# --- the command line ------------------------------------------------------

def the_tool_fails_loudly_with_no_device() -> None:
    """Exit code, and a message that says what to do.

    A tool that hangs when the device is absent is worse than one that fails:
    an agent waits for it, and a CI job times out with no diagnosis.
    """
    sys.path.insert(0, str(HERE.parent))
    import watch_control  # noqa: PLC0415

    stderr = io.StringIO()
    real_stderr, sys.stderr = sys.stderr, stderr
    try:
        code = watch_control.main(["--socket", "/nonexistent/attadipa.sock", "info"])
    finally:
        sys.stderr = real_stderr

    check(code != 0, "a missing device is a non-zero exit")
    check("could not connect" in stderr.getvalue(), "and the message names the cause")


def serial_disconnects_are_reported_without_tracebacks() -> None:
    from watch.client import SerialTransport, WatchError  # noqa: PLC0415

    class BrokenSerial:
        @property
        def in_waiting(self):
            raise OSError("unplugged")

        def write(self, _data):
            raise OSError("unplugged")

        def close(self):
            raise OSError("unplugged")

    transport = SerialTransport.__new__(SerialTransport)
    transport._serial = BrokenSerial()
    transport._port = "/dev/fake-watch"
    check_raises(WatchError, "a serial send disconnect is handled",
                 lambda: transport.send(b"request"))
    check_raises(WatchError, "a serial read disconnect is handled",
                 lambda: transport.recv(0.01))
    check_raises(WatchError, "a serial close disconnect is handled",
                 transport.close)


class _FakeScreen:
    """Just enough of `Watch` for `resolve_point` -- a geometry, no socket.

    Both boards are checked on the host this way, which is the point: the
    defect being pinned here is a coordinate that only ever fitted one panel,
    and a check that needs a running simulator would not have caught it in the
    job that matters.
    """

    def __init__(self, width: int, height: int) -> None:
        self._size = (width, height)

    def screen_size(self) -> tuple[int, int]:
        return self._size


def scenarios_load() -> None:
    from watch import scenario  # noqa: PLC0415
    from watch.client import WatchError  # noqa: PLC0415

    root = HERE.parent.parent
    tour = root / "tests" / "ui" / "scenarios" / "diagnostic_tour.yaml"
    check(tour.exists(), "the shipped scenario is where both documents say it is")
    if tour.exists():
        # No silent skip on a missing PyYAML. The old spelling matched
        # "PyYAML" in the message and dropped the failure, so the group printed
        # `ok` having parsed nothing -- and this is the *worse* place for that
        # than the one next door, because `e2e_test.py` runs only behind
        # ATTADIPA_BUILD_SIMULATOR while this runs unconditionally. On a host
        # job without PyYAML, nothing would read `diagnostic_tour.yaml` at all
        # and the count would not move. A skip that is worth taking says so in
        # the count; see e2e_test.py's own note on the same shape.
        try:
            steps = scenario.load(str(tour))
        except Exception as exc:  # noqa: BLE001
            failures.append(
                f"the shipped scenario failed to load: {exc}"
                + (" -- install PyYAML; this is a real gap in the run, not a"
                   " skip" if "PyYAML" in str(exc) else ""))
        else:
            check(len(steps) > 0, "the shipped scenario parses into steps")
            check(all("action" in step for step in steps), "and every step names an action")

    gesture = root / "tests" / "ui" / "gestures" / "example.json"
    check(gesture.exists(), "the shipped gesture file is where the documents say it is")
    if gesture.exists():
        # Both documents point at this file under a heading saying to run it on
        # both boards, and nothing loaded it: it was written in Waveshare pixels
        # and three of its five points are off a 240x240 panel, so it refused
        # before a byte went out. Resolved without a connection, which is what
        # makes the fractions provable on the host.
        for width, height, board in ((240, 240, "t-watch-s3-plus"),
                                     (410, 502, "waveshare-amoled-206")):
            points, duration = scenario.load_gesture(
                str(gesture), _FakeScreen(width, height))
            check(len(points) >= 2 and duration > 0,
                  f"the shipped gesture file parses into a path for the {board}")
            check(all(0 <= x < width and 0 <= y < height for x, y in points),
                  f"and every point lands inside the {board} panel")
        check_raises(
            WatchError, "and a fraction with no device to resolve it says so",
            lambda: scenario.load_gesture(str(gesture)))

    # The endpoints of the convention. `0.0 < value < 1.0` was exclusive, so
    # `1.0` and `"100%"` -- the two spellings that read as "the far edge" --
    # fell through to `int(1.0)` and landed on pixel 1. A full-span swipe
    # became a two-pixel twitch, and nothing downstream could tell: the screen
    # changed, so every check passed.
    screen = _FakeScreen(240, 320)
    check(scenario.resolve_point([1.0, 1.0], screen) == (239, 319),
          "1.0 is the far edge, not pixel 1")
    check(scenario.resolve_point("100%,100%", screen) == (239, 319),
          "and so is 100%")
    check(scenario.resolve_point([0.0, 0.0], screen) == (0, 0),
          "0.0 is the near edge")
    check(scenario.resolve_point([0.5, 0.5], screen) == (120, 160),
          "and a half is still a half")
    check(scenario.resolve_point([1, 1], screen) == (1, 1),
          "a whole number is a pixel, including 1")
    # The endpoint's neighbourhood, which the first fix left out. Everything
    # from `(span - 0.5) / span` up *rounds* onto `span`, one past the last
    # pixel, and `_check_point` then refused it as outside the screen -- so
    # `0.999` was an error on a 240-wide panel while `1.0` was fine.
    check(scenario.resolve_point([0.999, 0.999], screen) == (239, 319),
          "0.999 lands on the last pixel rather than one past it")
    check(scenario.resolve_point("99.9%,99.9%", screen) == (239, 319),
          "and so does 99.9%")
    check(scenario.resolve_point([0.9, 0.9], screen) == (216, 288),
          "a fraction well inside the span is untouched by that clamp")
    # Past the edge resolves out of bounds on purpose rather than clamping, so
    # the refusal names the coordinate instead of silently moving it.
    check(scenario.resolve_point("120%,0", screen)[0] == 288,
          "and a percentage past 100 resolves out of bounds rather than clamping")
    # A float is a fraction whatever its value. Bounding that to [0.0, 1.0]
    # sent everything outside down the pixel path, where `int()` truncated
    # `1.5` to pixel 1 and `-0.5` to pixel 0 -- both on the panel, so the tap
    # landed, the screen changed and every check downstream agreed.
    check(scenario.resolve_point([1.5, 0.0], screen)[0] == 360,
          "a fraction past 1.0 resolves out of bounds rather than truncating")
    check(scenario.resolve_point([-0.5, 0.0], screen)[0] < 0,
          "and a negative fraction stays negative rather than becoming pixel 0")
    check(scenario.resolve_point([1, 1], screen) == (1, 1),
          "while whole numbers are still pixels, which is what keeps the two apart")
    check_raises(WatchError, "a coordinate that is not a number says so",
                 lambda: scenario.resolve_point(["a", "b"], screen))

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "steps.json")
        Path(path).write_text('[{"action": "wait", "seconds": 0}]', encoding="utf-8")
        check(scenario.load(path) == [{"action": "wait", "seconds": 0}],
              "a JSON scenario loads without PyYAML")


def a_scenario_that_runs_nothing_is_not_a_pass() -> None:
    from watch import scenario  # noqa: PLC0415
    from watch.client import WatchError  # noqa: PLC0415

    with tempfile.TemporaryDirectory() as directory:
        empty = os.path.join(directory, "empty.json")
        Path(empty).write_text("[]", encoding="utf-8")
        check_raises(WatchError, "an empty scenario is refused, not passed",
                     lambda: scenario.load(empty))

        # The failure this actually guards: a mis-keyed document. `.get("steps",
        # [])` turned `actions:` into zero steps, and zero steps reported
        # success and exited 0.
        wrong = os.path.join(directory, "wrong.json")
        Path(wrong).write_text('{"actions": [{"action": "wait"}]}', encoding="utf-8")
        check_raises(WatchError, "a document with no 'steps' key is refused",
                     lambda: scenario.load(wrong))


def an_unknown_wire_value_is_reported_not_raised() -> None:
    # A device one build ahead sends a pixel format this checkout does not
    # know. That is a thing to report; `PixelFormat(fmt)` raised a bare
    # ValueError that escaped as a traceback.
    body = struct.pack("<HHBBBB", 240, 240, 99, 0, 1, 0)
    body += b"\0" * (p.BUTTON_BYTES * 4)
    body += struct.pack("<HIH", 182, 30000, 500)
    check_raises(p.ProtocolError, "an unknown pixel format is a typed error",
                 lambda: p.Capabilities.decode(body))


def every_error_code_has_a_human_sentence() -> None:
    # The two sides are mirrored by hand, so the way this drifts is a code
    # added to one and not the other -- and the symptom is a device refusal
    # printed as a bare enum name at the moment somebody needed a sentence.
    missing = [code.name for code in p.ErrorCode
               if code is not p.ErrorCode.NONE and code not in p.ERROR_TEXT]
    check(not missing, f"every ErrorCode has an ERROR_TEXT entry (missing: {missing})")
    check(p.ErrorCode.QUEUE_FULL in p.ERROR_TEXT,
          "including QUEUE_FULL, which is what a dropped input event now says")
    check(p.ERROR_TEXT[p.ErrorCode.QUEUE_FULL] != p.ERROR_TEXT[p.ErrorCode.BUSY],
          "and it does not say the same thing as BUSY, which was the bug")


# --- the host's own logic, without a device --------------------------------

class ScriptedDevice:
    """A device made of a callback. No socket, no simulator, no board.

    The two defects this file grew for both lived in the *host*: a poll that
    never polled, and a blacklist that outlived what it was protecting. The
    end-to-end test cannot reach either -- it needs a running simulator, so it
    cannot be a host test, and it exercises the happy path in any case. This is
    the missing rig: the wire is real, and what is behind it is a table.
    """

    def __init__(self, reply) -> None:
        self._reply = reply
        self._decoder = p.FrameDecoder()
        self._out = bytearray()
        self.asked: list[tuple] = []

    def send(self, data: bytes) -> None:
        self._decoder.push(data)
        for payload in self._decoder:
            envelope = p.envelope_decode(payload)
            self.asked.append((envelope.op, envelope.body))
            for out in self._reply(envelope):
                self._out += p.frame_encode(out)

    def recv(self, timeout: float) -> bytes:  # noqa: ARG002 - answers are already queued
        data = bytes(self._out)
        self._out.clear()
        return data

    def close(self) -> None:
        pass

    def describe(self) -> str:
        return "scripted"


def _reply_to(envelope, op: "p.Op", body: bytes = b"") -> bytes:
    return p.envelope_encode(p.Envelope(op=op, req_id=envelope.req_id, body=body))


def a_time_sync_is_validated_and_not_retried() -> None:
    from watch.client import Watch, WatchError  # noqa: PLC0415

    device = ScriptedDevice(
        lambda e: [_reply_to(e, p.Op.TIME_SYNC_OK)] if e.op is p.Op.TIME_SYNC else [])
    watch = Watch(device, timeout=1.0)
    watch.sync_time(1787687654, 300, 86400, allow_large_correction=True)
    check(device.asked == [(p.Op.TIME_SYNC,
                            bytes.fromhex("e6f28d6a000000002c01005c260501"))],
          "one synchronization writes exactly one fixed body")

    silent_device = ScriptedDevice(lambda e: [])
    silent = Watch(silent_device, timeout=0.0)
    check_raises(WatchError, "a lost time-sync acknowledgement is reported",
                 lambda: silent.sync_time(1787687654, 300, 86400))
    check(len(silent_device.asked) == 1,
          "and the non-idempotent RTC write is not retried")
    check_raises(WatchError, "an impossible timezone offset is refused before the wire",
                 lambda: watch.sync_time(1787687654, 900, 86400))


def mesh_commands_are_validated_and_a_send_is_not_retried() -> None:
    from watch.client import Watch, WatchError  # noqa: PLC0415

    device = ScriptedDevice(lambda e: [_reply_to(e, p.Op.MESH_OK)])
    watch = Watch(device, timeout=1.0)
    watch.mesh_configure(123456)
    watch.mesh_disconnect()
    watch.mesh_forget_bond()
    watch.mesh_send(bytes.fromhex("010203040506"), "Hello", 1234567890)
    watch.mesh_room_send(bytes.fromhex("00" * 32), "pass", "Hello", 1234567890)
    check(device.asked == [
        (p.Op.MESH_CONFIGURE, bytes.fromhex("40e20100")),
        (p.Op.MESH_DISCONNECT, b""),
        (p.Op.MESH_FORGET_BOND, b""),
        (p.Op.MESH_SEND,
         bytes.fromhex("010203040506d202964900000000") + b"Hello"),
        (p.Op.MESH_ROOM_SEND,
         bytes.fromhex("00" * 32) + b"\x04pass" +
         bytes.fromhex("d202964900000000") + b"Hello"),
    ], "MeshCore commands use the fixed bounded wire bodies")

    silent_device = ScriptedDevice(lambda e: [])
    silent = Watch(silent_device, timeout=0.0)
    check_raises(WatchError, "a lost send acknowledgement is reported",
                 lambda: silent.mesh_send(bytes.fromhex("010203040506"),
                                          "Hello", 1234567890))
    check(len(silent_device.asked) == 1,
          "and the non-idempotent MeshCore send is not retried")
    check_raises(WatchError, "a short peer prefix is refused before the wire",
                 lambda: watch.mesh_send(b"short", "Hello", 1234567890))
    check_raises(WatchError, "a Room Server password over 15 bytes is refused before the wire",
                 lambda: watch.mesh_room_send(bytes(32), "0123456789abcdef",
                                               "Hello", 1234567890))


def a_stability_wait_actually_waits() -> None:
    from watch.client import Watch, WatchError  # noqa: PLC0415

    # Not settled for the first three asks, settled on the fourth.
    state = {"asks": 0}

    def device(envelope):
        if envelope.op is not p.Op.WAIT_STABLE:
            return []
        state["asks"] += 1
        return [_reply_to(envelope, p.Op.STABLE_OK,
                          b"\x01" if state["asks"] >= 4 else b"\x00")]

    watch = Watch(ScriptedDevice(device), timeout=2.0)
    check(watch.wait_stable(250, timeout=2.0, poll=0.0),
          "a wait that eventually settles returns True")

    # The old implementation sent one request and returned whatever came back,
    # which is why it could not have waited for anything.
    check(state["asks"] == 4, f"and it asked until it did ({state['asks']} times)")

    # The duration reaches the device. It used to be an empty body, so the
    # device answered `stable_since(now_ms)` -- a question about uptime.
    op, body = watch._transport.asked[0]  # noqa: SLF001 - the point of the rig
    check(op is p.Op.WAIT_STABLE and body == struct.pack("<H", 250),
          f"and 250 ms travelled as {body!r}")


def a_stability_wait_that_never_settles_says_so() -> None:
    from watch.client import Watch, WatchError  # noqa: PLC0415

    never = Watch(ScriptedDevice(
        lambda e: [_reply_to(e, p.Op.STABLE_OK, b"\x00")]
        if e.op is p.Op.WAIT_STABLE else []), timeout=1.0)

    # False, not an exception and not a silent True. `scenario.py` turns this
    # into a failed step; a step that cannot fail is not a step.
    check(never.wait_stable(100, timeout=0.15, poll=0.0) is False,
          "an interface that never goes quiet reports False rather than passing")

    check_raises(WatchError, "a quiet period too large for the wire is refused",
                 lambda: never.wait_stable(70000))


def a_finished_screenshot_blacklists_nothing() -> None:
    from watch.client import Watch  # noqa: PLC0415

    width, height = 4, 3
    image = bytes((i * 37 + 11) & 0xFF for i in range(width * height * 3))

    def device(envelope):
        if envelope.op is p.Op.HELLO:
            return []
        if envelope.op is not p.Op.SCREEN_REQUEST:
            return []
        info = struct.pack("<IHHBBIII", 1, width, height,
                           int(p.PixelFormat.RGB888), int(p.Orientation.DEG0),
                           len(image), p.crc32_of(image), 0)
        out = [_reply_to(envelope, p.Op.SCREEN_INFO, info)]
        step = 16
        for offset in range(0, len(image), step):
            out.append(_reply_to(envelope, p.Op.SCREEN_DATA,
                                 struct.pack("<I", offset) + image[offset:offset + step]))
        out.append(_reply_to(envelope, p.Op.SCREEN_END))
        return out

    watch = Watch(ScriptedDevice(device), timeout=2.0)
    watch.capabilities = p.Capabilities(width=width, height=height,
                                        format=p.PixelFormat.RGB888)
    shot = watch.screenshot()
    check(shot.rgb == image, "a scripted frame reassembles to the bytes that were sent")

    # A transfer that finished consumed its own chunks: there is nothing left in
    # flight for the blacklist to catch. Blacklisting anyway grew the set on
    # every screenshot and made every later envelope pay for a membership test
    # that could never match.
    check(watch._abandoned == {},  # noqa: SLF001 - the point of the rig
          "and it leaves no req_id blacklisted")


def a_frame_that_never_finishes_gives_up_at_the_deadline() -> None:
    from watch.client import Watch, WatchError  # noqa: PLC0415

    width, height = 16, 16
    total = width * height * 3

    class Trickle(ScriptedDevice):
        """Answers every wait, and never finishes the frame.

        The deadline `screenshot()` computes was passed to each `_await` and
        read by nothing else, so a device like this one kept the collect loop
        alive indefinitely: every individual wait was answered inside its own
        tenth of a second, and the transfer as a whole had no bound at all.
        """

        def __init__(self) -> None:
            super().__init__(self._answer)
            self.chunks = 0
            self._request = None

        def _answer(self, envelope):
            if envelope.op is not p.Op.SCREEN_REQUEST:
                return []
            self._request = envelope
            info = struct.pack("<IHHBBIII", 1, width, height,
                               int(p.PixelFormat.RGB888), int(p.Orientation.DEG0),
                               total, 0, 0)
            return [_reply_to(envelope, p.Op.SCREEN_INFO, info)]

        def recv(self, timeout: float) -> bytes:
            queued = super().recv(timeout)
            if queued or self._request is None:
                return queued
            # Bounded, so a regression fails slowly instead of hanging for
            # ever. Always offset 0: the same sixteen bytes, so `seen` never
            # fills and SCREEN_END never comes.
            if self.chunks >= 400:
                return b""
            self.chunks += 1
            time.sleep(timeout)
            return p.frame_encode(_reply_to(
                self._request, p.Op.SCREEN_DATA, struct.pack("<I", 0) + b"\0" * 16))

    device = Trickle()
    watch = Watch(device, timeout=5.0)
    watch.capabilities = p.Capabilities(width=width, height=height,
                                        format=p.PixelFormat.RGB888)

    started = time.monotonic()
    check_raises(WatchError, "a frame that never finishes is refused rather than awaited",
                 lambda: watch.screenshot(0.4))
    elapsed = time.monotonic() - started

    # The refusal is the easy half. This is the finding: it must arrive at the
    # deadline it was given, not after the device stops talking. Without the
    # check in `_collect_frame` this takes the 400 chunks plus a five-second
    # `_await` timeout on top.
    check(elapsed < 3.0,
          f"and it gives up at its own deadline ({elapsed:.2f}s for a 0.4s budget, "
          f"{device.chunks} chunk(s) consumed)")


def an_abandoned_transfer_evicts_the_oldest_id() -> None:
    from watch.client import Watch  # noqa: PLC0415

    watch = Watch(ScriptedDevice(lambda e: []), timeout=0.1)
    for req_id in range(1, 100):
        watch._abandon(req_id)  # noqa: SLF001 - the point of the rig

    kept = list(watch._abandoned)  # noqa: SLF001
    check(len(kept) <= 64, f"the blacklist stays bounded ({len(kept)})")

    # The comment on the eviction argues from age -- "an id that old cannot
    # still be in flight". A `set` has no order to deliver that, so slicing one
    # could evict the id abandoned a microsecond ago and keep one from an hour
    # back, which is the opposite of what was claimed.
    check(kept == sorted(kept) and kept[-1] == 99,
          "and what it keeps is the newest, which is what the eviction claims")


class FakeClock:
    """The monotonic clock and `sleep`, made of arithmetic.

    Patched over `watch.client.time`, so the schedule an input path produces
    is decided by the code rather than by how busy the machine was, and a test
    that asserts six hundred milliseconds costs none of them. Everything the
    module reads from `time` and this does not define falls through to the
    real one.

    `sleep` refuses a negative interval instead of raising `ValueError` from
    inside the library, because the two are the same bug seen from different
    ends and only one of them says which call site did it.
    """

    def __init__(self, start: float = 0.0) -> None:
        self.now = float(start)
        self.slept: list[float] = []
        self._reads = 0

    def monotonic(self) -> float:
        # A clock that only moves when someone sleeps cannot run a timeout
        # out, so a wait for a reply that never comes would spin here for
        # ever and take the CI job's whole budget with it. Bounded on purpose,
        # the way `Trickle` bounds its chunks: a regression fails, slowly and
        # legibly, rather than hanging.
        self._reads += 1
        if self._reads > 100_000:
            raise AssertionError(
                "the fake clock was read 100000 times with no sleep between -- "
                "something is waiting on a reply the scripted device never sends")
        return self.now

    def sleep(self, seconds: float) -> None:
        if seconds < 0:
            raise AssertionError(f"asked to sleep for {seconds!r} seconds")
        self.slept.append(seconds)
        self.now += seconds
        self._reads = 0

    def advance(self, seconds: float) -> None:
        """Time passing that is *not* a wait -- a round trip, work, a scheduler.

        Kept apart from `sleep` because the difference is the whole subject:
        a schedule built on absolute deadlines spends this inside the interval
        it happened in, and one built on `sleep(gap)` adds it to the path.
        A clock that cannot tell them apart cannot test either.
        """
        self.now += seconds
        self._reads = 0

    def __getattr__(self, name):
        return getattr(time, name)


class InputLog(ScriptedDevice):
    """Every injected event, stamped with the clock it arrived on.

    The device side of a timing test: what matters is not only which events
    were sent and in which order, but *when* -- and the wire carries no
    timestamp of its own, so it is taken here, on arrival, from the same clock
    the sender is sleeping against.

    `cost` is what one round trip takes. At the default of zero it takes none,
    which is fine for counting intervals and wrong for testing the mechanism
    that produces them -- with a free wire, absolute deadlines and a plain
    `sleep(gap)` in a loop generate identical schedules. Give the wire a price
    and the two come apart.
    """

    def __init__(self, clock: FakeClock, cost: float = 0.0) -> None:
        super().__init__(self._answer)
        self._clock = clock
        self._cost = cost
        self.events: list[tuple] = []

    def _answer(self, envelope):
        if envelope.op is not p.Op.INPUT_EVENT:
            return []
        self._clock.advance(self._cost)
        # Unpacked against `input_event_encode`'s documented layout rather
        # than through a decoder that shares its mistakes -- the same argument
        # the fixed vectors at the top of this file are here for.
        kind, _button, x, y, _touch, _at = struct.unpack("<BBhhBI", envelope.body)
        self.events.append((self._clock.now, p.EventType(kind), x, y))
        return [_reply_to(envelope, p.Op.INPUT_OK, b"\0\0")]


def _gesture_schedule(points, duration, size=(240, 240), cost=0.0, max_hold_ms=0):
    """Run one gesture on a fake clock. Returns `(stamped events, the clock)`."""
    from watch import client as client_module  # noqa: PLC0415
    from watch.client import Watch  # noqa: PLC0415

    clock = FakeClock()
    device = InputLog(clock, cost=cost)
    watch = Watch(device, timeout=1.0)
    watch.capabilities = p.Capabilities(width=size[0], height=size[1],
                                        format=p.PixelFormat.RGB888,
                                        max_hold_ms=max_hold_ms)

    real, client_module.time = client_module.time, clock
    try:
        watch.gesture(points, duration=duration)
    finally:
        client_module.time = real
    return device.events, clock


def _about(value: float, expected: float, tolerance: float = 1e-6) -> bool:
    return abs(value - expected) <= tolerance


def a_gesture_takes_the_time_it_was_given() -> None:
    """`duration` is the whole path, `PointerDown` to `PointerUp`.

    Three things were wrong at once and only the first is visible in a long
    path. The sleep was attached to the intermediate points, so an `N`-point
    gesture waited `N - 2` times instead of `N - 1`; it came *after* each
    point was sent, so the first segment had no length; and a two-point
    gesture -- which has no intermediate points at all -- ignored `duration`
    completely and delivered `Down` and `Up` back to back, however slow it was
    asked to be. A recogniser reads speed, so that is a swipe reported as a
    flick while the run says it asked for neither.
    """
    # Two points: the case with nothing in the middle, which is the one the
    # old shape could not express at all.
    events, _ = _gesture_schedule([(10, 10), (60, 80)], 0.6)
    kinds = [kind for _, kind, _, _ in events]
    check(kinds == [p.EventType.POINTER_DOWN, p.EventType.POINTER_UP],
          f"a two-point gesture is a down and an up ({[k.name for k in kinds]})")
    if len(events) == 2:
        check(_about(events[0][0], 0.0), "the down is at the start")
        check(_about(events[1][0], 0.6),
              f"and the up is a whole 0.6s later, not immediately ({events[1][0]:.4f}s)")
        check((events[0][2], events[0][3]) == (10, 10)
              and (events[1][2], events[1][3]) == (60, 80),
              "with the coordinates it was given")

    # Five points, 0.6s: four equal intervals of 0.15, the last of them before
    # the `PointerUp`. This is the shipped file's shape, and it used to take
    # 0.45s with a zero-length first segment.
    path = [(10, 10), (10, 40), (10, 70), (40, 90), (80, 90)]
    events, _ = _gesture_schedule(path, 0.6)
    kinds = [kind for _, kind, _, _ in events]
    check(kinds == [p.EventType.POINTER_DOWN] + [p.EventType.POINTER_MOVE] * 3
          + [p.EventType.POINTER_UP],
          f"a five-point gesture is down, three moves and an up ({[k.name for k in kinds]})")
    check([(x, y) for _, _, x, y in events] == path,
          "in the order and at the coordinates it was given")
    stamps = [stamp for stamp, _, _, _ in events]
    gaps = [round(b - a, 6) for a, b in zip(stamps, stamps[1:])]
    check(all(_about(gap, 0.15) for gap in gaps),
          f"four equal intervals of 0.15s, the last one before the up ({gaps})")
    check(_about(stamps[-1], 0.6),
          f"and the path as a whole takes the 0.6s asked for ({stamps[-1]:.4f}s)")

    # A path that does not divide evenly still lands on its duration: the
    # deadlines are absolute, so rounding cannot accumulate along a long path.
    long_path = [(i, i) for i in range(0, 70, 7)]
    stamps = [stamp for stamp, _, _, _ in _gesture_schedule(long_path, 1.0)[0]]
    check(len(stamps) == len(long_path) and _about(stamps[-1], 1.0),
          f"a ten-point second-long path ends at 1.0s ({stamps[-1]:.6f}s)")

    # Zero is a real request -- the shape without a claim about its speed --
    # and it must not become a refusal or a wait.
    stamps = [stamp for stamp, _, _, _ in _gesture_schedule(path, 0.0)[0]]
    check(len(stamps) == 5 and all(_about(stamp, 0.0) for stamp in stamps),
          f"a zero duration sends the whole path at once ({stamps})")


def a_gesture_absorbs_its_round_trips_rather_than_adding_them() -> None:
    """Absolute deadlines, and this is the only group that can tell.

    Every check above runs against a device that answers in no time at all,
    and on a free wire `_sleep_until(started + gap*i)` and a plain
    `time.sleep(gap)` in a loop produce byte-identical schedules. So the
    interval *count* was pinned and the mechanism introduced to fix it was
    not: reverting to relative sleeps left the whole suite green. Found in
    review of #187, which is exactly the kind of thing a second reader is for.

    Give the round trip a price and the two come apart. Absolute deadlines
    spend it inside the interval it happened in, so an `N`-point path still
    spans `duration`; summing sleeps adds one round trip per point, and the
    longer the path the further out it is -- which is the claim the docstring
    and WATCH_CONTROL.md both make.
    """
    cost = 0.02
    path = [(10, 10), (10, 40), (10, 70), (40, 90), (80, 90)]
    events, clock = _gesture_schedule(path, 0.6, cost=cost)
    stamps = [stamp for stamp, _, _, _ in events]
    gaps = [round(b - a, 6) for a, b in zip(stamps, stamps[1:])]
    check(all(_about(gap, 0.15) for gap in gaps),
          f"a 20ms round trip comes out of its interval rather than onto it "
          f"({gaps} -- relative sleeps would give 0.17)")
    span = stamps[-1] - stamps[0] if stamps else float("nan")
    check(_about(span, 0.6),
          f"so the path still spans the 0.6s asked for ({span:.4f}s -- summing "
          f"sleeps would make it {0.6 + 4 * cost:.2f}s)")
    check(all(_about(slept, 0.15 - cost) for slept in clock.slept),
          f"and every wait is short by exactly what the wire took ({clock.slept})")

    # A wire slower than the interval it is being asked to keep. Every point is
    # already due on arrival, so the path goes out as fast as the connection
    # manages -- and nothing sleeps backwards to make the time up, which
    # `FakeClock.sleep` would refuse anyway. Unreachable over a Unix socket
    # and ordinary on `SerialTransport` at T-114, which is why it is tested
    # here rather than filed as something a board would have to show.
    events, clock = _gesture_schedule(path, 0.05, cost=cost)
    stamps = [stamp for stamp, _, _, _ in events]
    check(clock.slept == [],
          f"a round trip longer than the interval waits for nothing ({clock.slept})")
    span = stamps[-1] - stamps[0] if stamps else float("nan")
    check(len(stamps) == 5 and _about(span, 4 * cost),
          f"and the path takes what the wire took, not what was asked "
          f"({span:.4f}s for a 0.05s request)")


def a_gesture_longer_than_the_device_will_hold_is_refused() -> None:
    """The bound is the device's, and it became reachable with this fix.

    The bridge releases anything held past `max_hold_ms` -- 30 s on the
    simulator -- and pushes its own `PointerUp` when it does. While a gesture
    held the pointer for `duration * (N-2)/(N-1)`, and for *nothing at all*
    over two points, that could not bite. Now the pointer stays down for the
    whole path, so a long one gets an expiry nobody asked for, a click on
    whatever is underneath, and then its real release refused as "impossible
    from the current state" -- a message about the wrong subsystem, arriving
    after the damage. `button_hold` has refused in a sentence for exactly this
    reason since it was written; this now does too. Found in review of #187.

    Read from the capabilities rather than hardcoded, because the number is
    the device's to choose: a T-114 firmware with a tighter bound than the
    simulator's turns an ordinary one-second gesture into this case.
    """
    from watch.client import WatchError  # noqa: PLC0415

    # 2 s, which a real board might well pick. Refused, and the sentence says
    # what the device does rather than naming an internal state.
    try:
        _gesture_schedule([(10, 10), (60, 80)], 3.0, max_hold_ms=2000)
        check(False, "a gesture longer than the device will hold was accepted")
    except WatchError as exc:
        check("releases anything held longer than 2000 ms" in str(exc),
              f"a 3s gesture against a 2s hold limit is refused: {exc}")
        check("cut short" in str(exc),
              "and the sentence says what would have happened, not which flag was false")

    # The wire stays clean, like every other refusal here: nothing is held,
    # because nothing was ever sent.
    from watch import client as client_module  # noqa: PLC0415
    from watch.client import Watch  # noqa: PLC0415
    clock = FakeClock()
    device = InputLog(clock)
    watch = Watch(device, timeout=1.0)
    watch.capabilities = p.Capabilities(width=240, height=240,
                                        format=p.PixelFormat.RGB888, max_hold_ms=2000)
    real, client_module.time = client_module.time, clock
    try:
        check_raises(WatchError, "and it is refused before anything is on the wire",
                     lambda: watch.gesture([(10, 10), (60, 80)], duration=3.0))
    finally:
        client_module.time = real
    check(device.events == [], f"leaving nothing held ({len(device.events)} event(s) sent)")

    # At the bound and under it, nothing changes. `max_hold_ms = 0` is the
    # host declining to enforce a bound it was not given: a check that fired
    # on it would refuse every gesture against a device that had not answered
    # the question. It is NOT the device promising an unbounded hold -- the
    # bridge reads 0 as expire-immediately (bridge.cpp:597) -- so the
    # assertion below is about this tool and says so.
    events, _ = _gesture_schedule([(10, 10), (60, 80)], 2.0, max_hold_ms=2000)
    check(len(events) == 2, "a gesture exactly at the bound still runs")
    events, _ = _gesture_schedule([(10, 10), (60, 80)], 90.0, max_hold_ms=0)
    check(len(events) == 2, "and a device that gave no bound is not given an invented one")


def every_pointer_hold_verb_is_bounded_before_pointer_down() -> None:
    """One device limit covers every host verb that keeps a pointer down."""
    from watch import client as client_module  # noqa: PLC0415
    from watch.client import Watch, WatchError  # noqa: PLC0415

    calls = (
        ("long tap", lambda watch: watch.long_tap(10, 10, duration=2.04)),
        ("swipe", lambda watch: watch.swipe((10, 10), (60, 80), duration=2.04, steps=2)),
        ("drag", lambda watch: watch.drag((10, 10), (60, 80), duration=2.04, steps=2)),
        ("gesture", lambda watch: watch.gesture([(10, 10), (60, 80)], duration=2.04)),
    )
    for name, call in calls:
        clock = FakeClock()
        device = InputLog(clock)
        watch = Watch(device, timeout=1.0)
        watch.capabilities = p.Capabilities(width=240, height=240,
                                            format=p.PixelFormat.RGB888,
                                            max_hold_ms=2000)
        refused = False
        real, client_module.time = client_module.time, clock
        try:
            call(watch)
        except WatchError as exc:
            refused = True
            check("requested 2040 ms" in str(exc),
                  f"{name} reports the unrounded request in milliseconds: {exc}")
        finally:
            client_module.time = real
        check(refused, f"{name} over the device hold limit is refused")
        check(device.events == [],
              f"and {name} is refused before pointer down ({len(device.events)} event(s))")


def a_gesture_that_cannot_be_timed_is_refused_before_the_finger_lands() -> None:
    """And refused *before* the `PointerDown`, which is the half that matters.

    `time.sleep` rejects a negative interval with a `ValueError` from inside
    the standard library -- after the finger is down, naming neither the
    gesture nor the caller. `sleep(nan)` does not reject it at all: it returns
    immediately, so a gesture asked to take an unreadable length of time takes
    none and the run reports success. `inf` blocks for ever with an input
    held. All three are the same mistake and all three now stop before
    anything is on the wire.
    """
    from watch import client as client_module  # noqa: PLC0415
    from watch.client import Watch, WatchError  # noqa: PLC0415

    for duration, name in ((-1.0, "a negative duration"),
                           (float("nan"), "a NaN duration"),
                           (float("inf"), "an infinite duration"),
                           ("later", "a duration that is not a number")):
        clock = FakeClock()
        device = InputLog(clock)
        watch = Watch(device, timeout=1.0)
        watch.capabilities = p.Capabilities(width=240, height=240,
                                            format=p.PixelFormat.RGB888)
        real, client_module.time = client_module.time, clock
        try:
            check_raises(WatchError, f"{name} is refused",
                         lambda: watch.gesture([(10, 10), (60, 80)], duration=duration))  # noqa: B023
        finally:
            client_module.time = real
        check(device.events == [],
              f"and {name} left nothing on the wire, so no input is held "
              f"({len(device.events)} event(s) sent)")

    # Still two points minimum, and that refusal also predates the wire.
    watch = Watch(InputLog(FakeClock()), timeout=1.0)
    watch.capabilities = p.Capabilities(width=240, height=240, format=p.PixelFormat.RGB888)
    check_raises(WatchError, "a one-point gesture is still refused",
                 lambda: watch.gesture([(10, 10)], duration=0.5))


def the_shipped_gesture_keeps_its_timing_on_both_panels() -> None:
    """The file both documents point at, timed on the host, at both geometries.

    `scenarios_load` proves it *resolves* on a 240x240 and on a 410x502 panel.
    That was the whole check, and it is a check on the coordinates: the file
    carries a `duration` as well, and nothing read it. Run here rather than
    only in the simulator job, because the end-to-end test is gated behind
    ATTADIPA_BUILD_SIMULATOR and this defect is entirely in the host.
    """
    from watch import scenario  # noqa: PLC0415

    root = HERE.parent.parent
    gesture = root / "tests" / "ui" / "gestures" / "example.json"
    if not gesture.exists():
        check(False, "the shipped gesture file is missing")
        return

    for width, height, board in ((240, 240, "t-watch-s3-plus"),
                                 (410, 502, "waveshare-amoled-206")):
        points, duration = scenario.load_gesture(str(gesture), _FakeScreen(width, height))
        events, _ = _gesture_schedule(points, duration, size=(width, height))
        if len(events) != len(points):
            check(False, f"the shipped gesture sent {len(events)} events for "
                         f"{len(points)} points on the {board}")
            continue
        stamps = [stamp for stamp, _, _, _ in events]
        gaps = [round(b - a, 6) for a, b in zip(stamps, stamps[1:])]
        expected = duration / (len(points) - 1)
        check(all(_about(gap, expected) for gap in gaps),
              f"the shipped gesture keeps {len(points) - 1} equal intervals "
              f"of {expected:.3f}s on the {board} ({gaps})")
        check(_about(stamps[-1], duration),
              f"and finishes at the {duration}s it declares on the {board} "
              f"({stamps[-1]:.4f}s)")
        check(events[-1][1] is p.EventType.POINTER_UP,
              f"ending on the up, on the {board}")


def an_error_code_this_build_does_not_know_is_still_a_sentence() -> None:
    from watch.client import Watch  # noqa: PLC0415

    # `ErrorCode` is documented appended, never renumbered, so a device one
    # version ahead sends a value this checkout has no name for. The old code
    # called `p.ErrorCode(raw)` on it, and the resulting bare `ValueError` is
    # neither `WatchError` nor `ProtocolError`: it escaped `main()`'s handler
    # and `scenario.py`'s, so `cmd_run` never reached its cleanup `input_reset`
    # and the scenario left a finger down until the 30-second expiry.
    #
    # The value is computed rather than written, because the first spelling of
    # this test hard-coded 11 and stopped testing anything the day 11 became
    # `CaptureFailed`. It failed loudly, which is the good outcome; a test that
    # asserts a property of "unknown" must derive the unknown.
    unknown = max(int(code) for code in p.ErrorCode) + 1
    ahead = Watch(ScriptedDevice(
        lambda e: [_reply_to(e, p.Op.ERROR, struct.pack("<H", unknown))]), timeout=1.0)
    try:
        ahead.request(p.Op.HELLO, b"", (p.Op.HELLO_OK,))
        check(False, "an unknown error code was not reported at all")
    except p.ProtocolError as exc:
        check(str(unknown) in str(exc), f"the unknown code is named: {exc}")
        check("newer build" in str(exc), "and the reader is told what to suspect")
    except Exception as exc:  # noqa: BLE001
        check(False, f"an unknown error code raised {type(exc).__name__}: {exc}")

    # A known one still reads as a sentence rather than an enum name.
    known = Watch(ScriptedDevice(
        lambda e: [_reply_to(e, p.Op.ERROR,
                             struct.pack("<H", int(p.ErrorCode.BAD_INPUT)))]), timeout=1.0)
    try:
        known.request(p.Op.HELLO, b"", (p.Op.HELLO_OK,))
        check(False, "a known error code was not reported at all")
    except p.ProtocolError as exc:
        check("impossible from the current state" in str(exc),
              f"a known code keeps its human sentence: {exc}")

    # An ERROR with no code at all is the third case, and it used to become
    # `ErrorCode.NONE` -- a code the device never sent.
    silent = Watch(ScriptedDevice(
        lambda e: [_reply_to(e, p.Op.ERROR, b"")]), timeout=1.0)
    try:
        silent.request(p.Op.HELLO, b"", (p.Op.HELLO_OK,))
        check(False, "an empty error body was not reported at all")
    except p.ProtocolError as exc:
        check("said nothing about why" in str(exc),
              f"an error with no code says so: {exc}")


def an_id_is_never_issued_twice_on_one_connection() -> None:
    """The whole id space, through the allocator that ships.

    The wrap is the thing under test, so a rig that sets the counter near it
    would be testing the rig. 65535 pure-Python allocations cost a few
    milliseconds and touch no wire at all.
    """
    from watch.client import Watch, WatchIdsExhausted  # noqa: PLC0415

    watch = Watch(ScriptedDevice(lambda envelope: []), timeout=0.01)
    issued: list[int] = []
    while len(issued) <= 0x10000:
        try:
            issued.append(watch._allocate_req_id())  # noqa: SLF001 - the point of the test
        except WatchIdsExhausted as exc:
            check("Reconnect" in str(exc), f"the refusal says what to do: {exc}")
            break
    else:
        check(False, "the allocator never ran out")
        return

    check(len(issued) == len(set(issued)), "no request id is issued twice")
    check(len(issued) == 0xFFFF, f"the space is 1..0xffff, not {len(issued)} ids")
    check(0 not in issued, "0 stays reserved for an unsolicited message")
    check(issued[0] == 1 and issued[-1] == 0xFFFF, "and it is spent end to end")
    check_raises(WatchIdsExhausted, "a connection that ran out stays out",
                 watch._allocate_req_id)  # noqa: SLF001


def a_connection_out_of_ids_refuses_rather_than_reusing_one() -> None:
    """Fail closed, before the duplicate goes out -- not after it is answered.

    Both old failures needed the reuse to reach the wire: a stale reply in
    `_pending` was matched by `_await` before `_pump` ever read the fresh one,
    and a reused abandoned id had its `SCREEN_DATA` dropped as the tail of the
    transfer that was given up. Refusing at the allocator is upstream of both.
    """
    from watch.client import Watch, WatchError, WatchIdsExhausted  # noqa: PLC0415

    # Its own type, not a bare `WatchError`: `live` mode has to tell this one
    # failure from every other, because every command after it raises the same
    # thing and a loop that cannot tell prints the sentence for ever.
    check(issubclass(WatchIdsExhausted, WatchError),
          "the exhaustion error is still a WatchError for callers that catch one")

    def answer(envelope):
        return [_reply_to(envelope, p.Op.HELLO_OK, b"")]

    device = ScriptedDevice(answer)
    watch = Watch(device, timeout=0.5)
    watch._next_req_id = 0xFFFF  # noqa: SLF001 - one id left in the space

    reply = watch.request(p.Op.HELLO, b"", (p.Op.HELLO_OK,))
    check(reply.req_id == 0xFFFF, "the last id in the space is used like any other")

    sent = len(device.asked)
    check_raises(WatchIdsExhausted, "the request past the end of the space is refused",
                 lambda: watch.request(p.Op.HELLO, b"", (p.Op.HELLO_OK,)))
    check(len(device.asked) == sent,
          "and refused before a duplicate id reaches the wire")
    check_raises(WatchIdsExhausted, "a screenshot past the end is refused too",
                 watch.screenshot)
    check(len(device.asked) == sent, "also without sending anything")

    # The blacklist is pruned by how many other transfers were abandoned
    # since, never by time, so a single given-up screenshot outlived the whole
    # 16-bit cycle and ate the chunks of the next transfer to land on its
    # number. There is no next transfer on that number now.
    watch = Watch(ScriptedDevice(answer), timeout=0.5)
    watch._next_req_id = 0xFFFE  # noqa: SLF001
    given_up = watch._allocate_req_id()  # noqa: SLF001
    watch._abandon(given_up)  # noqa: SLF001
    reply = watch.request(p.Op.HELLO, b"", (p.Op.HELLO_OK,))
    check(reply.req_id != given_up and reply.req_id not in watch._abandoned,  # noqa: SLF001
          "a later request never lands on a blacklisted id")


def _capabilities_body(width: int = 4, height: int = 3) -> bytes:
    """A CAPABILITIES_OK the host decodes; the device sends it, so the Python
    side has no encoder and this spells the layout out once."""
    return (struct.pack("<HHBBBB", width, height, int(p.PixelFormat.RGB888),
                        int(p.Orientation.DEG0), 1, 0)
            + b"\0" * (4 * p.BUTTON_BYTES) + struct.pack("<HIH", 180, 30000, 0))


def _session_device(reply=None):
    """A device that opens sessions: echoes the HELLO's generation and answers
    CAPABILITIES. Anything else goes to `reply`."""
    def answer(envelope):
        if envelope.op is p.Op.HELLO:
            asked = p.Hello.decode(envelope.body)
            return [_reply_to(envelope, p.Op.HELLO_OK,
                              p.Hello(board_id="scripted", build="sim 0.0.1",
                                      session=asked.session).encode())]
        if envelope.op is p.Op.CAPABILITIES:
            return [_reply_to(envelope, p.Op.CAPABILITIES_OK, _capabilities_body())]
        return reply(envelope) if reply else []
    return ScriptedDevice(answer)


def _left_on_the_port(device: ScriptedDevice, *leftovers) -> ScriptedDevice:
    """Process A's late replies, already in the port's read buffer when process
    B opens it. Each is (op, req_id, body): A's ids were 1, 2, 3, exactly the
    ids B is about to use."""
    for op, req_id, body in leftovers:
        device._out += p.frame_encode(p.envelope_encode(  # noqa: SLF001 - the rig
            p.Envelope(op=op, req_id=req_id, body=body)))
    return device


STALE_SESSION = 0xDEAD0001  # what process A drew; B draws its own


def _stale_hello_ok() -> bytes:
    return p.Hello(board_id="scripted", build="sim 0.0.1", session=STALE_SESSION).encode()


def a_previous_invocations_reply_is_not_this_ones_answer() -> None:
    """The id space restarts at 1 in every process, so ids collide across them.

    On USB Serial/JTAG closing the tty is not a bus disconnect: nothing on the
    watch ran `on_disconnect` when process A exited, and A's late replies are
    in the port's read buffer when process B opens it. B's sequence is
    deterministic -- HELLO 1, CAPABILITIES 2, then the command as 3 -- so
    every one of A's numbers is one B is about to use. Not a coincidence but
    the ordinary case.

    Until #348 the host evicted a stale reply under the id it was about to
    issue, which closed the frame that had arrived before the request and
    nothing else: a `HELLO_OK` under A's id 1 confirmed B's handshake on its
    own, and a `STABLE_OK` A had been waiting on answered B's first poll.
    Now the handshake carries a generation the device echoes, and nothing
    read ahead of the echo is B's -- that one check is what every case below
    turns on, and removing it turns every one of them red.
    """
    from watch.client import Watch  # noqa: PLC0415

    # --- 1. A's handshake and a late STABLE_OK are all on the port ----------
    device = _left_on_the_port(
        _session_device(lambda e: [_reply_to(e, p.Op.STABLE_OK, b"\x00")]),
        (p.Op.HELLO_OK, 1, _stale_hello_ok()),
        (p.Op.CAPABILITIES_OK, 2, _capabilities_body(99, 99)),
        (p.Op.STABLE_OK, 3, b"\x01"),
        (p.Op.MESH_OK, 3, b""))
    watch = Watch(device, timeout=0.5)
    watch.connect()

    drawn = p.Hello.decode(next(body for op, body in device.asked if op is p.Op.HELLO)).session
    check(drawn not in (0, STALE_SESSION), "the host drew its own generation")
    check(watch.hello is not None and watch.hello.session == drawn,
          "and the handshake it accepted carries that generation, not A's")
    check(watch.capabilities is not None and watch.capabilities.width == 4,
          "the capabilities are the fresh answer, not A's 99x99")
    check(watch._pending == [],  # noqa: SLF001
          "and nothing A left on the port is still queued as an answer")
    check([op for op, _ in device.asked] == [p.Op.HELLO, p.Op.CAPABILITIES],
          "the device was asked, rather than the queue")

    # --- 2. A's `STABLE_OK = true` does not settle B's wait -----------------
    # B's first WAIT_STABLE is req_id 3, the number A's `true` is addressed
    # to. The device says `false`; B must say so and keep polling.
    check(watch.wait_stable(100, timeout=0.15, poll=0.0) is False,
          "an interface that is not quiet is not reported quiet by A's reply")

    # --- 3. A's success does not mask B's refusal ---------------------------
    device = _left_on_the_port(
        _session_device(lambda e: [p.envelope_encode(p.Envelope(
            op=p.Op.ERROR, req_id=e.req_id,
            body=bytes((p.ErrorCode.OPERATION_FAILED, 0))))]),
        (p.Op.HELLO_OK, 1, _stale_hello_ok()),
        (p.Op.CAPABILITIES_OK, 2, _capabilities_body()),
        (p.Op.MESH_OK, 3, b""))
    watch = Watch(device, timeout=0.5)
    watch.connect()
    check_raises(p.ProtocolError,
                 "a stale MESH_OK under the same id is not this request's answer",
                 lambda: watch.request(p.Op.MESH_FORGET_BOND, b"", (p.Op.MESH_OK,),
                                       retries=0))
    check(any(op is p.Op.MESH_FORGET_BOND for op, _ in device.asked),
          "and the request did reach the device rather than being answered from the queue")


def an_aborted_screenshots_chunks_do_not_enter_the_next_processs_frame() -> None:
    """Regression 4 of #348: process A gave up on a screenshot mid-transfer and
    exited; its chunks and SCREEN_END are on the port under req_id 3, which
    is exactly the id B's first screenshot gets."""
    from watch.client import Watch  # noqa: PLC0415

    width, height = 4, 3
    fresh = bytes((i * 37 + 11) & 0xFF for i in range(width * height * 3))
    stale = bytes(0xEE for _ in fresh)

    def screen(envelope, image):
        info = struct.pack("<IHHBBIII", 1, width, height,
                           int(p.PixelFormat.RGB888), int(p.Orientation.DEG0),
                           len(image), p.crc32_of(image), 0)
        out = [(p.Op.SCREEN_INFO, info)]
        for offset in range(0, len(image), 16):
            out.append((p.Op.SCREEN_DATA, struct.pack("<I", offset) + image[offset:offset + 16]))
        out.append((p.Op.SCREEN_END, b""))
        return out

    device = _session_device(
        lambda e: [_reply_to(e, op, body) for op, body in screen(e, fresh)]
        if e.op is p.Op.SCREEN_REQUEST else [])
    _left_on_the_port(device, *((op, 3, body) for op, body in screen(None, stale)))

    watch = Watch(device, timeout=2.0)
    watch.connect()
    shot = watch.screenshot()
    check(shot.rgb == fresh, "the frame B assembles is B's, with none of A's pixels in it")
    check(watch._abandoned == {},  # noqa: SLF001
          "and B abandoned nothing to get there")


def a_device_without_a_session_generation_is_refused_as_such() -> None:
    """A v1 device answers HELLO with 49 bytes and no session. That is not a
    timeout and not a stale reply: it is a device this tool cannot open a
    session with, and it should say so once, by name."""
    from watch.client import Watch  # noqa: PLC0415

    device = ScriptedDevice(lambda e: [_reply_to(
        e, p.Op.HELLO_OK, p.Hello(board_id="old", build="sim 0.0.0").encode()[:p.HELLO_V1_BYTES])]
        if e.op is p.Op.HELLO else [])
    watch = Watch(device, timeout=0.5)
    try:
        watch.connect()
    except p.ProtocolError as exc:
        check("v1" in str(exc) and "generation" in str(exc),
              f"the refusal names the version and the missing field: {exc}")
    else:
        failures.append("a v1 HELLO_OK opened a session")
    check(sum(op is p.Op.HELLO for op, _ in device.asked) == 1,
          "and it was not retried: the answer was an answer")


CASES = (
    a_previous_invocations_reply_is_not_this_ones_answer,
    an_aborted_screenshots_chunks_do_not_enter_the_next_processs_frame,
    a_device_without_a_session_generation_is_refused_as_such,
    fixed_vectors,
    framing_round_trip,
    framing_survives_any_fragmentation,
    framing_rejects_a_corrupted_byte,
    framing_resynchronises_after_a_text_log,
    framing_rejects_an_impossible_length,
    framing_never_loses_a_split_sync,
    envelope_round_trip,
    envelope_rejects_corruption,
    envelope_rejects_a_length_disagreement,
    a_debug_message_fits_one_frame,
    bodies_round_trip,
    rgb888_and_bgr888,
    rgb565_expands_to_full_scale,
    a_wrong_length_is_refused,
    orientation_turns_the_right_way,
    png_is_a_real_png,
    a_screenshot_survives_the_whole_chain,
    the_tool_fails_loudly_with_no_device,
    serial_disconnects_are_reported_without_tracebacks,
    scenarios_load,
    a_scenario_that_runs_nothing_is_not_a_pass,
    an_unknown_wire_value_is_reported_not_raised,
    every_error_code_has_a_human_sentence,
    a_time_sync_is_validated_and_not_retried,
    mesh_commands_are_validated_and_a_send_is_not_retried,
    a_stability_wait_actually_waits,
    a_stability_wait_that_never_settles_says_so,
    a_finished_screenshot_blacklists_nothing,
    a_frame_that_never_finishes_gives_up_at_the_deadline,
    an_abandoned_transfer_evicts_the_oldest_id,
    a_gesture_takes_the_time_it_was_given,
    a_gesture_absorbs_its_round_trips_rather_than_adding_them,
    a_gesture_longer_than_the_device_will_hold_is_refused,
    every_pointer_hold_verb_is_bounded_before_pointer_down,
    a_gesture_that_cannot_be_timed_is_refused_before_the_finger_lands,
    the_shipped_gesture_keeps_its_timing_on_both_panels,
    an_error_code_this_build_does_not_know_is_still_a_sentence,
    an_id_is_never_issued_twice_on_one_connection,
    a_connection_out_of_ids_refuses_rather_than_reusing_one,
)


def run() -> int:
    for case in CASES:
        before = len(failures)
        case()
        mark = "ok  " if len(failures) == before else "FAIL"
        print(f"  {mark} {case.__name__.replace('_', ' ')}")

    if failures:
        print(f"\nwatch selftest FAILED ({len(failures)}):\n", file=sys.stderr)
        for failure in failures:
            print(f"  * {failure}", file=sys.stderr)
        return 1

    print(f"\nwatch selftest: {len(CASES)} groups, all passed. "
          f"Nothing here needed a device or a socket.")
    return 0


if __name__ == "__main__":
    sys.exit(run())
