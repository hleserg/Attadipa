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

The tool is `tools/watch_control.py`. It talks to whatever is running: the
desktop simulator today, a device when there is firmware for one. Nothing in
this file changes when that happens, because the tool asks the device what it is
rather than holding a table of board facts.

## 0. What exists, and what does not

**There is no Attadipa firmware yet.** `README.md` says so. So "the watch" in
this skill means the **simulator**, which is a real LVGL stack with a real
framebuffer and a real input path — not a mock, but not a panel either.

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

The simulator listens **only** when given `--debug-socket`. That is deliberate:
the feature is off unless asked for.

## 2. What can it do?

`info` prints the panel size, the pixel format, the orientation, how many touch
points and the buttons **by name**. Read it before acting; do not assume.

Two things it will tell you that matter:

- **`touch points 1`.** Single touch. LVGL's pointer device carries one point,
  so there is no pinch and no rotate, and asking for a second finger is a typed
  error rather than a silent merge. Do not write a test that needs two.
- **`role NOT established`** beside a button. On the Waveshare the owner counted
  two pressable buttons and **which named input each one reaches is open
  question D5**. They are called `button-1` and `button-2` because naming one
  "power" would be inventing the answer. Press them and observe; do not assume.

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
python3 tools/watch_control.py drag  --from 60,420  --to 350,420 --duration 1.2
python3 tools/watch_control.py button button-1 click
python3 tools/watch_control.py button button-1 hold --duration 1.5
python3 tools/watch_control.py button button-1 press     # and release separately,
python3 tools/watch_control.py button button-1 release   # to test real timing
python3 tools/watch_control.py gesture --file tests/ui/gestures/example.json
```

`--screenshot-after` acts and photographs the result in **one connection**,
which is what you want in a loop: two processes would mean a disconnect between
them, and a disconnect releases everything held.

Coordinates are logical, origin top-left, in the size `info` reported.

A swipe is sent as a genuine `down → move… → up` at the speed you asked for. A
`drag` is the same shape, slower — and the difference is the point, so do not
substitute one for the other.

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

## 7. A whole journey

```bash
python3 tools/watch_control.py run tests/ui/scenarios/diagnostic_tour.yaml
```

Steps are `screenshot` (named), `tap`, `swipe`, `drag`, `button`, `wait`,
`input_reset`, and two weak assertions — `expect_screen_changed` and
`expect_screen_same`. Weak on purpose: a pixel-exact expectation against a live
renderer fails on an antialiased glyph and gets switched off within a week.
**The strong check is you, opening the PNGs.**

Images land in `artifacts/watch/<scenario>/` and every path is printed. The run
stops at the first failure with everything written so far kept.

## 8. Many actions, one connection

```bash
python3 tools/watch_control.py live --screenshot-after
```

Then `tap 120 180`, `swipe 200 180 40 180`, `click button-1`, `shot`,
`series 5 0.2`, `reset`, `help`, `quit`. One handshake, no reconnect per action,
so a `press` and a much later `release` genuinely test a hold.

## 9. When it goes wrong

| What you see | What it means | What to do |
|---|---|---|
| `no watch found` | nothing is listening | start the simulator with `--debug-socket` |
| `the device did not answer within 10.0s` | it is wedged, or something else is connected | check the simulator is alive; only one client is served at a time |
| `the device refused: that input is impossible…` | a release with nothing held, or a button this board lacks | `input-reset`, then re-read `info` |
| `the device refused: this stack is single-touch` | you asked for a second finger | there is no multitouch; use one-point gestures |
| `the device refused: a screenshot is already in progress` | two overlapping requests | let the first finish |
| `the frame is incomplete` / `does not match its checksum` | a torn transfer | retry once; if it repeats, it is a bug — report it with the message |
| the screen is stuck mid-gesture | a crashed run left a finger down | `python3 tools/watch_control.py input-reset` |
| a button is stuck down | same | `input-reset`. The device also releases after 30 s and on disconnect |

**`input-reset` is the escape hatch and it is safe.** It lifts only what the
remote is holding.

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
