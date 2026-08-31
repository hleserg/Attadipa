# Physical watch-control evidence — 2026-08-25

Unit: Waveshare ESP32-S3-Touch-AMOLED-2.06, USB serial
`28:84:85:B2:18:A4`. All commands below used the physical USB-Serial/JTAG
endpoint. The AMOLED stayed at the firmware's 5% brightness.

Primary board sources:

- [Waveshare product/wiki page](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)
- [current Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06/ESP32-S3-Touch-AMOLED-2.06.pdf)
- [XPowersLib AXP2101 ESP-IDF example](https://github.com/lewisxhe/XPowersLib/blob/master/examples/ESP_IDF_Example/main/port_axp2101.cpp)

The schematic establishes PWR → AXP2101 `PWRON`, BOOT → GPIO0/GND and
`SYS_OUT` → GPIO10. `SYS_OUT` is a PMU system-state output, not a PWR-key
mirror. XPowersLib establishes AXP2101 PWR positive/negative edge interrupts
and the `0x41` enable / `0x49` status register pair used here while awake. The
later sleep/wake correction is recorded in `SLEEP_WAKE_2026-08-26.md`.

## Build and flash

ESP-IDF v5.5.5 built the flash image successfully:

```text
attadipa.bin binary size 0x9f570 bytes.
Smallest app partition is 0x400000 bytes. 0x360a90 bytes (84%) free.
```

`idf.py -B build-t114 -p /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_28:84:85:B2:18:A4-if00 flash`
identified ESP32-S3 revision 0.2, 8 MB embedded PSRAM and MAC
`28:84:85:b2:18:a4`; every written region passed esptool's hash verification.

## Endpoint, screenshot and remote input

No port was named:

```text
$ python3 tools/watch_control.py --timeout 20 info
connected over serial:/dev/ttyACM1
  board            waveshare-amoled-206
  build            device attadipa-claim-1
  debug protocol   v1
  screen           410 x 502, RGB565_LE, DEG0
  touch points     1
  buttons
    power
    boot   service key, not simulated
```

The `ttyACM1` value is only what the selected by-id link resolved to on that
enumeration; selection used USB serial, not the number.

`screenshot --output /tmp/attadipa-t114-pmu.png` returned a complete 410×502
RGB565 frame. The PNG was opened at original resolution: white title and RTC,
red/green/blue swatches in that order, the centred `TOUCH ME` target and the 5%
brightness footer were all upright and intact.

`tap --x 205 --y 285 --screenshot-after` then returned frame 6. That PNG was
also opened at original resolution and showed `TOUCH OK 1`, proving the remote
request reached the same live LVGL UI rendered by the physical panel path.

The first serial implementation allowed console text to land inside a partial
framed write; repeated screenshots ended with 1,602 missing bytes and eight bad
frame CRCs. Firmware now writes each complete frame in one driver call, so logs
can occur only between records where the decoder resynchronises. Five
consecutive post-fix screenshots completed and passed both frame CRC-16 and
whole-image CRC-32.

## Measured timing and bounded memory

Five consecutive captures on one physical connection measured:

```text
request to ScreenInfo: 98.7, 83.0, 86.5, 78.7, 85.1 ms
whole transfer:       4482.7, 4469.2, 4475.5, 4474.5, 4471.5 ms
median:                 85.1 ms / 4474.5 ms
```

Request-to-`ScreenInfo` is an upper bound on the synchronous LVGL snapshot plus
CRC because it also includes the 5 ms firmware poll and USB/host scheduling.
Before selecting the ESP32-S3 ROM CRC implementation, the same five-run median
was 341.8 ms to `ScreenInfo`; the bitwise CRC was therefore removed from the
device path.

The enabled endpoint owns one caller-supplied RGB565 frame in PSRAM:
410 × 502 × 2 = 411,640 bytes. LVGL snapshots directly into it. The output
queue is a fixed 16 KiB and pumping is bounded to 64 bridge frames and eight USB
writes per 5 ms poll. `CONFIG_ATTADIPA_WATCH_CONTROL` gates the endpoint. It is
off in the Kconfig default and off in `sdkconfig.defaults`; the measurements on
this page were taken on a build with it on, which is now `sdkconfig.hil` and is
a bench image rather than the product (#346, and
[the trust boundary](../testing/WATCH_CONTROL.md#the-trust-boundary)). The
physical-input results below are not affected: that path moved to
`firmware/main/physical_input.cpp` and is in every image.

## Physical input

With the endpoint firmware running, the owner pressed both case keys twice.
The raw USB log contained two distinct BOOT transitions:

```text
I (...) watch-control: physical boot down
I (...) watch-control: physical boot up
I (...) watch-control: physical boot down
I (...) watch-control: physical boot up
```

This is MEASURED evidence for the GPIO0 producer, debounce, physical origin,
shared `InputQueue` and its drain. The first GPIO10 polling implementation
produced no PWR events and was removed; the AXP2101 status path is the awake
event producer. A later attempt to use GPIO10 as a Light-sleep key wake also
failed on the physical board and confirmed that it is not the key level.
After flashing source HEAD `0413fcc`, one press of each case key produced:

```text
I (199964) watch-control: physical boot down
I (200164) watch-control: physical boot up
I (200364) watch-control: physical power down
I (200564) watch-control: physical power up
```

This confirms the replacement AXP2101 PWR negative/positive edge path on the
physical unit as well as the GPIO0 path.

The earlier six FT3168 presses in `BRINGUP_2026-08-25.md` predate the shared
queue producer. On the same `0413fcc` run the owner tapped the visible target;
the next physical screenshot was opened at original resolution and showed
`TOUCH OK 1`. This is MEASURED evidence that the FT3168 producer routed the
physical pointer transition through the shared `InputQueue` into LVGL.

## Evidence boundary

This run proves the physical USB endpoint, automatic serial selection, complete
RGB565 capture, orientation/colour order, bounded transfer, remote tap and the
physical touch, BOOT and PWR input paths. It does not prove sunlight
readability, panel gamma, touch sensitivity across the panel, PWR wake from an
SoC-off state or the sleep/wake lifecycle.
