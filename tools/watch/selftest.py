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
import sys
import tempfile
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


def fixed_vectors() -> None:
    check(p.crc16_ccitt(b"123456789") == CRC16_CHECK,
          f"CRC-16/CCITT-FALSE check value is 0x{CRC16_CHECK:04X}")
    check(p.crc32_of(b"123456789") == CRC32_CHECK,
          f"CRC-32 check value is 0x{CRC32_CHECK:08X}")
    check(p.frame_encode(b"hello") == FRAME_HELLO,
          f"a frame around b'hello' is {FRAME_HELLO.hex()}")
    check(p.length_check(0) == 0x5A, "the length check is salted, so 0 does not map to 0")
    check(p.length_check(0xFFFF) == 0x5A, "0xFFFF does not map to 0 either")


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


def scenarios_load() -> None:
    from watch import scenario  # noqa: PLC0415

    root = HERE.parent.parent
    tour = root / "tests" / "ui" / "scenarios" / "diagnostic_tour.yaml"
    if tour.exists():
        try:
            steps = scenario.load(str(tour))
            check(len(steps) > 0, "the shipped scenario parses into steps")
            check(all("action" in step for step in steps), "and every step names an action")
        except Exception as exc:  # noqa: BLE001 - PyYAML may be absent
            if "PyYAML" not in str(exc):
                failures.append(f"the shipped scenario failed to load: {exc}")

    with tempfile.TemporaryDirectory() as directory:
        path = os.path.join(directory, "steps.json")
        Path(path).write_text('[{"action": "wait", "seconds": 0}]', encoding="utf-8")
        check(scenario.load(path) == [{"action": "wait", "seconds": 0}],
              "a JSON scenario loads without PyYAML")


CASES = (
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
    scenarios_load,
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
