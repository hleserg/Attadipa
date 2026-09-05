#include "board_power.h"

#include "sdkconfig.h"

#include "power_button_edges.h"
#include "wake_classification.h"

#include <new>

#include "esp_check.h"
#include "esp_lcd_co5300.h"
#include "esp_log.h"
#include "esp_sleep.h"

namespace attadipa::firmware {
namespace {

constexpr char kTag[] = "board-power";

// The PMU has no wake line to this SoC, so a button press is found by waking on
// a timer and reading the AXP2101's latched edge. 100 ms is what the shipping
// firmware used and this change does not re-tune it.
constexpr std::uint64_t kPmuSleepPollUs = 100'000;
constexpr std::uint64_t kDebugWakeDelayUs = 750'000;
constexpr std::uint8_t kAxpInterruptStatus2 = 0x49;

// The rail map of POWER_OWNERSHIP.md §6.1, as data.
//
// It is data because the research says so in the row that matters: "The ⚠️ row
// is the single most important line in this document for anyone writing the
// executable change. It belongs in a board-side table where a wrong call fails
// a build or a test, not in prose."
//
// The distinction the table exists to keep is between the two reasons a rail is
// not switched today. `NotAuthorised` is a rail that could be gated once a
// measurement justifies it; `Never` is a rail that must not be gated whatever
// any measurement says. `set_rail()` refuses both, so the day gating is
// authorised is the day somebody relaxes that refusal — and without the table
// the cheapest way to relax it is to delete it, which takes ALDO2 with it.
enum class RailPolicy : std::uint8_t {
  Never,          // no measurement can authorise this one
  NotAuthorised,  // gateable in principle; nothing has measured the gain
};

struct Rail {
  std::uint8_t voltage_register;  // 0 where the rail has no voltage of its own
  const char *name;
  RailPolicy policy;
  const char *why;
};

#if CONFIG_ATTADIPA_BOARD_TWATCH_S3_PLUS
// T-Watch S3 Plus. Loads per HARDWARE_MATRIX.md "AXP2101 rail map"; the ALDO1
// row is the one the schematic and the vendor document disagree on (H8), and
// a rail whose load is disputed is never gated.
constexpr Rail kRails[] = {
    {0x82, "DC1", RailPolicy::Never, "the SoC supply this image is running from"},
    {0x92, "ALDO1", RailPolicy::Never,
     "CONFLICTING: the schematic puts net +3V3 — SoC I/O, BMA423, PCF8563, "
     "DRV2605 VDD — on it, the vendor document calls it unused (HARDWARE_MATRIX "
     "H8)"},
    {0x93, "ALDO2", RailPolicy::NotAuthorised,
     "backlight supply; GPIO 45 already gates the LED, so a second-order saving"},
    {0x94, "ALDO3", RailPolicy::NotAuthorised,
     "panel and touch controller; cycling it is also the only recovery for a "
     "wedged FT6336U (TWATCH_S3_PLUS_BSP_REUSE.md §8)"},
    {0x95, "ALDO4", RailPolicy::NotAuthorised,
     "radio; gateable when the radio holds no lease"},
    // The row the comment below used to predict. The GNSS images raise this
    // rail on purpose -- `board_power_enable_gnss_rail()` writes 0x96 = 0x1C
    // and sets REG 90 bit 4 -- so a domain owner asking which regulator the
    // receiver is on has an answer that is not "look in another function".
    // *The GNSS images*, not every image: that function is a no-op unless
    // `CONFIG_ATTADIPA_GNSS_BRIDGE` or `CONFIG_ATTADIPA_GNSS_LOCAL` is set,
    // and this table is built either way. Which rail feeds the receiver is a
    // fact about the board and holds in every image; whether anything raised
    // it is a fact about the build. A row that conflated the two would tell a
    // reader on a plain T-Watch that BLDO1 is up when it is wherever the PMU
    // left it.
    // `NotAuthorised`, unchanged and deliberately: naming a rail is not
    // licensing a gate, and nothing has measured what this one costs.
    {0x96, "BLDO1", RailPolicy::NotAuthorised,
     "GNSS at 3300 mV (HARDWARE_MATRIX.md \"AXP2101 rail map\"); raised in "
     "the GNSS images by board_power_enable_gnss_rail(), and nothing has "
     "measured it. An LS550G "
     "would need DC4 at 850 mV as well, and which module is fitted is per unit"},
    {0, "BLDO2/DLDO1", RailPolicy::NotAuthorised,
     "haptic enable, amplifier; nothing has measured them"},
};

// rail_for() indexes by position, and this is the table most likely to grow:
// BLDO1 became a row above when GNSS arrived, and DC4 and BLDO2 each become
// one when an LS550G or the haptic does. A row inserted above ALDO3 would
// silently move Display onto another regulator.
static_assert(kRails[3].voltage_register == 0x94 &&
                  kRails[4].voltage_register == 0x95 &&
                  kRails[5].voltage_register == 0x96,
              "Display is ALDO3 (0x94), Radio is ALDO4 (0x95) and Gnss is "
              "BLDO1 (0x96): a row was inserted above them, so re-index "
              "rail_for()");

const Rail *rail_for(attadipa::core::PowerDomain domain) {
  switch (domain) {
  case attadipa::core::PowerDomain::Display:
    return &kRails[3];
  case attadipa::core::PowerDomain::Radio:
    return &kRails[4];
  case attadipa::core::PowerDomain::Gnss:
    return &kRails[5];
  default:
    return nullptr;
  }
}
#else
constexpr Rail kRails[] = {
    {0x82, "DC1", RailPolicy::Never,
     "the main 3.3 V supply the board is brought up on"},
    {0x92, "ALDO1", RailPolicy::NotAuthorised,
     "analogue audio supply on net A3V3; gateable when audio holds no lease"},
    {0x93, "ALDO2", RailPolicy::Never,
     "not a supply: the R10 10 K pull-up that holds DSI_PWR_EN high. No GPIO "
     "drives that pin and the panel is fed from VCC3V3, so switching this off "
     "blanks the display by a route that reads as a wiring fault"},
    {0x94, "ALDO3", RailPolicy::NotAuthorised,
     "vibration motor, already gated at Q1 from GPIO18; a second-order saving"},
    {0x95, "ALDO4", RailPolicy::NotAuthorised, "1.8 V, feeds nothing"},
    {0, "BLDO1/BLDO2/CPUSLDO/DLDO1/DLDO2", RailPolicy::NotAuthorised,
     "on from the factory, feed nothing; what they cost is UNKNOWN"},
};

// ALDO2 is the reason this table is not prose. If the row is ever edited to
// something a measurement could unlock, the build stops here.
static_assert(kRails[2].voltage_register == 0x93 &&
                  kRails[2].policy == RailPolicy::Never,
              "ALDO2 is the DSI_PWR_EN pull-up, not a supply: it may never be "
              "gated (POWER_OWNERSHIP.md §6.1)");

// What a domain would reach for if rail gating were authorised.
//
// Only Display maps, and it maps to the trap rather than to a supply: a reader
// who asks this owner to cut display power is asking for ALDO2, and the answer
// they need is why that is the wrong rail. The other domains reach that same
// nullptr by three different routes, and they are worth keeping apart because
// only one of them means nothing is there.
//
// Radio and NodeLink live on the SoC. No GNSS is fitted —
// `docs/research/HARDWARE_MATRIX.md:407` — "| GNSS | — | **not present** |"
// — so a receiver on the expansion pads is one wired to a board whose rail
// map names no rail for it. The IMU is the case that is *not* absence: a
// QMI8658 sits on the main bus and has answered a scan —
// `docs/research/HARDWARE_MATRIX.md:392` — "**`0x6B`, MEASURED**" — and its
// `Power rail` column reads `D13`, which that table defines as a load known to
// be on a PMU rail whose rail is unresolved. So for `Imu` this nullptr says
// "which rail is UNKNOWN", not "nothing is there", and the two are not the
// same answer to give whoever comes to gate the accelerometer. An invented
// mapping would be a hardware claim with no source either way.
const Rail *rail_for(attadipa::core::PowerDomain domain) {
  return domain == attadipa::core::PowerDomain::Display ? &kRails[2] : nullptr;
}
#endif

esp_err_t write_reg(i2c_master_dev_handle_t device, std::uint8_t reg,
                    std::uint8_t value) {
  const std::uint8_t bytes[] = {reg, value};
  return i2c_master_transmit(device, bytes, sizeof(bytes), 100);
}

esp_err_t read_reg(i2c_master_dev_handle_t device, std::uint8_t reg,
                   std::uint8_t *value) {
  return i2c_master_transmit_receive(device, &reg, 1, value, 1, 100);
}

// The SoC's own bitmap, in this project's vocabulary.
//
// ESP-IDF returns `BIT(esp_sleep_wakeup_cause_t)`, so several bits can be set
// at once — which is the whole reason ADR-0016 §6 replaced
// `esp_sleep_get_wakeup_cause()`, whose own header says "This API will only
// return one wakeup source. If multiple wakeup sources wake up at the same
// time, the wakeup source information may be lost."
//
// GPIO maps to Touch because this owner arms the GPIO source for exactly one
// pin. That is a claim about this file, and it is only true while this file is
// the only one arming wake sources — which is what ADR-0016 §1 and its CI check
// are for.
//
// Three causes have a name here and the SoC can report a dozen. The rest are
// **not** dropped: they go back as `unmapped_from_soc`, because dropping them
// is what makes `SleepReport::unexpected_causes` read zero both when nothing is
// wrong and when the one thing this owner watches for has happened.
void soc_causes_to_wake_sources(std::uint32_t causes,
                                attadipa::core::WakeCauses &out) {
  constexpr std::uint32_t kNamed = (1U << ESP_SLEEP_WAKEUP_TIMER) |
                                   (1U << ESP_SLEEP_WAKEUP_GPIO) |
                                   (1U << ESP_SLEEP_WAKEUP_UART);
  std::uint16_t sources = 0;
  if ((causes & (1U << ESP_SLEEP_WAKEUP_TIMER)) != 0) {
    sources |= attadipa::core::wake_bit(attadipa::core::WakeSource::Timer);
  }
  if ((causes & (1U << ESP_SLEEP_WAKEUP_GPIO)) != 0) {
    sources |= attadipa::core::wake_bit(attadipa::core::WakeSource::Touch);
  }
  if ((causes & (1U << ESP_SLEEP_WAKEUP_UART)) != 0) {
    sources |= attadipa::core::wake_bit(attadipa::core::WakeSource::Usb);
  }
  out.from_soc = sources;
  out.unmapped_from_soc = causes & ~kNamed;
}

class WaveshareHardware final : public attadipa::core::PowerHardware {
public:
  esp_err_t attach(i2c_master_dev_handle_t pmu, esp_lcd_panel_handle_t panel,
                   gpio_num_t touch_interrupt, std::uint8_t awake_brightness) {
    if (pmu == nullptr || panel == nullptr) {
      return ESP_ERR_INVALID_ARG;
    }
    pmu_ = pmu;
    panel_ = panel;
    touch_interrupt_ = touch_interrupt;
    awake_brightness_ = awake_brightness;
    return ESP_OK;
  }

  void set_debug_timer_wake(bool on) { debug_timer_wake_ = on; }

  void detach() {
    pmu_ = nullptr;
    panel_ = nullptr;
    touch_interrupt_ = GPIO_NUM_NC;
    touch_armed_ = false;
    awake_brightness_ = 0;
    debug_timer_wake_ = false;
  }

  bool suspend(attadipa::core::PowerDomain domain) override {
    if (panel_ == nullptr) {
      return false;
    }
    if (domain != attadipa::core::PowerDomain::Display) {
      // No other consumer has a suspend path on this board yet, and saying yes
      // to one that does not exist is how a plan starts believing it quiesced
      // something.
      ESP_LOGE(kTag, "no suspend path for %s",
               attadipa::core::to_string(domain));
      return false;
    }
    esp_err_t result = esp_lcd_panel_co5300_set_brightness(panel_, 0);
    if (result == ESP_OK) {
      result = esp_lcd_panel_disp_on_off(panel_, false);
      if (result != ESP_OK) {
        // Half-done is not done. The brightness went to zero and the panel is
        // still on, so put the brightness back before reporting the failure:
        // the owner will not call resume() for a suspend that never succeeded.
        (void)esp_lcd_panel_co5300_set_brightness(panel_, awake_brightness_);
      }
    }
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "suspend display: %s", esp_err_to_name(result));
      return false;
    }
    return true;
  }

  bool resume(attadipa::core::PowerDomain domain) override {
    if (domain != attadipa::core::PowerDomain::Display || panel_ == nullptr) {
      return false;
    }
    esp_err_t result = esp_lcd_panel_disp_on_off(panel_, true);
    if (result == ESP_OK) {
      result = esp_lcd_panel_co5300_set_brightness(panel_, awake_brightness_);
    }
    if (result != ESP_OK) {
      // This is the path that publishes Failed. The panel is on or off and
      // nobody knows which, and a watch that reports Ready here is one showing
      // a stale screen while answering nothing.
      ESP_LOGE(kTag, "resume display: %s", esp_err_to_name(result));
      return false;
    }
    return true;
  }

  bool set_rail(attadipa::core::PowerDomain domain, bool on) override {
    // ADR-0016's Consequences: gating a rail to save power is not authorised by
    // the ADR. It is authorised by a measurement, applied through this owner,
    // and every measurement it would need is UNKNOWN or NOT EXECUTED. So every
    // request is refused — but the table says which refusal this is, because
    // "no measurement yet" and "never, whatever the measurement" are different
    // answers and only one of them expires.
    const Rail *rail = rail_for(domain);
    if (rail == nullptr) {
      ESP_LOGE(kTag, "refused to switch the %s rail %s: no rail on this board "
                     "feeds that domain",
               attadipa::core::to_string(domain), on ? "on" : "off");
      return false;
    }
    ESP_LOGE(kTag, "refused to switch %s %s for %s: %s (%s)", rail->name,
             on ? "on" : "off", attadipa::core::to_string(domain), rail->why,
             rail->policy == RailPolicy::Never
                 ? "never gateable"
                 : "no measurement authorises gating it");
    return false;
  }

  bool arm_wake(attadipa::core::WakeSource source) override {
    switch (source) {
    case attadipa::core::WakeSource::Timer: {
      const std::uint64_t us =
          debug_timer_wake_ ? kDebugWakeDelayUs : kPmuSleepPollUs;
      const esp_err_t result = esp_sleep_enable_timer_wakeup(us);
      if (result != ESP_OK) {
        ESP_LOGE(kTag, "arm timer wake: %s", esp_err_to_name(result));
        return false;
      }
      return true;
    }
    case attadipa::core::WakeSource::Touch: {
      if (touch_interrupt_ == GPIO_NUM_NC) {
        // Attached without a touch controller: the line is undriven and its
        // level UNKNOWN, so it is refused like Button below, not guessed.
        ESP_LOGE(kTag, "no touch line to arm on this boot");
        return false;
      }
      esp_err_t result = gpio_wakeup_enable(touch_interrupt_, GPIO_INTR_LOW_LEVEL);
      if (result == ESP_OK) {
        result = esp_sleep_enable_gpio_wakeup();
        if (result != ESP_OK) {
          (void)gpio_wakeup_disable(touch_interrupt_);
        }
      }
      if (result != ESP_OK) {
        ESP_LOGE(kTag, "arm touch wake: %s", esp_err_to_name(result));
        return false;
      }
      touch_armed_ = true;
      return true;
    }
    default:
      break;
    }
    // Everything else, and Button above all. The AXP2101 has no wake line to
    // this SoC: a press is found by reading register 0x49 after some *other*
    // source brought the CPU back, which is a poll and not an armed source. Returning true here would put
    // the software's wake plan and the hardware's out of agreement in the one
    // place nothing downstream can detect.
    ESP_LOGE(kTag, "this board cannot arm %s as a wake source",
             attadipa::core::to_string(source));
    return false;
  }

  bool disarm_wake(attadipa::core::WakeSource source) override {
    esp_err_t result = ESP_ERR_NOT_SUPPORTED;
    switch (source) {
    case attadipa::core::WakeSource::Timer:
      result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
      break;
    case attadipa::core::WakeSource::Touch:
      result = touch_interrupt_ == GPIO_NUM_NC
                   ? ESP_ERR_INVALID_STATE  // never armed; the branch below
                   : gpio_wakeup_disable(touch_interrupt_);
      if (result == ESP_OK) {
        result = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
      }
      touch_armed_ = false;
      break;
    default:
      break;
    }
    if (result == ESP_ERR_INVALID_STATE) {
      // Not armed, which is exactly what this call was asked to achieve, so
      // it is a success. ESP-IDF v5.5.5 reaches its `else` and returns
      // `ESP_ERR_INVALID_STATE` when the trigger bit is clear:
      // `esp_sleep_disable_wakeup_source` guards every branch with
      // `CHECK_SOURCE(source, value, mask)`, defined as
      // `((s_config.wakeup_triggers & mask) && (source == value))`, and a
      // chain of `else if` guards that all fail reaches the final `else`. The
      // trace is in docs/research/POWER_OWNERSHIP.md.
      //
      // **Nothing in this tree reaches it today**, and it is kept rather than
      // deleted as dead code, which is the trade worth stating. Every path
      // that could produce it closes itself: the owner disarms only a source
      // it recorded as armed, `arm_wake(Touch)` un-does its own first step
      // when its second fails, and `recover()` retries only a disarm that
      // failed -- which left the trigger bit set, so the retry gets `ESP_OK`.
      // What the branch is for is the arithmetic on the other side. Mapping
      // this code to a failure costs a board that is provably in the requested
      // state a latch into `Failed` and a reboot to leave it; mapping it to
      // success costs one log line if a future source can be half-armed. The
      // second is the cheaper way to be wrong.
      ESP_LOGW(kTag, "disarm %s: already disarmed",
               attadipa::core::to_string(source));
      return true;
    }
    if (result != ESP_OK) {
      ESP_LOGE(kTag, "disarm %s: %s", attadipa::core::to_string(source),
               esp_err_to_name(result));
      return false;
    }
    return true;
  }

  bool sleep(attadipa::core::PowerState state,
             attadipa::core::WakeCauses &causes) override {
    // Consumed here rather than when the timer was armed, so that a
    // transaction refused or rolled back before this point keeps it: the flag
    // says "the next sleep that actually happens wakes on a debug timer", and
    // a sleep that never happened has not spent it.
    const bool debug_wake = debug_timer_wake_;
    debug_timer_wake_ = false;

    // Names what was armed and nothing else: a touchless boot (#367 item 6)
    // never arms the line, and the one line that explains a sleep must not
    // claim a source the hardware never agreed to.
    ESP_LOGI(kTag, "entering %s (%s%s)", attadipa::core::to_string(state),
             touch_armed_ ? "touch + " : "",
             debug_wake ? "debug timer" : "PMU polling");

    for (;;) {
      const esp_err_t result = esp_light_sleep_start();
      if (result != ESP_OK) {
        ESP_LOGE(kTag, "light sleep: %s", esp_err_to_name(result));
        return false;
      }

      // Assigned, not accumulated. The PMU poll wakes on the timer every
      // 100 ms and goes straight back down, so an episode that ends on a touch
      // five minutes later has seen three thousand timer wakes -- and OR-ing
      // them in would mean no wake this firmware ever reports is without
      // Timer in it. What the report answers is why the sleep *ended*, and
      // that read is itself a bitmap, so two sources arriving together still
      // both survive.
      const std::uint32_t soc = esp_sleep_get_wakeup_causes();
      soc_causes_to_wake_sources(soc, causes);

      // ADR-0016 §6: the pin is a corroborating signal, never the classifier.
      // It used to *be* the classifier, and a GPIO wake with the line already
      // released then fell through to "cause unknown". It stays here rather
      // than inside the classification because it is the one part of it that
      // needs a GPIO, and it says nothing about the verdict either way.
      if ((causes.from_soc &
           attadipa::core::wake_bit(attadipa::core::WakeSource::Touch)) != 0 &&
          touch_interrupt_ != GPIO_NUM_NC &&
          gpio_get_level(touch_interrupt_) != 0) {
        ESP_LOGW(kTag, "GPIO wake with the touch line already high");
      }

      // One decision for every route out of sleep, in wake_classification.h.
      // Two branches reading register 0x49 for different purposes is what
      // #367's P3 finding was: the GPIO route spent the latch and dropped the
      // Button it proved.
      if (classify_wake(causes, debug_wake,
                        [this] { return consume_power_edge(); }) ==
          WakeVerdict::Report) {
        return true;
      }

      // Nothing to report. Re-arm the poll and go back down. The debug delay is
      // deliberately not re-used: it applies to the first descent only.
      const esp_err_t rearm = esp_sleep_enable_timer_wakeup(kPmuSleepPollUs);
      if (rearm != ESP_OK) {
        ESP_LOGE(kTag, "re-arm PMU poll: %s", esp_err_to_name(rearm));
        return false;
      }
    }
  }

private:
  // Read and clear the AXP2101's latched power-key edges. True means one was
  // there, which on this board is the whole of "the button woke us".
  bool consume_power_edge() const {
    if (pmu_ == nullptr) {
      return false;
    }
    std::uint8_t status = 0;
    const esp_err_t read_result = read_reg(pmu_, kAxpInterruptStatus2, &status);
    if (read_result != ESP_OK) {
      ESP_LOGW(kTag, "read PMU while asleep: %s", esp_err_to_name(read_result));
      return false;
    }
    const std::uint8_t edges = status & kAxpPowerEdges;
    if (edges == 0) {
      return false;
    }
    const esp_err_t clear_result = write_reg(pmu_, kAxpInterruptStatus2, edges);
    if (clear_result != ESP_OK) {
      ESP_LOGW(kTag, "clear PMU power edge: %s", esp_err_to_name(clear_result));
      return false;
    }
    return true;
  }

  i2c_master_dev_handle_t pmu_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  gpio_num_t touch_interrupt_ = GPIO_NUM_NC;
  bool touch_armed_ = false;
  std::uint8_t awake_brightness_ = 0;
  bool debug_timer_wake_ = false;
};

WaveshareHardware hardware;
attadipa::core::PowerOwner owner(hardware);

} // namespace

esp_err_t board_power_enable_gnss_rail(i2c_master_dev_handle_t pmu) {
#if CONFIG_ATTADIPA_BOARD_TWATCH_S3_PLUS && \
    (CONFIG_ATTADIPA_GNSS_BRIDGE || CONFIG_ATTADIPA_GNSS_LOCAL)
  ESP_RETURN_ON_FALSE(pmu != nullptr, ESP_ERR_INVALID_ARG, kTag, "no PMU");
  std::uint8_t aldo = 0;
  // BLDO1 feeds the GNSS daughterboard — HARDWARE_MATRIX.md's GNSS row, "BLDO1
  // (+ DC4 @850 mV for LS550G)". Same encoding as the ALDOs, 0x1C = 3.3 V (REG
  // 96, AXP2101 datasheet V1.4 6.13.2.81), enable REG 90 bit 4 (6.13.2.75).
  //
  // WHEN this runs is the caller's business and it is not a detail. A rail with
  // no driver behind it is current spent on nothing, so neither caller raises
  // it unconditionally -- but they raise it at different moments, and the
  // difference is a defect the review caught in #442. The bridge asks during
  // `board_power_bring_up_rails()`, early, because it runs after the UI has
  // returned and needs the rail up however that call ended. The local receiver
  // asks from `twatch_board.cpp`, immediately before `local_gnss_start()` and
  // past every `abandon_twatch_after`, because one branch of
  // `rollback_boot_retaining_all()` drops the PMU handle and returns without
  // rebooting -- the branch taken when no display is registered, which is the
  // one the "display capability" rollback takes. A rail raised before that
  // point and rolled back over it is a powered module with nothing reading it
  // and no handle left to switch it off. Raised after it, that cannot happen.
  // The rollbacks that keep a registered display keep the handle with it, so
  // those were never the hazard; the branch-by-branch argument is at the caller.
  //
  // DC3 is deliberately *not* written. The vendor drives it to 3300 mV for
  // "earlier versions" of this watch, but our datasheet copy documents DCDC3
  // only to 1.54 V and marks 1011000~1111111 reserved (6.13.2.72) — the range
  // LilyGO uses is undocumented here, so writing it would be an unevidenced
  // hardware write. DC4 at 850 mV, which an LS550G needs for its core, waits on
  // the same thing: knowing which module is fitted. Answering that is what the
  // bridge is for; if BLDO1 alone yields silence, each of those is a separate
  // evidenced step, not a guess to add here now.
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x96, 0x1C), kTag, "BLDO1 3.3 V");
  ESP_RETURN_ON_ERROR(read_reg(pmu, 0x90, &aldo), kTag, "read LDO enables");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x90, aldo | 0x10), kTag, "enable BLDO1");
  ESP_LOGI(kTag, "AXP2101: LDO enable -> 0x%02x (BLDO1 3.3 V, GNSS)",
           aldo | 0x10);
  // Read-only. Question D6 asked which rail feeds
  // the GNSS on *this* unit, BLDO1 or DC3; a module that answers with both up
  // would not have answered it. The bench run of 2026-09-05 answered it with
  // DCDC3 clear while the module was talking, so this line is now a check that
  // the answer still holds rather than the question. REG 80 bit 2 is DCDC3
  // (datasheet V1.4 6.13.2.68).
  std::uint8_t dcdc = 0;
  ESP_RETURN_ON_ERROR(read_reg(pmu, 0x80, &dcdc), kTag, "read DC enables");
  ESP_LOGI(kTag, "AXP2101: DC enable 0x%02x (DC3 %s) -- read, not written",
           dcdc, (dcdc & 0x04) ? "ON" : "off");
  return ESP_OK;
#else
  // Nothing to do, for either of two reasons. On the Waveshare no PMU rail
  // feeds GNSS at all -- the module sits on pads that take the board's own
  // 3V3. On a T-Watch image with neither GNSS symbol there is no consumer, and
  // a rail with nothing behind it is current spent on nothing; that image must
  // not pay for a receiver it does not have, which is also why the body above
  // is compiled out rather than merely left uncalled.
  (void)pmu;
  return ESP_OK;
#endif
}

esp_err_t board_power_bring_up_rails(i2c_master_dev_handle_t pmu) {
  ESP_RETURN_ON_FALSE(pmu != nullptr, ESP_ERR_INVALID_ARG, kTag, "no PMU");

#if CONFIG_ATTADIPA_BOARD_TWATCH_S3_PLUS
  // ALDO3 feeds the panel and the touch controller, ALDO2 the backlight, both
  // at 3.3 V — HARDWARE_MATRIX.md rows "Display"/"Touch", and the vendor's own
  // bring-up (LilyGoLib@38e6f8d LilyGoWatchS3.cpp:443-444, 3300 mV). Encoding:
  // 0x1C = 0.5 V + 28 × 100 mV (AXP2101 datasheet V1.4 §6.13.2.78–79); the
  // enables are REG 90 bit 1 (ALDO2) and bit 2 (ALDO3), §6.13.2.75. DC1 and
  // ALDO1 are not touched: this image is running from them.
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x94, 0x1C), kTag, "ALDO3 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x93, 0x1C), kTag, "ALDO2 3.3 V");
  std::uint8_t aldo = 0;
  ESP_RETURN_ON_ERROR(read_reg(pmu, 0x90, &aldo), kTag, "read LDO enables");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x90, aldo | 0x06), kTag,
                      "enable ALDO2/3");
  ESP_LOGI(kTag, "AXP2101: LDO enable 0x%02x -> 0x%02x (ALDO3 panel+touch, "
                 "ALDO2 backlight)",
           aldo, aldo | 0x06);

#if CONFIG_ATTADIPA_GNSS_BRIDGE
  // The bridge, and only the bridge, gets its rail here. It runs from
  // `attadipa_main.cpp` *after* `start_twatch_ui()` has returned -- including
  // when it returned an error -- so it needs BLDO1 up before that call and
  // regardless of how it ends. `CONFIG_ATTADIPA_GNSS_LOCAL` does not: it reads
  // the module from inside the UI bring-up, past every rollback point, and it
  // raises the rail there. See `board_power_enable_gnss_rail()`.
  ESP_RETURN_ON_ERROR(board_power_enable_gnss_rail(pmu), kTag, "GNSS rail");
#endif
  return ESP_OK;
#else
  // Preserve unrelated rails. The known-working board implementation needs
  // DC1 plus ALDO1/2, so own only those outputs instead of blanking the PMU.
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x82, 0x12), kTag, "DC1 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x92, 0x1C), kTag, "ALDO1 3.3 V");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x93, 0x1C), kTag, "ALDO2 3.3 V");

  std::uint8_t dcdc = 0;
  std::uint8_t aldo = 0;
  ESP_RETURN_ON_ERROR(read_reg(pmu, 0x80, &dcdc), kTag, "read DC enables");
  ESP_RETURN_ON_ERROR(read_reg(pmu, 0x90, &aldo), kTag, "read LDO enables");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x80, dcdc | 0x01), kTag, "enable DC1");
  ESP_RETURN_ON_ERROR(write_reg(pmu, 0x90, aldo | 0x03), kTag,
                      "enable ALDO1/2");

  ESP_LOGI(kTag, "AXP2101: DC enable 0x%02x, LDO enable 0x%02x", dcdc | 0x01,
           aldo | 0x03);
  return ESP_OK;
#endif
}

esp_err_t board_power_attach(i2c_master_dev_handle_t pmu,
                            esp_lcd_panel_handle_t panel,
                            gpio_num_t touch_interrupt,
                            std::uint8_t awake_brightness) {
  return hardware.attach(pmu, panel, touch_interrupt, awake_brightness);
}

void board_power_detach() { hardware.detach(); }

attadipa::core::PowerOwner &board_power_owner() { return owner; }

void board_power_set_debug_timer_wake(bool on) {
  hardware.set_debug_timer_wake(on);
}

} // namespace attadipa::firmware
