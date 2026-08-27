# Download mode on the T-Watch S3 Plus: every host route failed, one hand route worked

Bench session of 2026-08-28, issue
[#300](https://github.com/hleserg/Attadipa/issues/300), T-010 slice 1. The
slice set out to prove a second board boots our image and to scan its I2C bus.
**It did both** — but not from this host, and not by the vendor's recipe. The
sections stay in the order things happened: §1–§3 are the routes that failed and
are kept because they are the measured part of the answer, §6–§8 are the route
that worked and what the bus said.

**Nothing was written to flash. No eFuse was touched.** The image that ran was
loaded into RAM and left no trace; the unit returns to its factory application
on the next power cycle. The unit is the one recorded in
[BENCH_DEVICES](BENCH_DEVICES.md), USB serial `DC:B4:D9:18:49:40`.

## 1. The RAM image builds and passes both guards — MEASURED

Built in a detached worktree, ESP-IDF v5.5.5, by the recipe the CI job uses
verbatim:

```
idf.py -B build-ram -DSDKCONFIG=build-ram/sdkconfig \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ramprobe" build
```

- `CONFIG_APP_BUILD_TYPE_PURE_RAM_APP=y` is present in `build-ram/sdkconfig`, so
  the option took and this is a RAM build rather than a flash image reported as
  one.
- `pure_ram_elf_check.py`: no forbidden flash or partition symbols.
- `firmware_elf_check.py`: every required Attadipa library is linked.
- `build-ram/attadipa.bin`, 145.8 KB.

A fresh worktree carries no `firmware/sdkconfig`, so the stale-configuration
trap named in [firmware/AGENTS.md](../../firmware/AGENTS.md) could not apply.

This says the image is well formed. It says nothing about the watch: the image
never reached it.

## 2. The unit refuses every CDC control-line request — MEASURED

`tools/flash/ramhold.py` failed before transmitting a byte:

```
OSError: [Errno 71] Protocol error
  at serialposix.py _update_rts_state -> fcntl.ioctl(TIOCMBIC, TIOCM_RTS)
```

**The confound, and why it is not the explanation.** A USB-Serial/JTAG reset
re-enumerates the device, and a tool racing that re-enumeration would fail this
way too. It is not what happened: six retries, each re-resolving the
`/dev/serial/by-id` symlink, all failed identically, and `dmesg` records **no
USB event at all during those six attempts**. The request is refused on a
stable, continuously enumerated device.

Opening the port with pyserial's `rtscts` and `dsrdtr` set — which suppresses
those two ioctls inside `open()` — succeeds. The port is reachable. Only the
control-line request is refused.

**One thing in the same log does not fit, and is recorded rather than
explained.** During the *first* invocation the unit reset and re-enumerated
three times — `134342`, `134375`, `134381` — so something did reach it then.
The measured claim above is scoped to the later window, in which the six
retries ran against an enumeration that produced no USB events at all. Why the
earlier attempts moved the chip and the later ones did not is `UNKNOWN`, and it
is a second sign that this unit's USB behaviour is stateful rather than a fixed
property.

**The control.** The same script, same host, same kernel, same `cdc_acm`, same
`303a:1001`, run against each unit in turn:

| Request | T-Watch `DC:B4:D9:18:49:40` | Waveshare `28:84:85:B2:18:A4` |
| --- | --- | --- |
| DTR low | `errno 71` Protocol error | accepted |
| RTS low | `errno 71` Protocol error | accepted |
| DTR high | `errno 71` Protocol error | accepted |
| RTS high | `errno 71` Protocol error | accepted |

Four out of four against zero out of four. The difference is the device.

**The consequence.** Every esptool reset strategy is built on those two lines,
and `usb_reset` is not an escape hatch: esptool's `loader.py` routes that mode
to the same `USBJTAGSerialReset`, which toggles DTR and RTS like the others. So
**none of the host's flashing tools can reach download mode on this unit.**
Connecting with `no_reset` opens the port, transmits, and receives no SLIP
answer, which says the chip was not sitting in download mode either.

That is narrower than "no route exists", and deliberately. What was tested is
the control-line path every esptool strategy takes. Whether the factory
application offers its own way in over the CDC *data* channel — a console
command that sets the force-download boot bit, say — was **not tested** and is
`UNKNOWN`.

**What this means for the bench.** The Waveshare can be driven unattended and
the T-Watch cannot. Every RAM load on this unit needs a human holding BOOT while
pressing RESET — and both buttons sit on the GNSS daughterboard and reach the
main board over the FPC, so a unit without that daughterboard has no route in at
all. The owner's procedure of 2026-08-27 is not a quirk of that session; it is
the only way in that has been demonstrated.

**Why the unit refuses the request is `UNKNOWN`.** The factory application's USB
configuration, a sleep state, and a silicon difference are all still open, and
no evidence here separates them.

## 3. The unit is off the bus, in a state it has been in before

With no route in through the control lines, `USBDEVFS_RESET` was issued on the
unit's usbfs node — a spec-defined bus operation, which the ESP32-S3's
USB-Serial/JTAG peripheral turns into a chip reset. The intent was to reboot the
factory application and re-test the control lines.

The ioctl returned `ENODEV`, and `dmesg` shows the sequence:

```
[143562.761490] usb 3-1: reset full-speed USB device number 19 using xhci-hcd
[143562.887162] usb 3-1: device descriptor read/64, error -71
...
[143566.011224] usb usb3-port1: attempt power cycle
[143567.339260] usb usb3-port1: unable to enumerate USB device
```

The kernel retried through device numbers 20–23, attempted its own port power
cycle, and gave up. The unit has been absent from `/dev/serial/by-id` since.

**This is a sequence, and it is deliberately not written up as a measured
cause.** What is MEASURED: the unit had been enumerated with no USB events for
9 181 s (2.55 h); the first error follows the reset by 126 ms; the kernel gave up
4.6 s after that.

**An identical episode happened earlier, with no action of ours.** Searching the
whole kernel log for the signature finds it twice, and the two run step for step
the same — three `reset full-speed`, `not accepting address … error -71`, a
retry, `error -22`, `attempt power cycle`, two further addresses refused, then
`unable to enumerate`:

| | Episode A | Episode B |
| --- | --- | --- |
| Started | `128605.224010` | `143562.761490` |
| Gave up | `128609.821867` | `143567.339260` |
| Preceded by our `USBDEVFS_RESET` | **no** | yes, by 126 ms |
| Came back | **yes, at `129331.656898` — 722 s (12.0 min) later** | not as of this report |

Episode A predates every action of this session, and it too was preceded by
`reset full-speed USB device … using xhci-hcd`, which nothing of ours issued. So
a host-issued USB reset is a **sufficient** trigger for this state and is not
established as the only one. The honest reading: this is a recurring failure
mode of this unit on this host, our reset plausibly triggered its second
occurrence, and the unit is known to recover from it unaided.

A USB bus reset is not a mechanism that can damage silicon; the flash was never
written and no eFuse was touched. Whether the watch is wedged, asleep, or has
lost its USB PHY state is `UNKNOWN`.

The Waveshare, on the same host controller, was unaffected throughout. The fault
is specific to the port the watch is on.

**Recovery.** No host-side recovery is available to an unprivileged user: the
sysfs port controls are root-only. Given episode A, the first thing to try is
nothing at all for a quarter of an hour. After that it is physical — unplug and
replug the micro-USB cable, then RESET, then a long press on PWR, which reaches the
AXP2101's `PWRON` pin.

## 4. What T-010 slice 1 delivered, and what it did not

| Item | Outcome |
| --- | --- |
| RAM image runs on the watch | **MEASURED** — §6, §8 |
| I2C scan on the main bus | **MEASURED** — §8, all five pre-registered expectations held |
| AXP2101 `REG 0x64`, closing D22 | **MEASURED** — §8, `0x04` = 4.35 V, D22 closed |
| No flash write, no eFuse | held |
| RAM image builds and passes both guards | §1 |
| No host route into download mode | §2, MEASURED, with a same-host control — still true, and §6 is a hand route, not a host one |

## 5. Expectations for the scan, registered before it runs

Recorded here so that a later run cannot be read backwards into whatever it
finds. The main I2C bus is SDA 10 / SCL 11, VERIFIED from the schematic in
[HARDWARE_MATRIX](HARDWARE_MATRIX.md).

| Address | Expect | Part |
| --- | --- | --- |
| `0x34` | present | AXP2101 PMU |
| `0x51` | present | PCF8563 RTC |
| `0x19` | present | BMA423 accelerometer |
| `0x5A` | present | DRV2605 haptic — it sits behind BLDO2, so an absence is a rail result rather than a missing part |
| `0x38` | **absent** | the FT6336U touch controller is on a *separate* bus, SDA 39 / SCL 40. Reporting it missing from this scan would be the easy error to make |
| `0x42` | either | the u-blox DDC address. Present or absent, it is evidence on whether the GNSS daughterboard connects SDA and SCL |

## 6. The route that worked — MEASURED, once

The owner reached download mode by **powering the unit up with `BOOT` already
held**, not by pulsing `RESET`:

1. unplug the micro-USB cable, so the unit is unpowered from the host;
2. press and hold `BOOT` — the button to the left of the GNSS daughterboard,
   GPIO 0, FPC pin 2, VERIFIED in [HARDWARE_MATRIX](HARDWARE_MATRIX.md);
3. plug the micro-USB cable back in while still holding `BOOT`;
4. release `BOOT`.

The screen stayed dark throughout, which is what a ROM loader looks like: the
factory application never ran.

**The boot log corroborates the sequence independently of the owner's account.**
Our own image printed `Reset : PowerOn (ESP-IDF code 1)`. That is
`ESP_RST_POWERON`, not `ESP_RST_EXT`; the chip came up from the supply arriving,
not from a `RESET` pulse. Two independent sources therefore agree on the route.

**The vendor and Meshtastic recipe — hold `BOOT`, click `RESET`, release `BOOT` —
was tried repeatedly on this unit and never produced an enumeration**, although a
`RESET`-based entry did work once on 2026-08-27. That is **CONFLICTING and
unexplained**. What differs between the two is not established: a `RESET` pulse
on this board reaches the ESP32-S3 through the GNSS daughterboard's FPC pin 6,
and whether the factory application, the AXP2101's power state, or the FPC
connection is responsible is **UNKNOWN**.

**A withdrawn claim.** During the session this author told the owner that the
polling loop of the host-side script was almost certainly the cause of the
failed attempts, by knocking a unit that had reached download mode back out of
it. **That claim is withdrawn.** The entry method and the host connect path
(§7) both changed between the failing and the succeeding attempts, so nothing
in the record isolates either variable. No attempt was ever made to open the
port plainly against a unit *known* to be in download mode, which is the
experiment that claim needed. The cause of the 2026-08-28 failures is
**UNKNOWN**.

## 7. The connect path this unit requires — MEASURED

§2 records that this unit refuses every CDC `SET_CONTROL_LINE_STATE` request
with `errno 71`. That defeats esptool's reset strategies, but it also defeats
something less obvious: **`--connect-mode no_reset` is not sufficient by
itself.** That option stops esptool from *toggling* DTR and RTS; it does not
stop **pyserial from asserting them inside `Serial.open()`**, which happens
before esptool sees the port at all. The open itself is what the unit rejects.

Two facts combine into a route:

- pyserial suppresses those ioctls in `open()` when the port is constructed with
  `rtscts=True, dsrdtr=True` — hardware flow control nominally hands the lines
  to the driver;
- `esptool.loader.ESPLoader.__init__` guards its own port construction with
  `if isinstance(port, str):`, so a **pre-opened `serial.Serial` object is used
  as-is**.

So the working sequence is to open the port ourselves, with the control lines
suppressed, and hand the object to esptool:

```python
ser = serial.Serial(port, baudrate=115200, rtscts=True, dsrdtr=True, timeout=0.1)
esp = esptool.detect_chip(port=ser, connect_mode="no_reset", connect_attempts=1)
esptool.cmds.load_ram(esp, SimpleNamespace(filename=image))
```

The `load_ram` argument shape is version-dependent — ESP-IDF v5.5.5 ships
esptool 4.12.0, which reads `args.filename`; 5.x takes the path as a string.
See `tools/flash/ramhold.py`, where both this and the suppressed open now live
under `--connect-mode no_reset`.

**What each part of this is measured by.** The load that produced §8's scan ran
through a scratch script, not through `ramhold.py`; the two now issue an
identical sequence, and `ramhold.py --connect-mode no_reset` was afterwards run
against the watch to confirm the changed path. It **opened the port** — the step
that used to raise `Could not configure port: (5, 'Input/output error')` — and
reached `ESPLoader.sync()`, failing there with `Write timeout` because the watch
was by then running our RAM application rather than the ROM loader, and that
application never drains the CDC OUT endpoint. So the suppressed open is
MEASURED through `ramhold.py`; a full `load_ram` through `ramhold.py` is **NOT
EXECUTED — HARDWARE REQUIRED**, and needs a hand to put the unit back into
download mode.

`rtscts=True` nominally gates writes on `CTS`, which would be a fair suspicion
for that timeout. It is not the cause: the scratch script used the same flags
and transmitted a whole image successfully to the ROM loader on this unit.

**This is measured on one unit.** Whether the ROM loader itself would accept
control-line requests is still **UNKNOWN**: every refusal in §2 was observed
against the factory application, and once the ROM loader was reachable there was
no reason to provoke it.

## 8. The scan — MEASURED

Raw log: `~/attadipa-bench/twatch_i2c_scan_2026-08-28.log`, outside the
repository. The image is the `i2c_probe` build of `main` at `c352b59`, loaded
into RAM over the route above. **Nothing was written to flash and no eFuse was
touched**; the probe reads and never writes a register.

```
W (2352) i2c-probe: probe build — the board identity printed above is the build's, not this unit's
I (2352) i2c-probe: scanning SDA 10 / SCL 11 at 100000 Hz
I (2352) i2c-probe: ACK 0x19
I (2362) i2c-probe: ACK 0x34
I (2362) i2c-probe: ACK 0x51
I (2362) i2c-probe: ACK 0x5a
I (2372) i2c-probe: 4 address(es) acknowledged
I (2372) i2c-probe: AXP2101 REG 0x64 = 0x04
```

That warning line earned its place. The banner above it reads `Board :
Waveshare ESP32-S3-Touch-AMOLED-2.06` — the build's identity, printed while
running on a LilyGO T-Watch S3 Plus. Anyone reading the log without the warning
would have recorded the scan against the wrong board.

**Every one of §5's five pre-registered expectations held.**

| Address | Registered | Result | Reading |
| --- | --- | --- | --- |
| `0x34` | present | **ACK** | matches the schematic's AXP2101 |
| `0x51` | present | **ACK** | matches the schematic's PCF8563 |
| `0x19` | present | **ACK** | matches the schematic's BMA423 |
| `0x5A` | present | **ACK** | matches the schematic's DRV2605; since it sits behind `BLDO2`, the rail was also up at scan time |
| `0x38` | **absent** | **no ACK** | as registered — the FT6336U is on the separate touch bus, SDA 39 / SCL 40, which this scan never touched |
| `0x42` | either | **no ACK** | ambiguous, see below |

An acknowledgement is an **address** result, not an identity proof: no chip-ID
register was read from any of the four. The reading is "the address the
schematic predicts answered", which is what a scan can say.

**`0x42` — D9 is not closed by this.** §5 registered that presence or absence
would be evidence on whether the GNSS daughterboard bridges its DDC lines onto
the main bus. **That was one step too confident, and is corrected here.**
Presence would have been unambiguous; absence is not. The MIA-M10Q is fed
through `GPS_LDO` on FPC pin 3, the probe touched no rail, and **the state of
`GPS_LDO` at scan time is UNKNOWN** — a module held unpowered is silent whatever
its wiring. D9 stays open; separating the two cases needs a run that brings the
rail up first.

**`REG 0x64 = 0x04` closes D22.** The decode traces to vendor source rather than
to this repository's own prose: in XPowersLib — the library LilyGO ship for this
watch — `XPOWERS_AXP2101_CV_CHG_VOL_SET` is `0x64`, `getChargeTargetVoltage()`
masks the low three bits with `0x07`, and the enumeration runs
`XPOWERS_AXP2101_CHG_VOL_4V = 1`, `4V1 = 2`, `4V2 = 3`, **`4V35 = 4`**,
`4V4 = 5`. The measured field is `4`, so **the factory constant-voltage target
on this unit is 4.35 V — MEASURED**.

That settles the cell, which [HARDWARE_MATRIX](HARDWARE_MATRIX.md) carried as
CONFLICTING. Two independent unit-specific sources now agree on a
high-voltage chemistry — the cell's own label, photographed at 3.8 V nominal,
and the PMU's factory charge target of 4.35 V — against one generic vendor
document that says 3.7 V. A 4.35 V target configured onto a 4.2 V cell would be
a manufacturing defect; the simpler reading is that the label is right.

**This changes no code and prescribes no write.** Nothing in Attadipa writes
`REG 0x64` on this board, and this note does not propose that it start.
[BATTERY_UPGRADE](BATTERY_UPGRADE.md)'s rule — never write `100b` or `101b`,
write `011b` — was written for the Waveshare's 4.2 V cell and remains correct
*there*; it is now scoped explicitly, because applying it to this watch would
undercharge a cell the manufacturer charges to 4.35 V.

**One more MEASURED fact fell out of the banner**: this unit's SoC is
`esp32s3 rev v0.2`, 2 cores, WiFi and BLE.
