# Waveshare sleep/wake lifecycle — 2026-08-26

Unit: Waveshare ESP32-S3-Touch-AMOLED-2.06, USB serial
`28:84:85:B2:18:A4`. The unattended five-cycle run used source commit `4b4e20f`;
the failed GPIO10 hypothesis was tested at `8f098ba`; the corrected PMU-polling
path is source commit `0475188` on T-167 / PR #270.

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

The current Waveshare schematic establishes one direct level wake signal:

- FT3168 interrupt is GPIO38, active low;
- the PWR key pulls the AXP2101 `PWRON` input low. Its press/release edge status
  is latched inside the AXP2101 and read over I2C.

`SYS_OUT/GPIO10` is the PMU system-output state, not a PWR-key level. The
AXP2101 IRQ net is routed to `EXIO5`, not to an ESP32-S3 GPIO, and this board has
no fitted I/O expander that would bridge it. Therefore the firmware cannot arm
a direct PMU interrupt wake. While the screen is off it uses a 100 ms timer
wake to read the latched AXP2101 edge status; a miss immediately re-enters
Light-sleep without enabling the panel. Touch remains direct GPIO38 wake.

The transition is fail-closed: sleep is not entered until the shared
`InputQueue` is empty, neither origin holds an input, LVGL has released its
pointer, and the touch wake input is inactive. The panel brightness is set
to zero and the CO5300 is commanded off before `esp_light_sleep_start()`. On
return the panel is enabled, safe brightness is restored, the PCF85063-backed
Clock is refreshed and LVGL is forced to render a new frame. The log names the
state being entered, and on return the route back and the wake source
([`firmware/main/board_power.cpp:311-313`](../../firmware/main/board_power.cpp) —
"attadipa::core::to_string(state)," and
[`firmware/main/physical_input.cpp:228-231`](../../firmware/main/physical_input.cpp) —
"attadipa::firmware::board_power_owner().cycles()),"). It does not name
`Active -> Idle ->`. The entry line carried that half until the sleep path moved
into `board-power`, and no line prints it now.

## Build and flash

ESP-IDF v5.5.5 built the flash image successfully from a clean, separate
configuration using `sdkconfig.defaults`:

```text
attadipa.bin binary size 0x15d9a0 bytes.
Smallest app partition is 0x400000 bytes. 0x2a2660 bytes (66%) free.
firmware ELF contains all required Attadipa libraries
```

The physical flash command identified ESP32-S3 revision 0.2, 8 MB embedded
PSRAM and MAC `28:84:85:b2:18:a4`. Bootloader, partition table and application
all passed esptool's written-data hash verification. The final unattended
cycle ran at the repository-safe 5% brightness; a short owner-requested 70%
demonstration window was returned to 5% when no physical interaction occurred.

## Repeat loop through the debug channel

Remote `power` click uses the same input queue and sleep lifecycle as the
physical event. For this debug-origin path, a 750 ms timer lets an unattended
host recover the connection
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

All five frames retained the date, live time, seconds badge, year and progress
card without a blank/stale frame. These frames predate the static raster
background and remain lifecycle evidence, not evidence for the current art.

## Physical product wake sources

The corrected build was then exercised with the case PWR key and the physical
touch panel. Two cycles woke directly from GPIO38 touch. On the third cycle the
case PWR key was pressed while the panel was off; the 100 ms timer wake consumed
the AXP2101 edge and classified the transition as Button:

```text
watch-control: display off; entering Light-sleep (touch + PMU polling)
watch-control: wake cycle 3: LightSleep -> Idle -> Active by Button
               (cause=4 gpio=0x0)
```

`cause=4` is the timer wake used to read the PMU; `by Button` is emitted only
after register `0x49` reports a latched PWR edge. GPIO is zero for this route.
The panel restored and the watch continued answering the debug channel.

The transcript above is what that run printed and is left as it was recorded.
It is no longer what a repeat prints, and in four ways rather than one. Both
lines moved to `firmware/main/physical_input.cpp` in #346, so that they survive
a build with no debug endpoint, and the tag moved with them. Then the sleep line
moved again in [#367](https://github.com/hleserg/Attadipa/issues/367): it comes
from the board's power owner now, so its tag is `board-power`, and it has lost
the `display off;` prefix — turning the panel off became a `suspend(Display)`
the owner records and un-does, rather than a step the log narrates
([`firmware/main/board_power.cpp:312-314`](../../firmware/main/board_power.cpp) —
"attadipa::core::to_string(state),"). The wake line kept `physical-input` and
lost the `(cause=4 gpio=0x0)` suffix: causes are a bitmap now, and every one of
them is named
([`firmware/main/physical_input.cpp:227-231`](../../firmware/main/physical_input.cpp) —
"describe_wake(named, sizeof(named), report.wake_causes);") instead of the
single value the lossy `esp_sleep_get_wakeup_cause()` returned. So `cause=4` is
in no line this firmware can print, and a repeat grepped for the text above
finds nothing.

Fourth, the wake line no longer says `by Button` on this route. The PWR key is
still classified from register `0x49`, but it is classified *during* a timer
wake, so the board's derived `Button` is now reported alongside the SoC's own
`Timer`
([`firmware/main/board_power.cpp:352-354`](../../firmware/main/board_power.cpp) —
"attadipa::core::wake_bit(attadipa::core::WakeSource::Button);")
rather than replacing it — the owner publishes the union of the two halves
([`core/src/power_owner.cpp:461`](../../core/src/power_owner.cpp) —
"causes.from_soc | causes.derived);"). `main` picked one answer from a
three-way ladder; this branch reports a bitmap, and the line reads
`by Timer+Button`. So a repeat grepped for `by Button` also finds nothing.

All four together are the failure this paragraph exists to prevent. It is one
of the two present-tense claims on a page of recorded transcript, and the other
one — the route sentence in § Mode and wake wiring — went stale in the same
change and for the same reason, which is why it is worth counting them. What the
two lines report did not change, and nothing here was re-measured — **NOT
EXECUTED — HARDWARE REQUIRED**.

## Evidence boundary

- **MEASURED:** the flash build and verified write; five debug-origin
  Light-sleep/timer-wake cycles on the physical Waveshare; five complete,
  visually inspected post-wake frames; continued watch-control response; two
  direct GPIO38 touch wakes; and a corrected PMU-poll PWR wake classified as
  Button. On `8f098ba`, physical PWR correctly slept the watch but did not wake
  it through GPIO10; the following touch wake was logged as GPIO38 and stale
  PMU edges made the display sleep again. This invalidated the GPIO10 key-mirror
  assumption.
- **VERIFIED from the current schematic / ESP-IDF source:** GPIO38 active-low
  touch wake, PWRON as an AXP2101 input, `SYS_OUT/GPIO10` as a PMU state output,
  and AXP IRQ terminating at `EXIO5` rather than an ESP32-S3 GPIO.
- No current instrument was attached. Sleep current, wake latency and energy
  savings remain `UNKNOWN`; the polling interval's power cost is also unknown.
