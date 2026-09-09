# Manual brightness

Issue [#514](https://github.com/hleserg/Attadipa/issues/514) owns acceptance.
The native flow is Clock tap > Settings > Display > Brightness. Holding a
node page opens Settings because its short tap already switches Mesh/Nav.
Clock hold retains provisioning. The shared status remains visible.

The displayed percentage is the requested setting, not measured luminance.
Preview applies immediately; only Save writes `attadipa_screen/brightness` in
the already initialized default NVS. Successful commit also replaces the power
owner's wake request. Cancel restores the last successfully saved request.
Sleep discards an uncommitted preview and redraws that saved request on wake.
Reconnection updates status without displacing an active editor.

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

## Checkpoint limitations

The same face is built by the simulator at both native geometries. Its storage
is deliberately volatile and its apply callback does not control a monitor.
Host tests exercise the shipping application model and native write transaction,
including a setter that changes storage before returning failure. The actual
simulator Settings caller checks page transitions, saved/cancelled values,
theme/locale changes, header pixels and all four corners of the full slider hit
row. Reintroducing the production redraw defect makes this test fail. These
are not physical panel tests or an executed IDF flash-fault experiment.

The initial `fdae291` checkpoint built in pinned ESP-IDF5.5.5 HIL. Real NVS
reboot/wake and physical readability acceptance remain pending:
**NOT EXECUTED — HARDWARE REQUIRED** for hardware outcomes.
The current T-Watch native backlight backend is binary on/off; its normalized
brightness application and native Settings entry remain pending. A 240 × 240
simulator image must not be presented as their implementation.
