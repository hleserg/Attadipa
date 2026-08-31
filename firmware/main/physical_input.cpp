#include "physical_input.h"

#include "power_button_edges.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_co5300.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "attadipa/core/input.h"
#include "attadipa/core/power_state.h"
#include "attadipa/platform/board_profile.h"

namespace {

constexpr char kTag[] = "physical-input";
constexpr char kBoardProfileId[] = "waveshare-amoled-206";
constexpr std::size_t kMaxInputPerRead = 16;
constexpr std::uint32_t kPollMs = 5;
constexpr std::uint8_t kButtonDebounceSamples = 2;
constexpr std::uint32_t kPmuPollMs = 20;
constexpr gpio_num_t kTouchInterrupt = GPIO_NUM_38;
constexpr std::uint64_t kPmuSleepPollUs = 100'000;
constexpr std::uint64_t kDebugWakeDelayUs = 750'000;
constexpr std::uint8_t kAxpInterruptEnable2 = 0x41;
constexpr std::uint8_t kAxpInterruptStatus2 = 0x49;

class PhysicalInput {
public:
  PhysicalInput(const attadipa::platform::BoardProfile &board,
                esp_lcd_touch_handle_t touch, i2c_master_dev_handle_t pmu,
                esp_lcd_panel_handle_t panel, std::uint8_t awake_brightness,
                void (*refresh_ui)())
      : board_(board), touch_(touch), pmu_(pmu), panel_(panel),
        awake_brightness_(awake_brightness), refresh_ui_(refresh_ui) {}

  attadipa::core::InputQueue &queue() { return input_queue_; }
  std::uint32_t sleep_cycles() const { return sleep_cycles_; }

  attadipa::core::InputState &state() { return input_state_; }

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
    result = write_pmu(kAxpInterruptStatus2,
                       attadipa::firmware::kAxpPowerEdges);
    if (result != ESP_OK) {
      return result;
    }
    result = write_pmu(kAxpInterruptEnable2,
                       enabled | attadipa::firmware::kAxpPowerEdges);
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

    // The sleep decision used to ride on the transport's own poll timer, which
    // is why turning the transport off used to turn sleep off with it. It has
    // its own timer now, at the same period, so the two are independent.
    lv_timer_t *timer = lv_timer_create(sleep_timer, kPollMs, this);
    if (timer == nullptr) {
      lv_indev_delete(indev);
      return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
  }

private:
  static void sleep_timer(lv_timer_t *timer) {
    static_cast<PhysicalInput *>(lv_timer_get_user_data(timer))->maybe_sleep();
  }

  static void read_pointer(lv_indev_t *indev, lv_indev_data_t *data) {
    static_cast<PhysicalInput *>(lv_indev_get_user_data(indev))->read(data);
  }

  esp_err_t read_pmu(std::uint8_t reg, std::uint8_t &value) const {
    return i2c_master_transmit_receive(pmu_, &reg, 1, &value, 1, 100);
  }

  esp_err_t write_pmu(std::uint8_t reg, std::uint8_t value) const {
    const std::uint8_t bytes[] = {reg, value};
    return i2c_master_transmit(pmu_, bytes, sizeof(bytes), 100);
  }

  bool consume_sleep_power_edge() const {
    std::uint8_t status = 0;
    const esp_err_t read_result = read_pmu(kAxpInterruptStatus2, status);
    if (read_result != ESP_OK) {
      ESP_LOGW(kTag, "read PMU while asleep: %s", esp_err_to_name(read_result));
      return false;
    }
    const std::uint8_t edges =
        status & attadipa::firmware::kAxpPowerEdges;
    if (edges == 0) {
      return false;
    }
    const esp_err_t clear_result = write_pmu(kAxpInterruptStatus2, edges);
    if (clear_result != ESP_OK) {
      ESP_LOGW(kTag, "clear PMU power edge: %s", esp_err_to_name(clear_result));
      return false;
    }
    return true;
  }

  void maybe_sleep() {
    if (!sleep_requested_ || !input_queue_.empty() ||
        input_state_.held_count(attadipa::core::InputOrigin::Physical) != 0 ||
        input_state_.held_count(attadipa::core::InputOrigin::Remote) != 0 ||
        pointer_pressed_ || gpio_get_level(kTouchInterrupt) == 0) {
      return;
    }

    sleep_requested_ = false;
    const std::uint16_t wake_plan =
        attadipa::core::wake_bit(attadipa::core::WakeSource::Button) |
        attadipa::core::wake_bit(attadipa::core::WakeSource::Touch) |
        attadipa::core::wake_bit(attadipa::core::WakeSource::Timer);
    if (!attadipa::core::wake_plan_is_legal(
            attadipa::core::PowerState::LightSleep, wake_plan)) {
      ESP_LOGE(kTag, "refused illegal LightSleep wake plan 0x%04x", wake_plan);
      return;
    }

    esp_err_t result = gpio_wakeup_enable(kTouchInterrupt, GPIO_INTR_LOW_LEVEL);
    if (result == ESP_OK) {
      result = esp_sleep_enable_gpio_wakeup();
    }
    if (result == ESP_OK) {
      result = esp_sleep_enable_timer_wakeup(
          debug_timer_wake_ ? kDebugWakeDelayUs : kPmuSleepPollUs);
    }
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "arm LightSleep wake sources: %s",
               esp_err_to_name(result));
      return;
    }

    result = esp_lcd_panel_co5300_set_brightness(panel_, 0);
    if (result == ESP_OK) {
      result = esp_lcd_panel_disp_on_off(panel_, false);
    }
    if (result != ESP_OK) {
      (void)esp_lcd_panel_co5300_set_brightness(panel_, awake_brightness_);
      ESP_LOGE(kTag, "turn AMOLED off before LightSleep: %s",
               esp_err_to_name(result));
      return;
    }

    ESP_LOGI(kTag,
             "display off; Active -> Idle -> LightSleep "
             "(touch + %s)",
             debug_timer_wake_ ? "debug timer" : "PMU polling");
    esp_err_t sleep_result = ESP_OK;
    esp_sleep_wakeup_cause_t cause = ESP_SLEEP_WAKEUP_UNDEFINED;
    bool by_button = false;
    bool by_touch = false;
    bool by_timer = false;
    for (;;) {
      sleep_result = esp_light_sleep_start();
      if (sleep_result != ESP_OK) {
        break;
      }
      cause = esp_sleep_get_wakeup_cause();
      by_touch = cause == ESP_SLEEP_WAKEUP_GPIO &&
                 gpio_get_level(kTouchInterrupt) == 0;
      if (by_touch) {
        (void)consume_sleep_power_edge();
        break;
      }
      if (cause != ESP_SLEEP_WAKEUP_TIMER) {
        break;
      }
      if (debug_timer_wake_) {
        by_timer = true;
        break;
      }
      if (consume_sleep_power_edge()) {
        by_button = true;
        break;
      }
      sleep_result = esp_sleep_enable_timer_wakeup(kPmuSleepPollUs);
      if (sleep_result != ESP_OK) {
        break;
      }
    }
    (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    debug_timer_wake_ = false;

    const std::uint64_t pins = by_touch ? 1ULL << kTouchInterrupt : 0;

    esp_err_t restore_result = esp_lcd_panel_disp_on_off(panel_, true);
    if (restore_result == ESP_OK) {
      restore_result =
          esp_lcd_panel_co5300_set_brightness(panel_, awake_brightness_);
    }
    if (refresh_ui_ != nullptr) {
      refresh_ui_();
    }
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);

    if (sleep_result != ESP_OK) {
      ESP_LOGE(kTag, "LightSleep failed: %s", esp_err_to_name(sleep_result));
      if (restore_result != ESP_OK) {
        ESP_LOGE(kTag, "restore AMOLED after failed LightSleep: %s",
                 esp_err_to_name(restore_result));
      }
      return;
    }

    const std::uint32_t now_ms =
        static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
    ++sleep_cycles_;
    const attadipa::core::WakeSource wake_source =
        by_button  ? attadipa::core::WakeSource::Button
        : by_touch ? attadipa::core::WakeSource::Touch
                   : attadipa::core::WakeSource::Timer;
    const bool wake_known = by_button || by_touch || by_timer;
    const attadipa::core::WakeRecord wake{
        attadipa::core::PowerState::LightSleep, wake_source,
        attadipa::core::MonotonicTime{now_ms}};
    ESP_LOGI(kTag,
             "wake cycle %u: LightSleep -> Idle -> Active by %s "
             "(cause=%d gpio=0x%llx)",
             static_cast<unsigned>(sleep_cycles_),
             wake_known ? attadipa::core::to_string(wake.by) : "UNKNOWN",
             static_cast<int>(cause), static_cast<unsigned long long>(pins));
    if (restore_result != ESP_OK) {
      ESP_LOGE(kTag, "restore AMOLED after LightSleep: %s",
               esp_err_to_name(restore_result));
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
    esp_err_t clear_result = ESP_OK;
    (void)attadipa::firmware::deliver_power_edges(
        status,
        attadipa::core::InputQueue::kCapacity - input_queue_.size(),
        [this, &clear_result](std::uint8_t edges) {
          clear_result = write_pmu(kAxpInterruptStatus2, edges);
          return clear_result == ESP_OK;
        },
        [this](bool pressed) {
          (void)queue_physical_button(0, pressed);
        },
        [&clear_result]() {
          ESP_LOGW(kTag, "clear awake PMU power edge: %s",
                   esp_err_to_name(clear_result));
        });
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
        if (event.button == 0 &&
            event.type == attadipa::core::InputEventType::ButtonUp) {
          sleep_requested_ = true;
          debug_timer_wake_ =
              event.origin == attadipa::core::InputOrigin::Remote;
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
  esp_lcd_panel_handle_t panel_ = nullptr;
  std::uint8_t awake_brightness_ = 0;
  void (*refresh_ui_)() = nullptr;
  attadipa::core::InputQueue input_queue_{};
  attadipa::core::InputState input_state_{};
  bool physical_pressed_ = false;
  std::int16_t physical_x_ = 0;
  std::int16_t physical_y_ = 0;
  bool pointer_pressed_ = false;
  std::int16_t pointer_x_ = 0;
  std::int16_t pointer_y_ = 0;
  std::size_t input_read_burst_ = 0;
  std::uint32_t last_pmu_poll_ms_ = 0;
  std::uint32_t sleep_cycles_ = 0;
  bool sleep_requested_ = false;
  bool debug_timer_wake_ = false;
  PhysicalButton physical_buttons_[1] = {{GPIO_NUM_0, false, 1}};
};

PhysicalInput *service = nullptr;

} // namespace

esp_err_t start_physical_input(esp_lcd_touch_handle_t touch,
                               i2c_master_dev_handle_t pmu,
                               esp_lcd_panel_handle_t panel,
                               std::uint8_t awake_brightness,
                               void (*refresh_ui)()) {
  if (service != nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  const attadipa::platform::BoardProfile *board =
      attadipa::platform::find_board_profile(kBoardProfileId);
  if (board == nullptr || touch == nullptr || pmu == nullptr ||
      panel == nullptr || awake_brightness > 100 || refresh_ui == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  auto *candidate = new (std::nothrow)
      PhysicalInput(*board, touch, pmu, panel, awake_brightness, refresh_ui);
  if (candidate == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  const esp_err_t result = candidate->attach();
  if (result != ESP_OK) {
    delete candidate;
    return result;
  }
  service = candidate;
  ESP_LOGI(kTag, "physical input ready: touch, GPIO0 and the AXP2101 power key");
  return ESP_OK;
}

attadipa::core::InputQueue *physical_input_queue() {
  return service == nullptr ? nullptr : &service->queue();
}

attadipa::core::InputState *physical_input_state() {
  return service == nullptr ? nullptr : &service->state();
}

std::uint32_t physical_input_sleep_cycles() {
  return service == nullptr ? 0 : service->sleep_cycles();
}
