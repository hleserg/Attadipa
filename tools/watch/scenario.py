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


@dataclass
class Report:
    steps: list[StepResult] = field(default_factory=list)
    artefacts: list[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return all(step.ok for step in self.steps)


def load(path: str) -> list[dict]:
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()
    if path.endswith((".yaml", ".yml")):
        try:
            import yaml  # type: ignore
        except ImportError as exc:
            raise WatchError(
                f"{path} is YAML and PyYAML is not installed. Either "
                f"`pip install pyyaml` or write the scenario as JSON -- the "
                f"structure is identical.") from exc
        data = yaml.safe_load(text)
    else:
        data = json.loads(text)

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
    value = step[key]
    if isinstance(value, str):
        x, _, y = value.partition(",")
        return _axis(x, watch, "width"), _axis(y, watch, "height")
    return _axis(value[0], watch, "width"), _axis(value[1], watch, "height")


def _axis(value, watch: Watch | None, axis: str) -> int:
    if isinstance(value, str):
        value = value.strip()
        if value.endswith("%"):
            value = float(value[:-1]) / 100.0
        else:
            value = float(value) if "." in value else int(value)
    if isinstance(value, float) and 0.0 < value < 1.0:
        if watch is None:
            raise WatchError(
                f"a fractional coordinate ({value}) needs the device's screen size, "
                f"and this step was resolved without a connection")
        caps = watch._caps()  # noqa: SLF001 - the runner is the client's own caller
        span = caps.width if axis == "width" else caps.height
        return int(round(value * span))
    return int(value)


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
                watch.wait_stable()
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
                        raise WatchError("this board simulates no buttons at all")
                    name = injectable[0].id
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
                released = watch.input_reset()
                result.detail = f"released {released}"
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
