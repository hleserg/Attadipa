# RTC slow-clock routing on the target boards

Checked 2026-08-26 for [#268](https://github.com/hleserg/Attadipa/issues/268).
This report answers a wiring question. It does not measure sleep current.

## Result

Neither target board routes a 32.768 kHz source to the ESP32-S3 slow-clock
input as shipped.

| Board | Vendor-schematic result | Safe choice for T-167 |
|---|---|---|
| LilyGO T-Watch S3 Plus | PCF8563 `CLKOUT` reaches the pulled-up `RTC_CLKOUT` net and test point `TP66`, but no ESP32 pin. `XTAL_32K_P` / GPIO15 is the MAX98357A `LRCLK`; `XTAL_32K_N` / GPIO16 is touch `INT`. | Keep `CONFIG_RTC_CLK_SRC_INT_RC`. Do not select `EXT_CRYS` or `EXT_OSC` for the stock board. |
| Waveshare ESP32-S3-Touch-AMOLED-2.06 | PCF85063ATL `CLKOUT` has a no-connect marker and `CLKOE` has no destination. GPIO15 is the shared I2C SDA and GPIO16 is audio `I2S_MCLK`. | Keep `CONFIG_RTC_CLK_SRC_INT_RC`. Do not select `EXT_CRYS` or `EXT_OSC` for the stock board. |

This closes the routing prerequisite for T-167. It does **not** establish the
lowest-current configuration: the internal fast-clock/divider option remains a
documented ESP-IDF capability, not a board current measurement.

## ESP32-S3 and ESP-IDF boundary

The ESP32-S3 datasheet maps `XTAL_32K_P` to GPIO15 and `XTAL_32K_N` to GPIO16.
ESP-IDF v5.5.5 offers four RTC slow-clock choices:

- `RTC_CLK_SRC_INT_RC`, the default internal RC source;
- `RTC_CLK_SRC_EXT_CRYS`, requiring the external-crystal pins;
- `RTC_CLK_SRC_EXT_OSC`, a driven external clock at `32K_XP` / GPIO15;
- `RTC_CLK_SRC_INT_8MD256`, the internal fast oscillator divided by 256.

The RTC chips provide programmable 32.768 kHz outputs, but component capability
does not create a board connection. If a future hardware revision deliberately
routes an RTC output to GPIO15, the driven-clock mode would be `EXT_OSC`, not
`EXT_CRYS`. That is a new hardware design and is outside #268.

No `RTC_CLK_SRC` override exists in the current firmware defaults, so ESP-IDF's
`INT_RC` default is already the fail-closed configuration.

## Board traces

### LilyGO T-Watch S3 Plus

The vendor PDF `T_WATCH-S3 25-03-24.pdf` shows:

- sheet 2: ESP32-S3 GPIO15 and GPIO16 are the `XTAL_32K_P/N` package pins;
- sheet 3: PCF8563 pin 7 `CLKOUT` becomes `RTC_CLKOUT`, is pulled up to +3V3 by
  R291 (10 kΩ), and terminates at `TP66`;
- sheet 4: GPIO16 is the touch-controller `INT` route;
- sheet 6: GPIO15 is MAX98357A `LRCLK`.

`RTC_CLKOUT` appears nowhere outside sheet 3. The earlier repository statement
that associated it with unpopulated `R126` was wrong: sheet 3 places `R126` on
the DRV2605L supply pin, unrelated to the RTC.

### Waveshare ESP32-S3-Touch-AMOLED-2.06

The vendor PDF `ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf`, PDF page 1,
shows:

- PCF85063ATL pin 9 `CLKOUT` with a no-connect marker and pin 3 `CLKOE` as an
  unrouted stub;
- GPIO15 as `TP_SDA` / `RTC_SDA` / `ESP32_SDA`;
- GPIO16 as `I2S_MCLK`;
- no 32.768 kHz crystal or oscillator on the ESP32-S3 `XTAL_32K_P/N` pins.

The RTC's own 32.768 kHz crystal clocks the RTC only. It is not a slow-clock
source for the ESP32-S3 on this board.

## Power-evidence boundary

Espressif's v5.5.5 NimBLE power-save example publishes typical ESP32-S3 values
of 3.3 mA in light sleep using the main crystal and 230 µA using a 32 kHz
crystal. Its ESP32-S3 defaults explicitly select `RTC_CLK_SRC_EXT_CRYS`.
Those are vendor example figures, not measurements of either Attadipa board and
not evidence for an RTC-driven `EXT_OSC` path.

**Current consumption on both target boards: NOT EXECUTED — HARDWARE REQUIRED.**
No suitable current instrument was used in this task.

## Primary sources

- [ESP32-S3 Series Datasheet v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf), §2.2 / Table 2-8; downloaded SHA-256 `2d5a7cb7fd559d8d972bd88db32669c0196d23f22d7afaafb0f63d099b589a3f`.
- ESP-IDF v5.5.5, commit `b774170ff46c393eeb5e495ea37936038d3f4f4f`: [RTC Kconfig](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/esp_hw_support/port/esp32s3/Kconfig.rtc), [pin definitions](https://github.com/espressif/esp-idf/blob/b774170ff46c393eeb5e495ea37936038d3f4f4f/components/soc/esp32s3/register/soc/io_mux_reg.h), and [NimBLE power-save example](https://github.com/espressif/esp-idf/tree/b774170ff46c393eeb5e495ea37936038d3f4f4f/examples/bluetooth/nimble/power_save).
- LilyGO `LilyGoLib` commit `38e6f8dee3ba78b340512af9a013365ef248a7d0`, [`schematic/T_WATCH-S3 25-03-24.pdf`](https://github.com/Xinyuan-LilyGO/LilyGoLib/blob/38e6f8dee3ba78b340512af9a013365ef248a7d0/schematic/T_WATCH-S3%2025-03-24.pdf), SHA-256 `3fc71eba5b30085b4fe20c6222df26230af0602ddff08e257b9cdf090c58d931`.
- Waveshare commit `b099739ad0e33b34e5fbaae77f02bd84805d79a3`, [`Schematic/ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf`](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.06/blob/b099739ad0e33b34e5fbaae77f02bd84805d79a3/Schematic/ESP32-S3-Touch-AMOLED-2.06-Schematic-V1.0.pdf), SHA-256 `6d531fb458863c666210c92294a07204d675bcb7997a54fc219d92fadbbacf9d`.
- NXP [PCF8563 datasheet](https://www.nxp.com/docs/en/data-sheet/PCF8563.pdf) and [PCF85063A/ATL datasheet](https://www.nxp.com/docs/en/data-sheet/PCF85063A.pdf), programmable `CLKOUT` descriptions and pinouts.
