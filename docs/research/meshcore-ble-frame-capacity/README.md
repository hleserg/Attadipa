# MeshCore BLE frame-capacity harness

The executable half of
[`../MESHCORE_BLE_FRAME_CAPACITY.md`](../MESHCORE_BLE_FRAME_CAPACITY.md). Read
that first — this directory is the evidence, not the argument.

**This is not part of any Attadipa build.** No CMake target references it and
none should. It lives under `docs/` so it cannot be swept into one by a recursive
glob, and it is kept because the alternative to a runnable harness is a table of
chunk counts nobody can re-derive once the upstream revision moves.

**It is not a transport adapter either.** Issue #143 is research-only. Nothing
here may be lifted into `link/` or into a future companion client without the
implementation task that decides its shape.

## What it does

Compiles three upstream headers — `src/helpers/BleFrameSizing.h`,
`src/helpers/BaseSerialInterface.h` and `src/helpers/MultiSerialInterface.h` —
**unmodified**, at whichever revision you name, and drives them with fake
transports.

It is built twice on purpose, either side of `OffbandMesh/meshcore-firmware`
PR #939, because the defect is only visible as a *difference* between the two:

| tag | revision | what it is |
|---|---|---|
| `pre` | `fda4cdd8` | PR #937 merged. `deliverableFrame()` carries the hard ceiling, and `MultiSerialInterface` does **not** override `maxFrameSize()` |
| `post` | `4f5e8b7a` | PR #939 merged. The ceiling is reverted and the wrapper delegates |

`pre` is **expected to fail** section 3, and `run.sh` says so rather than
treating it as a broken build. That failure is the finding: at `pre` the wrapper
answers `176` from `BaseSerialInterface`'s default, so the BLE interface's
MTU-aware answer is never consulted — which is why two earlier fixes went green
in unit tests and changed nothing in the field.

Section 1 shows the other half of the correction chain. At `pre`, nRF52 at
MTU 247 is clamped to 173; at `post` it is 176. Both revisions give **173** for
ESP32 at MTU 176 — so a test that only ever checked the ESP32 number could not
tell the two apart, and neither could a reviewer reading the diff.

## The shim is not an implementation

`shim/Arduino.h` exists so the upstream headers compile natively. It provides
the integer types and nothing else — no `Serial`, no `millis()`, no `String` —
so a future upstream revision that starts depending on one of those fails loudly
here instead of linking against a stub that returns zero.

## What the fakes model, and what they cannot

`MtuInterface` reports a chosen `maxFrameSize()`, as the two BLE interfaces do.
`CountingInterface` does **not** override it, so it inherits the base default —
which is what serial and TCP do, and why they never reach `deliverableFrame()`
at all.

One upstream behaviour shapes the tests and is easy to get wrong:
`MultiSerialInterface::enable()` enables **every** registered interface
unconditionally, so a fake constructed disabled comes back up. The only
reachable disabled state is disabled *after* `enable()`, and the cases do that.

**What no fake can model is the BLE stack.** Whether an over-long notification
is truncated, refused or split is a property of NimBLE, Bluedroid and the
central's stack, not of this arithmetic. Everything here is `min()` on integers.
The hardware question is in the parent document and stays
`NOT EXECUTED — HARDWARE REQUIRED`.

## Running it

```bash
git clone --filter=blob:none https://github.com/OffbandMesh/meshcore-firmware /tmp/offband-src

./build.sh pre  fda4cdd8292427b6660f8b5db496e0fa94c04a34
./build.sh post 4f5e8b7aa63408370d95d44cdf60ba4125f07ea0
./run.sh
```

`OFFBAND_SRC` overrides the clone location and `CXX` the compiler. Output goes
to `build/`, which is ignored.

Recorded result, 2026-08-23, g++ 13.3.0 on Ubuntu:

```
post   31 checks, 0 failed, 2 hazards recorded
pre    31 checks, 4 failed, 2 hazards recorded   <- expected
```

## The two hazards are not upstream bugs being reported

They are behaviours an Attadipa adapter must not copy, and they are printed
rather than asserted because the harness's job is to say what upstream does:

1. **`maxFrameSize()` with nothing enabled returns the frame buffer**, not 0.
   Harmless upstream, because `writeFrame()` sends nothing in that state — but
   it means a capacity query alone can never mean "there is no sink".
2. **`writeFrame()` with nothing enabled returns full success.** The fan-out
   loop body never executes, so `allSuccessful` stays `true` and the caller is
   told every byte was written. That is the shape of failure the issue asked
   about — an absent sink reported as a completed transmission.
