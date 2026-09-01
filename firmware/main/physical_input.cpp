#include "physical_input.h"

#include "board_power.h"
#include "power_button_edges.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "attadipa/core/input.h"
#include "attadipa/core/power_owner.h"
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
constexpr std::uint8_t kAxpInterruptEnable2 = 0x41;
constexpr std::uint8_t kAxpInterruptStatus2 = 0x49;

class PhysicalInput {
public:
  PhysicalInput(const attadipa::platform::BoardProfile &board,
                esp_lcd_touch_handle_t touch, i2c_master_dev_handle_t pmu,
                void (*refresh_ui)())
      : board_(board), touch_(touch), pmu_(pmu), refresh_ui_(refresh_ui) {}

  attadipa::core::InputQueue &queue() { return input_queue_; }
  std::uint32_t sleep_cycles() const {
    return attadipa::firmware::board_power_owner().cycles();
  }

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

  // A name for a wake-source bitmask, for the one log line that explains a
  // wake. "0x0005" is not an explanation, and a wake nobody can explain is a
  // battery complaint nobody can debug.
  // One body for both bit spaces. `PowerDomain` and `WakeSource` are separate
  // words in `SleepReport` precisely because their bits overlap, and printing
  // one of them as hex while the other is named was the asymmetry a reader had
  // to decode. `to_string` is overloaded on the enum, so the only difference is
  // which count bounds the loop.
  template <typename Enum, std::uint8_t Count>
  static void describe_bits(char *out, std::size_t size, std::uint16_t mask) {
    std::size_t used = 0;
    out[0] = '\0';
    for (std::uint8_t i = 0; i < Count; ++i) {
      if ((mask & (1U << i)) == 0) {
        continue;
      }
      const char *name = attadipa::core::to_string(static_cast<Enum>(i));
      const int written = std::snprintf(out + used, size - used, "%s%s",
                                        used == 0 ? "" : "+", name);
      if (written <= 0 || static_cast<std::size_t>(written) >= size - used) {
        return;
      }
      used += static_cast<std::size_t>(written);
    }
    if (used == 0) {
      (void)std::snprintf(out, size, "UNKNOWN");
    }
  }

  static void describe_wake(char *out, std::size_t size, std::uint16_t mask) {
    describe_bits<attadipa::core::WakeSource,
                  attadipa::core::kWakeSourceCount>(out, size, mask);
  }

  static void describe_domains(char *out, std::size_t size, std::uint16_t mask) {
    describe_bits<attadipa::core::PowerDomain,
                  attadipa::core::kPowerDomainCount>(out, size, mask);
  }

  void maybe_sleep() {
    if (!sleep_requested_ || !input_queue_.empty() ||
        input_state_.held_count(attadipa::core::InputOrigin::Physical) != 0 ||
        input_state_.held_count(attadipa::core::InputOrigin::Remote) != 0 ||
        pointer_pressed_ || gpio_get_level(kTouchInterrupt) == 0) {
      return;
    }
    sleep_requested_ = false;

    // The decision stays here -- whether the watch is quiet enough to sleep is
    // a question about input, and this is where input lives. The *episode* is
    // the power owner's: arming, suspending, sleeping, classifying and the
    // unwind all happen in board_power.cpp, which is the only translation unit
    // allowed to touch any of it (ADR-0016 §1).
    attadipa::core::SleepPlan plan;
    plan.state = attadipa::core::PowerState::LightSleep;
    // Two sources, because two is what this board can arm. The power button is
    // not a third: the AXP2101 has no wake line to this SoC, so a press is
    // found by reading its latched edge during a timer wake and comes back as
    // a derived cause. Naming it here would be a wake plan the hardware never
    // agreed to.
    plan.wake_sources =
        attadipa::core::wake_bit(attadipa::core::WakeSource::Timer) |
        attadipa::core::wake_bit(attadipa::core::WakeSource::Touch);
    plan.suspend = attadipa::core::domain_bit(attadipa::core::PowerDomain::Display);

    const attadipa::core::MonotonicTime now{
        static_cast<std::uint64_t>(esp_timer_get_time() / 1000)};
    const attadipa::core::SleepReport report =
        attadipa::firmware::board_power_owner().sleep(plan, now);

    // The screen is back, or the owner never took it away. Either way LVGL has
    // to be told, and on the refused paths it costs one redraw of a screen that
    // was never dark.
    if (refresh_ui_ != nullptr) {
      refresh_ui_();
    }
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(nullptr);

    if (report.overdue_leases != 0) {
      // Reported, never reclaimed. A consumer holding a domain past its
      // deadline is a bug in that consumer, and taking it away would turn it
      // into a bug nobody can find.
      char overdue[96];
      describe_domains(overdue, sizeof(overdue), report.overdue_leases);
      ESP_LOGW(kTag, "power lease past its deadline on domains %s", overdue);
    }
    if (report.unexpected_causes != 0 || report.unmapped_causes != 0) {
      char named[96];
      describe_wake(named, sizeof(named), report.unexpected_causes);
      // The raw SoC word is printed unconditionally: for an unmapped cause it
      // is the only evidence there is, and for a named one it is what the
      // single-cause log this replaced used to carry.
      ESP_LOGE(kTag, "woke on a source nobody armed: %s (unmapped 0x%08x)",
               named, static_cast<unsigned>(report.unmapped_causes));
    }
    if (!report.hardware_known) {
      // What is un-done, printed here and not only on the refused path below,
      // because an unwind that fails after the SoC actually slept leaves
      // `outcome == Woken`: `!report.slept()` is false and this is the only
      // branch that runs. A domain still suspended and a wake source the SoC
      // still holds need different repairs, so they are named separately.
      char stuck[96];
      char stuck_domains[96];
      describe_wake(stuck, sizeof(stuck), report.blocked_sources);
      describe_domains(stuck_domains, sizeof(stuck_domains), report.blocked_by);
      ESP_LOGE(kTag,
               "power hardware state is unknown after a failed unwind; "
               "availability is Failed until re-initialisation "
               "(domains %s, sources %s)",
               report.blocked_by == 0 ? "none" : stuck_domains,
               report.blocked_sources == 0 ? "none" : stuck);
    }

    if (!report.slept()) {
      char refused[96];
      char refused_domains[96];
      describe_wake(refused, sizeof(refused), report.blocked_sources);
      describe_domains(refused_domains, sizeof(refused_domains), report.blocked_by);
      ESP_LOGE(kTag, "LightSleep %s (domains %s, sources %s)",
               attadipa::core::to_string(report.outcome),
               report.blocked_by == 0 ? "none" : refused_domains,
               report.blocked_sources == 0 ? "none" : refused);
      return;
    }

    char named[96];
    describe_wake(named, sizeof(named), report.wake_causes);
    ESP_LOGI(kTag, "wake cycle %u: LightSleep -> Idle -> Active by %s",
             static_cast<unsigned>(
                 attadipa::firmware::board_power_owner().cycles()),
             named);
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
          // A remote button-up has no finger behind it, so the next sleep wakes
          // on a timer instead of waiting for one -- and a local one does, so it
          // says so. Assigned rather than raised: setting it only for Remote
          // leaves a stale debug wake armed after a remote press is followed by
          // a real one.
          attadipa::firmware::board_power_set_debug_timer_wake(
              event.origin == attadipa::core::InputOrigin::Remote);
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
  bool sleep_requested_ = false;
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
  // Bind the power owner before anything can ask it to sleep. This is the
  // composition point: the touch interrupt pin is known here and nowhere else,
  // and board_power.cpp is the only file that may act on it.
  const esp_err_t power_result = attadipa::firmware::board_power_attach(
      pmu, panel, kTouchInterrupt, awake_brightness);
  if (power_result != ESP_OK) {
    return power_result;
  }
  auto *candidate = new (std::nothrow)
      PhysicalInput(*board, touch, pmu, refresh_ui);
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
