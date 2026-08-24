"""The closed loop, end to end, with nothing mocked.

Starts the simulator, connects the real host tool to it over a real socket,
injects real input through the real LVGL input path, pulls the real framebuffer
back and writes real PNGs -- then checks the pictures.

This is the test that catches what the unit tests structurally cannot. Both
sides of the protocol are pinned to fixed bytes, the state machine is exercised
without a transport, and the pixel conversions are checked against known values;
none of that would notice if the simulator forgot to register the input device,
or drained the queue too eagerly and collapsed a swipe, or handed back a
framebuffer with the wrong stride.

  python3 tools/watch/e2e_test.py <path-to-attadipa_sim>

The assertions on the images are deliberately about *structure*, not
appearance -- corner pixels, colour separation, how many pixels a swipe left
behind. A pixel-exact expectation would fail on an antialiased glyph and be
switched off within a week. Whether the screen looks *right* is for a person or
an agent to decide by opening the file, which is the whole point of the
mechanism.
"""

from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from watch import protocol as p             # noqa: E402
from watch import scenario as scenario_mod  # noqa: E402
from watch.client import WatchError, connect  # noqa: E402

failures: list[str] = []
skipped: list[str] = []


def check(condition: bool, what: str) -> None:
    if not condition:
        failures.append(what)
    print(f"  {'ok  ' if condition else 'FAIL'} {what}")


def skip(what: str, why: str) -> None:
    """Record coverage that did not run, and say why.

    A board that does not offer a capability is not a board that passed the
    test for it. This prints and accumulates separately from `check` so the
    run neither goes red nor quietly reads as if the coverage happened.
    """
    skipped.append(f"{what} -- {why}")
    print(f"  skip {what} -- {why}")



def lvgl_clicks(watch, workdir, name):
    """How many clicks LVGL dispatched, counted off the diagnostic screen.

    The screen draws one 8x8 marker per click in a colour it uses nowhere else,
    so this is an exact count from the PNG -- no OCR, and no new wire field to
    carry a number the screen already shows. Same trick as the touch trail, for
    the same reason.

    This is the only assertion in this file that observes the **interface**
    rather than the transport. Everything else here would still pass on an
    input path that delivered every event perfectly and then merged them before
    a widget saw them, which is exactly the defect that shipped.
    """
    path = os.path.join(workdir, name)
    watch.save_screenshot(path)
    _, _, pixels = read_png(path)
    marked = 0
    for index in range(0, len(pixels), 3):
        r, g, b = pixels[index], pixels[index + 1], pixels[index + 2]
        if 0x80 <= r <= 0xA0 and 0x80 <= g <= 0xA0 and b >= 0xF0:
            marked += 1
    # 8x8 per marker; integer-divide so antialiasing at the edges cannot
    # promote seven markers into eight.
    return marked // 64


# The screen draws at most this many markers -- `diagnostic_screen.cpp`. Past it
# the row saturates and two counts differenced report *no* clicks rather than
# too many, so the difference below has to know where it stops being arithmetic.
CLICK_MARK_CAP = 8


def read_png(path: str) -> tuple[int, int, bytes]:
    """Decode our own PNG without an image library, to check what was written."""
    raw = Path(path).read_bytes()
    assert raw[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    width = int.from_bytes(raw[16:20], "big")
    height = int.from_bytes(raw[20:24], "big")

    at, idat = 8, b""
    while at < len(raw):
        length = int.from_bytes(raw[at:at + 4], "big")
        if raw[at + 4:at + 8] == b"IDAT":
            idat += raw[at + 8:at + 8 + length]
        at += 12 + length

    decoded = zlib.decompress(idat)
    stride = width * 3 + 1
    rows = [decoded[y * stride + 1:(y + 1) * stride] for y in range(height)]
    return width, height, b"".join(rows)


def pixel(rgb: bytes, width: int, x: int, y: int) -> tuple[int, int, int]:
    at = (y * width + x) * 3
    return rgb[at], rgb[at + 1], rgb[at + 2]


def _socket_path_is_not_a_scratch_pad(simulator: str, board: str) -> None:
    """`--debug-socket` used to delete whatever was at the path, then bind.

    Two ways that went wrong, and both are here because the fix is one `stat`
    and the failure is somebody's file. The first is a mistyped path: the
    simulator unlinked a regular file, silently, and the CLAUDE.md rule about
    looking at a target before deleting it is not suspended because the caller
    typed the path. The second is the *documented* usage -- one socket path,
    both boards -- where the second simulator unlinked the first's inode and
    bound its own, leaving the first alive, still printing that it was
    listening, and unreachable for the rest of its life.
    """
    environment = dict(os.environ, SDL_VIDEODRIVER="dummy")
    with tempfile.TemporaryDirectory() as workdir:
        keepme = os.path.join(workdir, "keepme.txt")
        Path(keepme).write_text("a file that is not a socket\n")

        refused = subprocess.run(
            [simulator, "--board", board, "--debug-socket", keepme],
            capture_output=True, text=True, env=environment, timeout=60)
        check(Path(keepme).exists(), "a non-socket path is not deleted")
        check(Path(keepme).read_text() == "a file that is not a socket\n",
              "and it is not overwritten either")
        check(refused.returncode != 0, "and the simulator exits non-zero")
        check("not a socket" in (refused.stdout + refused.stderr),
              f"and says why: {(refused.stdout + refused.stderr)[-200:]!r}")

        # Now the two-simulator case, on a path the first one is serving.
        taken = os.path.join(workdir, "taken.sock")
        log_path = os.path.join(workdir, "first.log")
        with open(log_path, "wb") as log:
            first = subprocess.Popen(
                [simulator, "--board", board, "--debug-socket", taken],
                stdout=log, stderr=subprocess.STDOUT, env=environment)
            try:
                deadline = time.monotonic() + 30
                while not os.path.exists(taken) and time.monotonic() < deadline:
                    if first.poll() is not None:
                        break
                    time.sleep(0.05)
                check(os.path.exists(taken), "the first simulator listens")
                before = os.stat(taken)

                second = subprocess.run(
                    [simulator, "--board", board, "--debug-socket", taken],
                    capture_output=True, text=True, env=environment, timeout=60)
                check(second.returncode != 0, "a second simulator on the same path is refused")
                check("already served" in (second.stdout + second.stderr),
                      f"and says so: {(second.stdout + second.stderr)[-200:]!r}")

                after = os.stat(taken)
                check((before.st_dev, before.st_ino) == (after.st_dev, after.st_ino),
                      "and the first simulator still owns the inode it bound")
                check(first.poll() is None, "and is still running")
            finally:
                first.terminate()
                try:
                    first.wait(timeout=10)
                except subprocess.TimeoutExpired:      # pragma: no cover
                    first.kill()

        # The first was killed rather than closed, so its socket file outlives
        # it -- which is the case the unlink exists for in the first place. A
        # third simulator must now take the path, because nothing is serving it:
        # `connect` gets ECONNREFUSED, which is the only signal that tells a
        # stale inode from a live one.
        check(os.path.exists(taken), "a killed simulator leaves its socket behind")
        third_log = os.path.join(workdir, "third.log")
        with open(third_log, "wb") as log:
            third = subprocess.Popen(
                [simulator, "--board", board, "--debug-socket", taken],
                stdout=log, stderr=subprocess.STDOUT, env=environment)
            try:
                # Not asserted by inode: /tmp is tmpfs here and reuses a freed
                # inode number immediately, so the replacement socket routinely
                # lands on the same one. What is being tested is that the stale
                # entry did not turn into a refusal, so the assertion is that it
                # bound and said so.
                deadline = time.monotonic() + 30
                listening = False
                while time.monotonic() < deadline and third.poll() is None:
                    if "listening on" in Path(third_log).read_text(errors="replace"):
                        listening = True
                        break
                    time.sleep(0.05)
                check(third.poll() is None, "and a stale one does not block the next simulator")
                check(listening, "which binds the path and says so")
            finally:
                third.terminate()
                try:
                    third.wait(timeout=10)
                except subprocess.TimeoutExpired:      # pragma: no cover
                    third.kill()


def run(simulator: str, board: str = "waveshare-amoled-206") -> int:
    print("socket path")
    _socket_path_is_not_a_scratch_pad(simulator, board)

    with tempfile.TemporaryDirectory() as workdir:
        socket_path = os.path.join(workdir, "sim.sock")
        log_path = os.path.join(workdir, "sim.log")

        environment = dict(os.environ, SDL_VIDEODRIVER="dummy")
        with open(log_path, "wb") as log:
            process = subprocess.Popen(
                [simulator, "--board", board, "--diagnostic", "--debug-socket", socket_path],
                stdout=log, stderr=subprocess.STDOUT, env=environment)
            try:
                return _drive(process, socket_path, workdir, board, log_path)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:      # pragma: no cover
                    process.kill()


def _drive(process, socket_path: str, workdir: str, board: str, log_path: str) -> int:
    deadline = time.monotonic() + 30
    while not os.path.exists(socket_path):
        if process.poll() is not None:
            print(Path(log_path).read_text(errors="replace"), file=sys.stderr)
            print("the simulator exited before it listened", file=sys.stderr)
            return 1
        if time.monotonic() > deadline:
            print("the simulator never created its socket", file=sys.stderr)
            return 1
        time.sleep(0.05)

    watch = connect(socket_path=socket_path, timeout=30.0)
    try:
        caps = watch.capabilities
        hello = watch.hello
        assert caps and hello

        print("handshake")
        check(hello.board_id == board, f"the device names itself: {hello.board_id}")
        check(hello.build.startswith("sim"),
              f"and says it is a simulator, not hardware: '{hello.build}'")
        check(caps.width > 0 and caps.height > 0,
              f"it reports a panel: {caps.width}x{caps.height}")

        # Everything injected below is in **logical** coordinates, which is what
        # `screen_size()` reports and what `core/input.h` says an injected point
        # already is. `caps.width/height` is the framebuffer, and the two differ
        # by a transpose on DEG90 and DEG270. Both boards report DEG0 today, so
        # the wrong one passed -- the same identity the PNG assertion below
        # refuses to rely on, and there is no reason for this file to assert it
        # in one direction and deny it in the other.
        inject_w, inject_h = watch.screen_size()
        check(caps.max_touch_points == 1,
              "and one touch point -- single touch, asserted about LVGL's pointer device")
        check(len(caps.buttons) >= 1, f"and {len(caps.buttons)} button(s)")

        print("\nscreenshot")
        absolute, shot = watch.save_screenshot(os.path.join(workdir, "01.png"))
        check(os.path.getsize(absolute) > 1000, "a screenshot writes a non-trivial file")
        width, height, rgb = read_png(absolute)
        # `screen_size`, not `caps.width/height`: the PNG has been through
        # `apply_orientation`, which transposes it for DEG90 and DEG270, so
        # comparing it to the framebuffer geometry asserts an identity that
        # only holds while the device reports DEG0. Both boards do today --
        # which is exactly why the wrong comparison passed.
        check((width, height) == watch.screen_size(),
              f"the PNG is the size the device declared: {width}x{height}")

        # The diagnostic screen's corner markers, by colour. This is what
        # catches a rotation or a mirror -- four corners, four colours, each
        # checked where it belongs.
        top_left = pixel(rgb, width, 2, 2)
        top_right = pixel(rgb, width, width - 3, 2)
        bottom_left = pixel(rgb, width, 2, height - 3)
        bottom_right = pixel(rgb, width, width - 3, height - 3)
        check(top_left[0] > 200 and top_left[2] < 80,
              f"the top-left marker is the orange one {top_left} -- not rotated, not mirrored")
        check(top_right[2] > 200 and top_right[0] < 80,
              f"the top-right marker is the blue one {top_right}")
        check(bottom_left[0] > 200 and bottom_left[1] > 150 and bottom_left[2] < 80,
              f"the bottom-left marker is the yellow one {bottom_left}")
        check(bottom_right[1] > 200 and bottom_right[0] < 120,
              f"the bottom-right marker is the green one {bottom_right}")

        # The swatch strip: pure red, green and blue at known positions. A
        # swapped R and B passes every corner check above and fails here.
        strip_y = height - height // 12 - height // 8 + 4
        swatch = width // 6
        red = pixel(rgb, width, swatch // 2, strip_y)
        green = pixel(rgb, width, swatch + swatch // 2, strip_y)
        blue = pixel(rgb, width, 2 * swatch + swatch // 2, strip_y)
        check(red == (255, 0, 0), f"the red swatch is pure red: {red}")
        check(green == (0, 255, 0), f"the green swatch is pure green: {green}")
        check(blue == (0, 0, 255), f"the blue swatch is pure blue: {blue}")

        print("\ninput")
        watch.tap(inject_w // 2, inject_h // 2)
        time.sleep(0.3)
        _, after_tap = watch.save_screenshot(os.path.join(workdir, "02.png"))
        check(after_tap.rgb != rgb, "a tap changes the screen")

        # The assertion that a swipe is a real sequence: the diagnostic screen
        # draws one dot per point it received, so a swipe delivered as a single
        # artificial jump leaves two dots and a real one leaves a line.
        watch.swipe((int(inject_w * 0.85), int(inject_h * 0.25)),
                    (int(inject_w * 0.15), int(inject_h * 0.80)), duration=0.5)
        time.sleep(0.3)
        swipe_path = os.path.join(workdir, "03.png")
        watch.save_screenshot(swipe_path)
        width, height, swiped = read_png(swipe_path)

        trail = 0
        for index in range(0, len(swiped), 3):
            r, g, b = swiped[index], swiped[index + 1], swiped[index + 2]
            if r < 80 and g > 200 and b > 150:   # the trail's cyan
                trail += 1
        # Each point is a 6x6 dot, so a genuine multi-point swipe leaves far
        # more than two dots' worth of pixels.
        #
        # **What this proves and what it does not.** The trail is drawn by the
        # queue-drain listener, so it proves the transport delivered every
        # point of a real down/move/up. It says nothing about what LVGL then
        # made of them -- and those genuinely came apart: an input path that
        # merged two taps into one click left a trail showing both. The next
        # check is the one that watches the interface.
        check(trail > 6 * 6 * 4,
              f"the swipe left a trail of {trail} pixels -- the transport delivered "
              f"a real down/move/up, not one artificial jump")

        print("\nwhat LVGL actually received")
        # Two taps with no gap between them, which is the case that used to
        # collapse. LVGL reads its devices every 33 ms; the simulator loop runs
        # at 5 ms, so both taps land inside one read window and a naive
        # one-state-per-read handoff reports a single click at the second tap's
        # coordinates. The diagnostic screen counts LVGL's own LV_EVENT_CLICKED,
        # so the interface's view is legible in a screenshot.
        before_clicks = lvgl_clicks(watch, workdir, "06-before.png")
        watch.tap(int(inject_w * 0.30), int(inject_h * 0.45))
        watch.tap(int(inject_w * 0.70), int(inject_h * 0.45))
        time.sleep(0.4)
        after_clicks = lvgl_clicks(watch, workdir, "06-after.png")
        check(after_clicks < CLICK_MARK_CAP,
              f"the click marker row has room left ({after_clicks} of "
              f"{CLICK_MARK_CAP}) -- at the cap the difference below stops "
              f"counting and starts reporting zero")
        check(after_clicks - before_clicks == 2,
              f"two taps with no gap produced {after_clicks - before_clicks} LVGL "
              f"click(s), and must produce 2 -- one would mean the input path "
              f"merged them before any widget saw them")

        print("\nwhat wait_stable is answering about")
        # The half of `stable_since` that only the simulator can reach. Its
        # other two terms -- the bridge's own queue, and LVGL's idle timer --
        # are pinned in `test_debug.cpp`; the third, `remote_input_pending()`,
        # cannot be, because `FakeScreen` has no transition FIFO. Delete that
        # term and every host test stays green.
        #
        # The shape below is the one that fails without it. A screenshot takes
        # long enough that LVGL is already idle past `quiet_ms` before the tap,
        # so the idle timer answers "settled" on the first ask -- while the tap
        # is still sitting between `remote_input_pump` and the 33 ms indev read
        # (the loop runs at 5 ms with a client attached). The tour's own
        # `wait_stable` cannot catch it: it follows a `long_tap`, and a held
        # pointer restamps the idle timer on every read.
        #
        # Counted off LVGL's own LV_EVENT_CLICKED rather than off the touch
        # trail, because the trail is drawn by the drain listener before LVGL
        # reads anything -- it would show the dot either way.
        stable_before = lvgl_clicks(watch, workdir, "07-before.png")
        time.sleep(0.5)
        watch.tap(int(inject_w * 0.50), int(inject_h * 0.45))
        settled = watch.wait_stable(300)
        stable_after = lvgl_clicks(watch, workdir, "07-after.png")
        check(settled, "wait_stable settled within its timeout after a bare tap")
        check(stable_after < CLICK_MARK_CAP,
              f"the click marker row still has room ({stable_after} of "
              f"{CLICK_MARK_CAP})")
        check(stable_after - stable_before == 1,
              f"a tap followed by wait_stable produced "
              f"{stable_after - stable_before} LVGL click(s) by the time the wait "
              f"returned, and must produce 1 -- zero means wait_stable answered "
              f"about LVGL's idle timer while the tap was still in flight")

        print("\nbuttons")
        check(bool(caps.buttons), "the board declares at least one button")
        injectable = [b for b in caps.buttons if b.injectable]
        if not injectable:
            # Not a failure and not a pass. `injectable` says the harness may
            # synthesise a press for this button, and the Waveshare declares
            # neither of its two -- D5 in HARDWARE_MATRIX leaves it open
            # whether Key1 is brought out at all. Asserting one anyway would
            # have this test fail on a fact about the board rather than on a
            # fault in the bridge.
            skip("a button press round-trips through the bridge",
                 f"{board} declares no injectable button")
        else:
            watch.button_click(injectable[0].id, 0.05)
            time.sleep(0.3)
            _, clicked = watch.save_screenshot(os.path.join(workdir, "04.png"))
            watch.button_hold(injectable[0].id, 0.9)
            time.sleep(0.3)
            _, held = watch.save_screenshot(os.path.join(workdir, "05.png"))
            # The screen prints the measured hold, so a click and a hold cannot
            # look the same. If they do, the duration never reached the device.
            check(clicked.rgb != held.rgb,
                  "a click and a nine-hundred-millisecond hold produce different screens")

        print("\nrefusals")
        try:
            watch.button_press("no-such-button")
            check(False, "a button that does not exist was accepted")
        except WatchError as exc:
            check("no button called" in str(exc), f"an unknown button is refused: {exc}")
        try:
            watch.tap(inject_w + 100, 0)
            check(False, "a coordinate off the screen was accepted")
        except WatchError as exc:
            check("outside the" in str(exc), f"a coordinate off the screen is refused: {exc}")
        try:
            watch._event(p.EventType.POINTER_UP, x=1, y=1)
            check(False, "a release with nothing held was accepted")
        except p.ProtocolError as exc:
            check(exc.code is p.ErrorCode.BAD_INPUT,
                  f"a release with nothing held is a typed error: {exc}")

        print("\nstuck input")
        watch._event(p.EventType.POINTER_DOWN, x=10, y=10)
        released, still_held = watch.input_reset()
        check(released == 1, f"input reset lifted {released} held input")
        check(still_held == 0, f"and left nothing held ({still_held})")
        check(watch.input_reset() == (0, 0), "and running it again is not an error")

        # A disconnect mid-press must lift the finger by itself. Proved from a
        # second connection, because the first one is gone.
        watch._event(p.EventType.POINTER_DOWN, x=20, y=20)
        watch._transport.close()
        time.sleep(0.5)
        watch = connect(socket_path=socket_path, timeout=30.0)
        check(watch.input_reset() == (0, 0),
              "a dropped connection released its own held finger, without being asked")

        print("\nscenario")
        tour = HERE.parent.parent / "tests" / "ui" / "scenarios" / "diagnostic_tour.yaml"
        if tour.exists():
            try:
                steps = scenario_mod.load(str(tour))
            except WatchError as exc:
                if "PyYAML" not in str(exc):
                    raise
                # A failure, not a skip. Elsewhere this branch registers a
                # failing test when a dependency is missing rather than
                # printing a line nobody reads (`tests/CMakeLists.txt`), and
                # the shipped scenario is the only end-to-end coverage the
                # tour has. A silent skip is how "23/23 on both boards" stops
                # meaning anything without the number changing.
                check(False, "the scenario is YAML and PyYAML is absent -- "
                             "install it (python3-yaml) or this test proves nothing")
            else:
                report = scenario_mod.run(watch, steps, os.path.join(workdir, "tour"))
                detail = "; ".join(f"{s.action}: {s.detail}" for s in report.steps if not s.ok)
                check(report.ok, f"the shipped scenario passes ({len(report.steps)} steps) {detail}")
                check(len(report.artefacts) >= 5,
                      f"and leaves {len(report.artefacts)} images to look at")
    finally:
        try:
            watch.close()
        except Exception:  # noqa: BLE001
            pass

    if skipped:
        print(f"\nnot executed ({len(skipped)}):")
        for one in skipped:
            print(f"  * {one}")
    if failures:
        print(f"\nend-to-end FAILED ({len(failures)}):", file=sys.stderr)
        for failure in failures:
            print(f"  * {failure}", file=sys.stderr)
        return 1
    print("\nend-to-end: the loop closes. Screenshot, inject, look, repeat.")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: e2e_test.py <path-to-attadipa_sim> [board]", file=sys.stderr)
        sys.exit(2)
    sys.exit(run(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "waveshare-amoled-206"))
