# Waveshare sleep/wake lifecycle — 2026-08-26

Unit: Waveshare ESP32-S3-Touch-AMOLED-2.06, USB serial
`28:84:85:B2:18:A4`. The final five-cycle run used source commit `4b4e20f` on
T-167 / PR #270.

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
attadipa.bin binary size 0x10aef0 bytes.
Smallest app partition is 0x400000 bytes. 0x2f5110 bytes (74%) free.
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
| 1 | 1 | 9,702 ms | `02:50:30` | `5da0b6fd7c35ddc20162337179a0aa32b5c0abbdf9ada756e0a634c6d128e5dc` |
| 2 | 2 | 9,584 ms | `02:50:41` | `051987a3a8d012a5acef0ff590637f3d69257af162ac1f83168523b287c2f549` |
| 3 | 3 | 9,644 ms | `02:50:53` | `a90b7f63a63258b68d3c748ded9f294274c1139a3bc7f929d1444cfa4891f7d2` |
| 4 | 4 | 9,669 ms | `02:51:05` | `c80579ca42473608f94613bf67352dbfe322720de53fae9c4ce22e0cbe174b91` |
| 5 | 5 | 9,629 ms | `02:51:17` | `2c097eb94e8c7671e5ffde23cb4ac5682bb4e0830d5c68ed681f481c1a997e57` |

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
