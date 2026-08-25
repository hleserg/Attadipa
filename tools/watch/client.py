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

import math
import os
import socket
import stat
import struct
import sys
import time
from dataclasses import dataclass

from . import protocol as p

DEFAULT_TIMEOUT = 10.0

# How many decoded replies may sit unclaimed before the oldest is dropped. A
# screenshot of the Waveshare panel is ~3400 chunks, all of which are consumed
# as they arrive, so the steady state is one or two; this is a ceiling on
# replies nobody will ever ask for, not a queue depth.
kMaxPending = 256

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
            data = self._sock.recv(65536)
        except socket.timeout:
            return b""
        except OSError as exc:
            raise WatchError(f"the connection dropped while reading: {exc}") from exc
        if not data:
            # A clean EOF. Returning b"" here would be indistinguishable from
            # "nothing arrived yet", so the caller would spin at full speed
            # until its whole timeout expired and then blame the device for not
            # answering. The simulator exiting, or refusing us as a second
            # client, both land here.
            raise WatchError(
                "the device closed the connection. It exited, or it already "
                "had a client -- only one is served at a time.")
        return data

    def close(self) -> None:
        self._sock.close()

    def describe(self) -> str:
        return f"unix:{self._path}"


class SerialTransport(Transport):
    """USB-Serial/JTAG transport used by the physical Waveshare watch."""

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
        try:
            self._serial.write(data)
        except Exception as exc:  # pragma: no cover - needs a device
            raise WatchError(
                f"the serial connection dropped while sending to {self._port}: {exc}"
            ) from exc

    def recv(self, timeout: float) -> bytes:
        deadline = time.monotonic() + timeout
        try:
            while time.monotonic() < deadline:
                waiting = self._serial.in_waiting
                if waiting:
                    return self._serial.read(waiting)
                time.sleep(0.002)
        except Exception as exc:  # pragma: no cover - needs a device
            raise WatchError(
                f"the serial connection dropped while reading from {self._port}: {exc}"
            ) from exc
        return b""

    def close(self) -> None:
        try:
            self._serial.close()
        except Exception as exc:  # pragma: no cover - needs a device
            raise WatchError(f"could not close {self._port}: {exc}") from exc

    def describe(self) -> str:
        return f"serial:{self._port}"


def discover_socket() -> str | None:
    """The documented default, and nothing else.

    An earlier version fell back to globbing ``/tmp/attadipa-*.sock``, which on
    a shared host means connecting to whatever another user happened to leave
    lying around -- and driving their interface. Auto-discovery that guesses
    across ownership boundaries is not a convenience. ``--socket`` is one flag.
    """
    for candidate in SOCKET_CANDIDATES:
        if os.path.exists(candidate) and _is_ours(candidate):
            return candidate
    return None


def _is_ours(path: str) -> bool:
    """True if this socket belongs to us and nobody else may write to it.

    The simulator creates it 0600 on purpose. Checking here as well means a
    socket that somehow ended up group- or world-writable is skipped by
    auto-discovery rather than silently used -- the user can still name it with
    ``--socket`` and take responsibility for it.
    """
    try:
        info = os.stat(path)
    except OSError:
        return False
    if info.st_uid != os.geteuid():
        return False
    return not info.st_mode & (stat.S_IWGRP | stat.S_IWOTH)


def _duration_seconds(value, what: str) -> float:
    """A length of time an input may be asked to take, or a refusal.

    Zero is allowed and means "as fast as the wire allows" -- there is a real
    use for it, sending a shape without asserting anything about its speed.
    Everything else here is a mistake to be named rather than absorbed: a
    negative gap reaches `time.sleep` as a `ValueError` from inside the
    library, and a NaN one does not raise at all -- `sleep(nan)` returns
    immediately, so a gesture asked to take an unreadable time takes none and
    reports success. `inf` is worse: it hangs the run with an input held.
    """
    try:
        seconds = float(value)
    except (TypeError, ValueError) as exc:
        raise WatchError(
            f"{what} needs a duration in seconds, and {value!r} is not a number") from exc
    if not math.isfinite(seconds):
        raise WatchError(
            f"{what} needs a duration in seconds, and {seconds} is not a length of time")
    if seconds < 0:
        raise WatchError(
            f"{what} cannot take {seconds:g} seconds. Zero is allowed and means "
            f"as fast as the connection manages")
    return seconds


def _sleep_until(deadline: float) -> None:
    """Wait for a point on the monotonic clock, never for a negative interval.

    Deadlines are absolute so that the work between them -- a request, a
    reply, a scheduler -- comes out of the interval it happened in instead of
    being added to the path. When something overruns its interval the next
    point is already due, and this returns rather than trying to sleep back in
    time.
    """
    remaining = deadline - time.monotonic()
    if remaining > 0:
        time.sleep(remaining)


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
        self._orphaned = 0
        # The req_id `_await` is currently blocked on, so the eviction above can
        # tell a reply that is still wanted from one that never will be.
        self._awaiting: int | None = None
        # req_ids whose screen transfer we have given up on. Chunks still in
        # flight for them are dropped on arrival rather than queued. A dict
        # rather than a set because the eviction below argues from age, and a
        # set has no order to evict by -- it could drop the id abandoned a
        # microsecond ago and keep one from last week.
        self._abandoned: dict[int, None] = {}
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
                envelope = p.envelope_decode(payload)
                if envelope.req_id in self._abandoned and envelope.op in (
                        p.Op.SCREEN_DATA, p.Op.SCREEN_END):
                    continue
                if len(self._pending) >= kMaxPending:
                    self._evict_orphan()
                self._pending.append(envelope)
            except p.ProtocolError:
                # A frame that framed correctly but did not decode is counted
                # and dropped. It cannot be answered: req_id was one of the
                # fields that failed to parse.
                continue

    def _await(self, req_id: int, ops: tuple[p.Op, ...], timeout: float | None = None):
        waited = timeout if timeout is not None else self._timeout
        deadline = time.monotonic() + waited
        previous, self._awaiting = self._awaiting, req_id
        try:
            return self._await_locked(req_id, ops, deadline, waited)
        finally:
            self._awaiting = previous

    def _await_locked(self, req_id: int, ops: tuple[p.Op, ...], deadline: float,
                      waited: float):
        while True:
            for index, envelope in enumerate(self._pending):
                if envelope.req_id != req_id:
                    continue
                if envelope.op is p.Op.ERROR:
                    del self._pending[index]
                    raise self._refusal(envelope)
                if envelope.op in ops:
                    del self._pending[index]
                    return envelope
            if time.monotonic() >= deadline:
                raise WatchError(
                    f"the device did not answer within {waited:.1f}s. "
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

        deadline = time.monotonic() + (timeout if timeout is not None else max(self._timeout, 30.0))

        # `ScreenInfo.decode` and `_check_frame_shape` belong **inside** the
        # try. Outside it they raised past the cleanup, and the device went on
        # pumping ~3400 chunks into `_pending` with nothing to consume them:
        # `_await` rescans that backlog linearly on every pass, there is no
        # cancel opcode, `screenshot` bypasses `request`, and only a reconnect
        # recovered. That is the leak the cleanup exists to close, escaping
        # through the two lines placed above it. Unreachable on a Unix socket;
        # on `SerialTransport` at T-114 a mid-frame resync is the expected
        # event.
        try:
            info = p.ScreenInfo.decode(self._await(req_id, (p.Op.SCREEN_INFO,), timeout).body)
            self._check_frame_shape(info)
            buffer = bytearray(info.total_bytes)
            seen = bytearray(info.total_bytes)
            shot = self._collect_frame(req_id, info, buffer, seen, deadline)
        except BaseException:
            # Only on the way out through an exception. A transfer that
            # finished consumed its own chunks and left nothing in flight, so
            # blacklisting its id buys nothing and costs the next 65535 ids a
            # membership test against a set that grew for no reason.
            #
            # Blacklisted **here**, not on the way in: an id marked abandoned
            # before the transfer starts drops the very chunks it is waiting
            # for, which is a 600 kB image arriving as 14 kB.
            self._pending = [e for e in self._pending if e.req_id != req_id]
            self._abandon(req_id)
            raise
        return shot

    def _evict_orphan(self) -> None:
        """Drop the oldest reply nobody is waiting for.

        `request()` allocates a fresh `req_id` per retry, so a late answer to
        attempt 1 matches nothing, is never claimed and is never evicted -- and
        `_await` rescans the whole list every 50 ms. Harmless over a socket,
        where a timeout means the reply really is not coming; it is
        `SerialTransport` at T-114, resynchronising after a noisy frame, that
        makes this grow without bound.

        **Only orphans.** The first spelling of this dropped the oldest entry
        outright and cut the middle out of a screenshot: a 410x502 transfer is
        thousands of chunks that share one `req_id`, `_pump` decodes a whole
        `recv` buffer of them before the consumer takes any, and the cap is
        reached with every one of them still wanted. So the id currently being
        awaited is skipped, and if everything queued belongs to it nothing is
        dropped -- the transfer's own bounds are what limit it then.
        """
        for index, envelope in enumerate(self._pending):
            if envelope.req_id == self._awaiting:
                continue
            del self._pending[index]
            self._orphaned += 1
            if self._orphaned == 1:
                print(f"warning: more than {kMaxPending} replies are unclaimed; "
                      f"dropping the oldest. The device is answering something "
                      f"this tool is no longer waiting for.", file=sys.stderr)
            return

    @staticmethod
    def _refusal(envelope: "p.Envelope") -> "p.ProtocolError":
        """Turn an ERROR reply into a human sentence, known code or not.

        `ErrorCode` is documented *appended, never renumbered*, so a device one
        version ahead sends a value this checkout has no name for. Calling
        `p.ErrorCode(raw)` on it raises a bare `ValueError`, which is neither
        `WatchError` nor `ProtocolError` -- so it escapes `main()`'s handler and
        `scenario.py`'s, taking `cmd_run`'s cleanup `input_reset` with it and
        leaving a finger down until the 30-second expiry. A refusal we do not
        recognise is still a refusal, and reporting it is the whole job.
        """
        if len(envelope.body) < 2:
            return p.ProtocolError("the device refused, and said nothing about why")
        raw = int.from_bytes(envelope.body[:2], "little")
        try:
            code = p.ErrorCode(raw)
        except ValueError:
            return p.ProtocolError(
                f"the device refused with error {raw}, which is not one this tool "
                f"knows. Is it running a newer build than this checkout?")
        return p.ProtocolError(
            f"the device refused: {p.ERROR_TEXT.get(code, code.name)}", code)

    def _abandon(self, req_id: int) -> None:
        self._abandoned.pop(req_id, None)   # re-insert, so it is the newest
        self._abandoned[req_id] = None
        # req_id wraps at 16 bits, so this is bounded to well under one cycle:
        # an id that old cannot still be in flight, and keeping every one
        # forever would be the leak this method exists to prevent. `dict`
        # preserves insertion order, so the 32 kept really are the 32 newest --
        # which is what "an id that old" needs in order to mean anything.
        if len(self._abandoned) > 64:
            self._abandoned = dict.fromkeys(list(self._abandoned)[-32:])

    def _collect_frame(self, req_id, info, buffer, seen, deadline) -> Screenshot:
        while True:
            # The deadline bounds the **transfer**, and this is the only place
            # that reads it as one. The floor below keeps a single `_await`
            # from being handed a negative timeout -- and without this check it
            # also kept the loop alive for ever, a tenth of a second at a time,
            # for as long as chunks kept arriving. Every individual wait
            # succeeded, so nothing underneath could notice that the transfer
            # as a whole had run out of time. On a Unix socket that is a device
            # that trickles; on `SerialTransport` at T-114 it is a slow link,
            # which is exactly what `--timeout` is for.
            if time.monotonic() >= deadline:
                arrived = len(buffer) - seen.count(0)
                raise WatchError(
                    f"the frame did not finish arriving in time: {arrived} of "
                    f"{len(buffer)} bytes are in and the device is still sending. "
                    f"Raise --timeout, or look at why the transfer is trickling.")
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
            stats = self._decoder.stats
            raise p.ProtocolError(
                f"the frame is incomplete: {missing} of {len(buffer)} bytes never arrived; "
                f"decoder saw {stats.bad_crc} bad CRC, {stats.bad_length} bad length, "
                f"{stats.resyncs} discarded bytes")
        # The image CRC is a second, independent check. The framing already
        # proved every chunk arrived intact; this proves they were assembled
        # correctly -- none missing, none doubled, none out of order.
        if p.crc32_of(bytes(buffer)) != info.crc32:
            raise p.ProtocolError(
                "the assembled frame does not match its checksum, so it is corrupt")

        rgb = p.to_rgb(bytes(buffer), info.width, info.height, info.format)
        rgb, width, height = p.apply_orientation(rgb, info.width, info.height, info.orientation)
        return Screenshot(info=info, rgb=rgb, width=width, height=height)

    def _check_frame_shape(self, info) -> None:
        """Refuse a declared length the geometry does not support.

        ``total_bytes`` is a 32-bit field straight off the wire and two
        ``bytearray`` allocations were made on it before anything looked -- a
        device (or a resync landing mid-frame) claiming 4 GB would have been
        obeyed. The wire format checks a length before trusting it everywhere
        else; this is the one place the host was not doing the same.
        """
        stride = p.BYTES_PER_PIXEL.get(info.format)
        if stride is None:
            raise p.ProtocolError(
                f"the device reported pixel format {info.format!r}, which has no known size")
        expected = info.width * info.height * stride
        if info.total_bytes != expected:
            raise p.ProtocolError(
                f"the device declared a {info.total_bytes}-byte frame, which is not "
                f"{info.width}x{info.height} in {info.format.name} ({expected} bytes)")
        caps = self.capabilities
        if caps is not None and (info.width != caps.width or info.height != caps.height):
            # width and height are themselves device-claimed 16-bit fields, so
            # the self-consistency test above still admits 65535x65535.
            raise p.ProtocolError(
                f"the device sent a {info.width}x{info.height} frame but reported a "
                f"{caps.width}x{caps.height} screen")

    def save_screenshot(self, path, timeout: float | None = None) -> tuple[str, Screenshot]:
        shot = self.screenshot(timeout)
        directory = os.path.dirname(os.path.abspath(path))
        os.makedirs(directory, exist_ok=True)
        p.write_png(path, shot.rgb, shot.width, shot.height)
        return os.path.abspath(path), shot

    def wait_stable(self, quiet_ms: int = 300, timeout: float = 5.0,
                    poll: float = 0.05) -> bool:
        """Poll until the interface has been idle for `quiet_ms`, or give up.

        Returns True if it settled, False if `timeout` ran out first. The
        caller decides what a False means -- `scenario.py` fails the step.

        The duration goes **on the wire**. It used to be an empty body, and the
        device answered `stable_since(now_ms)`: "idle for as long as the process
        has run", true before the first input and false ever after. The single
        request is also why it used to be misnamed: one ask is not a wait, and
        the old caller discarded the answer, so no scenario could observe
        either half of the defect.
        """
        if not 0 <= quiet_ms <= 0xFFFF:
            raise WatchError(f"quiet_ms must fit in 16 bits, got {quiet_ms}")
        body = struct.pack("<H", quiet_ms)
        deadline = time.monotonic() + timeout
        while True:
            reply = self.request(p.Op.WAIT_STABLE, body, (p.Op.STABLE_OK,),
                                 max(0.1, deadline - time.monotonic()))
            if reply.body and reply.body[0]:
                return True
            if time.monotonic() >= deadline:
                return False
            time.sleep(poll)

    # --- input ------------------------------------------------------------

    def button_index(self, name: str) -> int:
        caps = self._caps()
        for index, button in enumerate(caps.buttons):
            if button.id == name:
                if not button.injectable:
                    # Two reasons, and the sentence must not merge them: a
                    # known service key (the T-Watch's boot strap) is not the
                    # same as a key whose role nobody has traced yet. And a
                    # board may declare none at all -- "Injectable: " with
                    # nothing after it read as a truncated message.
                    others = ", ".join(b.id for b in caps.buttons if b.injectable)
                    why = ("is a service key on this board"
                           if button.role_known
                           else "has no established role on this board")
                    raise WatchError(
                        f"'{name}' {why} and is not simulated. "
                        + (f"Injectable: {others}" if others
                           else "This board declares no injectable button."))
                return index
        names = ", ".join(b.id for b in caps.buttons) or "none"
        raise WatchError(f"this board has no button called '{name}'. It has: {names}")

    def _event(self, event_type: p.EventType, **kwargs) -> None:
        # `retries=0`, and it is the only call site that says so.
        #
        # `request()` retries because a request lost to a resync is
        # indistinguishable from a wedged device until a second attempt is
        # answered. That argument holds for every operation here *except this
        # one*: injecting an event twice is not injecting it once. The device
        # has no req_id de-duplication, so a retried ButtonDown that actually
        # arrived the first time comes back `BadInput` -- "that press was
        # impossible" -- about a press that worked. Reporting a successful
        # action as an impossible one is worse than reporting a timeout.
        self.request(p.Op.INPUT_EVENT, p.input_event_encode(event_type, **kwargs),
                     (p.Op.INPUT_OK,), retries=0)

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

    def screen_size(self) -> tuple[int, int]:
        """The geometry a coordinate is expressed in: the **displayed** one.

        Three definitions used to be in play and nothing chose between them.
        `caps.width`/`height` are the *framebuffer's*; `apply_orientation`
        transposes the image for `DEG90` and `DEG270`, so the PNG an agent reads
        a coordinate off is the other way round on a rotated device; and
        `core/input.h` says an injected point is logical, *after* rotation.

        That last one wins, and everything here follows it: a coordinate is in
        the frame of the picture. It has to be, because the rule the whole tool
        exists to serve is "look at the frame, find the element in it, then tap
        it" -- a bound in a geometry the agent never sees cannot enforce that.

        Both boards report `DEG0` today, so the swap below has never run against
        a real rotation. **T-114.** Written down regardless: an untested branch
        with a stated convention is a smaller problem than three conventions.
        """
        caps = self._caps()
        if caps.orientation in (p.Orientation.DEG90, p.Orientation.DEG270):
            return caps.height, caps.width
        return caps.width, caps.height

    def _check_point(self, x: int, y: int) -> tuple[int, int]:
        width, height = self.screen_size()
        if not (0 <= x < width and 0 <= y < height):
            raise WatchError(
                f"({x}, {y}) is outside the {width}x{height} screen. "
                f"Coordinates are logical, with the origin at the top left, "
                f"in the frame of the picture the tool returns")
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

        **Approximately, and this sentence used to say it without the
        qualifier.** There are `steps` intervals between the points below and
        only `steps - 1` sleeps, the first of them of length zero, so a swipe
        runs `1/steps` short of `duration` -- 4% at the default. `gesture()`
        had the same shape of error and it was not proportional there: see its
        docstring and issue #117 for the firmware endpoint contract.
        Left as it is on purpose rather than by omission; changing it moves
        the timing of every swipe in the repository and wants its own test.
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
        """An arbitrary path: down at the first point, up at the last.

        `duration` is the **whole** path, from the `PointerDown` to the
        `PointerUp`. An `N`-point path has `N - 1` intervals between adjacent
        points and every one of them is waited out, the last one included --
        the two-point case, where there is nothing in the middle at all, is
        the one that says whether that is true.

        It was not. The sleep hung off the *intermediate* points and came
        after each was sent, so a five-point 0.6 s gesture spent 0.45 s and
        began with a zero-length first segment, and a two-point one was a
        `Down` immediately followed by an `Up` however long it was asked to
        take. A recogniser reads speed: that is the difference between a
        swipe, a drag and a flick, reported as whichever the timing happened
        to fall into while the run said it had asked for something else.

        The deadlines are absolute, from one `time.monotonic()` taken at the
        `PointerDown`, so the round trips do not accumulate into the path
        length -- summing `sleep(gap)` makes an `N`-point gesture late by
        `N - 1` round trips, and the longer the path the further out it is.
        """
        if len(points) < 2:
            raise WatchError("a gesture needs at least two points")
        # Every refusal happens before the `PointerDown`. One checked
        # afterwards would raise with a finger already down and leave the
        # cleanup to `watch_control.py`'s `finally` -- which does run, but a
        # `live` session or a scenario step recovering from the exception
        # would be holding an input nobody asked for in the meantime.
        seconds = _duration_seconds(duration, "a gesture")
        caps = self._caps()
        # The device expires a hold it has been given for too long, and this
        # became reachable the moment the pointer started staying down for the
        # whole duration: the bridge pushes its own `PointerUp` at
        # `max_hold_ms`, the interface takes a click nobody asked for, and the
        # host's real release then arrives at a state machine holding nothing
        # and is refused as "impossible from the current state" -- a message
        # about the wrong subsystem entirely. `button_hold` refuses in a
        # sentence rather than let that happen and so does this. The bound is
        # the *device's*, read from its capabilities: a firmware with a
        # tighter one than the simulator's 30 s is exactly the case a
        # hardcoded number here would get wrong. Found in review of #187.
        #
        # The falsy test is this tool declining to enforce a bound it was
        # not given -- NOT a reading of what the device means by 0. The
        # bridge treats 0 as expire-immediately (bridge.cpp:597 compares
        # with `>`), so a firmware wanting an unbounded hold must raise
        # the limit rather than zero it. Refusing every gesture against a
        # device that answered 0 would be worse and would not make the
        # device's behaviour any different. Found in review of #192.
        if caps.max_hold_ms and seconds * 1000 > caps.max_hold_ms:
            raise WatchError(
                f"the device releases anything held longer than {caps.max_hold_ms} ms, "
                f"and a gesture holds the pointer down for its whole duration -- so a "
                f"{seconds:.1f}s path would be cut short, and the release at the end of "
                f"it refused")
        checked = [self._check_point(int(x), int(y)) for x, y in points]
        gap = seconds / (len(checked) - 1)

        started = time.monotonic()
        self._event(p.EventType.POINTER_DOWN, x=checked[0][0], y=checked[0][1])
        last = len(checked) - 1
        for index, (x, y) in enumerate(checked[1:], start=1):
            _sleep_until(started + gap * index)
            self._event(p.EventType.POINTER_UP if index == last
                        else p.EventType.POINTER_MOVE, x=x, y=y)

    def input_reset(self) -> tuple[int, int]:
        """Lift everything this connection is holding. Returns (released, still_held).

        Two numbers because one cannot tell the two failures apart. The device
        marks an input released only if its event reached the input queue, so a
        `released` of 0 means *either* nothing was stuck *or* the queue was full
        and everything still is -- and this command is advertised as the escape
        hatch for the stalled interface that produces the second. A partial
        release is the quieter half of the same problem: a button out and a
        finger still down answers 1, which reads as complete.

        `still_held` is not an error. Nothing is lost -- the device's hold
        expiry retries -- so the caller decides whether to wait, retry or say
        so. It is only a lie when it is not reported.
        """
        reply = self.request(p.Op.INPUT_RESET, b"", (p.Op.INPUT_OK,))
        released = reply.body[0] if len(reply.body) >= 1 else 0
        still_held = reply.body[1] if len(reply.body) >= 2 else 0
        return released, still_held


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
