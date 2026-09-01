# Looking at the screen while you work on it

`tools/watch_control.py` takes a picture of what is on the watch, presses its
buttons, touches its screen, and takes another picture. That is the whole idea:
a change to the interface is checked by **looking at the interface**, not by
watching the build succeed.

It talks to both the desktop simulator and the physical Waveshare firmware over
the same protocol and with the same commands. With the bench watch attached,
no port argument is needed: the tool resolves its USB serial identity.

- The agent-facing version is
  [`.claude/skills/watch-ui-testing/SKILL.md`](../../.claude/skills/watch-ui-testing/SKILL.md).
- What it is built out of, and why, is [§ How it works](#how-it-works) below.

---

## What this can and cannot tell you

The build string is the evidence boundary. `sim ...` is a desktop LVGL result;
`device ...` is the ESP32-S3 endpoint on the physical Waveshare. A real-device
screenshot proves the rendered frame, transport and input loop, but it still
does not prove sunlight readability, touch sensitivity or power behaviour.

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

python3 tools/watch_control.py button power click
#   `hold` is shown on the pointer above, not here: `power` is SW7 on the
#   AXP2101's `PWRON` pin and its long-press behaviour is PMU register policy,
#   so on a device a held power key may be a shutdown rather than an event
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

### What `duration` measures

**The whole path, from the `PointerDown` to the `PointerUp`.** A gesture of `N`
points has `N - 1` intervals between adjacent points; each is `duration / (N-1)`
and every one of them is waited out, the interval before the release included.
So a two-point gesture — which has nothing in the middle at all — schedules
its release one whole `duration` after the press. On a connection fast enough
to meet every deadline, `0.6` therefore means the device sees 0.6 s whatever
the shape. A slow transport can stretch that span; the distinction below is
part of the contract rather than hidden latency.

**The requested duration is bounded by the device's own hold limit, which
`info` prints** — 30 s on the simulator, and a real firmware may choose less.
`long-tap`, `swipe`, `drag` and `gesture` all refuse an over-limit request
before `PointerDown`, in a sentence naming the request in milliseconds. The
number is read from capabilities rather than assumed. This check cannot prove
the achieved device-side span on a slow serial transport: acknowledgement
round trips can delay the host release beyond the request. T-114 must report
requested beside achieved time and establish any serial safety margin on the
real transport; this host-only change deliberately invents neither.

**`0` is a host convention and not a device declaration, and the two do not
agree.** A device that advertises `0` has given the tool no bound to enforce,
so the tool does not invent one and lets the gesture through. The bridge reads
the same `0` as *expire immediately*: `debug/src/bridge.cpp:778` —
"now_ms - pointer_down_at_ > limits_.max_hold_ms" — releases when that is true,
which it already is one millisecond after the `PointerDown`, and
`debug/src/bridge.cpp:764` — "now_ms - button_down_at_[i] > limits_.max_hold_ms"
— does the same for buttons.
`info` says so out loud — it prints `hold released after 0 ms`. So `0` is not a
way to ask for an unbounded hold; a firmware that wants one has to raise the
limit, not zero it. The default is `30000`
(`debug/include/attadipa/debug/bridge.h:148` — "max_hold_ms = 30000"), and
`bridge.cpp:383` — "caps.max_hold_ms" — copies the enforced limit into the
capabilities verbatim, so what is advertised and what
is enforced are one number.

This was not true before, which is why it is written down rather than assumed:
the wait hung off the *intermediate* points and came after each was sent, so a
five-point 0.6 s gesture spent 0.45 s with a zero-length first segment, and a
two-point one was a down immediately followed by an up. A recogniser reads
**speed** — it is what separates a swipe from a drag from a flick — so a report
saying a gesture took the time it asked for while the device saw a different
one is worse than no timing at all.

The deadlines are absolute, taken from one `time.monotonic()` at the
`PointerDown`, so the round trips come out of the intervals they happened in
rather than being added to the path: a long path does not drift late. If a
round trip overruns its interval, the next point is already due and is sent
immediately — the tool never tries to make time back up, and never sleeps for a
negative interval.

`duration: 0` is legal and means *as fast as the connection manages* — the
shape without a claim about its speed. A negative, infinite or NaN duration is
refused **before** the `PointerDown` goes out for every pointer-hold verb, so a
mistyped command, scenario or gesture file cannot leave a finger down.

`swipe` and `drag` are a hair short of this today — they take `steps` intervals
between their points and sleep for `steps - 1` of them, so a swipe runs `1/steps`
under its duration and starts with a zero-length segment. Bounded, unlike the
gesture defect above, and remains explicit in issue #117 rather than left to be
rediscovered.

### `info` is not decoration

It prints what the **device** said, not what this tool assumes. Two lines of it
are worth reading every time:

- **`touch points 1`** — single touch, and that is a fact about LVGL rather than
  a shortcut: `lv_indev_data_t` carries one point, so nothing above the input
  layer could consume a second finger. There is no pinch and no rotate.
  Whether either panel's controller can report two is a separate and still open
  question (T-113: the Waveshare's part number behind chip ID `0x64` has no
  datasheet yet).
- **`power` / `boot` on the Waveshare** — the official schematic and product
  page identify both case keys. Physical `power` edges are read from AXP2101
  interrupt status; `boot` is active-low GPIO0. `power` is remotely injectable.
  `boot` remains `not simulated` because it is a reset strap, even though a
  running firmware can observe its physical level.

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
polls until **three** things hold at once: the device holds no queued input,
nothing has been pumped to the interface and left unread, and the interface has
then been idle that long with nothing animating; and **fails the step** if it
never is), `tap`, `long_tap`,
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

**A step has three outcomes, not two.** `ok`, `FAIL`, and `skip` — a step this
board cannot run. `first-injectable` selects from the device capabilities; if a
profile declares none, the skip is counted separately and never as a pass.

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
| `the frame did not finish arriving in time…` | the device is still sending after the whole transfer's budget ran out | `--timeout` to allow longer, then look at why it is trickling. **Distinct from the row below:** this one means chunks are still coming, not that they stopped. It also used never to fire — the deadline bounded each individual wait and nothing bounded the transfer, so a trickling device was awaited for ever |
| `the frame is incomplete: N bytes never arrived` | a torn transfer | **not retried automatically** — run the command again. The retry in `request()` covers a lost request, not a torn stream, and a screenshot does not go through it |
| `the assembled frame does not match its checksum` | chunks assembled wrongly | same — the framing already proved each chunk was intact, so this is an assembly bug |
| `this build of the firmware cannot do that` | the debug channel is compiled out, **or** the frame buffer is smaller than the panel, **or** the button is one this board will not simulate | for a button, `info` prints which are simulated; `boot` is intentionally not injectable |
| the screen is stuck mid-gesture | a crashed run left a finger down | `input-reset` |

`input-reset` lifts only what the **remote** is holding, never what a person is
holding. The device does it by itself too — on a dropped connection, and after
30 seconds of any one hold.

**It answers with two numbers — `released N, still_held M` — and exits non-zero
when `M` is not zero.** The device marks an input released only if the release
reached the interface's queue; if that queue is full it deliberately keeps the
input held, because the widget under it still believes it is pressed and
forgetting about it here would strand that belief. So `released 0` alone means
*either* nothing was stuck *or* the queue refused every release and everything
still is — opposite states — and `released 1` alone reads as complete when a
button went out and the finger did not. The stalled interface is exactly the
condition that sends people to this command, so the count on its own is least
trustworthy where it is most needed. Nothing is lost when `M > 0`: the hold
expiry retries. A run that continues past it is running against a pressed
screen.

---

## What it costs

| | |
|---|---|
| **Off in every product image** | The simulator listens only with `--debug-socket`. Firmware uses `CONFIG_ATTADIPA_WATCH_CONTROL`, `n` in the Kconfig default *and* in `firmware/sdkconfig.defaults`. Turning it on is `sdkconfig.hil`, stacked by name; see [the trust boundary](#the-trust-boundary) below. |
| **RAM, when on** | One caller-owned 410 × 502 × 2 RGB565 frame in PSRAM: **411,640 bytes**. `lv_snapshot_take_to_draw_buf` renders into it directly. Firmware TX is a fixed **16 KiB** queue and cannot grow to a second frame. |
| **RAM, always** | the input queue: 64 events × 16 bytes ≈ **1 kB**, and it is the ordinary input path rather than a debug cost. |
| **Flash** | the protocol and bridge are a few kB. ESP32-S3 uses the ROM CRC-32 table; the host keeps the small bitwise implementation. |
| **Time, per screenshot** | Physical Waveshare over USB: **4,474.5 ms median of five**, measured 2026-08-25 after one connection. Simulator figures remain 0.48 s Waveshare / 0.14 s T-Watch on the 2026-08-23 desktop run. |
| **Interface pause, streaming** | none measurable, and the construction is the **pair** of bounds in `sim/debug_server.cpp` rather than the watermark alone: one poll hands over at most `kMaxChunksPerPoll` chunks *and* stops once `kOutputWatermark` bytes are waiting. At 64 × 199 = 12.7 kB the count binds first while the socket keeps draining, and the watermark takes over when it stops. Either way the transport sets the pace and nothing blocks waiting for a transfer to finish. |
| **Interface pause, capture** | Physical request-to-`ScreenInfo` is a measured **85.1 ms median of five** (78.7–98.7 ms), an upper bound on synchronous snapshot + CRC because it also includes USB/poll scheduling. The pre-fix bitwise CRC path was 341.8 ms median. Streaming after `ScreenInfo` remains bounded and non-blocking. |
| **Wire cost** | ~10 % overhead: 7 bytes of framing and 10 of envelope per 182-byte body. |

The device figures above are from the physical Waveshare unit identified by USB
serial `28:84:85:B2:18:A4`; simulator figures are labelled separately.

---

## The trust boundary

**This protocol has no authentication, and it is not going to grow one here.**
CRC-32 and the version byte establish that a frame is well formed and that the
peer speaks this revision. Neither establishes *who* the peer is. `Hello` is
deliberately exempt from the version gate and carries no nonce, no proof and no
host identity, so every privileged opcode below it — screenshot, input
injection, `TimeSync` writing host time as `Manual`/`Trusted`, and the MeshCore
operations that reach a live BLE session — is available to whatever process the
host OS lets open the CDC device. Host-side file permissions constrain
processes *after* the watch has already trusted the host; they are not
device-side owner authentication. A USB port with data lines is a host, whoever
owns the wall it is in.

The answer is therefore configuration, not cryptography: **the endpoint is not
in a product image at all.**

| | production | development / HIL |
|---|---|---|
| built from | `sdkconfig.defaults` alone | `sdkconfig.defaults;sdkconfig.hil` |
| `CONFIG_ATTADIPA_WATCH_CONTROL` | `n` | `y` |
| `attadipa_debug`, `Bridge`, the 411,640-byte snapshot buffer | not linked | linked |
| `Hello`, `Screen`, `Input`, `TimeSync`, mesh opcodes | no code to reach | reachable |
| boot log | `production image: no USB watch-control endpoint` | `HIL image: USB watch-control endpoint ENABLED and unauthenticated` |
| `tools/flash/firmware_elf_check.py` | `--variant flash` requires the dispatcher **absent** | `--variant hil` requires it **present** |

Physical touch, the GPIO0/BOOT and AXP2101 buttons, and the whole sleep and
wake path are in both, because they live in `firmware/main/physical_input.cpp`,
which no Kconfig symbol gates. That separation is what made `n` affordable: it
used to be that turning the endpoint off also turned the buttons off, which is
why the shipping defaults had it on (#346).

**What a product image consequently cannot do.** `TimeSync` and `mesh-configure`
are both debug opcodes, and they were the only way in for the two things a watch
has to be told once. The two are not equally recoverable, and the difference is
worth stating precisely, because a provisioning procedure that half works is
worse than one that visibly does not.

EVERY LINE NUMBER BELOW CARRIES A FINGERPRINT, because this paragraph is the
canonical statement of the change's trust boundary and a bare number rots
silently — it lands on a real, non-blank, wrong line and nothing notices. An
earlier round of this document computed these against a branch while `main` grew
`meshcore_ble.cpp` by forty lines underneath it, and six of thirteen pointed
elsewhere on the merge ref. `tools/docs/check_docs.py` now keeps them.

*The clock survives the round trip.* A production image reads the PCF85063 and
restores a persisted UTC offset — `restore_time_metadata()`
(`waveshare_board.cpp:680` "restore_time_metadata()") is outside the `#if` — but
cannot write the clock or persist an offset, because `BoardTimeSink` and
`save_time_metadata` are inside it. Flashing the HIL image, setting the time, and
flashing back therefore works: the PCF85063 is battery-backed and the offset is
in NVS.

*MeshCore has no round trip at all.* `configure_meshcore_ble()`
(`meshcore_ble.cpp:1328` "bool configure_meshcore_ble") has exactly one caller,
`BoardMeshSink::configure` (`waveshare_board.cpp:378`
"if (!configure_meshcore_ble(passkey))"), inside the same `#if`, so a production
image contains no call to it. What that call sets is per-boot RAM rather than
storage: `configured` and `reconnect_allowed` are `std::atomic_bool{false}`
(`meshcore_ble.cpp:151` "std::atomic_bool configured", `meshcore_ble.cpp:153`
"std::atomic_bool reconnect_allowed"), the `Configure` event is the only thing
that sets `configured` **true** (`meshcore_ble.cpp:1063`
"configured.store(true)", `meshcore_ble.cpp:1064`
"reconnect_allowed.store(true)" — every other write clears them), and
`start_scan()` returns unless both are true (`meshcore_ble.cpp:248`
"void start_scan()"). `CONFIG_BT_NIMBLE_NVS_PERSIST=y` persists bonds, and a
bond buys nothing without a scan.

One other event re-arms `reconnect_allowed`: `ForgetBond`
(`meshcore_ble.cpp:1174` "reconnect_allowed.store(true)"), which is #325's
recovery from a stale bond. It changes nothing here — it is reached only
through `MeshForgetBond`, inside the same `#if`, and it re-arms a scan that
`configured` still gates. A product image cannot reach it and would gain
nothing if it could.

So provisioning over the HIL image does not
survive being flashed away — it does not survive a power cycle of the HIL image
either, which is what shows the round trip never existed. A product image stays
`Unprovisioned` for its whole life and nothing on the watch can change that:
`mesh_screen_requested` (`waveshare_board.cpp:120`
"std::atomic_bool mesh_screen_requested") is set only at `waveshare_board.cpp:381`
"mesh_screen_requested.store(true)", inside the same `#if`, so the mesh screen
never appears.

It still pays for the subsystem. `start_meshcore_ble()` is unconditional
(`attadipa_main.cpp:310` "start_meshcore_ble()", under `CONFIG_BT_NIMBLE_ENABLED`
and `!CONFIG_APP_BUILD_TYPE_PURE_RAM_APP` only), so every product image runs
`nimble_port_init()` (`meshcore_ble.cpp:1268` "nimble_port_init()"), brings the
controller up and creates the `meshcore` task with a 6,144-byte stack
(`meshcore_ble.cpp:1289` "xTaskCreate(mesh_task") for a subsystem that can never
scan. That cost is real and is recorded against
[#356](https://github.com/hleserg/Attadipa/issues/356) rather than removed here:
gating the BLE start is a change to what the product does, and this change is
about the USB control plane.

All of this is a deliberate consequence of the trust boundary rather than an
oversight, and giving a product image its own consented provisioning path — the
only thing that would make mesh reachable on a shipped watch — is #356.

There is no runtime switch, by design. A flag an unauthenticated host could
set itself would be the same control plane wearing a hat.

If the endpoint is ever wanted in a product image, that is a different design —
an authenticated session or a physical-consent gesture — and a different issue.
It is not this document's to invent.

---

## How it works

```
tools/watch_control.py                the command line
  └─ tools/watch/client.py            handshake, requests, retries, PNG
       └─ tools/watch/protocol.py     framing, envelope, pixels — a pure module
            │
            │  Unix socket (simulator)  ·  USB-Serial/JTAG (physical device)
            │
       ┌────┴──────────────────────────────────────────────┐
       │ sim/debug_server.cpp / firmware/main/watch_control.cpp │
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

   Said precisely: the simulator's
   own mouse is LVGL's `lv_sdl_mouse_create()` and reaches LVGL as its **own**
   indev without passing through the queue. On firmware, the FT3168, AXP2101
   power key, GPIO0 boot key and remote bridge all share one `InputQueue` on the
   LVGL task; physical events carry `InputOrigin::Physical`.

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

With the bench Waveshare attached and the development firmware flashed:

```bash
python3 tools/watch_control.py info
python3 tools/watch_control.py screenshot --output artifacts/watch/device.png
python3 tools/watch_control.py tap --x 205 --y 285 --screenshot-after
```

The default is resolved from USB serial `28:84:85:B2:18:A4`, never from a
`ttyACM` number. Use `--serial` or `ATTADIPA_WATCH_SERIAL` for another unit and
`--port` only as an explicit override.

**Naming a unit is binding.** With `--serial` or `ATTADIPA_WATCH_SERIAL` set,
a device that is absent, or a serial that matches two, is an error and the tool
exits 2 -- it does not fall back to a simulator. The fallback belongs to the
case where nobody named anything, because there the bench serial was itself a
guess. This matters on a bench with more than one unit: the T-Watch still
carries its factory image, and a scenario aimed at one board and served by
another is the accident with no undo. `SerialTransport`, screenshots and
remote taps were executed against this physical unit on 2026-08-25; see
[`WATCH_CONTROL_2026-08-25.md`](../hardware/WATCH_CONTROL_2026-08-25.md).
