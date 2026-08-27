# The T-Watch S3 Plus cannot be put into download mode from this host

Bench session of 2026-08-28, issue
[#300](https://github.com/hleserg/Attadipa/issues/300), T-010 slice 1. The
slice set out to prove a second board boots our image and to scan its I2C bus.
It did neither, and what stopped it is worth more than the scan would have been.

**Nothing was written to flash. No eFuse was touched. No image reached the
watch.** The unit is the one recorded in [BENCH_DEVICES](BENCH_DEVICES.md), USB
serial `DC:B4:D9:18:49:40`.

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
there is no host-side route into download mode on this unit. Connecting with
`no_reset` opens the port, transmits, and receives no SLIP answer, which says
the chip was not sitting in download mode either.

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
| Came back | **yes, at `129331.656898` — 722 s (12.0 min) later** | see below |

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
replug the USB-C cable, then RESET, then a long press on PWR, which reaches the
AXP2101's `PWRON` pin.

## 4. What T-010 slice 1 delivered, and what it did not

| Item | Outcome |
| --- | --- |
| RAM image runs on the watch | **NOT EXECUTED — HARDWARE REQUIRED** |
| I2C scan on the main bus | **NOT EXECUTED** — downstream of the boot |
| AXP2101 `REG 0x64`, closing D22 | **NOT EXECUTED** — downstream of the boot |
| No flash write, no eFuse | held |
| RAM image builds and passes both guards | §1 |
| No host route into download mode | §2, MEASURED, with a same-host control |

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
