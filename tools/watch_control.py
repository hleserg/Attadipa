#!/usr/bin/env python3
"""Drive a watch -- or the simulator standing in for one -- from the command line.

    tools/watch_control.py info
    tools/watch_control.py screenshot --output artifacts/watch/screen.png
    tools/watch_control.py button power click
    tools/watch_control.py tap --x 120 --y 180 --screenshot-after
    tools/watch_control.py swipe --from 200,180 --to 40,180 --duration 0.5
    tools/watch_control.py run tests/ui/scenarios/diagnostic_tour.yaml
    tools/watch_control.py live

Every command exits non-zero on failure and prints the absolute path of any
image it wrote, because the point of the whole mechanism is that an agent can
then open that file and look at it. A line of text saying the tap succeeded is
not the deliverable; the PNG is.

The tool holds no board facts. Panel size, pixel format, orientation, touch
count and the button list all come from the device's `capabilities` reply --
`info` prints exactly what it was told.
"""

from __future__ import annotations

import argparse
from datetime import datetime
import getpass
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from watch import protocol as p            # noqa: E402
from watch import scenario as scenario_mod  # noqa: E402
from watch.client import Watch, WatchError, connect  # noqa: E402
from flash.ramhold import DEFAULT_SERIAL, resolve_port  # noqa: E402

DEFAULT_OUTPUT_DIR = os.path.join("artifacts", "watch")


def parse_point(text: str) -> tuple[int, int]:
    x, sep, y = text.partition(",")
    if not sep:
        raise argparse.ArgumentTypeError(f"'{text}' is not an x,y pair")
    try:
        return int(x), int(y)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"'{text}' is not an x,y pair") from exc


def default_shot_path(prefix: str = "screen") -> str:
    """A path that is not the one the last call returned.

    Whole seconds are not enough. `after_action` and `cmd_live` both call this
    with a fixed prefix, so two `--screenshot-after` actions inside one second
    produced the same name twice: the tool printed both paths and left one
    file, destroying the before/after pair it exists to produce while still
    reporting it. Milliseconds make that unlikely and the loop makes it
    impossible -- and impossible is the right bar for something whose whole
    output is the picture.
    """
    now   = time.time()
    stamp = time.strftime("%Y%m%d-%H%M%S", time.localtime(now))
    stamp = f"{stamp}-{int((now % 1) * 1000):03d}"
    path  = os.path.join(DEFAULT_OUTPUT_DIR, f"{prefix}-{stamp}.png")
    serial = 1
    while os.path.exists(path):
        path = os.path.join(DEFAULT_OUTPUT_DIR, f"{prefix}-{stamp}-{serial}.png")
        serial += 1
    return path


def emit(args, payload: dict, text: str) -> None:
    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print(text)


def take_screenshots(watch: Watch, args, prefix: str = "screen") -> list[dict]:
    """One image, or a series. The series is what animations need."""
    shots = []
    count = max(1, int(getattr(args, "count", 1) or 1))
    interval = float(getattr(args, "interval", 0.0) or 0.0)
    base = getattr(args, "output", None)

    for index in range(count):
        if base and count > 1:
            root, ext = os.path.splitext(base)
            path = f"{root}-{index:02d}{ext or '.png'}"
        elif base:
            path = base
        else:
            path = default_shot_path(prefix if count == 1 else f"{prefix}-{index:02d}")

        started = time.monotonic()
        absolute, shot = watch.save_screenshot(path)
        shots.append({
            "path": absolute,
            "width": shot.width,
            "height": shot.height,
            "format": shot.info.format.name,
            "orientation": shot.info.orientation.name,
            "frame_id": shot.info.frame_id,
            "device_ms": shot.info.at_ms,
            "elapsed_ms": int((time.monotonic() - started) * 1000),
        })
        if index + 1 < count and interval > 0:
            time.sleep(interval)
    return shots


def print_shots(args, shots: list[dict]) -> None:
    if args.json:
        print(json.dumps({"screenshots": shots}, indent=2))
        return
    for shot in shots:
        print(f"{shot['path']}  ({shot['width']}x{shot['height']}, {shot['format']}, "
              f"{shot['orientation']}, frame {shot['frame_id']}, {shot['elapsed_ms']} ms)")


# --- commands -------------------------------------------------------------

def cmd_info(watch: Watch, args) -> int:
    caps = watch.capabilities
    hello = watch.hello
    assert caps and hello

    payload = {
        "transport": watch.describe(),
        "board": hello.board_id,
        "build": hello.build,
        "protocol_version": hello.protocol_version,
        "screen": {"width": caps.width, "height": caps.height,
                   "format": caps.format.name, "orientation": caps.orientation.name},
        "max_touch_points": caps.max_touch_points,
        "buttons": [{"id": b.id, "injectable": b.injectable, "role_known": b.role_known}
                    for b in caps.buttons],
        "limits": {"max_body": caps.max_body, "max_hold_ms": caps.max_hold_ms,
                   "max_events_per_s": caps.max_events_per_s},
    }
    if args.json:
        print(json.dumps(payload, indent=2))
        return 0

    print(f"connected over {watch.describe()}")
    print(f"  board            {hello.board_id}")
    print(f"  build            {hello.build}")
    print(f"  debug protocol   v{hello.protocol_version}")
    print(f"  screen           {caps.width} x {caps.height}, {caps.format.name}, "
          f"{caps.orientation.name}")
    print(f"  touch points     {caps.max_touch_points}"
          + ("  (single touch: LVGL's pointer device carries one point)"
             if caps.max_touch_points == 1 else ""))
    if caps.buttons:
        print("  buttons")
        for button in caps.buttons:
            notes = []
            if not button.injectable:
                # Two different reasons wear this flag, and calling both
                # "service key" would state as known the very thing the next
                # note says is not. `boot` is a boot-mode strap on both current
                # profiles; the Waveshare `power` key is injectable and also
                # arrives physically through AXP2101 edge status.
                notes.append("service key, not simulated" if button.role_known
                             else "not simulated")
            if not button.role_known:
                notes.append("role NOT established -- the board has it, "
                             "nobody has traced what it does")
            suffix = ("   " + "; ".join(notes)) if notes else ""
            print(f"    {button.id}{suffix}")
    else:
        print("  buttons          none declared")
    print(f"  limits           {caps.max_events_per_s} events/s, "
          f"hold released after {caps.max_hold_ms} ms, {caps.max_body}-byte bodies")
    return 0


def cmd_screenshot(watch: Watch, args) -> int:
    print_shots(args, take_screenshots(watch, args))
    return 0


def cmd_sync_time(watch: Watch, args) -> int:
    utc_seconds = args.utc_seconds if args.utc_seconds is not None else int(time.time())
    if args.offset_minutes is None:
        offset = datetime.now().astimezone().utcoffset()
        offset_minutes = int(offset.total_seconds() // 60) if offset is not None else 0
    else:
        offset_minutes = args.offset_minutes
    watch.sync_time(utc_seconds, offset_minutes, args.valid_for,
                    allow_large_correction=args.allow_large)
    emit(args,
         {"utc_seconds": utc_seconds, "offset_minutes": offset_minutes,
          "valid_for_seconds": args.valid_for, "allow_large_correction": args.allow_large},
         f"time synchronized: UTC {utc_seconds}, offset {offset_minutes:+d} min, "
         f"trusted for {args.valid_for}s")
    return 0


def read_secret(name: str, max_bytes: int) -> str:
    """One credential, taken from a terminal or a pipe -- never from argv.

    An argument is readable by every other process on the host for as long as
    the command runs, and a shell writes it to history besides. A prompt and a
    pipe are neither. With a terminal attached the operator is prompted; without
    one the value is a single line on stdin, which is what automation uses:

        printf '%s\n' "$ROOM_PASSWORD" | tools/watch_control.py mesh-room-send ...

    Bounded and fail-closed at this boundary: empty, over-long, or carrying a
    NUL is an error here rather than a half credential sent to the watch.
    """
    if sys.stdin.isatty():
        value = getpass.getpass(f"{name}: ")
    else:
        value = sys.stdin.readline().rstrip("\r\n")
    if "\x00" in value:
        raise WatchError(f"{name} must not contain a NUL byte")
    encoded = value.encode("utf-8")
    if not encoded:
        raise WatchError(
            f"{name} is required: type it at the prompt, or pipe it in on stdin")
    if len(encoded) > max_bytes:
        raise WatchError(
            f"{name} must be at most {max_bytes} bytes; got {len(encoded)}")
    return value


def cmd_mesh_configure(watch: Watch, args) -> int:
    if args.unpaired_probe:
        passkey = 0
    else:
        raw = read_secret("BLE passkey", 6)
        if not (len(raw) == 6 and raw.isascii() and raw.isdigit()):
            raise WatchError("the BLE passkey is six digits")
        passkey = int(raw)
    watch.mesh_configure(passkey)
    emit(args, {"configured": True}, "MeshCore BLE configured; engineering screen enabled")
    return 0


def cmd_mesh_disconnect(watch: Watch, args) -> int:
    watch.mesh_disconnect()
    emit(args, {"disconnected": True}, "MeshCore BLE stopped")
    return 0


def cmd_mesh_send(watch: Watch, args) -> int:
    try:
        prefix = bytes.fromhex(args.peer)
    except ValueError as exc:
        raise WatchError("peer must be a 12-digit hexadecimal prefix") from exc
    if len(prefix) != 6:
        raise WatchError("peer must be a 12-digit hexadecimal prefix")
    utc_seconds = args.utc_seconds if args.utc_seconds is not None else int(time.time())
    watch.mesh_send(prefix, args.text, utc_seconds)
    emit(args, {"peer": prefix.hex(), "text": args.text,
                "utc_seconds": utc_seconds},
         f"MeshCore message queued for {prefix.hex()}")
    return 0


def cmd_mesh_room_send(watch: Watch, args) -> int:
    try:
        room = bytes.fromhex(args.room)
    except ValueError as exc:
        raise WatchError("room must be a 64-digit hexadecimal public key") from exc
    if len(room) != 32:
        raise WatchError("room must be a 64-digit hexadecimal public key")
    utc_seconds = args.utc_seconds if args.utc_seconds is not None else int(time.time())
    # 15 bytes is the Room Server password ceiling the whole path enforces --
    # link/src/meshcore_companion.cpp kMaxRoomPasswordBytes, and the bridge and
    # board sinks below it. Refusing a longer one here says so to the operator
    # instead of failing opaquely three layers down.
    watch.mesh_room_send(room, read_secret("Room Server password", 15),
                         args.text, utc_seconds)
    emit(args, {"room": room.hex(), "text": args.text, "utc_seconds": utc_seconds},
         f"MeshCore Room Server message queued for {room.hex()}")
    return 0


def cmd_button(watch: Watch, args) -> int:
    if args.event == "press":
        watch.button_press(args.name)
    elif args.event == "release":
        watch.button_release(args.name)
    elif args.event == "hold":
        watch.button_hold(args.name, args.duration if args.duration else 1.0)
    else:
        watch.button_click(args.name, args.duration if args.duration else 0.05)
    return after_action(watch, args, f"button {args.name} {args.event}", f"button-{args.name}")


def cmd_tap(watch: Watch, args) -> int:
    watch.tap(args.x, args.y)
    return after_action(watch, args, f"tap {args.x},{args.y}", "tap")


def cmd_long_tap(watch: Watch, args) -> int:
    watch.long_tap(args.x, args.y, args.duration)
    return after_action(watch, args, f"long tap {args.x},{args.y} for {args.duration}s", "long-tap")


def cmd_double_tap(watch: Watch, args) -> int:
    watch.double_tap(args.x, args.y)
    return after_action(watch, args, f"double tap {args.x},{args.y}", "double-tap")


def cmd_swipe(watch: Watch, args) -> int:
    watch.swipe(args.start, args.end, args.duration, args.steps)
    return after_action(watch, args,
                        f"swipe {args.start[0]},{args.start[1]} -> "
                        f"{args.end[0]},{args.end[1]} in {args.duration}s", "swipe")


def cmd_drag(watch: Watch, args) -> int:
    watch.drag(args.start, args.end, args.duration, args.steps)
    return after_action(watch, args,
                        f"drag {args.start[0]},{args.start[1]} -> "
                        f"{args.end[0]},{args.end[1]} in {args.duration}s", "drag")


def cmd_gesture(watch: Watch, args) -> int:
    # Through `scenario` rather than `int()` on each coordinate, so that a
    # gesture file obeys the same pixels-or-fractions rule a scenario step
    # does and refuses in the same sentences. Both were true of the scenario
    # runner and neither was true here: `int(pt[0])` turned `[0.5, 0.5]` into a
    # silent tap on (0, 0), and every parse failure was a traceback.
    points, duration = scenario_mod.load_gesture(args.file, watch)
    watch.gesture(points, duration)
    return after_action(watch, args, f"gesture of {len(points)} points", "gesture")


def cmd_input_reset(watch: Watch, args) -> int:
    released, still_held = watch.input_reset()
    # Three outcomes, not two. The device releases an input only if the event
    # reached its queue, so a `released` of 0 over a `still_held` of 2 is the
    # stalled interface this command exists for -- and printing "nothing was
    # held" there would be the escape hatch reporting success for the state
    # that made it necessary.
    if still_held:
        line = (f"released {released} stuck input(s), and {still_held} could not be "
                f"released -- the interface queue is full, so the device kept them "
                f"held rather than stranding a pressed widget. Its hold expiry "
                f"retries; try again once the interface drains")
    elif released:
        line = f"released {released} stuck input(s)"
    else:
        line = "nothing was held"
    emit(args, {"released": released, "still_held": still_held}, line)
    # A command whose whole job is to leave nothing held has not done it.
    return 1 if still_held else 0


def cmd_run(watch: Watch, args) -> int:
    steps = scenario_mod.load(args.file)
    output_dir = args.output or os.path.join(
        DEFAULT_OUTPUT_DIR, os.path.splitext(os.path.basename(args.file))[0])

    def announce(result):
        if args.json:
            return
        # Three outcomes, not two. A step this board cannot run reads as
        # `skip`, so the line does not claim a press that never happened.
        mark = "FAIL" if not result.ok else ("skip" if result.skipped else "ok  ")
        line = f"  {mark} {result.index:2d} {result.action}"
        if result.detail:
            line += f"  -- {result.detail}"
        if result.screenshot:
            line += f"\n         {result.screenshot}"
        print(line)

    if not args.json:
        print(f"running {args.file} ({len(steps)} steps) against {watch.describe()}")
    report = scenario_mod.run(watch, steps, output_dir, on_step=announce)

    # Whatever happened, put the inputs back. A scenario that failed halfway
    # through a swipe leaves a finger down, and the next run would start with
    # the screen already being pressed.
    try:
        _, still_held = watch.input_reset()
        if still_held:
            print(f"warning: {still_held} input(s) are still held -- the interface "
                  f"queue was full and the device kept them rather than stranding a "
                  f"pressed widget", file=sys.stderr)
    except WatchError:
        pass

    if args.json:
        print(json.dumps({
            "scenario": os.path.abspath(args.file),
            "ok": report.ok,
            "artefacts": report.artefacts,
            "steps": [vars(step) for step in report.steps],
        }, indent=2))
    else:
        ran = sum(1 for s in report.steps if s.ok and not s.skipped)
        skipped = sum(1 for s in report.steps if s.skipped)
        print(f"{'PASSED' if report.ok else 'FAILED'}: "
              f"{ran}/{len(steps)} steps"
              f"{f', {skipped} skipped' if skipped else ''}, "
              f"{len(report.artefacts)} image(s) in {os.path.abspath(output_dir)}")
    # `report.steps` as well as `report.ok`: a run that executed nothing must
    # not exit 0. `load()` now refuses an empty scenario outright, so this is
    # the second lock on the same door.
    return 0 if (report.ok and report.steps) else 1


LIVE_HELP = """commands
  info                       what the device says it is
  shot [name]                screenshot; prints the path
  series <count> [interval]  a burst of screenshots, for an animation
  tap <x> <y>                a tap
  long <x> <y> [seconds]     a long tap
  swipe <x1> <y1> <x2> <y2> [seconds]
  drag  <x1> <y1> <x2> <y2> [seconds]
  press <button> / release <button> / click <button> / hold <button> [seconds]
  reset                      release everything stuck
  auto on|off                take a screenshot after every action
  delay <seconds>            wait this long before the automatic screenshot
  wait <seconds>
  help / quit
"""


def cmd_live(watch: Watch, args) -> int:
    """One connection, many commands.

    The handshake happens once. Reconnecting per action would cost a
    disconnect -- and a disconnect releases everything held, so a `press`
    followed by a `release` in two processes could never test a real hold.
    """
    auto = bool(args.screenshot_after)
    delay = float(args.delay or 0.15)
    print(f"connected to {watch.describe()}. 'help' for commands, 'quit' to leave.")
    if auto:
        print(f"automatic screenshot after each action, {delay}s later")

    while True:
        try:
            line = input("watch> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            break
        if not line:
            continue
        parts = line.split()
        verb, rest = parts[0].lower(), parts[1:]

        try:
            if verb in ("quit", "exit", "q"):
                break
            if verb in ("help", "?"):
                print(LIVE_HELP)
                continue
            if verb == "info":
                cmd_info(watch, argparse.Namespace(json=False))
                continue
            if verb == "auto":
                auto = not rest or rest[0] != "off"
                print(f"automatic screenshot {'on' if auto else 'off'}")
                continue
            if verb == "delay":
                delay = float(rest[0])
                print(f"screenshot delay {delay}s")
                continue
            if verb == "wait":
                time.sleep(float(rest[0]))
                continue
            if verb in ("shot", "screenshot"):
                path = os.path.join(DEFAULT_OUTPUT_DIR, f"{rest[0]}.png") if rest else None
                absolute, shot = watch.save_screenshot(path or default_shot_path())
                print(f"{absolute}  ({shot.width}x{shot.height})")
                continue
            if verb == "series":
                count = int(rest[0]) if rest else 3
                interval = float(rest[1]) if len(rest) > 1 else 0.2
                shots = take_screenshots(watch, argparse.Namespace(
                    count=count, interval=interval, output=None, json=False), "series")
                print_shots(argparse.Namespace(json=False), shots)
                continue
            if verb == "reset":
                released, still_held = watch.input_reset()
                print(f"released {released}" +
                      (f", {still_held} still held (interface queue full)" if still_held else ""))
                continue

            if verb == "tap":
                watch.tap(int(rest[0]), int(rest[1]))
            elif verb == "long":
                watch.long_tap(int(rest[0]), int(rest[1]),
                               float(rest[2]) if len(rest) > 2 else 1.0)
            elif verb in ("swipe", "drag"):
                seconds = float(rest[4]) if len(rest) > 4 else (0.4 if verb == "swipe" else 1.0)
                watch.swipe((int(rest[0]), int(rest[1])), (int(rest[2]), int(rest[3])), seconds)
            elif verb == "press":
                watch.button_press(rest[0])
            elif verb == "release":
                watch.button_release(rest[0])
            elif verb == "click":
                watch.button_click(rest[0])
            elif verb == "hold":
                watch.button_hold(rest[0], float(rest[1]) if len(rest) > 1 else 1.0)
            else:
                print(f"unknown command '{verb}'. 'help' lists them.")
                continue

            if auto:
                time.sleep(delay)
                absolute, shot = watch.save_screenshot(default_shot_path(verb))
                print(f"{absolute}  ({shot.width}x{shot.height})")
        except (WatchError, p.ProtocolError) as exc:
            print(f"error: {exc}")
        except (IndexError, ValueError):
            print(f"'{verb}' needs different arguments. 'help' lists them.")
    return 0


def after_action(watch: Watch, args, description: str, prefix: str) -> int:
    """Report the action, and photograph the result if asked.

    `--screenshot-after` is the flag that turns a command into one step of a
    loop: act, then look. Without it an agent has to run two processes, and the
    second one's connect would have released whatever the first one was holding.
    """
    if not getattr(args, "screenshot_after", False):
        emit(args, {"action": description, "ok": True}, description)
        return 0

    time.sleep(float(getattr(args, "delay", None) or 0.15))
    shots = take_screenshots(watch, args, prefix)
    if args.json:
        print(json.dumps({"action": description, "ok": True, "screenshots": shots}, indent=2))
    else:
        print(description)
        print_shots(args, shots)
    return 0


# --- argument parsing -----------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="watch_control.py",
        description="Screenshot a watch, press its buttons, touch its screen, and look "
                    "at what happened.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="With no --port and no --socket the tool first resolves the bench watch "
               "by USB serial, then falls back to the simulator sockets.")
    parser.add_argument("--port", help="a serial device; overrides USB-serial lookup")
    # No default here on purpose. A default is indistinguishable from a value
    # the operator typed, and the fallback below has to tell them apart: a
    # named device that is not present is an error, an unnamed one is the
    # documented simulator fallback. Issue #267.
    parser.add_argument("--serial",
                        help="USB serial of the watch (default: $ATTADIPA_WATCH_SERIAL, "
                             f"else {DEFAULT_SERIAL})")
    parser.add_argument("--socket", dest="socket_path", help="a Unix socket, for the simulator")
    parser.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for a reply")
    parser.add_argument("--json", action="store_true", help="machine-readable output")

    subparsers = parser.add_subparsers(dest="command", required=True)

    def with_screenshot(sub):
        sub.add_argument("--screenshot-after", action="store_true",
                         help="photograph the result and print its path")
        sub.add_argument("--delay", type=float, help="seconds to wait before that screenshot")
        sub.add_argument("--output", "-o", help="where to write it")
        sub.add_argument("--count", type=int, default=1)
        sub.add_argument("--interval", type=float, default=0.2)
        return sub

    info = subparsers.add_parser("info", help="what the device says it is")
    info.set_defaults(func=cmd_info)

    sync = subparsers.add_parser("sync-time", help="set UTC and local offset from this host")
    sync.add_argument("--utc-seconds", type=int,
                      help="Unix UTC seconds (default: this host's current time)")
    sync.add_argument("--offset-minutes", type=int,
                      help="local UTC offset (default: this host's current offset)")
    sync.add_argument("--valid-for", type=int, default=86400,
                      help="seconds before the synchronization becomes stale (default: 86400)")
    sync.add_argument("--allow-large", action="store_true",
                      help="allow a correction larger than five minutes")
    sync.set_defaults(func=cmd_sync_time)

    mesh_configure = subparsers.add_parser(
        "mesh-configure", help="configure the watch's MeshCore BLE companion link")
    # The passkey is read from the terminal or stdin, never from argv: see
    # read_secret. The unpaired probe carries no secret, so it stays a flag and
    # stays scriptable.
    mesh_configure.add_argument(
        "--unpaired-probe", action="store_true",
        help="run the unpaired diagnostic probe instead of pairing (no passkey)")
    mesh_configure.set_defaults(func=cmd_mesh_configure)

    mesh_disconnect = subparsers.add_parser(
        "mesh-disconnect", help="stop the watch's MeshCore BLE scan and link")
    mesh_disconnect.set_defaults(func=cmd_mesh_disconnect)

    mesh_send = subparsers.add_parser(
        "mesh-send", help="send one private MeshCore message")
    mesh_send.add_argument("--peer", required=True,
                           help="the target contact's 12-digit public-key prefix")
    mesh_send.add_argument("--text", required=True)
    mesh_send.add_argument("--utc-seconds", type=int,
                           help="Unix UTC seconds (default: this host's current time)")
    mesh_send.set_defaults(func=cmd_mesh_send)

    mesh_room_send = subparsers.add_parser(
        "mesh-room-send", help="log in to one MeshCore Room Server and send one message")
    mesh_room_send.add_argument("--room", required=True,
                                help="the Room Server's 64-digit public key")
    mesh_room_send.add_argument("--text", required=True)
    mesh_room_send.add_argument("--utc-seconds", type=int,
                                help="Unix UTC seconds (default: this host's current time)")
    mesh_room_send.set_defaults(func=cmd_mesh_room_send)

    shot = subparsers.add_parser("screenshot", help="one image, or a series")
    shot.add_argument("--output", "-o", help="path for the PNG")
    shot.add_argument("--count", type=int, default=1, help="how many, for an animation")
    shot.add_argument("--interval", type=float, default=0.2, help="seconds between them")
    shot.set_defaults(func=cmd_screenshot)

    button = with_screenshot(subparsers.add_parser("button", help="press a physical button"))
    button.add_argument("name")
    button.add_argument("event", choices=["press", "release", "click", "hold"])
    button.add_argument("--duration", type=float, help="seconds, for click and hold")
    button.set_defaults(func=cmd_button)

    tap = with_screenshot(subparsers.add_parser("tap"))
    tap.add_argument("--x", type=int, required=True)
    tap.add_argument("--y", type=int, required=True)
    tap.set_defaults(func=cmd_tap)

    long_tap = with_screenshot(subparsers.add_parser("long-tap"))
    long_tap.add_argument("--x", type=int, required=True)
    long_tap.add_argument("--y", type=int, required=True)
    long_tap.add_argument("--duration", type=float, default=1.0)
    long_tap.set_defaults(func=cmd_long_tap)

    double_tap = with_screenshot(subparsers.add_parser("double-tap"))
    double_tap.add_argument("--x", type=int, required=True)
    double_tap.add_argument("--y", type=int, required=True)
    double_tap.set_defaults(func=cmd_double_tap)

    for name, default_duration, handler in (("swipe", 0.4, cmd_swipe), ("drag", 1.0, cmd_drag)):
        sub = with_screenshot(subparsers.add_parser(name))
        sub.add_argument("--from", dest="start", type=parse_point, required=True,
                         metavar="X,Y")
        sub.add_argument("--to", dest="end", type=parse_point, required=True, metavar="X,Y")
        sub.add_argument("--duration", type=float, default=default_duration)
        sub.add_argument("--steps", type=int, default=0,
                         help="intermediate points; 0 picks about 60 per second")
        sub.set_defaults(func=handler)

    gesture = with_screenshot(subparsers.add_parser("gesture", help="a path from a file"))
    gesture.add_argument("--file", required=True)
    gesture.set_defaults(func=cmd_gesture)

    reset = subparsers.add_parser("input-reset",
                                  help="release anything stuck down (section 10's escape hatch)")
    reset.set_defaults(func=cmd_input_reset)

    run = subparsers.add_parser("run", help="a scenario file")
    run.add_argument("file")
    run.add_argument("--output", "-o", help="directory for the screenshots")
    run.set_defaults(func=cmd_run)

    live = subparsers.add_parser("live", help="an interactive session on one connection")
    live.add_argument("--screenshot-after", action="store_true")
    live.add_argument("--delay", type=float)
    live.set_defaults(func=cmd_live)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if not args.port and not args.socket_path:
        # Naming a device and getting a different one is the failure this
        # guards. `resolve_port` raises SystemExit with the precise reason --
        # no by-id directory, no such serial, an ambiguous match -- and
        # swallowing it dropped the operator onto whatever simulator happened
        # to be listening, silently and with an exit code of 0.
        named = args.serial or os.environ.get("ATTADIPA_WATCH_SERIAL")
        try:
            args.port = resolve_port(named or DEFAULT_SERIAL)
        except SystemExit as exc:
            if named is not None:
                print(f"error: {exc}", file=sys.stderr)
                return 2
            # Nobody named a device, so the bench watch was a guess. Falling
            # back to the simulator is what the epilog promises.
    try:
        watch = connect(port=args.port, socket_path=args.socket_path, timeout=args.timeout)
    except (WatchError, p.ProtocolError) as exc:
        # `ProtocolError` as well as `WatchError`: the handshake is where a
        # `VersionMismatch` reply lands, and that is a typed refusal. Catching
        # only `WatchError` here turned the one error this tool has the
        # friendliest message for into a traceback.
        print(f"error: {exc}", file=sys.stderr)
        return 2

    try:
        return args.func(watch, args)
    except (WatchError, p.ProtocolError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(file=sys.stderr)
        return 130
    finally:
        # Never leave a finger down because the tool exited. The device does
        # this too on a dropped connection; doing it here as well means an
        # ordinary exit does not depend on the disconnect being noticed.
        try:
            _, still_held = watch.input_reset()
            # Saying nothing here is how the tool exits 0 having left a finger
            # down: the comment above promises the opposite, and the state that
            # defeats the promise is the one nobody is watching for.
            if still_held:
                print(f"warning: exiting with {still_held} input(s) still held -- the "
                      f"interface queue was full. The device retries on its own hold "
                      f"expiry; a reconnect will also clear them", file=sys.stderr)
        except Exception:
            pass
        try:
            watch.close()
        except WatchError as exc:
            print(f"warning: {exc}", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
