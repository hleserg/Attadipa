# What opening the watch's USB port does, and what survives it

Research for [#636](https://github.com/hleserg/Attadipa/issues/636). It changes
no production code. It answers one question: when `watch_control` opens an
ESP32-S3 native USB-Serial/JTAG endpoint on one of this bench's two watches,
what happens to the device, to the host's descriptor, and to any command that
was in flight?

**The answer, in one paragraph.** On Linux the DTR/RTS assertion that resets an
ESP32-S3 over native USB is issued by the **kernel**, from inside `open(2)`,
before pyserial runs and before any application flag can intervene. The
USB-Serial/JTAG peripheral interprets RTS as a core reset, and the ESP32-S3 has
no register bit to refuse it. Therefore on the Waveshare — where the control
requests are accepted — **every `watch_control` invocation is expected to reboot
the watch twice: once at open and once at the last close**, and this repository
already measured both halves in a different context without connecting them to
the watch-control path. On the T-Watch S3 Plus the same open is expected to
**fail outright** rather than reset, because that unit refuses every CDC
`SET_CONTROL_LINE_STATE` request. Neither expectation has been run through
`watch_control` on either board: **NOT EXECUTED — HARDWARE REQUIRED.**

The practical consequence is not the reset. It is that a reset at open is
*invisible*: the handshake that follows succeeds against the freshly booted
firmware, so the tool reports a healthy connection and the operator reads
post-reboot state as the state they left behind.

---

## 0. Provenance

| Source | Revision | Licence | How it was read |
|---|---|---|---|
| Linux kernel | `v6.17`, `drivers/tty/tty_port.c`, `drivers/usb/class/cdc-acm.c` | GPL-2.0 | source read 2026-10-01 over the GitHub API; no code taken |
| `espressif/esptool` | `c85144be5f4edff13e818033a7a9a060f2f607cb` | GPL-2.0-or-later | `docs/en/esptool/advanced-options.rst` and `esptool/reset.py` read 2026-10-01 |
| `espressif/esp-pylib` | `e15358d5fedc7d6766ed4493c11851096b8b0291` | Apache-2.0 | `esp_pylib/serial_reset.py` read 2026-10-01 — where esptool's reset primitives now live |
| `espressif/esp-idf` | `v5.5.5` | Apache-2.0 | `components/soc/esp32s3/register/soc/usb_serial_jtag_reg.h`, the same file for `esp32c6`, and `docs/en/api-guides/usb-serial-jtag-console.rst` |
| `pyserial/pyserial` | `a5c48d445fbc1943d4fabf8d9090a50fda3172fd` | BSD-3-Clause | `serial/serialutil.py` and `serial/serialposix.py` read 2026-10-01 |
| MeshCadet, `jagoda/meshcadet` | PRs [#208](https://github.com/jagoda/meshcadet/pull/208) `0813abaf`, [#209](https://github.com/jagoda/meshcadet/pull/209) `6c5b81b4`, [#211](https://github.com/jagoda/meshcadet/pull/211) `8dd22d00`, [#212](https://github.com/jagoda/meshcadet/pull/212) `76d380d5`, [#213](https://github.com/jagoda/meshcadet/pull/213) `3edd6bb2` | GPL-3.0-only | all five confirmed `merged` over the API 2026-10-01; the closeout `docs/provisioning-connect-verification-kit.md` read at `3edd6bb2`. No code taken |
| Offband `meshcore-firmware` | [#1295](https://github.com/OffbandMesh/meshcore-firmware/pull/1295), merged `1b801a8f` | MIT | `boards/heltec-rc32.json`, `variants/heltec_rc32/platformio.ini` and `tools/diag/rc32-tester/sniffer/capture.py` read 2026-10-01. No code taken |
| MeshcoreChatter | [#3](https://github.com/vinceneil666/MeshcoreChatter/pull/3), merged `ff166a7c` 2026-09-14 | **none found** — the GitHub API returns `license: null` | PR body and diff read 2026-10-01. Evidence only; not a reuse candidate |
| Atta-dipa | `main@ee0c9a6` | this repository | source read; no device touched |

MeshCadet is **GPL-3.0-only**. This repository is `GPL-3.0-or-later`, so taking
its code would narrow the whole project to GPL-3.0-only. That is a second reason
not to, on top of the one that matters here: a different transport stack.

## 1. Has the finding gone stale?

`main` is 21 commits past the reviewed `39898a1`. The issue's applicability
claims were checked against `main@ee0c9a6` one at a time.

**Standing.** `tools/watch/client.py` has not changed since `39898a1` at all.
`tools/watch/client.py:127` — "self._serial = serial.Serial(port, baud, timeout=0)"
is still the whole of the open policy, and `request()` still retries on the same
transport instance without closing, re-resolving or reopening.

**Standing, and the issue is right to call it not-a-gap.** The 2026-09-28
correction says this client already survives MeshCadet #212's retry-boundary
byte loss. Confirmed: one decoder is constructed per connection at
`tools/watch/client.py:352` — "self._decoder = p.FrameDecoder()" — and
`request()` never replaces it, so a reply split across a retry boundary stays in
the decoder. Each attempt also draws a fresh id at
`tools/watch/client.py:513` — "req_id = self._allocate_req_id()" — so a late
reply to attempt 1 cannot be read as the answer to attempt 2.

**Moved.** `ramhold.py::resolve_port` is still there at
`tools/flash/ramhold.py:53` — "def resolve_port(serial: str) -> str:" — but it no
longer matches the way the issue read it: #731 changed the by-id comparison from
a substring test to equality on the serial between the last `_` and `-if`.

**Moved, and it closes half of the issue's own reuse recommendation.** The issue
proposes `resolve_port` as a local reuse candidate for the watch-control path.
It is already reused there: `tools/watch_control.py:39` —
"from flash.ramhold import DEFAULT_SERIAL, resolve_port  # noqa: E402" — and the
call is at `tools/watch_control.py:866` —
"args.port = resolve_port(named or DEFAULT_SERIAL)". So identity resolution is
done. What is missing is only the *re-resolution after a reset* already
described at `tools/flash/flash_no_reset.py:60` —
"a reset re-enumerates the USB-Serial/JTAG device".

**Neither the issue nor its producer is wrong about the code.** The finding has
not gone stale. What has changed is that two of its five research questions can
now be answered from this repository's own existing measurements rather than
from new hardware work, and §3 does that.

## 2. The mechanism, layer by layer

Four independent layers act on the two control lines. Only one of them is ours.

### 2.1 The peripheral interprets RTS as a core reset

Espressif's own documentation, `docs/en/esptool/advanced-options.rst` at
`c85144b`:

> With USB-Serial/JTAG, the peripheral interprets the RTS serial control
> signal as a core reset. This reset does not re-sample the boot strapping
> pins, so a chip that entered download mode manually may remain there.

Espressif's own reference sequence agrees on the ordering, and names which
transition is the reset. `esp_pylib/serial_reset.py::usb_jtag_bootloader_reset`
at `e15358d5`, with `PIN_LOW = True` and `PIN_HIGH = False` defined in the same
file as pyserial's booleans:

```python
set_rts(port, PIN_HIGH)
set_dtr(port, PIN_HIGH)  # Idle
time.sleep(settle_delay)
set_dtr(port, PIN_LOW)  # Set IO0
set_rts(port, PIN_HIGH)
time.sleep(settle_delay)
set_rts(port, PIN_LOW)  # Reset (calls inverted to traverse (1,1) instead of (0,0))
set_dtr(port, PIN_HIGH)
```

Read in pyserial terms: the step Espressif labels `# Reset` is `rts = True`
while DTR is deasserted. That is the same state MeshCadet #211 narrowed to from
the other direction and wrote as `DTR=0, RTS=1`. Two unrelated sources, one
vendor and one field investigation, agree on which combination resets the part —
and the vendor's comment also states that the *order* of the writes matters,
although for a Windows `usbser.sys` flush reason rather than a chip one.

### 2.2 The ESP32-S3 cannot refuse it

`components/soc/esp32s3/register/soc/usb_serial_jtag_reg.h` at `v5.5.5` is 732
lines and contains no `CHIP_RST` register and no `RST_DIS` bit; its highest
defined offset below the date register is `USB_SERIAL_JTAG_MEM_CONF_REG` at
`+0x48`. The ESP32-C6 file does define `USB_SERIAL_JTAG_CHIP_RST_REG` at `+0x4c`
and `USB_SERIAL_JTAG_USB_UART_CHIP_RST_DIS` as `BIT(2)` within it.

So the opt-out exists on a later part and not on ours, in the headers this
project builds against. The issue's "Rejected shortcuts" entry stands: do not
write that register by analogy. This is a statement about the inspected headers,
not a proof that no S3 mitigation exists anywhere.

### 2.3 The host asserts both lines from inside `open(2)`, and lowers them on the last close

This is the layer the issue's question 2 asks about, and it is the one that
settles the question negatively.

`drivers/tty/tty_port.c:503-506` at `v6.17`, in `tty_port_block_til_ready`:

```c
if (filp == NULL || (filp->f_flags & O_NONBLOCK)) {
	/* Indicate we are open */
	if (C_BAUD(tty))
		tty_port_raise_dtr_rts(port);
```

and `:355-356`, in `tty_port_shutdown`:

```c
	if (tty && C_HUPCL(tty))
		tty_port_lower_dtr_rts(port);
```

For `cdc_acm` those two land in `drivers/usb/class/cdc-acm.c:676`, where
`acm_port_dtr_rts(port, active)` sends `USB_CDC_CTRL_DTR | USB_CDC_CTRL_RTS` when
active and `0` when not. The failure path is `void` and logs at `dev_dbg` with
the comment *"This is broken in too many devices to spam the logs"*, so a device
that refuses the request fails **silently** at this layer.

Three consequences, and they are the core of this report:

1. pyserial opens with `os.O_NONBLOCK` (`serial/serialposix.py:335` at
   `a5c48d4`), so the first branch applies: the kernel raises DTR **and** RTS
   during the `open(2)` call itself. An application cannot get in front of this.
   `rtscts=True, dsrdtr=True` suppresses pyserial's own ioctls (§2.4) and does
   nothing about this one.
2. The raise is gated only on `C_BAUD(tty)` — the termios the port already
   carries, from whoever held it last. The lower is gated on `C_HUPCL(tty)`.
   That is exactly why this repository's own 2026-08-25 bench note records
   `stty -hupcl` as tried and useless: pyserial calls
   `_reconfigure_port(force_update=True)` on every open and writes the setting
   back.
3. "Pre-open suppression" as the issue words it does not exist on Linux for the
   *opening* process. What does exist is: hold one descriptor open for the whole
   session so there is no last close, which is what the flash tools already do;
   or do not be the first opener; or change the driver binding. None of these is
   a pyserial flag.

### 2.4 pyserial then asserts them again

`serial/serialutil.py:210-211` at `a5c48d4` initialises both states asserted —
`self._rts_state = True`, `self._dtr_state = True` — and `serial/serialposix.py`
applies them inside `open()`:

```python
            self._reconfigure_port(force_update=True)

            try:
                if not self._dsrdtr:
                    self._update_dtr_state()
                if not self._rtscts:
                    self._update_rts_state()
            except IOError as e:
                # ignore Invalid argument and Inappropriate ioctl
                if e.errno not in (errno.EINVAL, errno.ENOTTY):
                    raise
```

Two details matter downstream. The suppression the flash tools use
(`tools/flash/ramhold.py:143` — "target = serial.Serial(port, baudrate=args.baud, rtscts=True,")
is exactly these two `if not` tests, and nothing more. And only `EINVAL` and
`ENOTTY` are swallowed: any other errno — `EPROTO` is 71 — propagates out of the
constructor.

### 2.5 Nothing in this client calls `tcdrain`, and the unbounded call is somewhere else

MeshCadet #208's original mechanism put the indefinite block in `tcdrain(2)` on
a dead handle. That does not transfer: this client never calls `flush()`, which
is pyserial's only `tcdrain` caller. The unbounded call here is the write
itself. `write_timeout` defaults to `None` (`serial/serialutil.py:179`), and on
that path `serial/serialposix.py:641-649` takes the infinite branch —
`select.select([self.pipe_abort_write_r], [self.fd], [], None)`. So
`tools/watch/client.py:134` — "self._serial.write(data)" — can block with no
bound, for the right reason but not for the reason upstream gave.

## 3. What this predicts for each of the two boards

Both rows below are **derived from the sources in §2 plus measurements this
repository already holds**. Neither has been exercised through `watch_control`.

### 3.1 Waveshare `28:84:85:B2:18:A4` — the open is a reset, and it is already measured

[WAVESHARE_RUNNING_OUR_CODE](WAVESHARE_RUNNING_OUR_CODE.md) §2 records, MEASURED
on this unit: *"pyserial asserts DTR and RTS on `open()`, so simply opening the
port to watch is a hardware reset"*, and *"Two RAM images were destroyed by the
tool sent to observe them before this was noticed."* The same section records
the other half — *"The kernel drops the modem lines on the last close of a
`ttyACM`, so `esptool` exiting is itself a reset"* — with
`rst:0x15 (USB_UART_CHIP_RESET)` as the evidence, and
[`tools/flash/ramhold.py:18`](../../tools/flash/ramhold.py) — "host; `rst:0x15 (USB_UART_CHIP_RESET)` is the direct evidence."
carries it into the tool that works around it.

That is the same host action performed at `tools/watch/client.py:127` —
"serial.Serial(port, baud, timeout=0)". The reset is
produced by the peripheral from the control lines and has nothing to do with
what the chip is running, so there is no mechanism by which a flash-booted
Attadipa build would be exempt where a RAM image was not.

**So the answer to the issue's question 1, for this board, is "yes" — carried
over from this repository's own bench, not newly measured.** What is genuinely
`NOT EXECUTED` is the consequence for the watch-control path, which is §3.3.

### 3.2 T-Watch S3 Plus `DC:B4:D9:18:49:40` — the open fails instead

[TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28](TWATCH_S3_PLUS_DOWNLOAD_MODE_2026-08-28.md)
§2 records four out of four CDC control-line requests refused with `errno 71`
against zero out of four on the Waveshare, same script, same host, same
`cdc_acm`. §7 records the consequence for an unsuppressed open in that session's
own words: the step that *"used to raise `Could not configure port: (5, 'Input/output error')`"*
is the open itself, and only `rtscts=True, dsrdtr=True` got past it.

`errno 71` is `EPROTO`, which §2.4 shows pyserial re-raises. So the predicted
behaviour of the open at `tools/watch/client.py:127` —
"serial.Serial(port, baud, timeout=0)" — on this unit is a `WatchError` out of
`SerialTransport.__init__` — *"could not open …"* — and not a reset. That is a
different answer from `UNKNOWN`, and it means the watch-control path has never
been able to reach this board at all, which no document currently says.

The unit's USB behaviour is also **stateful**: the same report notes the unit
reset and re-enumerated three times during one earlier invocation and then
refused everything in a later window on the same enumeration, with the reason
`UNKNOWN`. A single run on this board proves nothing either way.

### 3.3 The consequence nobody has written down

[WATCH_CONTROL_2026-08-25](../hardware/WATCH_CONTROL_2026-08-25.md) is this
repository's measured watch-control evidence, and it is from the Waveshare over
*"the physical USB-Serial/JTAG endpoint"*. It records a successful
`watch_control --timeout 20 info`, a complete 410×502 screenshot and a tap that
reached the live UI.

Those observations are **consistent with a reset at open and do not exclude
one**. The firmware's own boot takes a few hundred milliseconds, `connect()`
retries, and the `--timeout 20` budget absorbs it; a reboot at open would show
up as a short delay and nothing else. Nothing in that transcript distinguishes
"the watch I left running answered" from "the watch rebooted and then answered",
because no observable in the session is tied to pre-open state.

If §3.1 holds through `watch_control`, then every hardware observation ever taken
through this tool on this board was taken against a watch that had rebooted
moments earlier. That invalidates nothing that was measured about a *fresh* boot,
and it does invalidate any reading of state that was supposed to persist across
the invocation — uptime, a step count, RTC trust lifetime, a queued mesh
message, a sleep state, an on-screen position reached by hand. This is the single
highest-value thing to measure on the bench, and it is one command with the
serial console on a second host.

## 4. Five states, and the contract between them

The issue asks for device identity, interface instance, open descriptor,
protocol session and command outcome to be separately observable. They are five
distinct things and the current code conflates three of them.

| State | What identifies it | How it ends | Who tracks it today |
|---|---|---|---|
| **Device identity** | USB serial = base MAC (`28:84:85:B2:18:A4`) | never, short of a new board | `resolve_port`, and `identity_mismatch` for the flash path |
| **USB interface instance** | an enumeration; `ttyACM*` is reused and is not it | a reset that drops the bus | nothing in `watch/` |
| **Open descriptor** | the `fd` behind `serial.Serial` | the interface going away, or `close()` | nothing — `SerialTransport` holds one for the process's life |
| **Protocol session** | the 32-bit generation `connect()` draws | a new `HELLO` | `Watch.connect()`, correctly |
| **Command outcome** | a reply carrying this attempt's `req_id` | the reply, or a timeout | `request()`, per attempt |

The session generation is the part that already works, and it is worth being
precise about what it buys: it discards replies written to a *previous process*,
which is a different failure from the device having rebooted under a live
descriptor. After a reset at open the device has no memory of a previous
session, so the generation check passes trivially — it is not a reset detector
and was never meant to be.

Espressif's own code assumes the interface instance does end. `esptool/reset.py`
at `c85144b` carries it twice: *"Chips talking over their internal USB
peripheral disappear from the bus during reset; `post_release_delay=0.2` gives
the device time to re-enumerate before any follow-up writes"*, and the
`ResetStrategy` docstring *"Targets with internal USB peripherals can drop and
re-enumerate the serial device during a reset; the loop opens the port up to
three times before giving up."* The second sentence of that docstring is also the
vendor's acknowledgement of §3.2's board: *"some platforms (RFC2217 ports,
certain USB ROM-loader interfaces) simply do not honour modem-control writes"*.

## 5. Which commands may be retried, read off the current call sites

`request()` retries once by default, and the reason it gives is sound on a byte
stream: *"a request can be lost to a resync and the difference between 'lost' and
'the device is wedged' is exactly whether a second attempt is answered"*. The
classification below is what the code already does; it is recorded here because
the issue asks for it per opcode and because a reset at open changes what the
first attempt of a session means.

| Operation | `retries` today | Class | Why |
|---|---|---|---|
| `HELLO` (via `connect`) | 1 | **safe** | establishes the session it would repeat; the generation check makes a stale answer unusable |
| `CAPABILITIES` | 1 | **safe** | a pure read of board-declared facts |
| `SCREEN_REQUEST` | no retry — `screenshot()` bypasses `request()` | **safe but expensive** | idempotent; a second transfer is ~3400 chunks, and the abandoned id is blacklisted |
| `WAIT_STABLE` | 1 | **safe** | a poll; the duration is on the wire and carries no state |
| `INPUT_RESET` | 1 | **safe** | releasing what is held twice releases nothing the second time |
| `TIME_SYNC` | **0** at `tools/watch/client.py:799` — "retries=0)" | **unsafe** | *"a lost acknowledgement must not turn one requested wall-clock correction into two hardware writes"* |
| `INPUT_EVENT` | **0** at `tools/watch/client.py:878` — "retries=0)" | **unsafe** | injecting an event twice is not injecting it once; the device answers `BadInput` about a press that worked |
| `MESH_CONFIGURE` | **0** at `tools/watch/client.py:810` — "retries=0)" | **unsafe** | a second Configure recycles a session the first already replaced |
| `MESH_FORGET_BOND` | **0** at `tools/watch/client.py:821` — "retries=0)" | **unsafe** | not idempotent in any useful sense; a repeat refuses |
| `MESH_SEND` | **0** at `tools/watch/client.py:830` — "retries=0)" | **unsafe** | a private message is not idempotent |
| `MESH_ROOM_SEND` | **0** at `tools/watch/client.py:840` — "retries=0)" | **unsafe** | login plus send, with a password-bearing frame |
| `MESH_DISCONNECT` | 1 | **safe** | converges on "not connected" |

**The classification is already correct and it is not sufficient**, for one
reason that belongs to this issue rather than to #348. Every `retries=0` above
means *"report the uncertainty"*, and that is the right call. What no opcode
anywhere survives is a **reconnect** that silently replaces the transport: an
automatic reopen would restart the id space at 1 against a device that has
rebooted, and the first command after it would be a first command, not a retry.
So the rule this research recommends — for a future executable issue, not for
this one — is that reconnection stays an explicit operator act for anything in
the unsafe half, and that the tool says a reconnect happened rather than
absorbing it.

## 6. What upstream establishes, and what it has taken back

Read in order, because four of the five MeshCadet pull requests retract
something the one before it claimed. The closeout at `3edd6bb2` is the only
state worth quoting, and this is what it leaves standing.

- **#208, merged 2026-09-22.** Claimed `open → DTR/RTS → rst:0x15 →
  re-enumeration → dead descriptor → unbounded tcdrain`. The middle of that
  chain is **retracted**; see #209.
- **#209, merged 2026-09-22.** A continuous `journalctl -k` across a reproducing
  connect showed **no enumeration event**; the earlier lines belonged to a
  preceding `esptool` flash. The closeout keeps this as *"Same-identity USB
  re-enumeration / stale-handle mechanism. Refuted"*.
- **#211, merged 2026-09-24.** Narrowed the trigger to an ordered transition
  through `DTR=0, RTS=1` and made the client clear RTS first. §2.1 of this
  report corroborates the *state* from Espressif's own sequence. The *fix* is
  **retracted**; see #212.
- **#212, merged 2026-09-24.** Reverted the ordered `setSignals()` to bare
  `port.open()`: it did not clear the wedge. Also fixed the retry-boundary byte
  loss (not a gap here, §1) and added a 64-byte split-write guard that upstream
  labels latent and unverified.
- **#213, merged 2026-09-25.** Localised the remaining failure to one host —
  Pop!_OS running Flatpak Chromium — with the native CLI working against the
  same device and the same page working from native Chrome elsewhere. It states
  plainly that *"The root cause on that host is **not identified**"*.

Two things in the closeout go **further than the issue records**, and both
matter here.

**There is no known recovery for the wedge.** The issue's 2026-09-23 correction
still records physical unplug/replug as the thing that did recover it. The
closeout withdraws that: *"it has since been tried, more than once, against a
live instance of this wedge and did not reliably clear it either. No recovery
action is currently known to reliably clear it"*. Any Attadipa design that
assumed "detect the wedge, then recover" has no upstream recovery to copy.

**The browser never touched the control lines.** The closeout notes that
Chromium's `serial_io_handler_posix.cc` `PostOpen()` only sets `TIOCEXCL`, and
that an A/B of the `SerialSplitDtrAndRts` feature flag behaved identically. So
the whole MeshCadet signal campaign was about writes the *page* made after open.
It is not evidence about what an open does — which is precisely our question,
and precisely where pyserial differs from Chromium: pyserial asserts both lines
after open (§2.4) and Chromium does not.

### 6.1 Offband #1295 — same SoC, and a method worth copying

The tested Heltec RadioCore RC32-L62 is an ESP32-S3 native-USB board:
`boards/heltec-rc32.json` at `1b801a8f` gives `"mcu": "esp32s3"`,
`-DARDUINO_USB_MODE=1` and hwid `0x303A`/`0x1001` — the same VID:PID all three
of this bench's ESP32-S3 devices enumerate as. `variants/heltec_rc32/platformio.ini`
states the behaviour directly, and then states its own limit:

> RC32 is a native-USB ESP32-S3 (USB-Serial/JTAG). Opening the serial port
> with default DTR/RTS handling RESETS the chip, so a monitor attach captures a
> boot it caused itself
>
> NOTE: this governs `pio device monitor` only. It does NOT change how a client
> application drives DTR/RTS on this port.

That is an independent same-SoC confirmation of §2.1 and §3.1, and the note is
the same distinction §2.3 makes about `rtscts/dsrdtr`: suppressing the
*observer's* control lines does nothing for the client's. Their capture tool is
built on the same premise — *"It never touches the RC32. The RC32's own USB
console power-cycles the board on attach, which is why every log ever captured
that way came from a boot that SUCCEEDED"*.

Hardware verification reported by the merged PR: Run A **7/7**, Run B **4/4**,
on `rc32-bench-1`, captured on an external UART sniffer. In the same capture,
retained RAM survived `rst:0x15` three times, while a rig `RST` reported
`rst:0x1 (POWERON)` and restarted the counter.

Two methods to adapt, and one observable this project does not have:

1. **Capture the DUT's UART from a second device**, never through the endpoint
   under test. A capture taken through the watch's own USB console is an
   intervention in the event being studied.
2. **Report numerator/denominator.** `7/7` is a result; "it worked" is not.
3. **A device-side `[usb]` line.** Offband's diag build prints bus attach,
   host lifecycle and a **flap count**. That is an on-device enumeration oracle
   that needs no kernel log and no second host, and it is the cheapest way to
   tell "the host lost the interface" from "the device rebooted". Nothing in
   Attadipa's firmware reports it.

### 6.2 MeshcoreChatter #3 — the one place a reading would be wrong

The issue calls this *"corroborates the control-line state-machine class"* and
is careful to say it does not prove native-USB behaviour. The caution does not
go far enough, because the measurement actually points the **other way** for the
question we are asking.

That board is an Elecrow ThinkNode M2 with a **CH340K external bridge**, where
DTR and RTS drive the classic EN/GPIO0 auto-reset transistor network. The PR's
own measurement table records:

| DTR | RTS | Node reset |
|---|---|---|
| as-opened, untouched | | no |
| `True` | `True` | no |
| `False` | `False` | yes |

So on an external-bridge board an untouched pyserial open — both lines asserted
— **does not reset**, because the auto-reset network responds to a transition
and not to a steady state. On our native-USB boards the peripheral decodes the
control bits directly, and this repository measured the opposite outcome for the
same host action (§3.1). Carrying the "untouched open is safe" row across would
be a specific error, not merely an unproven generalisation. This repository's own
note already draws the line — *"These are USB control bits, not GPIO0/EN pins on
this board"* — and that line is exactly where the transfer fails.

## 7. The HIL matrix — NOT EXECUTED — HARDWARE REQUIRED

Against `28:84:85:B2:18:A4` and `DC:B4:D9:18:49:40` only. Never against
`F8:5B:1B:A1:98:24`, which is somebody's MeshCore node.

The rig is the part §6.1 contributes: a **second** device captures the watch's
UART, and the host under test opens only the watch's USB endpoint. A capture
through the endpoint under test cannot answer question 1 at all.

| # | Experiment | What it reads out |
|---|---|---|
| 1 | One `watch_control info` against each board, external UART capture running, `journalctl -k -f` timestamped alongside | whether `rst:0x15` appears at open, at close, at both or at neither |
| 2 | The same, with a device-side uptime or boot counter printed before the run | §3.3 — whether the watch the tool answered is the watch the operator left |
| 3 | Open policies compared on one board: pyserial defaults; `rtscts=True, dsrdtr=True`; both states preset `False` before `open()`; explicit post-open changes in both orders | which layer of §2 each policy actually reaches. Record an ioctl failure as data |
| 4 | USB remove/add, `ttyACM*` target, by-id target and whether the original `fd` can still read, write and flush, for every row of 3 | separates interface instance from pathname from descriptor |
| 5 | Boot-to-`HELLO_OK` latency, 20+ cycles, reported as a distribution | a defensible reconnect bound. Do not inherit MeshCadet's 3.5 s |
| 6 | Repeated `watch_control info` cycles after the least-bad policy, numerator/denominator, every failure listed | intermittency; a single pass is not a result |
| 7 | Whether a native Python CLI shows any asymmetric host wedge at all: device→host alive while host→device dead, on a stable enumeration | the issue's question 4. Browser sandbox behaviour is not evidence for this |
| 8 | A host-side fake transport that removes and recreates the endpoint under one pathname, distinguishing failure-before-write, uncertain write, completed write with lost reply, and late bytes from the prior session | §5's classification, without a board |

Row 8 is the only row that can run here, and it did not: it is a test of a
fixture rather than of the shipping seam, and `AGENTS.md` is explicit that such
a test proves nothing about the production caller. It is listed because it is
cheap and because it is the right shape for the executable issue that follows,
not because running it would have closed anything.

## 8. What stays UNKNOWN

| Question | Why it is open |
|---|---|
| Does `watch_control` reset the Waveshare at open? | **Derived yes** (§3.1), measured never. One external capture settles it |
| Can `watch_control` open the T-Watch at all? | **Derived no** (§3.2). The same capture settles it |
| What did every prior `watch_control` observation actually observe? | §3.3. Depends on the first row |
| Is there any Linux-side policy, available to the opening process, that avoids the open-time raise? | §2.3 says no for a pyserial application. The `C_BAUD` gate is the only lever in the source and `B0` means hang up, so it is probably not one; untested |
| Does the asymmetric host wedge reproduce on a native Python CLI? | Upstream's only reproduction is a browser on one host, and upstream itself withdrew the general explanation |
| Does the T-Watch's ROM loader accept control-line requests? | Every refusal was observed against the factory application |
| macOS and Windows | Everything in §2.3 is Linux. `usbser.sys` is named in Espressif's own comment as behaving differently |

## 9. Why no ADR changed

`AGENTS.md` asks for an ADR only when a decision was genuinely made, and
`docs/adr/` is not the place to restate an experiment.

Nothing here decides. [ADR-0005](../adr/0005-node-protocol.md) §5's session
epoch is unchanged and §4 confirms it does the job it claims; the five-state
table in §4 is a description of today's code plus two states nothing tracks, not
a new contract. The replay classification in §5 is the classification already in
the call sites. The one candidate for a decision — *a `watch_control` connect is
not state-preserving* — rests on a derivation and not on a measurement on either
board, and writing it into an ADR would promote §3.1's inference to a settled
fact about the watch-control path. The executable issue that follows a bench run
is where that belongs.

## 10. What a follow-up issue should contain

Not filed by this research, because `AGENTS.md` asks for one finite issue with
an explicit Definition of Done and that needs the bench run first. What it will
be, if row 1 of §7 comes back positive:

- the tool says when a reset happened, instead of absorbing it into a
  successful handshake — the cheapest form is the device-side boot counter of
  §6.1, item 3, read in the handshake and reported when it moves;
- reconnection is bounded, re-resolves by USB serial the way the reopen at
  `tools/flash/flash_no_reset.py:608` — "pyserial.Serial(target, baudrate=BAUD"
  already does, opens exactly one fresh descriptor, and is **not** automatic for
  anything in §5's unsafe half;
- `write_timeout` stops being `None`, so §2.5's unbounded `select` becomes a
  named error.
