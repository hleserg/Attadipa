# MeshCore v1.16.0 → v1.17.1 (+ `dev`) — upstream review

Owner amendment, 2026-08-21: *"Перед продолжением текущего roadmap остановись и
проведи техническую ревизию актуального upstream MeshCore."* Recorded in
[OWNER_DECISIONS](../research/OWNER_DECISIONS.md); tracked as **T-041**.

**What this document is.** A read of the actual upstream tree and the actual
issue tracker, not of the release notes. Where a claim comes from source it
names the file and line at a commit; where it comes from a pull request or an
issue it names the number and whether that pull request is *merged*. Those two
are not the same thing and this review keeps them apart, because the owner's §3
is right: the latest release is not automatically the best firmware.

**What Attadipa has in its hands.** No Heltec board of any revision, and no
MeshCore device at all. **Nothing below has been executed on hardware by this
project.** Every performance or power number quoted here is upstream's, and is
attributed. Attadipa's own status for all of it is `NOT EXECUTED — HARDWARE
REQUIRED`.

## Revisions read

| What | Commit | Date |
|---|---|---|
| `companion-v1.16.0` | `24fe7d4b2d17a4d6b23eb13931346aebd681014c` | 2026-06-06 |
| `companion-v1.17.0` | `2af6126cfba734d9502368d8a9b3fba049385886` | 2026-08-09 |
| `companion-v1.17.1` | `8fe15c89ed5ac7d0096931b6d609f7b48f51272a` | 2026-08-14 |
| `origin/main` — **Attadipa's pin** | `d92964352441e53b93e8667b802e04f6e072b39e` | 2026-08-14 |
| `origin/dev` at review time | `e0031870f6e94657765a77e5f7676654d465dd86` | 2026-08-20 |

367 commits between v1.16.0 and v1.17.1; 29 more on `dev`.

One housekeeping fact first, because [DEPENDENCIES](../research/DEPENDENCIES.md)
asserts it: Attadipa's pin `d929643` and the tag `companion-v1.17.1` are
*different commit objects with identical trees* — `git diff 8fe15c8 d929643` is
empty and `git describe` returns `companion-v1.17.1` for both. The pin is
therefore the released tree, and the ledger's claim stands.

---

## The short version

Five things matter to Attadipa, in this order:

1. **`MultiSerialInterface` (#3049, merged) is the right idea with a
   broadcast-shaped implementation.** Adopt the idea, not the semantics: it
   writes every reply to *every* enabled interface and ORs `isWriteBusy()`
   across them, so one stalled transport stalls the others. `ADAPT`.
2. **The USB transport has no connection state, no backpressure and
   unresynchronisable framing** — verified in source, not only in #3214. This is
   the single most useful thing in the review, because Attadipa's watch↔node link
   is the same kind of link and can simply not be built that way. `ADAPT` the
   requirements, `REJECT` the implementation.
3. **1.17.x turned the Heltec V4.3 external LNA on by default and then removed
   the companion's ability to turn it off.** Two open issues report the noise
   floor rising 13–22 dB. This is a confirmed, released, unfixed regression, and
   it is the concrete answer to "is the latest release the best firmware". It
   also proves the owner's architectural point: FEM control is a **board**
   capability, not a property of "SX1262". `MONITOR` upstream, `ADOPT` the rule.
4. **`powerOff()` on the Heltec V4-R8 is wake-on-LoRa deep sleep**, so hibernate
   ends at the next packet. Verified in source. The fix (#3168) is open. The
   lesson is the power-state taxonomy the owner asked for. `ADOPT` the taxonomy.
5. **MeshCore already separates wall clock from monotonic clock** — `RTCClock`
   and `MillisecondClock` are distinct interfaces in `src/MeshCore.h`. Attadipa
   should copy that separation outright. `ADOPT`.

And one thing that is *not* true of upstream and should be true of Attadipa: **on
ESP32, MeshCore does not use the hardware RNG and has no hardware AES/SHA.**

---

## 1. Universal Companion — the transport layer

### What changed

PR **#3049** (*"Refactor Companion Interfaces + Add ThinkNode M7 Ethernet
Support"*, Liam Cottle) — **merged** 2026-07-29 into `dev`, released in v1.17.0.
87 files, +849/−410. It does not ship a multi-interface firmware; it makes one
possible. The release notes say so: *"There are no official multi-interface
companion builds yet, but all the 'plumbing' for this is now there."*

The shape, from `src/helpers/MultiSerialInterface.h` at `d929643`:

- `BaseSerialInterface` — `enable/disable/isEnabled`, `isConnected`, `loop`,
  `isWriteBusy`, `writeFrame`, `checkRecvFrame`. Seven virtuals, no state.
- `MultiSerialInterface` — itself a `BaseSerialInterface`, holding up to
  `MAX_INTERFACES` (4) registered children tagged with an `InterfaceType`
  (`Bluetooth`, `USB`, `WiFi`, `Ethernet`, `HardwareSerial`).

### Why it is the right idea

It is exactly the owner's requirement — the transport is not nailed to BLE, and
a node can carry several at once. Attadipa's `Attadipa Watch ↔ BLE/USB ↔ Attadipa
Node ↔ LoRa mesh` needs the same freedom, plus UART and possibly ESP-NOW, and
designing against a single BLE assumption now would cost a rewrite later.

### Four things not to copy

Read from the source, not inferred:

| Behaviour | Line | Consequence for Attadipa |
|---|---|---|
| `writeFrame()` writes the frame to **every enabled interface** | `MultiSerialInterface.h` | A reply to a USB request is also pushed over BLE. With two clients attached, each sees the other's traffic. Attadipa's node link must reply **to the interface the request arrived on**, or be genuinely session-less by design — not by accident |
| `isWriteBusy()` is the **OR** across interfaces | same | One transport that nobody is draining marks the whole stack busy and stalls every paced stream. #3214 hits precisely this and works around it by *dropping* writes while no client is connected. The structural answer is a queue per interface, which is what Attadipa should build |
| `checkRecvFrame()` returns the **first** interface with a frame, in registration order | same | Fixed priority, no fairness. A chatty USB client starves BLE |
| `writeFrame()` returns `len` only if **all** writes succeeded, else `0` | same | A partial multicast is indistinguishable from a total failure, and a caller that checks the return cannot tell which interface failed |

`enableBluetooth()` / `disableBluetooth()` / `isBluetoothEnabled()` also sit on
the generic manager, and `isBluetoothEnabled()` returns the first BLE child's
state rather than any aggregate. Transport-specific methods on a
transport-agnostic manager are the seam where the abstraction starts leaking;
Attadipa should express "turn the radio-bearing transports off" as a policy over
interface *properties*, not as a method named after one of them.

**Status: `ADAPT`.** Take the registry-of-interfaces shape and the
`InterfaceType` tag. Do not take the broadcast write, the OR'd busy flag or the
first-come read. Attadipa's version needs: per-interface bounded queues, a reply
routed to its originating interface, round-robin service, and a per-interface
error rather than a boolean.

---

## 2. USB — the most valuable finding in the review

### The chain, in order

1. **Issue #2734** (open, 2026-06-09): Heltec V4 USB companion did not work with
   `meshcore-cli` on v1.16.0; setting `ARDUINO_USB_MODE=1` fixed it.
2. **PR #3006** (merged, released in v1.17.0) changed exactly one thing:
   `boards/heltec_v4.json`, `-DARDUINO_USB_MODE=0` → `=1`. The issue is still
   open, so the tracker understates what has shipped — a *merged and released*
   fix behind an *open* issue.
3. `ARDUINO_USB_MODE=1` means the ESP32-S3 stops using TinyUSB CDC and uses the
   **USB-Serial-JTAG peripheral (HWCDC)** instead. That peripheral **has no CDC
   line-state** — no DTR — so there is no register that says whether a host has
   opened the port.
4. **PR #3214** (open, 2026-08-14) documents what that costs. Its analysis is
   confirmed against the shipped source at `d929643`:

```cpp
// src/helpers/ArduinoSerialInterface.cpp
bool ArduinoSerialInterface::isConnected() const {
  return true;   // no way of knowing, so assume yes
}
bool ArduinoSerialInterface::isWriteBusy() const {
  return false;
}
```

Both are unconditional. The consequences upstream reports, and which follow
directly from those two lines:

- `MyMesh` only calls `_ui->notify()` when no client is connected, so **message
  notifications never fire** on a USB companion build.
- `UITask` shows `< Connected >` instead of the BLE pairing PIN, so on a
  combined BLE+USB build **a freshly flashed device cannot be paired at all**.
- `CMD_GET_CONTACTS` streams up to `MAX_CONTACTS` frames back to back. The gate
  is `!_serial->isWriteBusy()`, which is always false, and `writeFrame()` never
  consults `availableForWrite()`. Against a 256-byte ESP32 TX ring, a host that
  stalls past the write timeout makes HWCDC latch itself disconnected and
  `flushTXBuffer()` **discards queued bytes mid-frame**.

### The framing, read rather than assumed

```
outbound:  '>' , len_lo , len_hi , payload
inbound:   '<' , len_lo , len_hi , payload
```

`MAX_FRAME_SIZE` is 176. There is **no checksum, no escaping and no resync
marker**. Two further properties of `checkRecvFrame()` that #3214 does not
mention and that matter more than the ones it does:

- A frame longer than `MAX_FRAME_SIZE` is **silently truncated and delivered as
  if complete** — `if (_frame_len > MAX_FRAME_SIZE) _frame_len = MAX_FRAME_SIZE;
  memcpy(dest, rx_buf, _frame_len); return _frame_len;`. A corrupted length field
  therefore produces a plausible short frame rather than an error.
- After a torn frame the parser resynchronises on the next byte that happens to
  equal `'<'` — which is `0x3C`, a byte that occurs freely inside binary
  payloads. So "resynchronisation" is a coin flip that can land inside a
  payload and manufacture another false frame.

For contrast, the same codebase does this correctly one file away.
`src/helpers/esp32/SerialBLEInterface.h` carries a **bounded** four-frame send
queue and a four-frame FreeRTOS receive queue, and logs and drops on overflow
(`"writeFrame(), send_queue is full!"`, `"onWrite(), recv_queue is full!"`). The
problem is not that upstream does not know how; it is that the USB path never
got the same treatment.

### What Attadipa takes

**Status: `ADAPT` the requirements, `REJECT` the implementation.**

Attadipa's node link is the same kind of link — a small MCU streaming structured
records to a host that can stall — so these are requirements from day one, and
they belong in the node protocol ADR rather than in a driver:

1. **Connection state must be observable, or explicitly `Unknown`.** Never a
   hardcoded `true`. Where the peripheral genuinely cannot say (USB-Serial-JTAG),
   the honest answer is a *liveness* signal — "a peer has sent us a frame within
   T" — and the state must be named so that a reader knows it is inferred.
2. **Framing must be resynchronisable.** A length prefix alone is not. At
   minimum: a start marker that cannot occur unescaped in a payload, a length,
   and a checksum over both, so a torn frame is *detected* rather than
   re-parsed. Over-long frames are an error, never a truncation.
3. **Backpressure must be real.** `isWriteBusy()` has to reflect the transport,
   and a frame is written whole or not at all.
4. **Every queue is bounded and every drop is counted.** BLE's shape, applied
   everywhere.
5. **A stalled transport must not stall the others.** Per-interface queues.

Note the provenance: #3214 and #3168 are marked `🤖🤖` under upstream's
`CONTRIBUTING.md` — AI-prepared patches from a fork, reviewed and bench-tested by
their author, **not merged**. Their *analysis* is confirmed here against the
shipped source, which is why it is used. Their *code* is not upstream and must
not be treated as though it were.

---

## 3. LoRa: preamble detection, listen-before-talk, CAD

### What changed

v1.17.0 rewrote collision avoidance (#3036 *missing preamble detect IRQ bit*,
#2977 *preamble and header IRQ rx timeout logic*), and v1.17.1 extended it to
the LR2021 (#3146). The release notes credit weeks of diagnostics and
simulation, and state the outcome plainly:

> *"he found that the new scheme performs on par with hardware CAD, but without
> the 4 second lock-up glitches that CAD still suffers from. So, we are still
> leaving hardware CAD off by default."*

### How it works, from source

`startReceive()` is overridden per radio to ask for the `PREAMBLE_DETECTED` IRQ
bit in addition to the RadioLib defaults. `isReceiving()` (e.g.
`src/helpers/radiolib/CustomLLCC68.h`) then runs a small state machine over three
IRQ bits — `PREAMBLE_DETECTED`, `HEADER_VALID`, `HEADER_ERR` — with **two
deadlines**:

- a preamble seen but no valid header within `_preambleMillis` → clear the
  preamble IRQ and declare the channel free;
- a valid header with no packet within `_maxPayloadMillis` → clear everything and
  declare the channel free.

Both deadlines are computed from the actual modulation:
`calcMaxPacketMillis(sf, bw, cr, preambleSymbols)` returns
`{preambleMillis, payloadMillis}` from the symbol time. The whole design exists
because those IRQ bits get *stuck*, and a transmitter that trusts a stuck bit
never transmits again.

Channel-busy itself is two independent tests (`RadioLibWrapper::isChannelActive`):

```cpp
if (_threshold != 0 && getCurrentRSSI() > _noise_floor + _threshold) return true;  // int.thresh
if (_cad_enabled) { ... performChannelScan() ... }                                 // off by default
```

The noise floor is sampled adaptively and clamped at −120 dBm, and #3158's
sibling fix is visible in `resetAGC()`: the floor is reset to 0 on AGC reset,
with the reason in the comment — a stuck −120 floor makes the sampling threshold
too low to accept normal samples, so it re-reinforces itself.

**Status: `ADAPT`, and do not write our own.** The owner's instruction —
*"Не реализовывай собственный LBT, пока не станет понятно, что можно безопасно
взять из MeshCore"* — is correct and this review's answer is: take the *scheme*,
including both watchdog deadlines and the airtime-derived timeouts, and take the
noise-floor reset reasoning. Attadipa's radio path is not written yet
([ADR-0003](../adr/0003-radio-not-lora.md) is still about which chip is even
fitted), so nothing is being replaced.

**Hardware CAD: `MONITOR`.** Upstream ships it off (#3121, *"companion, hardware
CAD disabled"*). Treat it as experimental exactly as the owner says, and never
enable it on a wrist device without a measurement of the 4-second lock-up.

---

## 4. Heltec V4 FEM / LNA — the released regression

This is the section the owner's §3 exists for.

### The board is two boards

`variants/heltec_v4/LoRaFEMControl.cpp` **auto-detects the front-end at boot**
from the default pull level of a shared GPIO:

```
GC1109  CSD: internal pull-down → reads LOW  → V4.2
KCT8103L CSD: internal pull-up  → reads HIGH → V4.3
```

and only the KCT8103L branch calls `setLnaCanControl(true)`. So on the *same
product name*, one revision can switch its external LNA and the other cannot,
and the firmware finds out by probing a pin. Attadipa readers should recognise
the shape: it is the T-Watch radio problem in a different package
([ADR-0003](../adr/0003-radio-not-lora.md)), and it is the reason the owner is
right that FEM control must be a **board capability**, never an assumption
attached to "SX1262".

### The regression

| Step | Evidence |
|---|---|
| v1.16.0 companion firmware had **no FEM preference at all** | `git show companion-v1.16.0:examples/companion_radio/MyMesh.cpp \| grep -c fem` → `0` |
| `e2aa7b98` (2026-08-10) added FEM prefs to the companion **defaulting to on** | `_prefs.radio_fem_rxgain = 1;` at `examples/companion_radio/MyMesh.cpp:888`, and the same line in `simple_repeater` and `simple_sensor` |
| The value is applied to the board | `board.setLoRaFemLnaEnabled(_prefs.radio_fem_rxgain);` at `MyMesh.cpp:979` |
| #3137 (merged) fixed `fem_rxgain` being serialised from `rx_boosted_gain` — so before it, `set radio.fem.rxgain` never survived a reboot | PR body; `def("fem_rxgain", _parent->radio_fem_rxgain)` in `src/helpers/CommonCLI.h:86` |
| #3203 then **disabled the companion's FEM prefs entirely** | `examples/companion_radio/NodePrefs.h`: the two `def()` calls sit inside `#if 0`, commented *"these cannot be set (yet) so don't load/save until we can. also, fem_rxgain WAS mapped to wrong JSON property previously"* |

Net effect on a Heltec V4.3 companion running 1.17.1: **the external LNA is on,
and there is no way to turn it off.** Two open issues report the symptom:

- **#3010** (open, 2026-07-20) — predicted it before release: *"When the LNA is
  enabled the noise floor increases dramatically from ± −115 dB to −95 dB … It is
  also generally known that a Heltec v4.3 performs better with the LNA set to
  OFF, the sx126x already has a LNA on its own."*
- **#3232** (open, 2026-08-17) — *"after updating from 1.17.0 to 1.17.1 the noise
  floor went from −108 dB to −86 dB."*
- **#3160** (open, 2026-08-10) — Heltec V4.3 companion loses its LoRa settings
  across reboot on 1.17. Consistent with the pref-serialisation churn, not yet
  proven to be the same cause. Recorded as **related, unconfirmed**.

### Status

| Item | Status |
|---|---|
| "FEM/LNA control is a board capability, expressed per board" | **`ADOPT`** — as a rule, immediately, in Attadipa's capability model |
| "the same product name can carry two different front-ends, detected at runtime" | **`ADOPT`** — it is direct support for [ADR-0003](../adr/0003-radio-not-lora.md)'s stance |
| Upstream's current FEM default and pref plumbing | **`REJECT`** — do not port. It is a released regression with the user's control removed |
| #2713 (*"Enable LNA by default t096, tracker v2, heltec v4"*, open, body: *"Not sure why it was disabled again"*) | **`REJECT`** — a default-on LNA proposed with no measurement, against two issues that have one |
| #3137's merged fix | **`MONITOR`** — correct, and a good example of a *pref key changing meaning on upgrade*, which is a migration hazard Attadipa must design for |
| Recommending a Heltec V4/V4.3 firmware upgrade to anyone | **not recommended today**, on the strength of #3010 and #3232 |

One documentation error, worth knowing before quoting the blog: the v1.17.1
release notes say *"The `radio.fem.rxgain` setting was not being persisted
properly in v1.17.1, this is now fixed"*. It was v1.17.**0** that had the bug;
v1.17.1 is the fix.

---

## 5. Power management

### The verified bug

`variants/heltec_v4_r8/HeltecV4R8Board.cpp` at `d929643`:

```cpp
void HeltecV4R8Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
  ...
  loRaFEMControl.setRxModeEnableWhenMCUSleep();
  esp_sleep_enable_ext1_wakeup((1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
  ...
}
void HeltecV4R8Board::powerOff() { enterDeepSleep(0); }
```

`powerOff()` — hibernate, "off" as a user understands it — is literally the
wake-on-LoRa sleep path: the FEM is left in RX and DIO1 is armed as a wake
source. Any received packet reboots the device. Issue **#3165** reports it,
PR **#3168** fixes it (**open**), and `ESP32Board::powerOff()` in the base class
does the right thing (`enterDeepSleep(0)` there disables all wake sources
explicitly with `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`).

The root cause is architectural rather than local: `c644720e` moved "screen and
radio power off" out of `UITask` and into the boards, and one board's override
then silently reused the wrong primitive. **Two behaviours that differ only in
their wake sources were given one function name.**

### The taxonomy, which is the real deliverable

The owner's list is right and this review adopts it verbatim as Attadipa's power
model:

| State | Radio | Wake sources | Notes |
|---|---|---|---|
| `ACTIVE` | RX/TX | — | screen on |
| `IDLE` | RX | — | screen off, CPU awake |
| `LIGHT_SLEEP` | RX | timer, button, radio IRQ | RAM retained |
| `MESH_LISTEN_SLEEP` | **RX, FEM held in RX across sleep** | **radio DIO**, timer, button | this is upstream's `enterDeepSleep(secs)` |
| `HIBERNATE` | **off, FEM shut down and latched** | button / RST only | this is what `powerOff()` must be |
| `POWER_OFF` | off | external only | PMU-level where the hardware allows it |

The distinction that upstream lost is between the fourth and fifth rows.
Attadipa's `PowerState` must make it a type error rather than a naming
convention, and the board layer must not be able to satisfy `HIBERNATE` by
arming a radio wake.

### The rest of the power PRs

All **open**, none released. Read for their reasoning, not for their code:

| PR | What it says | Status for Attadipa |
|---|---|---|
| **#1347** *Powersaving on companion when serial is off* | 30-minute ESP32-S3 light sleep when the serial interface is off and GPS is off; wake on LoRa ext1, timer or button. Also finds that `SerialWifiInterface::disable()` *only set a flag* — the Wi-Fi radio stayed powered. And that a button and DIO1 **cannot share an ext1 mask** because they need opposite trigger levels | `ADAPT`. The ext1-polarity fact is a hardware fact worth carrying; the "disable() did not disable" bug is the general lesson that a power API must be verified by measurement, not by reading its name |
| **#1686** *Short sleeps when phone disconnects* | 12 s sleep / 3 s awake once no phone has connected for 60 s, restarting BLE advertising each wake because light sleep powers the BLE radio down | `MONITOR`. Directly relevant to a watch↔node link that idles most of the day, but the duty cycle has to be chosen against Attadipa's own reconnect latency budget, not inherited |
| **#2627** *power brownout causing early shutdown* | Battery poll every 8 s with no awareness of transmit; on a Heltec V4 below ~50 % the sag during TX trips the low-battery shutdown. Also: `shutdown()`/`reboot()` do not flush the 5-second lazy contact write | `ADOPT` both rules: **never sample the battery during transmit**, and **flush dirty state on every shutdown path** |
| **#2689** *HeltecV4 battery ADC accuracy* | `ADC_MULTIPLIER` 5.42 → 4.9, from the R27/R28 divider `(390k+100k)/100k`, and `analogReadMilliVolts()` at 12-bit instead of `analogRead()` × 3.3/1024 | `ADOPT` the method. Note the V4-R8 board **already** uses `4.9f * 1.035f` with `analogReadMilliVolts` at 12-bit while plain V4 still uses the 10-bit path — so upstream disagrees with itself across two files for the same divider |

---

## 6. Storage and configuration

### What changed

v1.17.0 moved node config to **JSON** (#2982) with automatic migration: on boot,
`CommonCLI::loadPrefs()` reads `/prefs.json` if it exists, otherwise reads the
legacy `/com_prefs` binary blob and writes the JSON — and **deliberately leaves
the old file in place**, with the `fs->remove("/com_prefs")` commented out so a
downgrade still works. That is a good migration: forward-convert, do not destroy
the source.

The legacy loader is worth reading once as a cautionary tale. It is a
fixed-offset byte stream with hand-written offsets in comments, and it already
contains the scar of one migration: at offset 79, *"1 byte unused (was
rx_boosted_gain in v1.14.1, moved to end for upgrade compat)"*. After load, every
field is `constrain()`-ed to a sane range — which Attadipa should copy, since
[ADR-0006](../adr/0006-settings-and-bounded-values.md) already says bounded
values are the settings model.

### What is still not crash-safe

**PR #1447** (open) makes *contacts* atomic — temp file, flush, rename original
to `.bak`, rename temp into place, and fall back to `.bak` on load if the
primary is missing or empty. At `d929643` that has not landed, and
`DataStore::saveContacts()` still opens `/contacts3` for writing **in place** and
streams records into it, with `if (!success) break;` leaving a partially written
live file and no rollback.

Worse, and not covered by #1447 at all: `CommonCLI::savePrefs()` on nRF52 and
STM32 does

```cpp
fs->remove("/prefs.json");
File file = fs->open("/prefs.json", FILE_O_WRITE);
```

— it **deletes the config and then writes a new one**. Power loss in that window
loses the configuration outright. This may well be what #3160 (*"Heltec V4.3
companion lose LoRa settings after reboot"*) is; that board is ESP32 and takes
the `"w", true` branch instead, so it is a hypothesis rather than a diagnosis and
is recorded as such.

**Status: `ADOPT` the pattern from #1447, and apply it more widely than upstream
does.** Attadipa's rule, for every persistent structure and not only contacts:

```
write temp → flush → fsync/close → rename old to backup → atomic rename temp into place → drop backup
load: primary, else backup, else defaults — and say which one was used
```

with the platform guard #1447 identifies: it needs roughly 2× the storage during
the save, so a filesystem too small for that must be detected rather than
assumed.

Also `ADOPT`: **flush dirty state on every shutdown and reboot path** (#2627),
and **never destroy the old format during a migration** (#2982).

---

## 7. Time

`src/MeshCore.h` already draws the line the owner asks for:

- `mesh::RTCClock` — `getCurrentTime()` / `setCurrentTime()`, UNIX epoch
  seconds. Absolute time.
- `mesh::MillisecondClock` — `getMillis()`. Monotonic.

`VolatileRTCClock` in `src/helpers/ArduinoHelpers.h` is the fallback when there
is no RTC: it keeps a base epoch plus a millisecond accumulator ticked from
`millis()`, so setting the time re-bases without disturbing elapsed
measurements. `AutoDiscoverRTCClock` probes for a real RTC at boot.

v1.17.0 also fixed **#2937**, *"GPS time sync stall on long uptime nodes"* — the
classic 32-bit `millis()` wrap or an absolute-deadline comparison that stops
being true after enough uptime.

**Status: `ADOPT`.** Attadipa takes the two-clock separation as an architectural
rule, and the rule the owner states with it: **timers, timeouts, retries,
connection expiry and the scheduler use the monotonic clock; RTC and GNSS time
are used only where absolute time is genuinely required.** A GNSS fix that steps
the wall clock backwards must not be able to make a timeout fire late — or
never. This connects to T-042: GNSS *time validity* is one of the integrity
signals, and a clock that jumps is itself evidence.

---

## 8. RNG and crypto

### What upstream has

- **nRF52 got hardware crypto in v1.17.0** — #2824, *"nrf52 targets now use CC310
  hardware crypto functions"*, behind `USE_CC310_HW_CRYPTO` in `src/Utils.cpp`.
  v1.17.1 followed with #3154 (call `nRFCrypto.begin()/end()` once, not per
  operation) and #3206 (the nRF `RadioNoiseListener` now combines radio entropy
  *with* CC310).
- **ESP32 got neither.** Verified by grep across `src/`, `examples/` and
  `variants/` at `d929643`: `esp_random` / `esp_fill_random` appear **only** in
  the two ESP-NOW variants (`variants/generic_espnow/target.cpp`,
  `variants/sensecap_indicator-espnow/target.cpp`). There is **no** `mbedtls`,
  `esp_aes` or `esp_sha` anywhere in the tree.

So on an ESP32 LoRa node the RNG is:

```cpp
// src/helpers/radiolib/RadioLibWrappers.h — RadioNoiseListener
dest[i] = _radio->randomByte() ^ (::random(0, 256) & 0xFF);
```

radio noise XOR the **Arduino PRNG**, whose header comment says of itself:
*"NOTE: this is VERY SLOW! Use only for things like creating new
LocalIdentity"*.

**PR #2280** (open) proposes the replacement: an ascon-XOF-based PRNG seeded from
whatever hardware RNG the platform has (ESP32, nRF52, STM32 and RadioLib's),
with entropy persisted across reboots, plus an on-device crypto test suite whose
output is in the PR body. Its own summary of the status quo is *"slow and
potentially biased (modulo arithmetic) … 'shake & pray'"*.

### Status

| Item | Status |
|---|---|
| The `mesh::RNG` interface — one virtual, `random(uint8_t*, size_t)` | **`ADOPT`** the shape. It is the right seam for swappable backends |
| ESP32 must use `esp_fill_random()` as its entropy source | **`ADOPT`**, and it is a *gap* rather than a port: upstream does not do this on the LoRa path |
| ESP32-S3 hardware AES/SHA | **`ADAPT` — and verify first.** Nothing in MeshCore uses it, so there is nothing to copy. Whether the S3's accelerators help here has to be *measured* against the software path before any of it is designed around. Recorded as UNKNOWN, not as an opportunity |
| #2280's ascon-XOF construction | **`MONITOR`** — unmerged. The idea (seed once from hardware, squeeze thereafter, persist entropy) is sound and cheap; the code is not upstream |
| A crypto API bound to one backend | **`REJECT`.** Attadipa's crypto interface must admit `software`, `ESP32-S3 HW` and `nRF52 CC310` behind one seam, exactly as the owner specifies — which is what upstream's `USE_CC310_HW_CRYPTO` `#ifdef` sprawl in `src/Utils.cpp` argues *against* by example |

**Never do this before it is measured:** claim hardware acceleration is faster.
On these parts a hardware AES block can lose to software for short messages once
the driver's setup cost is counted, and MeshCore's payloads are ≤ 176 bytes.

---

## 9. BLE

Merged and released in v1.17.0: **#3005** *ESP32 BLE bonded reconnects fix* and
**#3007** *ESP32 BLE receive queue synchronization*. **PR #2333** (*BLE ghost
connection fix*) is **still open**, and its root-cause list is the useful part:

1. `onDisconnect()` did not reset `deviceConnected` unconditionally, so a
   disconnect arriving before `enable()` left stale state;
2. `onAuthenticationComplete()` set `deviceConnected = true` even when the
   interface was not enabled, letting a bonded phone "ghost-connect" before the
   device was ready;
3. `deviceConnected`, `oldDeviceConnected` and `adv_restart_time` were not reset
   in `begin()`, so RAM state survived a reset.

All three are the same mistake: **connection state that is not owned by one
place and not reset at one point.** Attadipa's link state must be a single state
machine with an explicit reset, not a set of booleans updated from callbacks.

What upstream's BLE path already does right, and Attadipa should copy: bounded
queues in both directions with explicit overflow drops (§2 above), and a
`writeFrame()` that returns 0 rather than blocking when the queue is full.

**Status: `ADOPT`** the queue discipline and the state-ownership lesson;
**`MONITOR`** #2333 itself.

---

## 10. What Attadipa already does right

Recorded because the owner asked for it, and because it is short:

- **The two-clock separation** is about to be adopted rather than rediscovered.
- **Bounded settings.** [ADR-0006](../adr/0006-settings-and-bounded-values.md)
  already requires every setting to carry its range, which is what upstream
  achieves after the fact with a wall of `constrain()` calls on load.
- **The transmit path stays closed until the region is known** (A4). Upstream
  has no equivalent, and the FEM regression above is the same class of problem:
  a radio parameter changed by default with no way for the user to see or revert
  it.
- **Capabilities are not assumed from a chip name.**
  [ADR-0003](../adr/0003-radio-not-lora.md) exists because the T-Watch ships one
  of five radios; the Heltec V4's two front-ends are the same lesson from a
  different vendor, and it means the FEM rule the owner asks for is a
  *confirmation* of Attadipa's model rather than a change to it.
- **The layer boundary is enforced by the build**
  ([ADR-0007](../adr/0007-two-capability-layers.md) §5), and the second boundary
  — the core does not speak English — landed with T-033. Upstream's equivalent
  boundary is a convention, which is how `powerOff()` came to mean
  wake-on-LoRa sleep on exactly one board.

## 11. What Attadipa must change

Each of these is filed as its own small task in [TASKS.md](../../TASKS.md); none
of them is code today, because none of the affected subsystems exists yet, and
that is the cheapest moment to get them right.

| # | Change | Why now |
|---|---|---|
| T-043 | **Transport abstraction for the node link** — several interfaces, per-interface bounded queues, replies routed to the originating interface, round-robin service | §1, §2. Designing against BLE alone would cost a rewrite |
| T-044 | **Framing requirements in the node protocol ADR** — resynchronisable, checksummed, over-long is an error | §2. [ADR-0005](../adr/0005-node-protocol.md) does not state them yet |
| T-045 | **`PowerState` taxonomy**, with `MESH_LISTEN_SLEEP` and `HIBERNATE` as separate states a board cannot conflate | §5 |
| T-046 | **Crash-safe persistence rule** for every persistent structure, with the storage-headroom guard | §6 |
| T-047 | **Monotonic vs wall clock rule**, written into the architecture and enforced by review | §7 |
| T-048 | **Crypto and RNG seam** with three backends named, ESP32 hardware RNG as the entropy source, and no performance claim before measurement | §8 |
| T-049 | **FEM/front-end control as a board capability** in the hardware model, never inferred from the radio chip | §4 |
| T-050 | **`MeshCore Adapter` boundary** — the layer diagram in §12, so MeshCore can be bumped without touching Attadipa's UI or HAL | owner §4 |

## 12. The compatibility boundary

The owner's requirement, as Attadipa's layer diagram:

```
Attadipa UI / Apps
        ↓                      knows StringId and Capability. Nothing else.
Attadipa Services
        ↓                      LocationService, MeshService, SettingsService
Mesh Service API               ← the boundary MeshCore may not cross
        ↓
MeshCore Adapter               ← the only code that includes a MeshCore header
        ↓
Radio / Companion transports
        ↓
HAL
        ↓
ESP32-S3 / SX1262 / BLE / USB / sensors
```

The rule is the one Attadipa already enforces twice: **a layer may not learn
where its answer came from.** An application asks for `Capability::MeshMessaging`
and never learns that MeshCore answered, exactly as it never learns which GPIO
powers the GNSS module and never learns which language it will be rendered in.

The `Mesh Service API` line is the one that has to be enforced mechanically
rather than by review, and the mechanism already exists in this repository: a
`PRIVATE` link plus a boundary test that compiles a fixture which must fail
([ADR-0007](../adr/0007-two-capability-layers.md) §5, `tests/boundary/`). T-050
is that test, written before there is an adapter to break.

---

## Summary table

| Item | Upstream state | Attadipa |
|---|---|---|
| `MultiSerialInterface` / multi-transport (#3049) | merged, released 1.17.0 | `ADAPT` — idea yes, broadcast/OR semantics no |
| USB connection state + backpressure (#3214) | **open**, AI-prepared, unmerged | `ADAPT` requirements · `REJECT` code |
| `ARDUINO_USB_MODE=1` on Heltec V4 (#3006 / issue #2734) | merged, released; **issue still open** | `MONITOR` — context for §2 |
| Preamble-detect LBT (#3036, #2977, #3146) | merged, released | `ADAPT` — take the scheme and both deadlines |
| Hardware CAD (#3121) | shipped **off** | `MONITOR` — experimental |
| AGC reset / noise-floor reset (#3158) | merged, released 1.17.1 | `ADOPT` the reasoning |
| FEM RX gain default-on + pref disabled (#3137, #3203, issues #3010, #3232) | released **regression**, unfixed | `REJECT` the implementation · `ADOPT` the board-capability rule |
| LNA default-on proposal (#2713) | open, no measurement | `REJECT` |
| V4-R8 hibernate wakes on LoRa (#3165 / #3168) | issue confirmed in source; fix **open** | `ADOPT` the power taxonomy |
| Companion light sleep (#1347, #1686) | **open** | `ADAPT` / `MONITOR` |
| Brownout during TX + flush on shutdown (#2627) | **open** | `ADOPT` both rules |
| Heltec V4 battery ADC (#2689) | **open**; V4-R8 already fixed in-tree | `ADOPT` the method |
| JSON config + non-destructive migration (#2982) | merged, released | `ADOPT` |
| Crash-safe contacts (#1447) | **open** | `ADOPT`, and widen to all persistent state |
| `RTCClock` vs `MillisecondClock` | in-tree, both releases | `ADOPT` |
| GPS time-sync stall on long uptime (#2937) | merged, released | `ADOPT` the lesson |
| nRF52 CC310 crypto (#2824, #3154, #3206) | merged, released | `MONITOR` — not our platform yet |
| ESP32 hardware RNG | **absent** on the LoRa path | `ADOPT` as a gap to fill |
| ESP32-S3 hardware AES/SHA | **absent everywhere** | UNKNOWN — measure before designing |
| ascon-XOF PRNG (#2280) | **open** | `MONITOR` |
| BLE bonded reconnect / rx queue (#3005, #3007) | merged, released | `ADOPT` the queue discipline |
| BLE ghost connection (#2333) | **open** | `MONITOR` — take the state-ownership lesson |
| `dev` since 1.17.1 (29 commits) | new boards, display, sensors, LR2021 TX-power fix `114093ee` | nothing architectural for Attadipa |

## Evidence and honesty

Every line above is either a citation to an issue or pull request by number with
its state at 2026-08-21, or a read of the tree at `d929643`. Where a claim rests
on a pull request that is not merged, it says so, and the pull request's *code*
is not proposed for use.

**No part of this review has been executed on hardware by this project.** No
Heltec board of any revision is in Attadipa's hands, and no MeshCore device has
been powered on here. Upstream's measurements are attributed to upstream. The
Attadipa status for every item in the summary table remains `NOT EXECUTED —
HARDWARE REQUIRED` until it is not.
