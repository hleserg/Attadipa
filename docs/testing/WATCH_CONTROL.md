# Looking at the screen while you work on it

`tools/watch_control.py` takes a picture of what is on the watch, presses its
buttons, touches its screen, and takes another picture. That is the whole idea:
a change to the interface is checked by **looking at the interface**, not by
watching the build succeed.

It talks to the desktop simulator today and to a device when there is firmware
for one, over the same protocol and with the same commands.

- The agent-facing version is
  [`.claude/skills/watch-ui-testing/SKILL.md`](../../.claude/skills/watch-ui-testing/SKILL.md).
- What it is built out of, and why, is [§ How it works](#how-it-works) below.

---

## What this can and cannot tell you

**There is no Attadipa firmware yet** — `README.md` says so in its own status
table. So "the watch" here means the **simulator**: a real LVGL stack with a
real framebuffer and a real input path, on a desktop.

| A screenshot from the simulator **is** evidence about | It is **not** evidence about |
|---|---|
| layout, wrapping, clipping, overlap | panel colour rendition or gamma |
| colour choice, contrast, both themes | refresh rate, tearing, ghosting |
| navigation paths and what each one reaches | touch sensitivity, palm rejection, wet fingers |
| touch-target geometry and reachability | readability in sunlight |
| whether an animation finishes — *after a `wait_stable`, which is what makes the frame a finished one* | power, thermals, timing under load |
| whether an action **arrived** — `wait_stable` answers `false` while an event is still queued on the device or pumped and unread, not only while something is animating; those two are what a settled interface means and the first was missing | |

The tool prints the build it is connected to. The simulator says `sim 0.0.1`.
**If it says `sim`, nothing you saw is a hardware result** — `CLAUDE.md` is
absolute about that, and the build string exists so the mistake cannot be made
quietly.

---

## Getting a screen to look at

The simulator listens **only** when asked. There is no always-on debug port.

```bash
cmake -S . -B build-sim -DATTADIPA_BUILD_SIMULATOR=ON
cmake --build build-sim -j

# with a window, on the path the tool finds without being told
./build-sim/sim/attadipa_sim --board waveshare-amoled-206 \
    --debug-socket /tmp/attadipa-sim.sock

# headless, for CI or over ssh
SDL_VIDEODRIVER=dummy ./build-sim/sim/attadipa_sim --board t-watch-s3-plus \
    --debug-socket /tmp/attadipa-tw.sock &
# and then: python3 tools/watch_control.py --socket /tmp/attadipa-tw.sock info
```

**One path per simulator, and the second one needs `--socket`.** These two used
to name the same socket, four lines apart, under a heading that says to run both
boards every time. Starting the second on a path the first is serving is now
**refused**, with a message saying so — rather than the second quietly taking
the name, which is what used to happen: the first stayed alive, went on printing
that it was listening, and was unreachable for the rest of its life while the
tool drove the other one.

A path that exists and is **not** a socket is refused too. `--debug-socket` used
to unlink whatever was at the path before binding, so a mistyped
`--debug-socket ~/notes.md` deleted the file without a word.

Add `--diagnostic` for the test pattern instead of the capability screen — see
[the diagnostic screen](#the-diagnostic-screen).

**Both boards, every time.** `waveshare-amoled-206` is 410 × 502 and
`t-watch-s3-plus` is 240 × 240; a layout that is comfortable on one is cramped
on the other, and the Definition of Done asks for both geometries. `--theme
night` and `--locale ru` are the other two axes — Russian runs 15–30 % longer
than English, which is a layout question.

---

## The commands

```bash
python3 tools/watch_control.py info
python3 tools/watch_control.py screenshot --output artifacts/watch/screen.png
python3 tools/watch_control.py screenshot --count 5 --interval 0.15   # an animation

python3 tools/watch_control.py tap       --x 205 --y 250 --screenshot-after
python3 tools/watch_control.py long-tap  --x 205 --y 250 --duration 1.0
python3 tools/watch_control.py double-tap --x 205 --y 250
python3 tools/watch_control.py swipe --from 350,120 --to 60,420 --duration 0.5
python3 tools/watch_control.py drag  --from 60,420  --to 350,420 --duration 1.2
python3 tools/watch_control.py gesture --file tests/ui/gestures/example.json
#   the shipped file is written in fractions of the panel, so it runs on both
#   boards; whole numbers in a gesture file are pixels, same rule as a scenario

python3 tools/watch_control.py button button-1 click
python3 tools/watch_control.py button button-1 hold --duration 1.5
python3 tools/watch_control.py input-reset
python3 tools/watch_control.py run tests/ui/scenarios/diagnostic_tour.yaml
python3 tools/watch_control.py live --screenshot-after
```

`press` and `release` are deliberately absent from that list. Each invocation
of the tool ends by calling `input-reset`, and the connection dropping releases
whatever the remote was holding, so a `release` in a second process is a release
with nothing held and is refused. A hold that spans time belongs in one process:
`hold --duration`, a `button` step in a scenario, or `press`/`release` inside
`live`.

`--json` on any of them for machine-readable output. Every command exits
non-zero on failure. Every image's **absolute path** is printed, because the
image is the deliverable.

By default the tool looks for a simulator socket at `./.attadipa-sim.sock` and
`/tmp/attadipa-sim.sock`. `--socket <path>` says where; `--port <device>` is for
a serial device.

### `info` is not decoration

It prints what the **device** said, not what this tool assumes. Two lines of it
are worth reading every time:

- **`touch points 1`** — single touch, and that is a fact about LVGL rather than
  a shortcut: `lv_indev_data_t` carries one point, so nothing above the input
  layer could consume a second finger. There is no pinch and no rotate.
  Whether either panel's controller can report two is a separate and still open
  question (T-113: the Waveshare's part number behind chip ID `0x64` has no
  datasheet yet).
- **`role NOT established`** beside a button — the board has it, and nobody has
  traced what it does. On the Waveshare the owner counted two pressable buttons
  by pressing them; which of `Key1`, `Key3` and the PMU's `PWRON` each one
  reaches is **open question D5**. They are `button-1` and `button-2` because
  calling one "power" would be inventing the answer.

### Series, for animations

`--count 5 --interval 0.15` writes `screen-00.png` … `screen-04.png`. Five
stills of a transition answer "does it finish, and does it look right on the
way" without any video machinery.

---

## Scenarios

A scenario is a data file, not a framework — this project's test runner is
CTest and there is no reason for a second one.

```yaml
steps:
  - action: screenshot
    name: 01-untouched
  - action: expect_screen_same       # the control: nothing happened yet
    since: 01-untouched
  - action: tap
    x: 205
    y: 250
  - action: wait
    seconds: 0.2
  - action: screenshot
    name: 02-after-tap
  - action: expect_screen_changed
    since: 01-untouched
```

Actions: `screenshot` (named), `wait`, `wait_stable` (`quiet_ms`, `timeout` —
polls until the interface has been idle that long **and nothing is animating**,
and **fails the step** if it never is), `tap`, `long_tap`,
`double_tap`, `swipe`, `drag`, `gesture`, `button`, `input_reset`,
`expect_screen_changed`, `expect_screen_same`.

YAML if PyYAML is installed, JSON always — the structure is identical.

**The assertions are deliberately weak.** A pixel-exact expectation against a
live renderer fails on an antialiased glyph, and a check that fails for reasons
nobody cares about is switched off within a week. What a scenario proves is that
the loop ran and the screen responded; what decides whether the screen is
*right* is a person opening the PNGs. `tests/ui/scenarios/diagnostic_tour.yaml`
is the worked example.

The run stops at the first failure and keeps every image taken so far.

---

## The diagnostic screen

`--diagnostic` draws a test pattern whose entire purpose is to make a broken
screenshot **visible** rather than plausible. Every element answers one specific
failure:

| Element | The failure it catches |
|---|---|
| Four corner markers, four colours, two sizes, lettered TL/TR/BL/BR | rotation, and mirroring — which an abstract pattern cannot show, because a mirrored pattern is still a pattern |
| An asymmetric **F** in the centre | all eight orientations of the frame, at a glance |
| Pure R, G, B, white, mid-grey, black at known 8-bit values | a swapped colour channel; an RGB565 round trip that lost its low bits shows in the grey |
| A labelled 8 × 8 grid with a named origin | scale, crop, and a stride error |
| The last button event **and how long it was held** | that a press arrived at all, and that a long press is distinguishable from a click |
| The touch point, its id, and a trail of recent points | that a swipe is a real `down → move… → up`. One artificial jump draws two dots; a real swipe draws a line |

Its colours are literal rather than design tokens, and it is named in
`tools/ui/check_raw_values.py`'s exemption list for that reason: a pattern drawn
from theme tokens could not detect a swapped channel, because the pattern and
the expectation would move together.

---

## When it goes wrong

| Message | Cause | Fix |
|---|---|---|
| `no watch found` | nothing is listening | start the simulator **with** `--debug-socket`; the feature is off by default |
| `could not connect to …` | the path is wrong, or the simulator died | check the path; `ls -l` the socket |
| `the device did not answer within 10.0s` | the device is wedged | `--timeout` to wait longer. This is **not** what a second client sees — see the next row |
| `the device closed the connection…` | the simulator exited, or it already had a client | only one is served at a time; close the other. The refusal is immediate, not a timeout |
| `that input is impossible from the current state` | a release with nothing held, or a button this board lacks | `input-reset`, then re-read `info`. A `release` in its own invocation always lands here: the previous one released it on exit |
| `this stack is single-touch` | a second finger was requested | there is no multitouch; use one-point gestures |
| `a screenshot is already in progress` | two overlapping requests | let the first finish. This means **only** a screen transfer collision now |
| `the device's input queue is full…` | the interface is not draining, so an event was dropped | its own code, because it used to answer with the row above and send you to the wrong subsystem. Look at why the interface stalled |
| `nothing has been rendered yet` | the screen has not been drawn | take the screenshot after the first frame. **This is now the only capture failure that waiting fixes**, and no shipped source returns it: `lv_snapshot_take` re-renders, so it succeeds before the first `lv_timer_handler` too. A device source may still need it |
| `the device could not produce a frame at all…` | the renderer is out of memory | **waiting will not fix it.** The simulator fixes `LV_MEM_SIZE` at 1 MiB on purpose, and a 410 × 502 screenshot asks that pool for 617,460 bytes plus stride over the widget tree and the display buffers. Its own code because it used to answer with the row above, which sent you to wait for a frame that had already been drawn |
| `the screen the device is showing is not the panel's size…` | the active screen is smaller or larger than the display | look at what built that screen. Reporting the board's dimensions over the snapshot's pixels would produce a skewed image that still looked like a picture, so the capture is refused instead |
| `the frame is incomplete: N bytes never arrived` | a torn transfer | **not retried automatically** — run the command again. The retry in `request()` covers a lost request, not a torn stream, and a screenshot does not go through it |
| `the assembled frame does not match its checksum` | chunks assembled wrongly | same — the framing already proved each chunk was intact, so this is an assembly bug |
| `this build of the firmware cannot do that` | the debug channel is compiled out, **or** the frame buffer is smaller than the panel, **or** the button is one this board will not simulate | for a button, `info` prints which are simulated; the T-Watch's `boot` is a boot-mode strap and produces no software event on real hardware |
| the screen is stuck mid-gesture | a crashed run left a finger down | `input-reset` |

`input-reset` lifts only what the **remote** is holding, never what a person is
holding. The device does it by itself too — on a dropped connection, and after
30 seconds of any one hold.

---

## What it costs

| | |
|---|---|
| **Off by default** | The simulator listens only with `--debug-socket`. A firmware build will gate the whole subsystem behind a config option, off in release. |
| **RAM, when on** | **three** allocations at the peak, and the third has no declared bound. The bridge's own buffer is 410 × 502 × 3 = **617 kB** on the Waveshare at RGB888 and 240 × 240 × 3 = **173 kB** on the T-Watch — but `lv_snapshot_take` allocates a second one of its own before the row copy, so the peak is ~**1.24 MB** and ~**346 kB**. At RGB565 on a device, half of each. This row said one frame and the whole argument for the design rested on it; the second allocation is in the capture path, `sim/screen_source.cpp`, and `screen_source.h` says that call would be the right one on a device too, so T-114 inherits the peak rather than avoiding it. The bridge's buffer is allocated by the composition root, not by the debug code, so a build that does not want it does not have it — the snapshot's is not, which is the part to fix on a device. **Third: the output vector.** `DebugServer::flush` compacts only when everything queued has gone out (`sim/debug_server.cpp:321-324`), so while the socket does not fully drain inside one poll, `out_` grows by everything written since the last full drain — up to a whole framed image, ~**690 kB** on the Waveshare. `kOutputMax` bounds *unsent* bytes, not the vector, and `clear()` keeps the capacity it reached. Almost certainly never reached against a host that reads promptly, and it is in this row anyway because **T-114 names this file as the model for the firmware transport**, where the reader is a USB link rather than a local socket. A bound on the vector itself, or a front-erase once it passes a threshold, is the thing to decide there rather than to inherit by accident. |
| **RAM, always** | the input queue: 64 events × 16 bytes ≈ **1 kB**, and it is the ordinary input path rather than a debug cost. |
| **Flash** | the protocol and bridge are a few kB of code, plus a bitwise CRC-32 chosen over a 1 kB table for exactly this reason. |
| **Time, per screenshot** | **0.48 s Waveshare, 0.14 s T-Watch**, MEASURED 2026-08-23 — median of five over a Unix socket after one warm-up, on this desktop; repeated runs land between 0.47 and 0.50 for the Waveshare. On USB it will be bounded by the link, not by the device. **Re-measure this row whenever `kOutputWatermark`, `kMaxChunksPerPoll` or the loop delay in `sim/main.cpp` changes** — a chunk cap of 16 made it 1.05 s and left this row still reading 0.5, which is a measurement that has quietly stopped being one. |
| **Interface pause, streaming** | none measurable, and the construction is the **pair** of bounds in `sim/debug_server.cpp` rather than the watermark alone: one poll hands over at most `kMaxChunksPerPoll` chunks *and* stops once `kOutputWatermark` bytes are waiting. At 64 × 199 = 12.7 kB the count binds first while the socket keeps draining, and the watermark takes over when it stops. Either way the transport sets the pace and nothing blocks waiting for a transfer to finish. |
| **Interface pause, capture** | **3.6 ms MEASURED on this desktop** for a 617 kB frame; **UNKNOWN on a device.** Synchronous on the interface's own thread, and the watermark above does not protect it. The row copy is nothing — 0.008 ms — and the **bitwise** CRC-32 is everything: 4.94 M inner loops, 3.605 ms, a factor of 450. On the T-Watch's 173 kB, 1.009 ms. The device figure is deliberately *not* this one scaled by clock. The ESP32-S3 runs at 240 MHz against this host's several GHz and has none of its width — and, the part that actually decides it, **a 617 kB frame buffer cannot live in internal SRAM and must sit in PSRAM**, which on this board is octal at 80 MHz with a 10-cycle fixed latency (D12a, from the vendor boot log). A byte-at-a-time CRC over PSRAM is a different machine from one over cache-resident DRAM, so scaling would be an estimate wearing a measurement's clothes. **T-114** owns the number; this row exists so nobody re-derives the question. |
| **Wire cost** | ~10 % overhead: 7 bytes of framing and 10 of envelope per 182-byte body. |

Every figure above is **MEASURED** on a desktop simulator over a Unix socket
**and none of them is a device figure** — including the capture pause, whose
desktop number is measured and whose device number is `UNKNOWN` for the reason
in its own row. No figure here is a hardware measurement, because there is no
firmware to measure — and the one row that would matter most on hardware is the
one nothing has measured yet, which is why it says so rather than sitting under
the banner with the others.

---

## How it works

```
tools/watch_control.py                the command line
  └─ tools/watch/client.py            handshake, requests, retries, PNG
       └─ tools/watch/protocol.py     framing, envelope, pixels — a pure module
            │
            │  Unix socket (simulator)  ·  USB-Serial/JTAG (a device, later)
            │
       ┌────┴──────────────────────────────────────────────┐
       │ sim/debug_server.cpp        the transport         │
       │ debug/bridge.cpp            dispatch, limits      │
       │ debug/protocol.cpp          the same wire format  │
       │ core/input.cpp              the ONE input queue   │
       │ sim/remote_input.cpp        queue → LVGL indev    │
       │ sim/screen_source.cpp       lv_snapshot → bytes   │
       └───────────────────────────────────────────────────┘
```

Five decisions worth knowing:

1. **It is not a second protocol.** `link::frame_codec`'s framing — sync `F1
   5E`, a checked length, CRC-16/CCITT, a resynchronising decoder — carries an
   [ADR-0005](../adr/0005-node-protocol.md) §4 envelope with its own message
   class. So a text log sharing the stream costs a few counted resyncs and can
   never corrupt an image, a corrupted length is rejected at the header, and an
   over-long frame is an error rather than a truncation.

2. **`kMaxPayload` was not raised.** It is 192 bytes, and RESOURCE_BUDGET §4
   requires the bound be declared. Screenshots are chunked to fit rather than
   widening a buffer every other subsystem shares.

3. **Injected input goes into the ordinary input queue** and reaches the
   interface through a real LVGL input device, so a widget cannot tell an
   injected tap from a finger. Nothing calls a screen's handler directly; a
   test that drove the interface through a private door would pass against code
   no finger can reach.

   Said precisely, because a looser sentence here was wrong: the simulator's
   own mouse is LVGL's `lv_sdl_mouse_create()` and reaches LVGL as its **own**
   indev without passing through the queue. The two coexist because LVGL runs
   each indev independently, not because they meet in the queue. The bridge is
   the queue's only producer today; the touch controller and the buttons are
   T-114's, and they are what `InputOrigin::Physical` is for.

4. **Physical input keeps working** while remote input is connected. Events
   carry an origin, and the origin is used for exactly one thing: cleaning up
   after a dropped connection without lifting a finger a person is holding.

5. **Orientation and pixel format travel on the wire.** The host holds no table
   of board facts — it asks. A tool that knew "the Waveshare is 410 × 502" would
   be the architecture rule broken in a script, and wrong the first time a panel
   was rotated.

### Tests

| What | Where | Needs |
|---|---|---|
| the input queue and state machine | `tests/test_input.cpp` | nothing |
| the wire format and the bridge | `tests/test_debug.cpp` | nothing |
| the host tool's format, pixels, PNG, exit codes | `tools/watch/selftest.py` | nothing |
| **the whole loop** | `tools/watch/e2e_test.py` | the simulator |

The C++ and Python implementations of the wire format are **independent**, and
both assert the same fixed byte literals. Two implementations that share a
mistake agree with each other perfectly; a hex string does not.

`ctest` runs all four. The end-to-end one starts the simulator, drives it
through the real socket and checks the resulting pictures — which colour is in
which corner, whether the swatches are pure, and how many pixels a swipe left
behind.

---

## Running against a device

**Not possible yet, and nothing here pretends otherwise.** There is no ESP-IDF
project in this repository, so nothing on the far end of a USB cable speaks this
protocol. When there is:

1. build the firmware with the debug channel enabled;
2. flash it — **which needs the owner's authorisation**, `CLAUDE.md`;
3. `python3 tools/watch_control.py --port <the unit's port> info` — and
   **resolve that port from the unit's USB serial, never type `ttyACM0`.** Two
   ESP32-S3 devices are attached to this host and both enumerate as
   `303a:1001`; one of them is the owner's MeshCore node. No tool resolves it
   yet, which is **T-116**, so until it exists a session writes its own guard or
   does not write.

`SerialTransport` in `tools/watch/client.py` is written and **NOT EXECUTED** —
it has never spoken to a device, because there has been no device to speak to.
It is marked that way in the source rather than left as a hole, and the framing
beneath it is fragment-agnostic by construction, which is the property a byte
stream needs either way.

Two things will need doing on the device that the simulator does not need: a
frame buffer sized for the panel behind the same config option, and a task that
calls `Bridge::pump` from the same context that services the interface. The
watermark shape in `sim/debug_server.cpp` is the model for the second.
