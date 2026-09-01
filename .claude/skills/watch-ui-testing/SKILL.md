---
name: watch-ui-testing
description: Drive the watch (or the simulator) over USB the way a person would — screenshot, look at the picture, tap or swipe or press a button, wait, screenshot again, check. Use after any change to screens, navigation, themes, fonts, widgets, animations, touch handling or buttons, and before calling such a change done. A build that compiles is not a screen that works.
---

# Testing the interface by looking at it

**A UI change that compiles has not been checked.** This skill is the loop that
checks it:

```
screenshot → open the image and look → act → wait → screenshot → compare → fix and repeat
```

The tool is `tools/watch_control.py`. It talks to the physical Waveshare over
USB-Serial/JTAG or to the simulator over a Unix socket, and asks the endpoint
for its board facts. When the bench watch is attached, use it; the simulator is
still required for the second 240×240 geometry.

## 0. What exists, and what does not

`info` says `device ...` for the physical firmware and `sim ...` for the
desktop LVGL endpoint. That build string is the evidence boundary.

That distinction is not a formality:

- A screenshot from the simulator is evidence about **layout, colour, wrapping,
  contrast, navigation and touch geometry**. Those are the same in both.
- It is **not** evidence about anything physical: panel colour rendition,
  refresh, tearing, touch sensitivity, how it reads in sunlight, or power.
- The tool prints the build it is talking to (`sim 0.0.1`). If it does not say
  `sim`, you are on hardware and may say so; if it does, **never write `PASS`
  for a hardware test** — `CLAUDE.md` forbids it and the build string is there
  so you cannot do it by accident.

## 1. Is anything there?

```bash
python3 tools/watch_control.py info
```

Exit 0 and a description means yes. Exit 2 with *"no watch found"* means
nothing is listening. Start one:

```bash
cmake -S . -B build-sim -DATTADIPA_BUILD_SIMULATOR=ON && cmake --build build-sim -j
SDL_VIDEODRIVER=dummy ./build-sim/sim/attadipa_sim \
    --board waveshare-amoled-206 --debug-socket /tmp/attadipa-sim.sock &
```

`SDL_VIDEODRIVER=dummy` runs it with no display. Drop it if you want a window
too. `--board t-watch-s3-plus` for the other geometry — **check both**, the
Definition of Done says so and 240×240 breaks layouts that 410×502 does not.

**Give the second board its own socket path.** Two simulators cannot share one:
the second is refused now, with a message saying which path is taken. Run it on
`/tmp/attadipa-tw.sock` and pass `--socket /tmp/attadipa-tw.sock` to the tool —
the default search covers `./.attadipa-sim.sock` and `/tmp/attadipa-sim.sock`,
in that order, and nothing else. A path that exists and is not a socket is
refused too; `--debug-socket` used to delete whatever was there.

The simulator listens **only** when given `--debug-socket`. That is deliberate:
the feature is off unless asked for.

## 2. What can it do?

`info` prints the panel size, the pixel format, the orientation, how many touch
points and the buttons **by name**. Read it before acting; do not assume.

Two things it will tell you that matter:

- **`touch points 1`.** Single touch. LVGL's pointer device carries one point,
  so there is no pinch and no rotate, and asking for a second finger is a typed
  error rather than a silent merge. Do not write a test that needs two.
- **`power` / `boot` on the Waveshare.** `power` is observable through AXP2101
  edge status and is remotely injectable. `boot` is active-low GPIO0 and stays
  non-injectable because it is a reset strap. Both physical paths enter the
  same `InputQueue` as remote input.

## 3. Look before you touch

```bash
python3 tools/watch_control.py screenshot --output artifacts/watch/before.png
```

It prints the absolute path. **Open it.** Reading the path is not looking at
the picture, and the whole mechanism exists so that you can look.

**Do not compute tap coordinates from the source code and fire at them.** That
tests your arithmetic. Find the element in the image, take its coordinates from
what you can see, then tap.

## 4. Act

```bash
python3 tools/watch_control.py tap --x 205 --y 250 --screenshot-after
python3 tools/watch_control.py long-tap --x 205 --y 250 --duration 1.0
python3 tools/watch_control.py swipe --from 350,120 --to 60,420 --duration 0.5
#   those are Waveshare pixels; `info` prints the size of the board you are on
python3 tools/watch_control.py drag  --from 60,420  --to 350,420 --duration 1.2
python3 tools/watch_control.py button power click
#   A *hold* is demonstrated on the pointer above rather than on this key:
#   injection bypasses the PMU, but `power` is SW7 on the AXP2101's `PWRON`
#   pin and long-press behaviour is PMU register policy, so on a device a held
#   power key may be a shutdown. `diagnostic_tour.yaml` gives the same reason
#   for holding a pointer instead, and it is the file this one should agree with
python3 tools/watch_control.py gesture --file tests/ui/gestures/example.json
#   fractions of the panel, so this one is board-independent -- it used to be
#   Waveshare pixels and refused outright on a 240x240 T-Watch
```

**`press` and `release` are not two commands.** Run as separate invocations
they cannot work, and it is worth knowing why before the error looks like a
device fault: each invocation ends by calling `input-reset`, and the socket
closing releases everything the remote held anyway (section 8). So the second
command asks the device to release a button nothing is holding, and gets
`that input is impossible from the current state` — from `InputState`, working
exactly as specified. Hold a button across time in **one process**: `hold
--duration`, a `button` step in a scenario, or `press` then `release` inside
`live` (section 7).

`--screenshot-after` acts and photographs the result in **one connection**,
which is what you want in a loop: two processes would mean a disconnect between
them, and a disconnect releases everything held.

Coordinates are logical, origin top-left, in the size `info` reported.

A swipe is sent as a genuine `down → move… → up` at the speed you asked for. A
`drag` is the same shape, slower — and the difference is the point, so do not
substitute one for the other.

**Two rapid actions need a gap, and when they do not get one that is a bug
worth reporting rather than working around.** LVGL reads its input devices
every 33 ms while the loop runs far faster, so a handoff that keeps one state
instead of a queue merges them. The diagnostic screen shows both views side by
side — the touch trail is drawn from the transport, the `lvgl:` line and its
markers from LVGL's own click events. If the trail shows a gesture the `lvgl:`
line did not count, the input path is dropping it **after** delivery, and no
screenshot of the app itself will tell you that.

## 5. Wait, then look again

The interface needs a frame or two. `--screenshot-after` waits 0.15 s by
default; `--delay 0.5` for an animation. For a transition, take a series:

```bash
python3 tools/watch_control.py screenshot --count 5 --interval 0.15 \
    --output artifacts/watch/transition.png
```

## 6. Check the picture, not the exit code

Open every image. Look for:

- the screen you expected, and every navigation path back out of it;
- **text clipped** at an edge or inside a container;
- **overlaps** — a label over an icon, two widgets on the same pixels;
- **padding** that collapsed, or that is different on the two panels;
- **contrast** — pale grey on white is unreadable on a wrist in daylight;
- **colours** — right hue, and right in both themes;
- **corrupted regions** — a stripe, a torn edge, a rectangle of stale content;
- **animation finished**, not caught halfway and left there;
- **touch targets** that are invisible or too small to hit;
- **double firing** — one tap that navigated two screens deep;
- **the UI still alive** after a series of actions, not wedged.

**What counts as right is written down.** Contrast, colour, type size, spacing
and icon legibility are not a matter of taste in this repository:
[`docs/ui/DESIGN_SYSTEM.md`](../../../docs/ui/DESIGN_SYSTEM.md) holds the tokens
and the measured contrast ratios. Check the picture against that, not against
your own eye.

## 7. A whole journey

```bash
python3 tools/watch_control.py run tests/ui/scenarios/diagnostic_tour.yaml
```

Steps are `screenshot` (named), `tap`, `long_tap`, `double_tap`, `swipe`,
`drag`, `gesture`, `button`, `wait`, `wait_stable`, `input_reset`, and two weak
assertions — `expect_screen_changed` and `expect_screen_same`. **`wait_stable`
is the one to reach for after an action**: it polls until the interface has been
idle for `quiet_ms` *and* nothing is queued, pumped or animating, and it **fails
the step** if that never happens — a `wait` of a fixed length is a guess that
passes either way. Weak on purpose: a pixel-exact expectation against a live
renderer fails on an antialiased glyph and gets switched off within a week.
**The strong check is you, opening the PNGs.**

**A step has three outcomes, not two** — `ok`, `FAIL` and `skip`. A step this
profile cannot run is counted separately and never as a pass.

Images land in `artifacts/watch/<scenario>/` and every path is printed. The run
stops at the first failure with everything written so far kept.

## 8. Many actions, one connection

```bash
python3 tools/watch_control.py live --screenshot-after
```

Then `tap 120 180`, `swipe 200 180 40 180`, `click power`, `shot`,
`series 5 0.2`, `reset`, `help`, `quit`. One handshake, no reconnect per action,
so a `press` and a much later `release` genuinely test a hold.

## 9. When it goes wrong

| What you see | What it means | What to do |
|---|---|---|
| `no watch found` | nothing is listening | start the simulator with `--debug-socket` |
| `the device did not answer within 10.0s` | it is wedged, or something else is connected | check the simulator is alive; only one client is served at a time |
| `the device refused: that input is impossible…` | a release with nothing held, or a button this board lacks | `input-reset`, then re-read `info`. **If you ran `press` in one command and `release` in the next, this is the expected answer, not a fault** — see section 4 |
| `button 'boot' is not remotely injectable` | the harness refuses the reset strap host-side | use `power` for an application-level injected button, or press BOOT physically |
| `the device refused: this stack is single-touch` | you asked for a second finger | there is no multitouch; use one-point gestures |
| `the device refused: the device is already doing one of those` | two overlapping requests of the same kind | let the first finish. Screen transfers are what a UI run hits; `mesh-forget-bond` shares the code and never collides with one |
| `the device refused: the device could not produce a frame at all…` | the renderer is out of memory | **retrying will not fix it.** A 410 × 502 screenshot asks LVGL's 1 MiB pool for 617 kB over the widget tree; simplify the screen or look at what is holding memory. It used to answer `nothing has been rendered yet`, which sent you to wait for a frame that was already drawn |
| `the device refused: the screen the device is showing is not the panel's size…` | the active screen is not the display's size | look at what built that screen — the capture is refused rather than reporting the board's dimensions over the wrong pixels |
| `the device refused: nothing has been rendered yet` | genuinely no frame yet | the one capture failure that waiting *does* fix. The simulator never returns it |
| `the frame is incomplete` / `does not match its checksum` | a torn transfer | retry once; if it repeats, it is a bug — report it with the message |
| the screen is stuck mid-gesture | a crashed run left a finger down | `python3 tools/watch_control.py input-reset` |
| a button is stuck down | same | `input-reset`. The device also releases after 30 s and on disconnect |

**`input-reset` is the escape hatch and it is safe.** It lifts only what the
remote is holding — never a key a person has their finger on.

**Read its second number.** It answers `released N, still_held M`, and exits
non-zero when `M` is not zero, because the device marks an input released only
if the release actually reached the interface's queue. A full queue is exactly
the stalled interface that made you reach for this command, so `released 0` on
its own means either *nothing was stuck* or *everything still is*. `M` is what
separates them. Nothing is lost when `M > 0` — the device's own 30 s hold expiry
retries, and so does a reconnect — but a run that continues past it is running
against a screen that still has a finger on it.

## 10. Keep the evidence

`artifacts/` is gitignored — the images are evidence for a review, not source.
So **put the paths in your reply, and say what you checked in each one.**
"Screenshots taken" is not a report. "Both geometries, both themes; the Russian
title wraps at 240 px and does not at 410; `after-swipe.png` shows the menu did
not move" is.

## 11. When to do this

**After** a substantive change to screens, navigation, themes, fonts, widgets,
system dialogs, lock, touch handling or buttons — and **before** calling it
done. Not after every keystroke: at logical checkpoints and at the end of a task
that touched the interface.

**If you changed the interface and did not look at it, the task is not
finished.** Compiling is not checking. [`docs/testing/WATCH_CONTROL.md`](../../../docs/testing/WATCH_CONTROL.md) has
the longer version, including how to run it against hardware when firmware
exists.
