#include "watch_control.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "attadipa/core/input.h"
#include "attadipa/debug/bridge.h"
#include "attadipa/link/frame_codec.h"
#include "attadipa/platform/board_profile.h"

namespace {

constexpr char kTag[] = "watch-control";
constexpr char kBoardProfileId[] = "waveshare-amoled-206";
constexpr std::size_t kOutputCapacity = 16 * 1024;
constexpr std::size_t kMaxFramesPerPoll = 64;
constexpr std::size_t kMaxUsbWritesPerPoll = 8;
constexpr std::size_t kMaxInputPerRead = 16;
constexpr std::uint32_t kPollMs = 5;
constexpr std::uint8_t kButtonDebounceSamples = 2;
constexpr std::uint32_t kPmuPollMs = 20;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_38;
constexpr std::uint8_t kAxpInterruptEnable2 = 0x41;
constexpr std::uint8_t kAxpInterruptStatus2 = 0x49;
constexpr std::uint8_t kAxpPowerPositiveEdge = 1U << 0;
constexpr std::uint8_t kAxpPowerNegativeEdge = 1U << 1;
constexpr std::uint8_t kAxpPowerEdges =
    kAxpPowerPositiveEdge | kAxpPowerNegativeEdge;

class FirmwareScreenSource final : public attadipa::debug::ScreenSource {
public:
  FirmwareScreenSource(const attadipa::platform::BoardProfile &board,
                       const attadipa::core::InputQueue &queue)
      : board_(board), queue_(queue) {
    const std::uint8_t count =
        std::min(board.button_count, attadipa::platform::kMaxBoardButtons);
    for (std::uint8_t i = 0; i < count; ++i) {
      std::strncpy(buttons_[i].id, board.buttons[i].id,
                   sizeof(buttons_[i].id) - 1);
      if (board.buttons[i].injectable) {
        buttons_[i].flags |= attadipa::debug::kButtonInjectable;
      }
      if (!board.buttons[i].role_known) {
        buttons_[i].flags |= attadipa::debug::kButtonRoleUnknown;
      }
    }
    button_count_ = count;
    std::snprintf(build_, sizeof(build_), "device %.16s",
                  esp_app_get_description()->version);
  }

  bool capture(std::uint8_t *out, std::size_t capacity,
               std::uint16_t &width_out, std::uint16_t &height_out,
               attadipa::debug::PixelFormat &format_out,
               attadipa::debug::Orientation &orientation_out,
               std::size_t &bytes_out, Failure &why_out) override {
    width_out = board_.display.width_px;
    height_out = board_.display.height_px;
    format_out = attadipa::debug::PixelFormat::Rgb565Le;
    orientation_out = attadipa::debug::Orientation::Deg0;
    bytes_out = frame_bytes();
    why_out = Failure::None;

    if (out == nullptr || capacity == 0) {
      why_out = Failure::ShapeQuery;
      return false;
    }
    if (capacity < bytes_out) {
      why_out = Failure::BufferTooSmall;
      return false;
    }

    lv_draw_buf_t snapshot{};
    if (lv_draw_buf_init(&snapshot, width_out, height_out,
                         LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO, out,
                         capacity) != LV_RESULT_OK ||
        lv_snapshot_take_to_draw_buf(lv_screen_active(), LV_COLOR_FORMAT_RGB565,
                                     &snapshot) != LV_RESULT_OK) {
      bytes_out = 0;
      why_out = Failure::RendererFailed;
      return false;
    }
    if (snapshot.header.w != width_out || snapshot.header.h != height_out) {
      bytes_out = 0;
      why_out = Failure::GeometryMismatch;
      return false;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(width_out) * 2;
    if (snapshot.header.stride < row_bytes) {
      bytes_out = 0;
      why_out = Failure::GeometryMismatch;
      return false;
    }
    if (snapshot.header.stride != row_bytes) {
      for (std::uint16_t y = 1; y < height_out; ++y) {
        std::memmove(out + static_cast<std::size_t>(y) * row_bytes,
                     out + static_cast<std::size_t>(y) * snapshot.header.stride,
                     row_bytes);
      }
    }
    bytes_out = frame_bytes();
    return true;
  }

  const char *board_id() const override { return board_.id; }
  const char *build_id() const override { return build_; }
  std::uint8_t button_count() const override { return button_count_; }
  const attadipa::debug::ButtonDescriptor *buttons() const override {
    return buttons_;
  }
  bool stable_since(std::uint32_t ms) const override {
    return queue_.empty() && lv_anim_count_running() == 0 &&
           lv_display_get_inactive_time(nullptr) >= ms;
  }
  std::size_t frame_bytes() const {
    return static_cast<std::size_t>(board_.display.width_px) *
           board_.display.height_px * 2;
  }

private:
  attadipa::platform::BoardProfile board_;
  const attadipa::core::InputQueue &queue_;
  attadipa::debug::ButtonDescriptor buttons_[4]{};
  std::uint8_t button_count_ = 0;
  char build_[24]{};
};

class WatchControl {
public:
  WatchControl(const attadipa::platform::BoardProfile &board,
               esp_lcd_touch_handle_t touch, i2c_master_dev_handle_t pmu,
               std::uint8_t *frame, std::size_t frame_capacity)
      : board_(board), touch_(touch), pmu_(pmu), source_(board_, input_queue_),
        bridge_(input_queue_, input_state_, source_, frame, frame_capacity) {}

  esp_err_t attach() {
    gpio_config_t buttons{};
    buttons.pin_bit_mask = 1ULL << GPIO_NUM_0;
    buttons.mode = GPIO_MODE_INPUT;
    buttons.pull_up_en = GPIO_PULLUP_DISABLE;
    buttons.pull_down_en = GPIO_PULLDOWN_DISABLE;
    buttons.intr_type = GPIO_INTR_DISABLE;
    const esp_err_t gpio_result = gpio_config(&buttons);
    if (gpio_result != ESP_OK) {
      return gpio_result;
    }
    for (PhysicalButton &button : physical_buttons_) {
      button.stable = button.candidate = button_pressed(button);
      button.samples = kButtonDebounceSamples;
    }

    std::uint8_t enabled = 0;
    esp_err_t result = read_pmu(kAxpInterruptEnable2, enabled);
    if (result != ESP_OK) {
      return result;
    }
    result = write_pmu(kAxpInterruptStatus2, kAxpPowerEdges);
    if (result != ESP_OK) {
      return result;
    }
    result = write_pmu(kAxpInterruptEnable2, enabled | kAxpPowerEdges);
    if (result != ESP_OK) {
      return result;
    }

    lv_indev_t *indev = lv_indev_create();
    if (indev == nullptr) {
      return ESP_ERR_NO_MEM;
    }
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, lv_display_get_default());
    lv_indev_set_user_data(indev, this);
    lv_indev_set_read_cb(indev, read_pointer);

    lv_timer_t *timer = lv_timer_create(poll_timer, kPollMs, this);
    if (timer == nullptr) {
      lv_indev_delete(indev);
      return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
  }

private:
  static void emit(void *ctx, const std::uint8_t *payload, std::size_t length) {
    static_cast<WatchControl *>(ctx)->queue(payload, length);
  }

  static void poll_timer(lv_timer_t *timer) {
    static_cast<WatchControl *>(lv_timer_get_user_data(timer))->poll();
  }

  static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
    static_cast<WatchControl *>(lv_indev_get_user_data(indev))->read(data);
  }

  esp_err_t read_pmu(std::uint8_t reg, std::uint8_t &value) const {
    return i2c_master_transmit_receive(pmu_, &reg, 1, &value, 1, 100);
  }

  esp_err_t write_pmu(std::uint8_t reg, std::uint8_t value) const {
    const std::uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(pmu_, bytes, sizeof(bytes), 100);
  }

  std::size_t output_free() const { return output_.size() - output_count_; }

  void queue(const std::uint8_t *payload, std::size_t length) {
    std::uint8_t frame[attadipa::link::kMaxFrame]{};
    const std::size_t bytes =
        attadipa::link::encode(payload, length, frame, sizeof(frame));
    if (bytes == 0 || bytes > output_free()) {
      overflowed_ = true;
      return;
    }
    if (output_head_ + output_count_ + bytes > output_.size()) {
      std::memmove(output_.data(), output_.data() + output_head_,
                   output_count_);
      output_head_ = 0;
    }
    std::memcpy(output_.data() + output_head_ + output_count_, frame, bytes);
    output_count_ += bytes;
  }

  void flush() {
    // One driver write per complete frame. Console logs share this USB stream;
    // letting a write boundary cut through a frame lets a concurrent log land
    // inside its CRC span. Whole-frame writes keep logs between frames, where
    // the resynchronising decoder is designed to discard them.
    for (std::size_t i = 0; i < kMaxUsbWritesPerPoll &&
                            output_count_ >= attadipa::link::kHeaderBytes;
         ++i) {
      const std::uint8_t *frame = output_.data() + output_head_;
      const std::size_t frame_bytes =
          static_cast<std::size_t>(frame[2] | (frame[3] << 8)) +
          attadipa::link::kOverheadBytes;
      if (frame_bytes > output_count_) {
        overflowed_ = true;
        return;
      }
      const int written = usb_serial_jtag_write_bytes(frame, frame_bytes, 0);
      if (written == 0) {
        break;
      }
      if (written != static_cast<int>(frame_bytes)) {
        overflowed_ = true;
        return;
      }
      output_head_ += frame_bytes;
      output_count_ -= frame_bytes;
      if (output_count_ == 0) {
        output_head_ = 0;
      }
    }
  }

  void dispatch(std::uint32_t now_ms) {
    std::uint8_t payload[attadipa::link::kMaxPayload]{};
    while (output_free() >= attadipa::link::kMaxFrame) {
      const attadipa::link::FrameResult frame =
          decoder_.next(payload, sizeof(payload));
      if (frame.exhausted()) {
        return;
      }
      if (frame.status == attadipa::link::FrameStatus::OutputTooSmall) {
        overflowed_ = true;
        return;
      }
      bridge_.handle(payload, frame.length, now_ms, emit, this);
    }
  }

  void reset_remote(std::uint32_t now_ms, const char *reason) {
    decoder_.reset();
    output_head_ = 0;
    output_count_ = 0;
    overflowed_ = false;
    bridge_.on_disconnect(now_ms);
    ESP_LOGW(kTag, "%s; reset remote state", reason);
  }

  void poll() {
    const std::uint32_t now_ms =
        static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    const bool usb_connected = usb_serial_jtag_is_connected();
    if (!usb_connected) {
      if (usb_connected_) {
        reset_remote(now_ms, "USB host disconnected");
      }
      usb_connected_ = false;
      return;
    }
    usb_connected_ = true;
    flush();

    std::uint8_t bytes[1024]{};
    const int received = usb_serial_jtag_read_bytes(bytes, sizeof(bytes), 0);
    if (received > 0) {
      std::size_t at = 0;
      const std::size_t total = static_cast<std::size_t>(received);
      while (at < total) {
        dispatch(now_ms);
        const std::size_t accepted = decoder_.push(bytes + at, total - at);
        if (accepted == 0) {
          break;
        }
        at += accepted;
      }
      dispatch(now_ms);
    }

    for (std::size_t i = 0;
         i < kMaxFramesPerPoll && output_free() >= attadipa::link::kMaxFrame;
         ++i) {
      if (!bridge_.pump(emit, this)) {
        break;
      }
    }
    bridge_.tick(now_ms, emit, this);
    flush();

    if (overflowed_) {
      reset_remote(now_ms, "USB client overran the bounded output queue");
    }
  }

  void poll_physical_touch() {
    if (input_queue_.size() == attadipa::core::InputQueue::kCapacity) {
      return;
    }
    if (!physical_pressed_ && gpio_get_level(kTouchInterrupt) != 0) {
      return;
    }
    if (esp_lcd_touch_read_data(touch_) != ESP_OK) {
      return;
    }

    esp_lcd_touch_point_data_t point{};
    std::uint8_t count = 0;
    if (esp_lcd_touch_get_data(touch_, &point, &count, 1) != ESP_OK) {
      return;
    }
    const bool has_point = count > 0;

    attadipa::core::InputEvent event{};
    event.origin = attadipa::core::InputOrigin::Physical;
    event.touch_id = 0;
    event.at_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);

    if (has_point) {
      if (point.x >= board_.display.width_px ||
          point.y >= board_.display.height_px) {
        return;
      }
      event.x = static_cast<std::int16_t>(point.x);
      event.y = static_cast<std::int16_t>(point.y);
      if (!physical_pressed_) {
        event.type = attadipa::core::InputEventType::PointerDown;
      } else if (event.x != physical_x_ || event.y != physical_y_) {
        event.type = attadipa::core::InputEventType::PointerMove;
      } else {
        return;
      }
    } else if (physical_pressed_) {
      event.type = attadipa::core::InputEventType::PointerUp;
      event.x = physical_x_;
      event.y = physical_y_;
    } else {
      return;
    }

    if (!input_state_.apply(event, board_.button_count) ||
        !input_queue_.push(event)) {
      return;
    }
    physical_pressed_ = has_point;
    physical_x_ = event.x;
    physical_y_ = event.y;
  }

  struct PhysicalButton {
    gpio_num_t pin;
    bool active_high;
    std::uint8_t index;
    bool stable = false;
    bool candidate = false;
    std::uint8_t samples = 0;
  };

  static bool button_pressed(const PhysicalButton &button) {
    const bool high = gpio_get_level(button.pin) != 0;
    return button.active_high ? high : !high;
  }

  bool queue_physical_button(std::uint8_t index, bool pressed) {
    if (input_queue_.size() == attadipa::core::InputQueue::kCapacity) {
      return false;
    }
    attadipa::core::InputEvent event{};
    event.type = pressed ? attadipa::core::InputEventType::ButtonDown
                         : attadipa::core::InputEventType::ButtonUp;
    event.origin = attadipa::core::InputOrigin::Physical;
    event.button = index;
    event.at_ms = static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    return input_state_.apply(event, board_.button_count) &&
           input_queue_.push(event);
  }

  void poll_power_button(std::uint32_t now_ms) {
    if (now_ms - last_pmu_poll_ms_ < kPmuPollMs) {
      return;
    }
    last_pmu_poll_ms_ = now_ms;

    std::uint8_t status = 0;
    if (read_pmu(kAxpInterruptStatus2, status) != ESP_OK) {
      return;
    }
    const std::uint8_t edges = status & kAxpPowerEdges;
    const std::size_t needed =
        ((edges & kAxpPowerNegativeEdge) != 0 ? 1U : 0U) +
        ((edges & kAxpPowerPositiveEdge) != 0 ? 1U : 0U);
    if (needed == 0 ||
        input_queue_.size() + needed > attadipa::core::InputQueue::kCapacity) {
      return;
    }

    if ((edges & kAxpPowerNegativeEdge) != 0) {
      (void)queue_physical_button(0, true);
    }
    if ((edges & kAxpPowerPositiveEdge) != 0) {
      (void)queue_physical_button(0, false);
    }
    (void)write_pmu(kAxpInterruptStatus2, edges);
  }

  void poll_physical_buttons() {
    for (PhysicalButton &button : physical_buttons_) {
      const bool pressed = button_pressed(button);
      if (pressed != button.candidate) {
        button.candidate = pressed;
        button.samples = 1;
      } else if (button.samples < kButtonDebounceSamples) {
        ++button.samples;
      }
      if (button.samples < kButtonDebounceSamples || pressed == button.stable ||
          input_queue_.size() == attadipa::core::InputQueue::kCapacity) {
        continue;
      }

      if (queue_physical_button(button.index, pressed)) {
        button.stable = pressed;
      }
    }
  }

  void read(lv_indev_data_t *data) {
    if (input_read_burst_ == 0) {
      poll_physical_touch();
      poll_physical_buttons();
      poll_power_button(
          static_cast<std::uint32_t>(esp_timer_get_time() / 1000));
    }

    attadipa::core::InputEvent event{};
    while (input_read_burst_ < kMaxInputPerRead && input_queue_.pop(event)) {
      ++input_read_burst_;
      switch (event.type) {
      case attadipa::core::InputEventType::PointerDown:
        pointer_pressed_ = true;
        pointer_x_ = event.x;
        pointer_y_ = event.y;
        goto pointer_ready;
      case attadipa::core::InputEventType::PointerMove:
        pointer_x_ = event.x;
        pointer_y_ = event.y;
        goto pointer_ready;
      case attadipa::core::InputEventType::PointerUp:
        pointer_pressed_ = false;
        pointer_x_ = event.x;
        pointer_y_ = event.y;
        goto pointer_ready;
      case attadipa::core::InputEventType::ButtonDown:
      case attadipa::core::InputEventType::ButtonUp:
        if (event.origin == attadipa::core::InputOrigin::Physical &&
            event.button < board_.button_count) {
          ESP_LOGI(kTag, "physical %s %s", board_.buttons[event.button].id,
                   event.type == attadipa::core::InputEventType::ButtonDown
                       ? "down"
                       : "up");
        }
        break;
      }
    }

  pointer_ready:
    data->point.x = pointer_x_;
    data->point.y = pointer_y_;
    data->state =
        pointer_pressed_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (input_queue_.empty() || input_read_burst_ >= kMaxInputPerRead) {
      data->continue_reading = false;
      input_read_burst_ = 0;
    } else {
      data->continue_reading = true;
    }
  }

  const attadipa::platform::BoardProfile board_;
  esp_lcd_touch_handle_t touch_ = nullptr;
  i2c_master_dev_handle_t pmu_ = nullptr;
  attadipa::core::InputQueue input_queue_{};
  attadipa::core::InputState input_state_{};
  FirmwareScreenSource source_;
  attadipa::debug::Bridge bridge_;
  attadipa::link::Decoder decoder_{};
  std::array<std::uint8_t, kOutputCapacity> output_{};
  std::size_t output_head_ = 0;
  std::size_t output_count_ = 0;
  bool overflowed_ = false;
  bool usb_connected_ = false;
  bool physical_pressed_ = false;
  std::int16_t physical_x_ = 0;
  std::int16_t physical_y_ = 0;
  bool pointer_pressed_ = false;
  std::int16_t pointer_x_ = 0;
  std::int16_t pointer_y_ = 0;
  std::size_t input_read_burst_ = 0;
  std::uint32_t last_pmu_poll_ms_ = 0;
  PhysicalButton physical_buttons_[1] = {{GPIO_NUM_0, false, 1}};
};

WatchControl *service = nullptr;

} // namespace

esp_err_t start_watch_control(esp_lcd_touch_handle_t touch,
                              i2c_master_dev_handle_t pmu) {
  if (service != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  const attadipa::platform::BoardProfile *board =
      attadipa::platform::find_board_profile(kBoardProfileId);
  if (board == nullptr || touch == nullptr || pmu == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }

  const std::size_t frame_bytes =
      static_cast<std::size_t>(board->display.width_px) *
      board->display.height_px * 2;
  auto *frame = static_cast<std::uint8_t *>(
      heap_caps_malloc(frame_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (frame == nullptr) {
    return ESP_ERR_NO_MEM;
  }

  usb_serial_jtag_driver_config_t usb{};
  usb.rx_buffer_size = 4096;
  usb.tx_buffer_size = kOutputCapacity;
  esp_err_t result = usb_serial_jtag_driver_install(&usb);
  if (result != ESP_OK) {
    heap_caps_free(frame);
    return result;
  }
  WatchControl *candidate =
      new (std::nothrow) WatchControl(*board, touch, pmu, frame, frame_bytes);
  if (candidate == nullptr) {
    usb_serial_jtag_driver_uninstall();
    heap_caps_free(frame);
    return ESP_ERR_NO_MEM;
  }
  result = candidate->attach();
  if (result != ESP_OK) {
    delete candidate;
    usb_serial_jtag_driver_uninstall();
    heap_caps_free(frame);
    return result;
  }
  usb_serial_jtag_vfs_use_driver();
  service = candidate;
  ESP_LOGI(kTag, "USB watch-control ready; screenshot buffer %u bytes PSRAM",
           static_cast<unsigned>(frame_bytes));
  return ESP_OK;
}
