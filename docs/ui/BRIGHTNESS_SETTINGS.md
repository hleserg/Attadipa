# Manual brightness

Issue [#514](https://github.com/hleserg/Attadipa/issues/514) owns acceptance.
The Waveshare flow is Clock tap > Settings > Display > Brightness. Holding a
node page opens Settings because its short tap already switches Mesh/Nav.
Clock hold retains provisioning. The shared status remains visible.

The displayed percentage is the requested setting, not measured luminance.
Preview applies immediately; only Save writes `attadipa_screen/brightness` in
the already initialized default NVS. On Waveshare, successful commit also replaces the power
owner's wake request. Cancel restores the last successfully saved request.
Sleep discards an uncommitted preview and redraws that saved request on wake.
Reconnection updates status without displacing an active editor. T-Watch has
no runtime sleep service yet; the shared editor does not add one.

Missing state uses the default without writing. Invalid or unreadable state
uses the default and shows “Using default”. Failed application keeps the prior
displayed request. A failure before the brightness setter keeps the editor open
with “Not saved” and Retry. A setter or commit failure instead shows “Restart
to check”: NVS may have written before reporting failure. Further brightness
writes stop; Cancel restores the previous visible request but retains this
notice. The explicit Restart action reboots through normal NVS recovery and
loads whichever valid value survived. It does not promise that an uncertain
write was undone. No NVS erase or automatic brightness control.

This distinction follows ESP-IDF v5.5.5's
[NVS setter contract](https://github.com/espressif/esp-idf/blob/v5.5.5/components/nvs_flash/include/nvs.h)
for `ESP_ERR_NVS_REMOVE_FAILED` and its
[native fault tests](https://github.com/espressif/esp-idf/blob/v5.5.5/components/nvs_flash/host_test/nvs_host_test/main/test_nvs.cpp),
which accept either the old or new value after `ESP_ERR_FLASH_OP_FAIL`.
Readback before recovery cannot prove which value a restart will recover.

## Current Waveshare policy and evidence

- Minimum/default: 5%; button step: 5 percentage points; maximum request: 100%.
  These are software policy and command limits, not a universal readable floor
  or a measured safe continuous maximum.
- Pinned `espressif/esp_lcd_co5300` 2.1.0 rejects requests above 100 and converts
  accepted percent to the 8-bit command with `(brightness_percent * 255) / 100`.
  Source: [Espressif component](https://components.espressif.com/components/espressif/esp_lcd_co5300/versions/2.1.0).
- The [2026-08-25 bench record](../hardware/BRINGUP_2026-08-25.md) records that
  the owner found 1% apparently black and saw RGB swatches at 5%. This supports
  retaining 5% for recovery; it does not establish readability in all lighting.
- Boot/sleep already own screen-off separately. The editor cannot request 0%.

## Native T-Watch policy and provenance

Holding the diagnostic screen after its panel exercise opens the same Settings
face. Back returns to the diagnostic screen. Boot initializes default NVS once,
without erasing it on error, and applies the saved request after the first frame.
Both boards use the same native read/write transaction and recovery behavior.

The existing full-on boot behavior remains the 100% default/recovery request.
The 5% minimum and 5-point step are provisional software policy. Readability at
the floor and a safe continuous maximum are **UNKNOWN**; they were not measured.
The editor rejects 0%, while initialization holds the backlight dark.

- The pinned Arduino T-Watch S3 variant assigns
  [DISP_BL to GPIO45](https://github.com/espressif/arduino-esp32/blob/3.3.2/variants/lilygo_twatch_s3/pins_arduino.h#L20),
  which the pinned [LilyGoWatchS3 composition passes to its display](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/38e6f8dee3ba78b340512af9a013365ef248a7d0/src/LilyGoWatchS3.cpp#L135).
- The vendor display interface uses
  [1000 Hz and 8-bit LEDC](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/38e6f8dee3ba78b340512af9a013365ef248a7d0/src/LilyGoDispInterface.h#L23).
  Attadipa maps percent to `percent * 256 / 100`; at 100% the duty is 256,
  matching [Arduino's full-on conversion](https://github.com/espressif/arduino-esp32/blob/3.3.2/cores/esp32/esp32-hal-ledc.c#L336).
- Writes use checked `ledc_set_duty` followed by `ledc_update_duty`, serialized
  by boot and then the LVGL task. The combined `ledc_set_duty_and_update` needs
  an installed fade service in [pinned IDF 5.5.5](https://github.com/espressif/esp-idf/blob/v5.5.5/components/esp_driver_ledc/src/ledc.c#L1603);
  fixed PWM here does not install that service.

## Evidence boundaries

The same face is built by the simulator at both native geometries. Its storage
is deliberately volatile and its apply callback does not control a monitor.
Host tests exercise the shipping application model and native write transaction,
including a setter that changes storage before returning failure. The actual
simulator Settings caller checks page transitions, saved/cancelled values,
theme/locale changes, header pixels and all four corners of the full slider hit
row. Reintroducing the production redraw defect makes this test fail. These
are not physical panel tests or an executed IDF flash-fault experiment.

Waveshare `7a20c8e` executed Save 10%, a real chip reset, and an eight-second boot
log reporting 10%; the real editor also showed 10% after reboot. A later sleep
attempt logged entry to LightSleep and lost USB before wake evidence was
captured. Successful wake restoration and physical readability remain
**NOT EXECUTED — HARDWARE REQUIRED**. USB access after reconnection alone does
not establish a successful sleep/wake cycle.

Native T-Watch Settings entry, Save/reboot, readable PWM range and physical
touch acceptance remain **NOT EXECUTED — HARDWARE REQUIRED**. The 240 × 240
simulator verifies the shared face, not those native paths. Runtime sleep/wake
acceptance remains pending its board service.
