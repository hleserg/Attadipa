#include "waveshare_board.h"

#include <cinttypes>
#include <cstdint>
#include <cstdio>

#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "attadipa/platform/board_profile.h"

namespace {

constexpr char kTag[] = "waveshare";
constexpr char kBoardProfileId[] = "waveshare-amoled-206";

constexpr int kWidth = 410;
constexpr int kHeight = 502;
constexpr int kPanelGapX = 0x16;
constexpr int kBrightnessPercent = 1;

constexpr gpio_num_t kLcdCs = GPIO_NUM_12;
constexpr gpio_num_t kLcdClock = GPIO_NUM_11;
constexpr gpio_num_t kLcdData0 = GPIO_NUM_4;
constexpr gpio_num_t kLcdData1 = GPIO_NUM_5;
constexpr gpio_num_t kLcdData2 = GPIO_NUM_6;
constexpr gpio_num_t kLcdData3 = GPIO_NUM_7;
constexpr gpio_num_t kLcdReset = GPIO_NUM_8;
constexpr gpio_num_t kTouchReset = GPIO_NUM_9;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_38;
constexpr gpio_num_t kI2cSda = GPIO_NUM_15;
constexpr gpio_num_t kI2cScl = GPIO_NUM_14;

constexpr std::uint8_t kAxp2101Address = 0x34;
constexpr std::uint8_t kPcf85063Address = 0x51;

constexpr std::uint8_t kC4[] = {0x80};
constexpr std::uint8_t kTearingLine[] = {0x01, 0xD1};
constexpr std::uint8_t kTearingOn[] = {0x00};
constexpr std::uint8_t kWriteControl[] = {0x20};
constexpr std::uint8_t kUnknown63[] = {0xFF};
constexpr std::uint8_t kBrightnessOff[] = {0x00};
constexpr std::uint8_t kColumns[] = {0x00, 0x16, 0x01, 0xAF};
constexpr std::uint8_t kRows[] = {0x00, 0x00, 0x01, 0xF5};

// The exact panel sequence used by the known-working vendor implementation,
// except display-on is delayed until the black UI objects exist. Brightness
// starts at zero and is raised to 1% only after that.
constexpr co5300_lcd_init_cmd_t kPanelInit[] = {
    {0x11, nullptr, 0, 120},
    {0xC4, kC4, sizeof(kC4), 0},
    {0x44, kTearingLine, sizeof(kTearingLine), 0},
    {0x35, kTearingOn, sizeof(kTearingOn), 0},
    {0x53, kWriteControl, sizeof(kWriteControl), 10},
    {0x63, kUnknown63, sizeof(kUnknown63), 10},
    {0x51, kBrightnessOff, sizeof(kBrightnessOff), 10},
    {0x2A, kColumns, sizeof(kColumns), 0},
    {0x2B, kRows, sizeof(kRows), 0},
};

struct BoardState {
  i2c_master_bus_handle_t i2c = nullptr;
  i2c_master_dev_handle_t pmu = nullptr;
  i2c_master_dev_handle_t rtc = nullptr;
  esp_lcd_panel_handle_t panel = nullptr;
  lv_display_t *display = nullptr;
  lv_obj_t *rtc_label = nullptr;
  lv_obj_t *touch_label = nullptr;
  std::uint32_t touches = 0;
};

BoardState state;

esp_err_t add_i2c_device(std::uint8_t address, i2c_master_dev_handle_t *out) {
  i2c_device_config_t config{};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = 400000;
  return i2c_master_bus_add_device(state.i2c, &config, out);
}

esp_err_t write_reg(i2c_master_dev_handle_t device, std::uint8_t reg,
                    std::uint8_t value) {
  const std::uint8_t bytes[] = {reg, value};
  return i2c_master_transmit(device, bytes, sizeof(bytes), 100);
}

esp_err_t read_reg(i2c_master_dev_handle_t device, std::uint8_t reg,
                   std::uint8_t *value) {
  return i2c_master_transmit_receive(device, &reg, 1, value, 1, 100);
}

esp_err_t initialize_i2c() {
  i2c_master_bus_config_t config{};
  config.i2c_port = I2C_NUM_0;
  config.sda_io_num = kI2cSda;
  config.scl_io_num = kI2cScl;
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&config, &state.i2c);
}

esp_err_t initialize_pmu() {
  ESP_RETURN_ON_ERROR(add_i2c_device(kAxp2101Address, &state.pmu), kTag,
                      "add AXP2101");

  // Preserve unrelated rails. The known-working board implementation needs
  // DC1 plus ALDO1/2, so own only those outputs instead of blanking the PMU.
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x82, 0x12), kTag, "DC1 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x92, 0x1C), kTag, "ALDO1 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x93, 0x1C), kTag, "ALDO2 3.3 V");

  std::uint8_t dcdc = 0;
  std::uint8_t aldo = 0;
  ESP_RETURN_ON_ERROR(read_reg(state.pmu, 0x80, &dcdc), kTag,
                      "read DC enables");
  ESP_RETURN_ON_ERROR(read_reg(state.pmu, 0x90, &aldo), kTag,
                      "read LDO enables");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x80, dcdc | 0x01), kTag,
                      "enable DC1");
  ESP_RETURN_ON_ERROR(write_reg(state.pmu, 0x90, aldo | 0x03), kTag,
                      "enable ALDO1/2");

  ESP_LOGI(kTag, "AXP2101: DC enable 0x%02x, LDO enable 0x%02x", dcdc | 0x01,
           aldo | 0x03);
  return ESP_OK;
}

std::uint8_t from_bcd(std::uint8_t value) {
  return static_cast<std::uint8_t>((value >> 4) * 10 + (value & 0x0F));
}

esp_err_t read_rtc(char *text, std::size_t text_size) {
  constexpr std::uint8_t kSecondsRegister = 0x04;
  std::uint8_t raw[7]{};
  esp_err_t err = i2c_master_transmit_receive(state.rtc, &kSecondsRegister, 1,
                                              raw, sizeof(raw), 100);
  if (err != ESP_OK) {
    std::snprintf(text, text_size, "RTC I2C ERROR");
    return err;
  }

  const unsigned second = from_bcd(raw[0] & 0x7F);
  const unsigned minute = from_bcd(raw[1] & 0x7F);
  const unsigned hour = from_bcd(raw[2] & 0x3F);
  if ((raw[0] & 0x80) != 0 || second > 59 || minute > 59 || hour > 23) {
    std::snprintf(text, text_size, "RTC NOT SET");
    return ESP_ERR_INVALID_RESPONSE;
  }

  std::snprintf(text, text_size, "RTC %02u:%02u:%02u", hour, minute, second);
  return ESP_OK;
}

void round_flush_area(lv_area_t *area) {
  area->x1 &= ~1;
  area->x2 |= 1;
}

void refresh_rtc(lv_timer_t *) {
  char text[24]{};
  read_rtc(text, sizeof(text));
  lv_label_set_text(state.rtc_label, text);
}

void touch_clicked(lv_event_t *) {
  ++state.touches;
  lv_label_set_text_fmt(state.touch_label, "TOUCH OK  %" PRIu32, state.touches);
  ESP_LOGI(kTag, "physical touch %" PRIu32, state.touches);
}

esp_err_t initialize_display() {
  spi_bus_config_t bus{};
  bus.sclk_io_num = kLcdClock;
  bus.data0_io_num = kLcdData0;
  bus.data1_io_num = kLcdData1;
  bus.data2_io_num = kLcdData2;
  bus.data3_io_num = kLcdData3;
  bus.max_transfer_sz = kWidth * 20 * sizeof(std::uint16_t);
  bus.flags = SPICOMMON_BUSFLAG_QUAD;
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO),
                      kTag, "initialize QSPI");

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.cs_gpio_num = kLcdCs;
  io_config.dc_gpio_num = -1;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40 * 1000 * 1000;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  esp_lcd_panel_io_handle_t panel_io = nullptr;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(SPI2_HOST, &io_config, &panel_io), kTag,
      "create panel IO");

  co5300_vendor_config_t vendor{};
  vendor.init_cmds = kPanelInit;
  vendor.init_cmds_size = sizeof(kPanelInit) / sizeof(kPanelInit[0]);
  vendor.flags.use_qspi_interface = true;

  esp_lcd_panel_dev_config_t panel_config{};
  panel_config.reset_gpio_num = kLcdReset;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_config.bits_per_pixel = 16;
  panel_config.vendor_config = &vendor;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_co5300(panel_io, &panel_config, &state.panel), kTag,
      "create CO5300 panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(state.panel), kTag, "reset CO5300");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(state.panel), kTag,
                      "initialize CO5300");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(state.panel, kPanelGapX, 0), kTag,
                      "set panel gap");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_co5300_set_brightness(state.panel, 0), kTag,
                      "hold AMOLED dark");

  lvgl_port_cfg_t port = ESP_LVGL_PORT_INIT_CONFIG();
  port.task_priority = 1;
  port.task_affinity = 1;
  ESP_RETURN_ON_ERROR(lvgl_port_init(&port), kTag, "initialize LVGL");

  const attadipa::platform::BoardProfile *profile =
      attadipa::platform::find_board_profile(kBoardProfileId);
  ESP_RETURN_ON_FALSE(profile != nullptr, ESP_ERR_NOT_FOUND, kTag,
                      "board profile missing");

  lvgl_port_display_cfg_t display{};
  display.io_handle = panel_io;
  display.panel_handle = state.panel;
  display.buffer_size = kWidth * 20;
  display.hres = kWidth;
  display.vres = kHeight;
  display.color_format = LV_COLOR_FORMAT_RGB565;
  display.rounder_cb = round_flush_area;
  display.flags.buff_dma = true;
  display.flags.swap_bytes = profile->display.rgb565_swap_bytes;
  state.display = lvgl_port_add_disp(&display);
  ESP_RETURN_ON_FALSE(state.display != nullptr, ESP_ERR_NO_MEM, kTag,
                      "add LVGL display");

  ESP_LOGI(kTag, "CO5300: QSPI 40 MHz, RGB565 swap %s, brightness pending",
           profile->display.rgb565_swap_bytes ? "yes" : "no");
  return ESP_OK;
}

esp_err_t initialize_touch() {
  esp_lcd_panel_io_i2c_config_t io_config{};
  io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS;
  io_config.control_phase_bytes = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.scl_speed_hz = 400000;
  io_config.flags.disable_control_phase = true;

  esp_lcd_panel_io_handle_t touch_io = nullptr;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_i2c(state.i2c, &io_config, &touch_io), kTag,
      "create touch IO");

  esp_lcd_touch_config_t touch_config{};
  touch_config.x_max = kWidth - 1;
  touch_config.y_max = kHeight - 1;
  touch_config.rst_gpio_num = kTouchReset;
  touch_config.int_gpio_num = kTouchInterrupt;
  touch_config.levels.reset = 0;
  touch_config.levels.interrupt = 0;

  esp_lcd_touch_handle_t touch = nullptr;
  ESP_RETURN_ON_ERROR(
      esp_lcd_touch_new_i2c_ft5x06(touch_io, &touch_config, &touch), kTag,
      "initialize FT3168 via FT5x06 driver");

  lvgl_port_touch_cfg_t lv_touch{};
  lv_touch.disp = state.display;
  lv_touch.handle = touch;
  ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&lv_touch) != nullptr, ESP_ERR_NO_MEM,
                      kTag, "add LVGL touch");
  ESP_LOGI(kTag, "FT3168: I2C 0x38, reset GPIO 9, interrupt GPIO 38");
  return ESP_OK;
}

void create_ui() {
  lv_obj_t *screen = lv_screen_active();
  lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_text_color(screen, lv_color_white(), LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, "ATTADIPA / T-166");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 42);

  state.rtc_label = lv_label_create(screen);
  refresh_rtc(nullptr);
  lv_obj_align(state.rtc_label, LV_ALIGN_TOP_MID, 0, 82);

  constexpr std::uint32_t colors[] = {0xF02020, 0x20D060, 0x2070F0};
  constexpr int offsets[] = {-80, 0, 80};
  for (unsigned i = 0; i < 3; ++i) {
    lv_obj_t *swatch = lv_obj_create(screen);
    lv_obj_set_size(swatch, 60, 18);
    lv_obj_set_style_bg_color(swatch, lv_color_hex(colors[i]), LV_PART_MAIN);
    lv_obj_set_style_border_width(swatch, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(swatch, 3, LV_PART_MAIN);
    lv_obj_align(swatch, LV_ALIGN_TOP_MID, offsets[i], 125);
  }

  lv_obj_t *button = lv_button_create(screen);
  lv_obj_set_size(button, 300, 96);
  lv_obj_align(button, LV_ALIGN_CENTER, 0, 35);
  lv_obj_set_style_bg_color(button, lv_color_hex(0x20242A), LV_PART_MAIN);
  lv_obj_add_event_cb(button, touch_clicked, LV_EVENT_CLICKED, nullptr);

  state.touch_label = lv_label_create(button);
  lv_label_set_text(state.touch_label, "TOUCH ME");
  lv_obj_center(state.touch_label);

  lv_obj_t *safety = lv_label_create(screen);
  lv_label_set_text(safety, "AMOLED 1% / RGB TEST");
  lv_obj_set_style_text_color(safety, lv_color_hex(0x808080), LV_PART_MAIN);
  lv_obj_align(safety, LV_ALIGN_BOTTOM_MID, 0, -38);

  lv_timer_create(refresh_rtc, 1000, nullptr);
}

} // namespace

esp_err_t start_waveshare_ui() {
  ESP_RETURN_ON_ERROR(initialize_i2c(), kTag, "initialize I2C");
  ESP_RETURN_ON_ERROR(initialize_pmu(), kTag, "initialize AXP2101");
  ESP_RETURN_ON_ERROR(add_i2c_device(kPcf85063Address, &state.rtc), kTag,
                      "add PCF85063");
  char rtc_text[24]{};
  const esp_err_t rtc_result = read_rtc(rtc_text, sizeof(rtc_text));
  if (rtc_result == ESP_OK) {
    ESP_LOGI(kTag, "PCF85063: %s", rtc_text);
  } else {
    ESP_LOGW(kTag, "PCF85063: %s (%s)", rtc_text, esp_err_to_name(rtc_result));
  }
  ESP_RETURN_ON_ERROR(initialize_display(), kTag, "initialize display");
  ESP_RETURN_ON_ERROR(initialize_touch(), kTag, "initialize touch");

  ESP_RETURN_ON_FALSE(lvgl_port_lock(1000), ESP_ERR_TIMEOUT, kTag, "lock LVGL");
  create_ui();
  lvgl_port_unlock();

  ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(state.panel, true), kTag,
                      "turn display on");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_co5300_set_brightness(state.panel, kBrightnessPercent),
      kTag, "set safe brightness");
  ESP_LOGI(kTag, "UI ready: AMOLED brightness %d%%", kBrightnessPercent);
  return ESP_OK;
}
