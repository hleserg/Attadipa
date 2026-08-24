"""A list of steps, run against a watch.

Deliberately small. The request says not to build a large test framework when
the project already has one, and this project's test runner is CTest -- so a
scenario is a *data file* that CTest can point at, not a framework with its own
lifecycle, fixtures and assertions.

A step is a dict with an action and its arguments:

    - action: screenshot
      name: settings-opened
    - action: tap
      x: 120
      y: 180
    - action: wait
      seconds: 0.4
    - action: expect_screen_changed
      since: settings-opened

Checkpoints are named, screenshots are written beside the scenario's output
directory under that name, and the run stops at the first failure with the
artefacts written so far -- a scenario that deleted its own evidence on failure
would be useless exactly when it mattered.

The comparisons are deliberately weak: `expect_screen_changed` and
`expect_screen_same`. Anything stronger is a pixel-exact expectation, and a
pixel-exact expectation against a live device fails on an antialiased glyph and
teaches everybody to ignore it. What decides whether the screen is *right* is a
person or an agent looking at the PNG, which is what the whole mechanism exists
for.
"""

from __future__ import annotations

import json
import os
import time
from dataclasses import dataclass, field

from .client import Watch, WatchError
from .protocol import ProtocolError


@dataclass
class StepResult:
    index: int
    action: str
    ok: bool
    detail: str = ""
    screenshot: str | None = None
    elapsed_ms: int = 0
    # A step that could not apply to this board, as opposed to one that ran.
    # Its own field rather than an `ok` with a note, because a skipped step that
    # counts as a pass is how a scenario reports coverage it never had -- the
    # failure this repository keeps finding in its own tests.
    skipped: bool = False


@dataclass
class Report:
    steps: list[StepResult] = field(default_factory=list)
    artefacts: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return all(step.ok for step in self.steps)


def read_document(path: str):
    """The parsed contents of a scenario or gesture file, or a `WatchError`.

    Shared so that every file this tool reads refuses in the same sentence.
    `gesture --file` used to open, parse and index the document itself, with
    none of the four failures below guarded, so a missing file or a mis-keyed
    document came out as a traceback while the scenario runner next door had a
    sentence for each.
    """
    try:
        with open(path, "r", encoding="utf-8") as handle:
            text = handle.read()
    except OSError as exc:
        raise WatchError(f"{path} could not be read: {exc.strerror}") from exc
    if path.endswith((".yaml", ".yml")):
        try:
            import yaml  # type: ignore
        except ImportError as exc:
            raise WatchError(
                f"{path} is YAML and PyYAML is not installed. Either "
                f"`pip install pyyaml` or write it as JSON -- the "
                f"structure is identical.") from exc
        try:
            return yaml.safe_load(text)
        except yaml.YAMLError as exc:
            raise WatchError(f"{path} is not valid YAML: {exc}") from exc
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise WatchError(
            f"{path} is not valid JSON at line {exc.lineno}: {exc.msg}") from exc


def load_gesture(path: str, watch: Watch | None = None):
    """The points and duration of a gesture file, resolved against the panel.

    Returns `(points, duration)`. Coordinates follow the same rule as a
    scenario step -- whole numbers are pixels, a value strictly between 0 and 1
    or a string ending in `%` is a fraction of the screen the device reported.
    That rule is the whole reason this goes through `resolve_point` rather than
    `int()`: the shipped `example.json` was written in Waveshare pixels, three
    of its five points are off the edge of a 240x240 T-Watch, and both
    `WATCH_CONTROL.md` and the skill point at it under a heading saying to check
    both geometries.
    """
    data = read_document(path)
    if isinstance(data, dict):
        if "points" not in data:
            raise WatchError(
                f"{path} has no 'points' list; its top-level keys are: "
                f"{', '.join(sorted(map(str, data))) or '(none)'}")
        points = data["points"]
        try:
            duration = float(data.get("duration", 0.5))
        except (TypeError, ValueError) as exc:
            raise WatchError(
                f"{path} has a duration that is not a number: "
                f"{data.get('duration')!r}") from exc
    else:
        points, duration = data, 0.5
    if not isinstance(points, list) or not points:
        raise WatchError(f"{path} does not contain a non-empty list of points")
    return [resolve_point(point, watch) for point in points], duration


def load(path: str) -> list[dict]:
    data = read_document(path)

    if isinstance(data, dict):
        if "steps" not in data:
            # `.get("steps", [])` turned a mis-keyed document -- `step:`,
            # `actions:`, a typo -- into zero steps, which then reported
            # success and exited 0. A scenario runner that passes when it ran
            # nothing is the failure mode a scenario runner exists to prevent.
            raise WatchError(
                f"{path} has no 'steps' list; its top-level keys are: "
                f"{', '.join(sorted(map(str, data))) or '(none)'}")
        data = data["steps"]
    if not isinstance(data, list):
        raise WatchError(f"{path} does not contain a list of steps")
    if not data:
        raise WatchError(f"{path} contains no steps")
    return data


def _point(step: dict, key: str, watch: Watch | None = None) -> tuple[int, int]:
    """A coordinate, in pixels or as a fraction of the screen.

    Whole numbers are pixels. A number strictly between 0 and 1 -- or a string
    ending in `%` -- is a fraction of the panel, resolved against what the
    device reported. That is what makes one scenario run on both boards: the
    shipped tour hard-coded `(350, 420)`, which is off the edge of a 240x240
    T-Watch, so it silently only ever ran on the Waveshare while
    `WATCH_CONTROL.md` said "both boards, every time".
    """
    return resolve_point(step[key], watch)


def _axis(value, watch: Watch | None, axis: str) -> int:
    percent = False
    if isinstance(value, str):
        value = value.strip()
        if value.endswith("%"):
            try:
                value = float(value[:-1]) / 100.0
            except ValueError as exc:
                raise WatchError(f"'{value}' is not a percentage") from exc
            # A `%` string says "fraction" in its own syntax, so it takes the
            # fraction path whatever its value -- `"120%"` then resolves past
            # the edge and is refused by name, which is the useful outcome.
            percent = True
        else:
            try:
                value = float(value) if "." in value else int(value)
            except ValueError as exc:
                raise WatchError(
                    f"'{value}' is not a coordinate: whole numbers are pixels, "
                    f"a number between 0 and 1 or a string ending in '%' is a "
                    f"fraction of the panel") from exc
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise WatchError(f"{value!r} is not a coordinate")
    # Inclusive at both ends. It used to be `0.0 < value < 1.0`, which made the
    # two spellings that read as "the far edge" -- `1.0` and `"100%"` -- fall
    # through to `int(1.0)` and land on **pixel 1**. A full-span swipe
    # `to: 1.0,0.5` became a two-pixel twitch at the left edge, the screen
    # changed, and every check downstream passed. The type keeps the two
    # meanings apart on its own: YAML `1` is an `int` and stays a pixel, `1.0`
    # is a `float` and is the edge.
    if percent or (isinstance(value, float) and 0.0 <= value <= 1.0):
        if watch is None:
            raise WatchError(
                f"a fractional coordinate ({value}) needs the device's screen size, "
                f"and this step was resolved without a connection")
        # The *displayed* geometry, the same one `_check_point` bounds against
        # and the same one the PNG is in. Reading it off `caps` directly would
        # put a fraction in the framebuffer's frame on a rotated device, which
        # is the divergence `Watch.screen_size` exists to close.
        width, height = watch.screen_size()
        span = width if axis == "width" else height
        if value <= 1.0:
            # `span` is one past the last pixel, so the far edge is `span - 1`
            # -- and so is everything that *rounds* onto `span`, which is the
            # whole band from `(span - 0.5) / span` up. Mapping only the exact
            # `1.0` left `0.999` and `"99.9%"` resolving to pixel 240 on a
            # 240-wide panel, refused by `_check_point` as outside the screen:
            # the endpoint was fixed and its neighbourhood was not.
            #
            # The clamp is deliberately below 1.0 and not above it. Anything
            # over the full span -- `"120%"` -- still resolves out of bounds and
            # is refused rather than silently pulled back to the edge, because
            # a scenario asking for 120 % of the screen is wrong about the
            # screen and should hear so.
            return min(span - 1, int(round(value * span)))
        return int(round(value * span))
    return int(value)


def resolve_point(value, watch: Watch | None = None) -> tuple[int, int]:
    """One coordinate pair in pixels or as a fraction of the panel.

    The public half of `_point`, so that `watch_control.py gesture --file`
    resolves coordinates the same way a scenario step does instead of calling
    `int()` on them -- which turned `[0.5, 0.5]` into a silent tap on (0, 0).
    """
    if isinstance(value, str):
        x, _, y = value.partition(",")
        return _axis(x, watch, "width"), _axis(y, watch, "height")
    if not isinstance(value, (list, tuple)) or len(value) != 2:
        raise WatchError(f"{value!r} is not an x,y pair")
    return _axis(value[0], watch, "width"), _axis(value[1], watch, "height")


def _xy(step: dict, watch: Watch) -> tuple[int, int]:
    return _axis(step["x"], watch, "width"), _axis(step["y"], watch, "height")


def run(watch: Watch, steps: list[dict], output_dir: str,
        on_step=None) -> Report:
    report = Report()
    os.makedirs(output_dir, exist_ok=True)
    shots: dict[str, bytes] = {}

    for index, step in enumerate(steps):
        action = str(step.get("action", "")).replace("-", "_")
        started = time.monotonic()
        result = StepResult(index=index, action=action, ok=True)
        try:
            if action == "screenshot":
                name = str(step.get("name", f"step{index:02d}"))
                path = os.path.join(output_dir, f"{name}.png")
                absolute, shot = watch.save_screenshot(path)
                shots[name] = shot.rgb
                result.screenshot = absolute
                result.detail = f"{shot.width}x{shot.height}"
                report.artefacts.append(absolute)
            elif action == "wait":
                time.sleep(float(step.get("seconds", 0.2)))
            elif action == "wait_stable":
                quiet_ms = int(step.get("quiet_ms", 300))
                limit = float(step.get("timeout", 5.0))
                if not watch.wait_stable(quiet_ms, limit):
                    # A step that cannot fail is not a step. This used to
                    # discard the boolean, so the action passed whatever the
                    # interface was doing -- and the device end could not have
                    # said "settled" truthfully anyway.
                    raise WatchError(
                        f"the interface never went quiet for {quiet_ms} ms "
                        f"within {limit:g} s")
                result.detail = f"quiet {quiet_ms} ms"
            elif action == "tap":
                watch.tap(*_xy(step, watch))
            elif action == "long_tap":
                watch.long_tap(*_xy(step, watch), float(step.get("duration", 1.0)))
            elif action == "double_tap":
                watch.double_tap(*_xy(step, watch))
            elif action == "swipe":
                watch.swipe(_point(step, "from", watch), _point(step, "to", watch),
                            float(step.get("duration", 0.4)), int(step.get("steps", 0)))
            elif action == "drag":
                watch.drag(_point(step, "from", watch), _point(step, "to", watch),
                           float(step.get("duration", 1.0)), int(step.get("steps", 0)))
            elif action == "gesture":
                watch.gesture([_point({"p": pt}, "p", watch) for pt in step["points"]],
                              float(step.get("duration", 0.5)))
            elif action == "button":
                name = str(step["button"])
                if name == "first-injectable":
                    # The two boards share no button name: the T-Watch has
                    # `power`/`boot`, the Waveshare `button-1`/`button-2`, and
                    # which physical input either of the latter reaches is open
                    # question D5. A scenario that must run on both asks for
                    # "one this board will actually simulate" instead of
                    # naming one and failing everywhere else.
                    injectable = [b for b in watch._caps().buttons if b.injectable]  # noqa: SLF001
                    if not injectable:
                        # Not a failure: the Waveshare declares none, because
                        # whether either of its two keys reaches software is
                        # open question D5 and `injectable` defaults to the
                        # restrictive side. Marked skipped so it reads as
                        # coverage that did not happen rather than as a press
                        # that worked.
                        result.skipped = True
                        result.detail = (
                            "skipped: this board declares no injectable button "
                            "(the Waveshare's two are open question D5)")
                    else:
                        name = injectable[0].id
                if not result.skipped:
                    what = str(step.get("event", "click"))
                    if what == "press":
                        watch.button_press(name)
                    elif what == "release":
                        watch.button_release(name)
                    elif what == "hold":
                        watch.button_hold(name, float(step.get("duration", 1.0)))
                    else:
                        watch.button_click(name, float(step.get("duration", 0.05)))
            elif action == "input_reset":
                released, still_held = watch.input_reset()
                result.detail = f"released {released}"
                if still_held:
                    # The step fails rather than reporting a number, because
                    # everything after it runs against an interface that still
                    # has a finger on it -- and the count alone would read as a
                    # successful cleanup.
                    raise WatchError(
                        f"released {released} input(s), but {still_held} could not be "
                        f"released: the device's input queue is full, so it kept them "
                        f"held rather than stranding a pressed widget")
            elif action in ("expect_screen_changed", "expect_screen_same"):
                since = str(step["since"])
                if since not in shots:
                    raise WatchError(f"no checkpoint named '{since}' has been taken yet")
                current = watch.screenshot().rgb
                changed = current != shots[since]
                want_change = action == "expect_screen_changed"
                if changed != want_change:
                    result.ok = False
                    result.detail = ("the screen did not change" if want_change
                                     else "the screen changed and should not have")
                else:
                    result.detail = "changed" if changed else "unchanged"
            else:
                result.ok = False
                result.detail = f"unknown action '{step.get('action')}'"
        except (WatchError, ProtocolError, KeyError, ValueError) as exc:
            # `ProtocolError` is a typed refusal from the device -- BadInput,
            # RateLimited, QueueFull. It is a `RuntimeError`, not a
            # `WatchError`, so it used to escape `run()` entirely: past the
            # per-step summary, past the artefact list, and past `cmd_run`'s
            # `input_reset` cleanup. A scenario that deletes its own evidence
            # on failure is useless exactly when it matters, which is what
            # this file promises at the top.
            result.ok = False
            result.detail = str(exc)

        result.elapsed_ms = int((time.monotonic() - started) * 1000)
        report.steps.append(result)
        if on_step:
            on_step(result)
        if not result.ok:
            # Stop at the first failure, with everything written so far kept.
            # Carrying on after a device error produces a cascade of failures
            # whose first cause is buried.
            break

    return report
