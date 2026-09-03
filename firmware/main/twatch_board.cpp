// The T-Watch S3 Plus board backend, first slice (#417).
//
// It composes ESP-IDF's own drivers — esp_lcd_panel_st7789 over SPI, the
// pinned esp_lcd_touch_ft5x06 for the FT6336U — and adds only what the
// datasheet or the vendor source made necessary (ADR-0017). Every pin and rail
// below is the VERIFIED row of docs/research/HARDWARE_MATRIX.md for this board.
//
// Not here, on purpose: the PCF8563 RTC (a different register map from the
// Waveshare's PCF85063), NVS, the physical-input poller (its interrupt pins are
// the Waveshare's), sleep, the sensors, radio, GNSS, audio. This image exists to
// produce one photograph; what it shows decides the next slice.

#include "twatch_board.h"

#include <cinttypes>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_lvgl_port_touch.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "sdkconfig.h"

#include "attadipa/platform/board_profile.h"
#include "board_power.h"
#include "twatch_boot_rollback.h"
#include "twatch_panel_exercise.h"

namespace {

constexpr char kTag[] = "twatch";
constexpr char kBoardProfileId[] = "t-watch-s3-plus";

// Main I2C: AXP2101 0x34, BMA423 0x19, PCF8563 0x51, DRV2605 0x5A. All four
// ACKed here on 2026-08-28 from a RAM-loaded probe (#300).
constexpr gpio_num_t kMainSda = GPIO_NUM_10;
constexpr gpio_num_t kMainScl = GPIO_NUM_11;
constexpr std::uint8_t kPmuAddress = 0x34;

// Panel: ST7789V3 240x240 over SPI. MISO and RESET are not connected, so the
// only reset the panel has is SWRESET over the bus.
constexpr gpio_num_t kLcdCs = GPIO_NUM_12;
constexpr gpio_num_t kLcdMosi = GPIO_NUM_13;
constexpr gpio_num_t kLcdSck = GPIO_NUM_18;
constexpr gpio_num_t kLcdDc = GPIO_NUM_38;
// GPIO 45 is also the VDD_SPI strap. The backlight transistor is active-high
// and dark at reset, so the pin is a plain output that is driven low first and
// high only once a frame is in GRAM — and it is never pulled up
// (HARDWARE_MATRIX.md, "Pins firmware cannot control").
constexpr gpio_num_t kBacklight = GPIO_NUM_45;

// Touch: FT6336U on its own bus. RESET is not fitted (R39 NC) and INT is the
// only line besides the bus.
constexpr gpio_num_t kTouchSda = GPIO_NUM_39;
constexpr gpio_num_t kTouchScl = GPIO_NUM_40;
constexpr gpio_num_t kTouchInt = GPIO_NUM_16;

constexpr int kWidth = 240;
constexpr int kHeight = 240;
constexpr int kLinesPerBuffer = 20;
// LilyGoLib clocks this panel at 80 MHz and Meshtastic at 40; what the panel
// tolerates is UNKNOWN (TWATCH_S3_PLUS_BSP_REUSE.md §6). The slower shipping
// value is the starting point, not a measurement.
constexpr int kPanelClockHz = 40 * 1000 * 1000;
#if CONFIG_ATTADIPA_TWATCH_PANEL_INVERT
constexpr bool kPanelInvert = true;
#else
constexpr bool kPanelInvert = false;
#endif

// Readiness of both devices after ALDO3 is UNKNOWN: no datasheet figure has
// been traced. These 100 ms floors are ESTIMATED experiment controls, not
// component limits. Naming them keeps rail ramp time separate from the reset
// delay that arms A/C vary, and keeps panel work from accidentally becoming the
// touch settle delay.
constexpr std::uint32_t kPanelSettleMs = 100;
constexpr std::uint32_t kTouchSettleMs = 100;
constexpr unsigned kTouchProbeAttempts = 3;
constexpr std::uint32_t kTouchRetryMs = 50;
constexpr std::uint32_t kDiagnosticHoldMs = 250;
constexpr std::uint32_t kObservationHoldMs = 5000;
// ESP-IDF's ST7789 sleep call waits 100 ms; the panel datasheet requires 120
// ms from SLPOUT to the following SLPIN (TWATCH_S3_PLUS_BSP_REUSE.md §4).
constexpr std::uint32_t kSleepIntervalMs = 120;
constexpr std::uint32_t kDriverSleepDelayMs = 100;
constexpr int kCheckerSize = 48;
std::uint16_t checker_pixels[kCheckerSize * kCheckerSize];

struct State {
  i2c_master_bus_handle_t main_i2c = nullptr;
  i2c_master_bus_handle_t touch_i2c = nullptr;
  i2c_master_dev_handle_t pmu = nullptr;
  bool spi_bus_up = false;
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  bool lvgl_up = false;
  lv_display_t *display = nullptr;
  esp_lcd_panel_io_handle_t touch_io = nullptr;
  esp_lcd_touch_handle_t touch = nullptr;
  lv_indev_t *indev = nullptr;
  lv_obj_t *marker = nullptr;
  lv_obj_t *readout = nullptr;
  lv_obj_t *partial_patch = nullptr;
  TickType_t rails_up = 0;  // when initialize_pmu() returned with ALDO3 on
  std::uint32_t panel_settle_ms = 0;
  std::uint32_t reset_interval_ms = 0;
  unsigned touch_attempts = 0;
};
State state;

std::uint32_t wait_from_rails(std::uint32_t floor_ms) {
  const TickType_t floor = pdMS_TO_TICKS(floor_ms);
  TickType_t elapsed = xTaskGetTickCount() - state.rails_up;
  if (elapsed < floor) {
    vTaskDelay(floor - elapsed);
    elapsed = xTaskGetTickCount() - state.rails_up;
  }
  return static_cast<std::uint32_t>(pdTICKS_TO_MS(elapsed));
}

esp_err_t new_i2c_bus(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl,
                      i2c_master_bus_handle_t *out) {
  i2c_master_bus_config_t config{};
  config.i2c_port = port;
  config.sda_io_num = sda;
  config.scl_io_num = scl;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&config, out);
}

esp_err_t backlight(bool on) {
  gpio_config_t config{};
  config.pin_bit_mask = 1ULL << kBacklight;
  config.mode = GPIO_MODE_OUTPUT;
  config.pull_up_en = GPIO_PULLUP_DISABLE;
  config.pull_down_en = GPIO_PULLDOWN_DISABLE;
  config.intr_type = GPIO_INTR_DISABLE;
  ESP_RETURN_ON_ERROR(gpio_config(&config), kTag, "backlight GPIO");
  return gpio_set_level(kBacklight, on ? 1 : 0);
}

esp_err_t initialize_pmu() {
  ESP_RETURN_ON_ERROR(new_i2c_bus(I2C_NUM_0, kMainSda, kMainScl, &state.main_i2c),
                      kTag, "main I2C");
  i2c_device_config_t device{};
  device.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device.device_address = kPmuAddress;
  device.scl_speed_hz = 400000;
  ESP_RETURN_ON_ERROR(
      i2c_master_bus_add_device(state.main_i2c, &device, &state.pmu), kTag,
      "AXP2101 at 0x34");
  // The backend needs ALDO3 (panel and touch) and ALDO2 (backlight). It says so
  // and the power owner grants or refuses; no rail register is written from
  // this file (ADR-0016, TWATCH_S3_PLUS_BSP_REUSE.md §10.4).
  return attadipa::firmware::board_power_bring_up_rails(state.pmu);
}

#if CONFIG_ATTADIPA_TWATCH_PANEL_VENDOR_TABLE
// Arm B of the §11 experiment: the vendor's live command table, sent as data
// over the public panel-IO call after the driver's own init. Transcribed from
// LilyGoLib@38e6f8d src/LilyGoDispInterface.cpp:505-524 as recorded in
// TWATCH_S3_PLUS_BSP_REUSE.md §5 — the file's second, `#if 0` table is dead
// and is not what is here.
//
// MIT License. Copyright (c) 2025 Xinyuan Electronics. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files, to deal in the software without restriction,
// subject to the above copyright notice and this permission notice being
// included in all copies or substantial portions of the software. THE SOFTWARE
// IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
struct PanelCommand {
  std::uint8_t command;
  std::uint8_t length;
  std::uint8_t data[14];
  std::uint16_t delay_ms;
};
constexpr PanelCommand kVendorTable[] = {
    {0x11, 0, {}, 120},                                 // SLPOUT
    {0xB2, 5, {0x1F, 0x1F, 0x00, 0x33, 0x33}, 0},       // PORCTRL
    {0x35, 1, {0x00}, 0},                               // TEON
    {0x36, 1, {0x00}, 0},                               // MADCTL
    {0x3A, 1, {0x05}, 0},                               // COLMOD
    {0xB7, 1, {0x00}, 0},                               // GCTRL
    {0xBB, 1, {0x36}, 0},                               // VCOMS
    {0xC0, 1, {0x2C}, 0},                               // LCMCTRL
    {0xC2, 1, {0x01}, 0},                               // VDVVRHEN
    {0xC3, 1, {0x13}, 0},                               // VRHS
    {0xC4, 1, {0x20}, 0},                               // VDVS
    {0xC6, 1, {0x13}, 0},                               // FRCTRL2
    {0xD6, 1, {0xA1}, 0},                               // undocumented
    {0xD0, 2, {0xA4, 0xA1}, 0},                         // PWCTRL1
    {0xD6, 1, {0xA1}, 0},                               // repeated in the source
    {0xE0, 14, {0xF0, 0x08, 0x0E, 0x09, 0x08, 0x04, 0x2F, 0x33, 0x45, 0x36, 0x13,
                0x12, 0x2A, 0x2D}, 0},                  // PVGAMCTRL
    {0xE1, 14, {0xF0, 0x0E, 0x12, 0x0C, 0x0A, 0x15, 0x2E, 0x32, 0x44, 0x39, 0x17,
                0x18, 0x2B, 0x2F}, 0},                  // NVGAMCTRL
    {0xE4, 3, {0x1D, 0x00, 0x00}, 0},                   // GATECTRL
};

esp_err_t send_vendor_table(esp_lcd_panel_io_handle_t io) {
  for (const PanelCommand &entry : kVendorTable) {
    ESP_RETURN_ON_ERROR(
        esp_lcd_panel_io_tx_param(io, entry.command,
                                  entry.length ? entry.data : nullptr,
                                  entry.length),
        kTag, "vendor command 0x%02x", entry.command);
    if (entry.delay_ms != 0) {
      vTaskDelay(pdMS_TO_TICKS(entry.delay_ms));
    }
  }
  return ESP_OK;
}
#endif

esp_err_t initialize_panel(const attadipa::platform::BoardProfile &profile) {
  spi_bus_config_t bus{};
  bus.sclk_io_num = kLcdSck;
  bus.mosi_io_num = kLcdMosi;
  bus.miso_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kWidth * kLinesPerBuffer * sizeof(std::uint16_t);
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                      kTag, "initialize SPI2");
  state.spi_bus_up = true;

  esp_lcd_panel_io_spi_config_t io{};
  io.cs_gpio_num = kLcdCs;
  io.dc_gpio_num = kLcdDc;
  io.spi_mode = 0;
  io.pclk_hz = kPanelClockHz;
  io.trans_queue_depth = 10;
  io.lcd_cmd_bits = 8;
  io.lcd_param_bits = 8;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(SPI2_HOST, &io, &state.panel_io),
                      kTag, "create panel IO");

  esp_lcd_panel_dev_config_t panel{};
  panel.reset_gpio_num = -1;  // not connected: the driver falls back to SWRESET
  panel.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel.bits_per_pixel = 16;
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(state.panel_io, &panel, &state.panel),
                      kTag, "create ST7789 panel");

  // The driver waits 20 ms after SWRESET; the ST7789V3 datasheet requires
  // 120 ms before SLPOUT when the panel is in sleep-in, which after power-on it
  // always is (TWATCH_S3_PLUS_BSP_REUSE.md §4). The driver has no knob for
  // that, so the wait is the board's. 100 here is arm C of §11; 0 reproduces
  // the driver as shipped, arm A.
  const std::int64_t reset_started_us = esp_timer_get_time();
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(state.panel), kTag, "SWRESET");
  if (CONFIG_ATTADIPA_TWATCH_PANEL_RESET_EXTRA_MS > 0) {
    // The driver's 20 ms wait and this wait share the tick timeline. The extra
    // tick removes the one-tick early wake allowed by vTaskDelay(), so arm C
    // cannot fall below the 120 ms floor it exists to test.
    vTaskDelay(pdMS_TO_TICKS(CONFIG_ATTADIPA_TWATCH_PANEL_RESET_EXTRA_MS) + 1);
  }
  state.reset_interval_ms = static_cast<std::uint32_t>(
      (esp_timer_get_time() - reset_started_us) / 1000);
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(state.panel), kTag,
                      "SLPOUT, MADCTL, COLMOD");
#if CONFIG_ATTADIPA_TWATCH_PANEL_VENDOR_TABLE
  ESP_RETURN_ON_ERROR(send_vendor_table(state.panel_io), kTag, "vendor table");
#endif
  // SWRESET leaves inversion off, and the vendor turns it on: INVON (21h) is
  // sent through the panel API, not from the table, so arm B does not carry it
  // either (TWATCH_S3_PLUS_BSP_REUSE.md §5, §11). Between the table and DISPON,
  // where the vendor sends it. Inverted, the swatch photographs complemented:
  // red reads cyan, the white block black -- a different picture from the byte
  // order being wrong, which is what §11 assigns a colour change to.
  ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(state.panel, kPanelInvert),
                      kTag, "%s", kPanelInvert ? "INVON" : "INVOFF");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(state.panel, true), kTag,
                      "DISPON");

  lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
  port.task_priority = 1;
  port.task_affinity = 1;
  ESP_RETURN_ON_ERROR(lvgl_port_init(&port), kTag, "initialize LVGL");
  state.lvgl_up = true;

  lvgl_port_display_cfg_t display{};
  display.io_handle = state.panel_io;
  display.panel_handle = state.panel;
  display.buffer_size = kWidth * kLinesPerBuffer;
  display.hres = kWidth;
  display.vres = kHeight;
  display.color_format = LV_COLOR_FORMAT_RGB565;
  display.flags.buff_dma = true;
  // A board-level transfer fact, so the profile states it and this port only
  // reads it, the way waveshare_board.cpp does. If it is the wrong call the
  // swatch below changes colour rather than mirroring, which is what its
  // asymmetry is for -- and the fix is the profile's line, not this one.
  display.flags.swap_bytes = profile.display.rgb565_swap_bytes;
  state.display = lvgl_port_add_disp(&display);
  ESP_RETURN_ON_FALSE(state.display != nullptr, ESP_ERR_NO_MEM, kTag,
                      "add LVGL display");
  return ESP_OK;
}

esp_err_t initialize_touch() {
  ESP_RETURN_ON_ERROR(
      new_i2c_bus(I2C_NUM_1, kTouchSda, kTouchScl, &state.touch_i2c), kTag,
      "touch I2C");

  esp_lcd_panel_io_i2c_config_t io{};
  io.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
  io.control_phase_bytes = 1;
  io.lcd_cmd_bits = 8;
  io.scl_speed_hz = 400000;
  io.flags.disable_control_phase = true;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_i2c(state.touch_i2c, &io, &state.touch_io),
                      kTag, "create touch IO");

  esp_lcd_touch_config_t config{};
  config.x_max = kWidth - 1;
  config.y_max = kHeight - 1;
  config.rst_gpio_num = GPIO_NUM_NC;  // R39 not fitted; no reset line exists
  config.int_gpio_num = kTouchInt;
  config.levels.interrupt = 0;
  // No sleep command is ever sent to this controller. The only recovery from a
  // wedged FT6336U is cycling ALDO3, which blanks the display
  // (TWATCH_S3_PLUS_BSP_REUSE.md §8, §10.5).
  (void)wait_from_rails(kTouchSettleMs);
  esp_err_t err = ESP_FAIL;
  for (state.touch_attempts = 1; state.touch_attempts <= kTouchProbeAttempts;
       ++state.touch_attempts) {
    err = esp_lcd_touch_new_i2c_ft5x06(state.touch_io, &config, &state.touch);
    if (err == ESP_OK) {
      break;
    }
    ESP_LOGW(kTag, "FT6336U probe %u of %u: %s", state.touch_attempts,
             kTouchProbeAttempts, esp_err_to_name(err));
    if (state.touch_attempts < kTouchProbeAttempts) {
      vTaskDelay(pdMS_TO_TICKS(kTouchRetryMs));
    }
  }
  if (err != ESP_OK) {
    state.touch_attempts = kTouchProbeAttempts;
    return err;
  }
  // This proves an ACK at 0x38 and nothing more. Touch is Ready when a
  // coordinate arrives (§10.2); the screen logs the first one.
  return ESP_OK;
}

void on_touch(lv_event_t *event) {
  lv_indev_t *indev = lv_indev_active();
  if (indev == nullptr) {
    return;
  }
  lv_point_t point{};
  lv_indev_get_point(indev, &point);
  lv_obj_set_pos(state.marker, point.x - 6, point.y - 6);
  lv_obj_remove_flag(state.marker, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text_fmt(state.readout, "%d,%d", static_cast<int>(point.x),
                        static_cast<int>(point.y));
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED) {
    ESP_LOGI(kTag, "touch %s at %d,%d", code == LV_EVENT_PRESSED ? "down" : "up",
             static_cast<int>(point.x), static_cast<int>(point.y));
  }
}

lv_obj_t *swatch(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t colour) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_remove_style_all(box);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(box, x, y);
  lv_obj_set_size(box, w, h);
  lv_obj_set_style_bg_color(box, colour, 0);
  lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
  return box;
}

lv_obj_t *corner(lv_obj_t *parent, lv_align_t align, const char *text) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label, lv_color_black(), 0);
  lv_obj_set_style_bg_color(label, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(label, LV_OPA_COVER, 0);
  lv_obj_align(label, align, 0, 0);
  return label;
}

// §11's screen: unequal colour blocks distinguish byte swap, mirror and
// rotation; named corners show orientation; the ramp exposes gamma/banding;
// the canvas makes a literal one-pixel checkerboard.
void build_bringup_screen() {
  lvgl_port_lock(0);
  lv_obj_t *screen = lv_screen_active();
  lv_obj_remove_style_all(screen);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

  swatch(screen, 0, 0, 140, 60, lv_palette_main(LV_PALETTE_RED));
  swatch(screen, 140, 0, 100, 90, lv_palette_main(LV_PALETTE_GREEN));
  swatch(screen, 0, 60, 90, 60, lv_palette_main(LV_PALETTE_BLUE));
  swatch(screen, 90, 90, 150, 30, lv_color_white());
  for (int band = 0; band < 8; ++band) {
    const std::uint8_t level = static_cast<std::uint8_t>((255 * band) / 7);
    swatch(screen, band * 30, 120, 30, 36,
           lv_color_make(level, level, level));
  }

  lv_obj_t *checker = lv_canvas_create(screen);
  lv_canvas_set_buffer(checker, checker_pixels, kCheckerSize, kCheckerSize,
                       LV_COLOR_FORMAT_RGB565);
  for (int y = 0; y < kCheckerSize; ++y) {
    for (int x = 0; x < kCheckerSize; ++x) {
      lv_canvas_set_px(checker, x, y, ((x + y) & 1) ? lv_color_white()
                                                    : lv_color_black(),
                       LV_OPA_COVER);
    }
  }
  lv_obj_set_pos(checker, 0, 168);

  state.partial_patch =
      swatch(screen, 64, 168, 48, 48, lv_palette_main(LV_PALETTE_YELLOW));
  corner(screen, LV_ALIGN_TOP_LEFT, "0,0");
  corner(screen, LV_ALIGN_TOP_RIGHT, "239,0");
  corner(screen, LV_ALIGN_BOTTOM_LEFT, "0,239");
  corner(screen, LV_ALIGN_BOTTOM_RIGHT, "239,239");

  state.readout = lv_label_create(screen);
  lv_label_set_text(state.readout, "tap");
  lv_obj_set_style_text_color(state.readout, lv_color_white(), 0);
  lv_obj_set_style_bg_color(state.readout, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(state.readout, LV_OPA_COVER, 0);
  lv_obj_center(state.readout);

  state.marker = lv_obj_create(screen);
  lv_obj_remove_style_all(state.marker);
  lv_obj_remove_flag(state.marker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(state.marker, 12, 12);
  lv_obj_set_style_radius(state.marker, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(state.marker, lv_palette_main(LV_PALETTE_YELLOW), 0);
  lv_obj_set_style_bg_opa(state.marker, LV_OPA_COVER, 0);
  lv_obj_add_flag(state.marker, LV_OBJ_FLAG_HIDDEN);

  lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(screen, on_touch, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(screen, on_touch, LV_EVENT_RELEASED, nullptr);
  lvgl_port_unlock();
}

struct TwatchPanelExerciseOps {
  esp_err_t error = ESP_OK;

  bool call(esp_err_t result, const char *step) {
    if (result == ESP_OK) {
      return true;
    }
    error = result;
    ESP_LOGE(kTag, "panel exercise: %s failed: %s", step,
             esp_err_to_name(result));
    return false;
  }

  void refresh_full() {
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(state.display);
  }

  bool show_patterns() {
    ESP_LOGI(kTag, "panel exercise: full flush");
    lvgl_port_lock(0);
    refresh_full();
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(kDiagnosticHoldMs));

    ESP_LOGI(kTag, "panel exercise: partial 48x48 flush");
    lvgl_port_lock(0);
    lv_obj_set_style_bg_color(state.partial_patch,
                              lv_palette_main(LV_PALETTE_PURPLE), 0);
    lv_obj_invalidate(state.partial_patch);
    lv_refr_now(state.display);
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(kDiagnosticHoldMs));
    return true;
  }

  bool rotation_and_gap() {
    ESP_LOGI(kTag, "panel exercise: rotation 90 -> 0");
    lvgl_port_lock(0);
    lv_display_set_rotation(state.display, LV_DISPLAY_ROTATION_90);
    refresh_full();
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(kDiagnosticHoldMs));
    lvgl_port_lock(0);
    lv_display_set_rotation(state.display, LV_DISPLAY_ROTATION_0);
    refresh_full();
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(kDiagnosticHoldMs));

    ESP_LOGI(kTag, "panel exercise: gap 1,1 -> 0,0");
    lvgl_port_lock(0);
    bool ok = call(esp_lcd_panel_set_gap(state.panel, 1, 1), "set gap 1,1");
    if (ok) {
      refresh_full();
    }
    lvgl_port_unlock();
    if (!ok) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(kDiagnosticHoldMs));
    lvgl_port_lock(0);
    ok = call(esp_lcd_panel_set_gap(state.panel, 0, 0), "restore gap 0,0");
    if (ok) {
      refresh_full();
    }
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(kDiagnosticHoldMs));
    return ok;
  }

  bool display_cycle(unsigned cycle) {
    ESP_LOGI(kTag, "panel exercise: display cycle %u of 10", cycle);
    lvgl_port_lock(0);
    bool ok = call(esp_lcd_panel_disp_on_off(state.panel, false), "DISPOFF");
    if (ok) {
      vTaskDelay(pdMS_TO_TICKS(kSleepIntervalMs));
      ok = call(esp_lcd_panel_disp_on_off(state.panel, true), "DISPON");
    }
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(kSleepIntervalMs));
    return ok;
  }

  bool sleep_cycle(bool respect_interval) {
    ESP_LOGI(kTag, "panel exercise: sleep target %" PRIu32 " ms (%s)",
             kDriverSleepDelayMs + (respect_interval ? 20 : 0),
             respect_interval ? "datasheet-conforming" : "deliberately short");
    lvgl_port_lock(0);
    bool ok = call(esp_lcd_panel_disp_sleep(state.panel, true), "SLPIN");
    const std::int64_t wake_started_us = esp_timer_get_time();
    if (ok) {
      ok = call(esp_lcd_panel_disp_sleep(state.panel, false), "SLPOUT");
    }
    if (ok && respect_interval) {
      // Together with the driver's 100 ms wait, the extra tick makes the
      // command-to-command interval at least 120 ms at the default 100 Hz.
      vTaskDelay(pdMS_TO_TICKS(kSleepIntervalMs - kDriverSleepDelayMs) + 1);
    }
    const std::uint32_t observed_ms = static_cast<std::uint32_t>(
        (esp_timer_get_time() - wake_started_us) / 1000);
    if (ok) {
      ESP_LOGI(kTag, "panel exercise: sleep interval MEASURED %" PRIu32
                     " ms (%s)",
               observed_ms,
               respect_interval ? "datasheet-conforming" : "deliberately short");
    }
    if (ok) {
      ok = call(esp_lcd_panel_disp_sleep(state.panel, true), "SLPIN after wake");
    }
    if (ok) {
      ok = call(esp_lcd_panel_disp_sleep(state.panel, false), "final SLPOUT");
    }
    if (ok) {
      lv_label_set_text_fmt(state.readout, "M%" PRIu32 " %s", observed_ms,
                            respect_interval ? "CONFORM" : "SHORT");
      lv_obj_invalidate(state.readout);
      lv_refr_now(state.display);
    }
    lvgl_port_unlock();
    if (ok) {
      // The panel has no read-back. Keep each labelled result on glass long
      // enough for its own photograph; the next arm must not erase the only
      // evidence before an operator can see it.
      vTaskDelay(pdMS_TO_TICKS(kObservationHoldMs));
    }
    return ok;
  }
};

template <typename Handle, typename Undo>
void undo(Handle &handle, const char *what, Undo undo_step) {
  if (handle == nullptr) {
    return;
  }
  const esp_err_t err = undo_step(handle);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "boot rollback: %s failed: %s", what, esp_err_to_name(err));
  }
  handle = nullptr;
}

struct TwatchRollbackOps {
  bool display_registered() const { return state.display != nullptr; }

  void retain_display_stack() {
    ESP_LOGE(kTag, "boot rollback: a live LVGL display may have queued SPI DMA; "
                   "the whole T-Watch stack stays alive rather than freeing "
                   "its display or framebuffer before the callback (#367)");
  }

  void remove_touch() {
    undo(state.touch, "delete touch", esp_lcd_touch_del);
  }
  void remove_touch_io() {
    undo(state.touch_io, "delete touch IO", esp_lcd_panel_io_del);
  }
  void remove_touch_bus() {
    undo(state.touch_i2c, "delete touch I2C bus", i2c_del_master_bus);
  }
  void stop_lvgl() {
    if (!state.lvgl_up) {
      return;
    }
    const esp_err_t err = lvgl_port_deinit();
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "boot rollback: stop LVGL failed: %s", esp_err_to_name(err));
    }
    state.lvgl_up = false;
  }
  void remove_panel() {
    undo(state.panel, "delete panel", esp_lcd_panel_del);
  }
  void remove_panel_io() {
    undo(state.panel_io, "delete panel IO", esp_lcd_panel_io_del);
  }
  void free_spi() {
    if (!state.spi_bus_up) {
      return;
    }
    const esp_err_t err = spi_bus_free(SPI2_HOST);
    if (err != ESP_OK) {
      ESP_LOGE(kTag, "boot rollback: free SPI2 failed: %s", esp_err_to_name(err));
    }
    state.spi_bus_up = false;
  }
  void remove_pmu() {
    if (state.pmu != nullptr) {
      ESP_LOGW(kTag, "boot rollback: ALDO2/3 stay as written -- their loads "
                     "have no measured safe power-down sequence");
    }
    undo(state.pmu, "remove AXP2101", i2c_master_bus_rm_device);
  }
  void remove_main_bus() {
    undo(state.main_i2c, "delete main I2C bus", i2c_del_master_bus);
  }
};

void abandon_touch() {
  TwatchRollbackOps ops;
  ops.remove_touch();
  ops.remove_touch_io();
  ops.remove_touch_bus();
}

esp_err_t abandon_twatch_after(esp_err_t err, const char *step) {
  ESP_LOGE(kTag, "%s failed: %s; rolling the boot back", step,
           esp_err_to_name(err));
  TwatchRollbackOps ops;
  attadipa::firmware::rollback_twatch_boot(ops);
  return err;
}

} // namespace

esp_err_t start_twatch_ui() {
  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  ESP_RETURN_ON_FALSE(profile != nullptr, ESP_ERR_NOT_FOUND, kTag,
                      "board profile missing");
  ESP_RETURN_ON_FALSE(profile->display.width_px == kWidth &&
                          profile->display.height_px == kHeight,
                      ESP_ERR_INVALID_STATE, kTag,
                      "profile geometry disagrees with this backend");

  esp_err_t err = backlight(false);
  if (err != ESP_OK) {
    return abandon_twatch_after(err, "hold the backlight dark");
  }
  // Without the PMU there is no ALDO3 and nothing below can answer.
  err = initialize_pmu();
  if (err != ESP_OK) {
    return abandon_twatch_after(err, "PMU and rails");
  }
  state.rails_up = xTaskGetTickCount();
  state.panel_settle_ms = wait_from_rails(kPanelSettleMs);

  // §10.1 and §10.3: a failed panel command loses the display and nothing
  // else; a dead touch bus leaves the display alone.
  const esp_err_t panel_err = initialize_panel(*profile);
  if (panel_err != ESP_OK) {
    ESP_LOGE(kTag, "display capability absent: %s", esp_err_to_name(panel_err));
  }
  esp_err_t touch_err = initialize_touch();
  if (touch_err != ESP_OK) {
    ESP_LOGE(kTag, "touch capability absent: %s", esp_err_to_name(touch_err));
    abandon_touch();
  }

  if (panel_err == ESP_OK) {
    if (touch_err == ESP_OK) {
      lvgl_port_touch_cfg_t touch{};
      touch.disp = state.display;
      touch.handle = state.touch;
      state.indev = lvgl_port_add_touch(&touch);
      if (state.indev == nullptr) {
        ESP_LOGE(kTag, "touch answered but LVGL could not add it");
        touch_err = ESP_ERR_NO_MEM;
        abandon_touch();
      }
    }
    build_bringup_screen();
    // Two LVGL refresh periods: the first frame is in GRAM before the LED is.
    vTaskDelay(pdMS_TO_TICKS(200));
    err = backlight(true);
    if (err != ESP_OK) {
      return abandon_twatch_after(err, "backlight on");
    }
    TwatchPanelExerciseOps exercise;
    if (!attadipa::firmware::run_twatch_panel_exercise(exercise)) {
      err = exercise.error == ESP_OK ? ESP_FAIL : exercise.error;
    }
  }

  ESP_LOGI(kTag,
           "T-Watch S3 Plus bring-up: panel %s, touch %s (probe %u of %u); SPI "
           "%d MHz, rail settle ESTIMATED >=%" PRIu32 " ms (observed %" PRIu32
           "), reset interval %s %" PRIu32 " ms (+%d configured), %s, "
           "RGB565 byte swap %s, vendor table %s, panel exercise %s",
           panel_err == ESP_OK ? "up" : "ABSENT",
           touch_err == ESP_OK ? "ACK at 0x38" : "ABSENT", state.touch_attempts,
           kTouchProbeAttempts, kPanelClockHz / 1000000, kPanelSettleMs,
           state.panel_settle_ms,
           state.reset_interval_ms == 0 ? "UNKNOWN" : "MEASURED",
           state.reset_interval_ms,
           CONFIG_ATTADIPA_TWATCH_PANEL_RESET_EXTRA_MS,
           kPanelInvert ? "INVON" : "INVOFF",
           profile->display.rgb565_swap_bytes ? "yes" : "no",
#if CONFIG_ATTADIPA_TWATCH_PANEL_VENDOR_TABLE
           "sent",
#else
           "not sent",
#endif
           panel_err != ESP_OK ? "NOT RUN" : err == ESP_OK ? "passed" : "FAILED"
  );
  if (err != ESP_OK) {
    return abandon_twatch_after(err, "panel exercise");
  }
  if (panel_err != ESP_OK) {
    return abandon_twatch_after(panel_err, "display capability");
  }
  return ESP_OK;
}
