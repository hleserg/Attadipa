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


def _point(step: dict, key: str) -> tuple[int, int]:
    value = step[key]
    if isinstance(value, str):
        x, _, y = value.partition(",")
        return int(x), int(y)
    return int(value[0]), int(value[1])


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
                watch.tap(int(step["x"]), int(step["y"]))
            elif action == "long_tap":
                watch.long_tap(int(step["x"]), int(step["y"]),
                               float(step.get("duration", 1.0)))
            elif action == "double_tap":
                watch.double_tap(int(step["x"]), int(step["y"]))
            elif action == "swipe":
                watch.swipe(_point(step, "from"), _point(step, "to"),
                            float(step.get("duration", 0.4)), int(step.get("steps", 0)))
            elif action == "drag":
                watch.drag(_point(step, "from"), _point(step, "to"),
                           float(step.get("duration", 1.0)), int(step.get("steps", 0)))
            elif action == "gesture":
                watch.gesture([_point({"p": pt}, "p") for pt in step["points"]],
                              float(step.get("duration", 0.5)))
            elif action == "button":
                name = str(step["button"])
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
