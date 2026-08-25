# Waveshare sleep/wake lifecycle — 2026-08-26

Unit: Waveshare ESP32-S3-Touch-AMOLED-2.06, USB serial
`28:84:85:B2:18:A4`. The tested implementation starts at source commit
`de21f26` on T-167 / PR #270.

Primary sources used for the wake wiring and ESP-IDF behaviour:

- [Waveshare product/wiki page](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06)
- [current Waveshare schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06/ESP32-S3-Touch-AMOLED-2.06.pdf)
- ESP-IDF v5.5.5 `docs/en/api-reference/system/sleep_modes.rst`, GPIO wakeup
  section, and `components/esp_hw_support/include/esp_sleep.h`

## Mode and wake wiring

The firmware deliberately uses ESP-IDF **Light-sleep**. RAM and the LVGL task
resume in place, which permits the existing input queue and UI to survive.
Deep-sleep reboots the ESP32-S3 and therefore cannot satisfy the restoration
criterion without a different persisted-state design.

The current Waveshare schematic establishes both level wake signals:

- FT3168 interrupt is GPIO38, active low;
- the PWR key pulls the AXP2101 `PWRON` node low. That same node drives the gate
  of T1 (`BSS138LT1G`) through R16. T1 and the R11 pull-up invert it onto
  `SYS_OUT`, so GPIO10 is high while PWR is held.

The second point corrects the earlier repository reading that called
`SYS_OUT/GPIO10` a PMU power-state output unrelated to key level. The discrete
transistor is outside the AXP2101 and is visibly driven from the key node.
Awake button events still come from the AXP2101 edge-status registers; GPIO10
is used only where I2C polling cannot work, while the CPU is asleep.

The transition is fail-closed: sleep is not entered until the shared
`InputQueue` is empty, neither origin holds an input, LVGL has released its
pointer, and both level wake inputs are inactive. The panel brightness is set
to zero and the CO5300 is commanded off before `esp_light_sleep_start()`. On
return the panel is enabled, safe brightness is restored, the PCF85063-backed
Clock is refreshed and LVGL is forced to render a new frame. The log names the
full `Active -> Idle -> LightSleep -> Idle -> Active` route and the wake source.

## Build and flash

ESP-IDF v5.5.5 built the flash image successfully from a clean, separate
configuration using `sdkconfig.defaults`:

```text
attadipa.bin binary size 0x10ae90 bytes.
Smallest app partition is 0x400000 bytes. 0x2f5170 bytes (74%) free.
firmware ELF contains all required Attadipa libraries
```

The physical flash command identified ESP32-S3 revision 0.2, 8 MB embedded
PSRAM and MAC `28:84:85:b2:18:a4`. Bootloader, partition table and application
all passed esptool's written-data hash verification. The final unattended
cycle ran at the repository-safe 5% brightness; a short owner-requested 70%
demonstration window was returned to 5% when no physical interaction occurred.

## Repeat loop through the debug channel

Remote `power` click uses the same input queue and sleep lifecycle as the
physical event. For this debug-origin path only, a 750 ms timer is added to the
two product GPIO wake sources so an unattended host can recover the connection
and take the next screenshot. Five consecutive cycles completed on the
physical board. Each screenshot was decoded as a complete 410 × 502 RGB565
frame and opened together at original aspect ratio.

| Cycle | Device frame | Transfer | Visible Clock | PNG SHA-256 |
|---:|---:|---:|---|---|
| 1 | 2 | 9,537 ms | `02:30:24` | `7c2794a917d20de6731cd48ca51ca9b9f57fffe7975c16ff286109f69472f931` |
| 2 | 3 | 9,578 ms | `02:31:28` | `6142d1b84e8b02b0d5db21b2b9002e5aca74173af9e70f95975d95e90e3fff04` |
| 3 | 4 | 9,728 ms | `02:31:40` | `2c8507a7b400010d326730c5d11b49d53b1c5fb08f5851db0b637e6126ae1bc2` |
| 4 | 5 | 9,875 ms | `02:31:52` | `41a732796a6967e3baf504b400d3f02c0065b89bc81bb98834165e11060c8b8f` |
| 5 | 6 | 9,706 ms | `02:32:04` | `af459c2214070d1fbbac1aa792aec56634358ffb51dd93971cc4f001dedc0b0b` |

All five frames retained the date, live time, seconds badge, year, progress
card and animated background without a blank/stale frame. The changing seconds
and animation positions make the five distinct hashes expected rather than a
failure of comparison.

## Evidence boundary

- **MEASURED:** the flash build and verified write; five debug-origin
  Light-sleep/timer-wake cycles on the physical Waveshare; five complete,
  visually inspected post-wake frames; continued watch-control response.
- **VERIFIED from the current schematic / ESP-IDF source:** GPIO38 active-low
  touch wake, the PWRON-to-T1-to-GPIO10 active-high circuit, and digital GPIO
  wake support in Light-sleep.
- **NOT EXECUTED — HARDWARE REQUIRED:** a physical finger waking through
  GPIO38 and a physical PWR press waking through GPIO10. Until that consolidated
  bench interaction is captured, the two product wake sources are not promoted
  from schematic-supported implementation to measured behaviour.
- No current instrument was attached. Sleep current, wake latency and energy
  savings remain `UNKNOWN`.
