"""Talking to a watch -- or to the simulator standing in for one.

The transport is deliberately the only thing that differs between the two. A
Unix socket reaches the simulator; a serial port will reach a device when there
is firmware to reach. Everything above `Transport` is identical, which is the
point: a scenario written against the simulator runs against hardware unchanged.

### What this does not do

It holds no table of board facts. Panel size, pixel format, orientation, how
many touch points and which buttons exist all come from the device's own
`capabilities` reply. A tool that knew that "the Waveshare is 410x502" would be
the architecture rule -- an application asks what the device can do, never which
device it is -- broken in a script, and it would be wrong the first time a panel
was rotated.
"""

from __future__ import annotations

import glob
import os
import socket
import time
from dataclasses import dataclass

from . import protocol as p

DEFAULT_TIMEOUT = 10.0

# Where a simulator is likely to be listening if nobody said. Not a search of
# the whole filesystem: two conventional places, in order.
SOCKET_CANDIDATES = (
    "./.attadipa-sim.sock",
    "/tmp/attadipa-sim.sock",
)


class WatchError(RuntimeError):
    """Anything that stopped the tool from doing what was asked."""


class Transport:
    def send(self, data: bytes) -> None: raise NotImplementedError
    def recv(self, timeout: float) -> bytes: raise NotImplementedError
    def close(self) -> None: raise NotImplementedError
    def describe(self) -> str: raise NotImplementedError


class SocketTransport(Transport):
    def __init__(self, path: str) -> None:
        self._path = path
        self._sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            self._sock.connect(path)
        except OSError as exc:
            raise WatchError(f"could not connect to {path}: {exc}") from exc

    def send(self, data: bytes) -> None:
        try:
            self._sock.sendall(data)
        except OSError as exc:
            raise WatchError(f"the connection dropped while sending: {exc}") from exc

    def recv(self, timeout: float) -> bytes:
        self._sock.settimeout(timeout)
        try:
            return self._sock.recv(65536)
        except socket.timeout:
            return b""
        except OSError as exc:
            raise WatchError(f"the connection dropped while reading: {exc}") from exc

    def close(self) -> None:
        self._sock.close()

    def describe(self) -> str:
        return f"unix:{self._path}"


class SerialTransport(Transport):
    """The device transport. Untested against a device, because none exists yet.

    There is no Attadipa firmware, so nothing on the far end of a serial port
    speaks this protocol today. The class is here because the alternative --
    leaving a hole where the device transport goes -- makes the shape of the
    thing harder to see, and because the framing has to survive a byte stream
    that arrives in arbitrary fragments either way, which is exactly what the
    decoder was built for.

    Marked in the report as NOT EXECUTED rather than claimed as working.
    """

    def __init__(self, port: str, baud: int = 921600) -> None:
        try:
            import serial  # type: ignore
        except ImportError as exc:
            raise WatchError(
                "pyserial is not installed, so a serial port cannot be opened. "
                "pip install pyserial") from exc
        try:
            self._serial = serial.Serial(port, baud, timeout=0)
        except Exception as exc:  # pragma: no cover - needs a device
            raise WatchError(f"could not open {port}: {exc}") from exc
        self._port = port

    def send(self, data: bytes) -> None:
        self._serial.write(data)

    def recv(self, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            waiting = self._serial.in_waiting
            if waiting:
                return self._serial.read(waiting)
            time.sleep(0.002)
        return b""

    def close(self) -> None:
        self._serial.close()

    def describe(self) -> str:
        return f"serial:{self._port}"


def discover_socket() -> str | None:
    for candidate in SOCKET_CANDIDATES:
        if os.path.exists(candidate):
            return candidate
    found = sorted(glob.glob("/tmp/attadipa-*.sock"))
    return found[0] if found else None


@dataclass
class Screenshot:
    info: p.ScreenInfo
    rgb: bytes
    width: int
    height: int


class Watch:
    """One connection, one conversation.

    Requests carry a `req_id` and replies are matched against it, so a stale
    answer to a timed-out request cannot be read as the answer to the next one.
    MeshCore's companion protocol has no correlation at all, which ADR-0005
    section 4 records as the reason this field exists.
    """

    def __init__(self, transport: Transport, timeout: float = DEFAULT_TIMEOUT) -> None:
        self._transport = transport
        self._decoder = p.FrameDecoder()
        self._timeout = timeout
        self._next_req_id = 1
        self._pending: list[p.Envelope] = []
        self.hello: p.Hello | None = None
        self.capabilities: p.Capabilities | None = None

    # --- plumbing ---------------------------------------------------------

    def close(self) -> None:
        self._transport.close()

    def describe(self) -> str:
        return self._transport.describe()

    def _allocate_req_id(self) -> int:
        req_id = self._next_req_id
        # 0 is reserved for an unsolicited message; wrapping skips it.
        self._next_req_id = (self._next_req_id + 1) & 0xFFFF or 1
        return req_id

    def _pump(self, timeout: float) -> None:
        data = self._transport.recv(timeout)
        if data:
            self._decoder.push(data)
        for payload in self._decoder:
            try:
                self._pending.append(p.envelope_decode(payload))
            except p.ProtocolError:
                # A frame that framed correctly but did not decode is counted
                # and dropped. It cannot be answered: req_id was one of the
                # fields that failed to parse.
                continue

    def _await(self, req_id: int, ops: tuple[p.Op, ...], timeout: float | None = None):
        deadline = time.monotonic() + (timeout if timeout is not None else self._timeout)
        while True:
            for index, envelope in enumerate(self._pending):
                if envelope.req_id != req_id:
                    continue
                if envelope.op is p.Op.ERROR:
                    del self._pending[index]
                    code = p.ErrorCode(int.from_bytes(envelope.body[:2], "little")) \
                        if len(envelope.body) >= 2 else p.ErrorCode.NONE
                    raise p.ProtocolError(
                        f"the device refused: {p.ERROR_TEXT.get(code, code.name)}", code)
                if envelope.op in ops:
                    del self._pending[index]
                    return envelope
            if time.monotonic() >= deadline:
                raise WatchError(
                    f"the device did not answer within {self._timeout:.1f}s. "
                    f"Is it still running, and is anything else connected to it?")
            self._pump(0.05)

    def request(self, op: p.Op, body: bytes, expect: tuple[p.Op, ...],
                timeout: float | None = None, retries: int = 1):
        """One request, one reply, with a retry.

        The retry is not optimism: on a byte stream a request can be lost to a
        resync and the difference between "lost" and "the device is wedged" is
        exactly whether a second attempt is answered. Retrying is skipped for a
        typed error, which is an answer.
        """
        last: Exception | None = None
        for attempt in range(retries + 1):
            req_id = self._allocate_req_id()
            self._transport.send(p.frame_encode(p.envelope_encode(
                p.Envelope(op=op, req_id=req_id, body=body))))
            try:
                return self._await(req_id, expect, timeout)
            except WatchError as exc:
                last = exc
                if attempt < retries:
                    continue
        raise last if last else WatchError("no reply")

    # --- handshake --------------------------------------------------------

    def connect(self) -> None:
        reply = self.request(p.Op.HELLO, p.Hello(board_id="host", build="watch_control").encode(),
                             (p.Op.HELLO_OK,))
        self.hello = p.Hello.decode(reply.body)
        if self.hello.protocol_version != p.PROTOCOL_VERSION:
            # Reported, not fatal. ADR-0005 section 5 keeps version and
            # capability set orthogonal; a host one behind should say so and
            # carry on rather than refuse to look at the screen.
            print(f"warning: the device speaks debug protocol v{self.hello.protocol_version}, "
                  f"this tool speaks v{p.PROTOCOL_VERSION}")
        reply = self.request(p.Op.CAPABILITIES, b"", (p.Op.CAPABILITIES_OK,))
        self.capabilities = p.Capabilities.decode(reply.body)

    def _caps(self) -> p.Capabilities:
        if self.capabilities is None:
            raise WatchError("connect() has not run")
        return self.capabilities

    # --- screen -----------------------------------------------------------

    def screenshot(self, timeout: float | None = None) -> Screenshot:
        req_id = self._allocate_req_id()
        self._transport.send(p.frame_encode(p.envelope_encode(
            p.Envelope(op=p.Op.SCREEN_REQUEST, req_id=req_id))))

        info = p.ScreenInfo.decode(self._await(req_id, (p.Op.SCREEN_INFO,), timeout).body)
        buffer = bytearray(info.total_bytes)
        seen = bytearray(info.total_bytes)

        deadline = time.monotonic() + (timeout if timeout is not None else max(self._timeout, 30.0))
        while True:
            envelope = self._await(req_id, (p.Op.SCREEN_DATA, p.Op.SCREEN_END),
                                   max(0.1, deadline - time.monotonic()))
            if envelope.op is p.Op.SCREEN_END:
                break
            offset = int.from_bytes(envelope.body[:4], "little")
            chunk = envelope.body[4:]
            if offset + len(chunk) > len(buffer):
                raise p.ProtocolError(
                    f"a chunk at offset {offset} runs past the {len(buffer)}-byte image")
            buffer[offset:offset + len(chunk)] = chunk
            seen[offset:offset + len(chunk)] = b"\1" * len(chunk)

        missing = seen.count(0)
        if missing:
            raise p.ProtocolError(
                f"the frame is incomplete: {missing} of {len(buffer)} bytes never arrived")
        # The image CRC is a second, independent check. The framing already
        # proved every chunk arrived intact; this proves they were assembled
        # correctly -- none missing, none doubled, none out of order.
        if p.crc32_of(bytes(buffer)) != info.crc32:
            raise p.ProtocolError(
                "the assembled frame does not match its checksum, so it is corrupt")

        rgb = p.to_rgb(bytes(buffer), info.width, info.height, info.format)
        rgb, width, height = p.apply_orientation(rgb, info.width, info.height, info.orientation)
        return Screenshot(info=info, rgb=rgb, width=width, height=height)

    def save_screenshot(self, path, timeout: float | None = None) -> tuple[str, Screenshot]:
        shot = self.screenshot(timeout)
        directory = os.path.dirname(os.path.abspath(path))
        os.makedirs(directory, exist_ok=True)
        p.write_png(path, shot.rgb, shot.width, shot.height)
        return os.path.abspath(path), shot

    def wait_stable(self, timeout: float | None = None) -> bool:
        reply = self.request(p.Op.WAIT_STABLE, b"", (p.Op.STABLE_OK,), timeout)
        return bool(reply.body and reply.body[0])

    # --- input ------------------------------------------------------------

    def button_index(self, name: str) -> int:
        caps = self._caps()
        for index, button in enumerate(caps.buttons):
            if button.id == name:
                if not button.injectable:
                    raise WatchError(
                        f"'{name}' is a service key on this board and is not simulated. "
                        f"Injectable: {', '.join(b.id for b in caps.buttons if b.injectable)}")
                return index
        names = ", ".join(b.id for b in caps.buttons) or "none"
        raise WatchError(f"this board has no button called '{name}'. It has: {names}")

    def _event(self, event_type: p.EventType, **kwargs) -> None:
        self.request(p.Op.INPUT_EVENT, p.input_event_encode(event_type, **kwargs),
                     (p.Op.INPUT_OK,))

    def button_press(self, name: str) -> None:
        self._event(p.EventType.BUTTON_DOWN, button=self.button_index(name))

    def button_release(self, name: str) -> None:
        self._event(p.EventType.BUTTON_UP, button=self.button_index(name))

    def button_click(self, name: str, duration: float = 0.05) -> None:
        index = self.button_index(name)
        self._event(p.EventType.BUTTON_DOWN, button=index)
        time.sleep(duration)
        self._event(p.EventType.BUTTON_UP, button=index)

    def button_hold(self, name: str, duration: float) -> None:
        caps = self._caps()
        if caps.max_hold_ms and duration * 1000 > caps.max_hold_ms:
            raise WatchError(
                f"the device releases anything held longer than {caps.max_hold_ms} ms, "
                f"so a {duration:.1f}s hold would be cut short")
        self.button_click(name, duration)

    def _check_point(self, x: int, y: int) -> tuple[int, int]:
        caps = self._caps()
        if not (0 <= x < caps.width and 0 <= y < caps.height):
            raise WatchError(
                f"({x}, {y}) is outside the {caps.width}x{caps.height} screen. "
                f"Coordinates are logical, with the origin at the top left")
        return x, y

    def tap(self, x: int, y: int) -> None:
        x, y = self._check_point(x, y)
        self._event(p.EventType.POINTER_DOWN, x=x, y=y)
        self._event(p.EventType.POINTER_UP, x=x, y=y)

    def long_tap(self, x: int, y: int, duration: float = 1.0) -> None:
        x, y = self._check_point(x, y)
        self._event(p.EventType.POINTER_DOWN, x=x, y=y)
        time.sleep(duration)
        self._event(p.EventType.POINTER_UP, x=x, y=y)

    def double_tap(self, x: int, y: int, gap: float = 0.12) -> None:
        """Two taps, composed on the host.

        Named as a convenience and nothing more: no device-side double-click
        exists, because this project has not defined one. Composing it here
        means the device sees exactly two taps -- which is what a finger
        produces, and what any future recogniser would have to work from.
        """
        self.tap(x, y)
        time.sleep(gap)
        self.tap(x, y)

    def swipe(self, start: tuple[int, int], end: tuple[int, int], duration: float = 0.4,
              steps: int = 0) -> None:
        """A real down / move... / up, at a real speed.

        Not one high-level event. The request is explicit about this and the
        reason is that a gesture recogniser reads *speed*: a swipe delivered as
        a single jump from start to end has infinite velocity and either fires
        everything or nothing. The intermediate points are sent with sleeps
        between them so the intervals on the device are the intervals asked for.
        """
        x0, y0 = self._check_point(*start)
        x1, y1 = self._check_point(*end)
        if steps <= 0:
            # About 60 Hz, which is the rate a real touch controller reports at,
            # bounded so a long slow drag does not become thousands of events.
            steps = max(2, min(64, int(duration * 60)))

        self._event(p.EventType.POINTER_DOWN, x=x0, y=y0)
        for step in range(1, steps):
            fraction = step / steps
            self._event(p.EventType.POINTER_MOVE,
                        x=int(round(x0 + (x1 - x0) * fraction)),
                        y=int(round(y0 + (y1 - y0) * fraction)))
            time.sleep(duration / steps)
        self._event(p.EventType.POINTER_UP, x=x1, y=y1)

    def drag(self, start, end, duration: float = 1.0, steps: int = 0) -> None:
        """The same shape as a swipe, slower.

        Kept as its own verb because the difference is entirely in the timing
        and that is what is being tested: a drag that is secretly a swipe would
        pass a test the interface fails under a finger.
        """
        self.swipe(start, end, duration=duration, steps=steps)

    def gesture(self, points, duration: float = 0.5) -> None:
        """An arbitrary path: down at the first point, up at the last."""
        if len(points) < 2:
            raise WatchError("a gesture needs at least two points")
        checked = [self._check_point(int(x), int(y)) for x, y in points]
        gap = duration / max(1, len(checked) - 1)

        self._event(p.EventType.POINTER_DOWN, x=checked[0][0], y=checked[0][1])
        for x, y in checked[1:-1]:
            self._event(p.EventType.POINTER_MOVE, x=x, y=y)
            time.sleep(gap)
        self._event(p.EventType.POINTER_UP, x=checked[-1][0], y=checked[-1][1])

    def input_reset(self) -> int:
        reply = self.request(p.Op.INPUT_RESET, b"", (p.Op.INPUT_OK,))
        return reply.body[0] if reply.body else 0


def connect(port: str | None = None, socket_path: str | None = None,
            timeout: float = DEFAULT_TIMEOUT) -> Watch:
    """Open a connection, discovering the endpoint if none was named."""
    if port and socket_path:
        raise WatchError("give a serial port or a socket, not both")

    if port:
        transport: Transport = SerialTransport(port)
    else:
        path = socket_path or discover_socket()
        if not path:
            raise WatchError(
                "no watch found. The simulator listens only when started with "
                "--debug-socket <path>; pass --socket <path> to say where, or "
                "--port <device> for a serial device. Looked in: "
                + ", ".join(SOCKET_CANDIDATES))
        transport = SocketTransport(path)

    watch = Watch(transport, timeout)
    watch.connect()
    return watch
