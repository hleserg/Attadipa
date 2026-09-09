#pragma once

#include <cstdint>

namespace attadipa::apps {

enum class BrightnessRead { Present, Missing, Failed };
enum class BrightnessWrite { Saved, Failed, Uncertain };
enum class BrightnessError { None, Load, Apply, Save, Uncertain };

// Requested percent, never a claim about measured light output. Implemented
// by the board composition root and the simulator; applications see neither.
struct BrightnessPort {
  virtual ~BrightnessPort() = default;
  virtual BrightnessRead load(std::uint8_t &percent) = 0;
  virtual bool apply(std::uint8_t percent) = 0;
  // Success includes remembering the committed level for the next wake.
  virtual BrightnessWrite store(std::uint8_t percent) = 0;
  virtual void restart() = 0;
};

class BrightnessSettings {
public:
  BrightnessSettings(BrightnessPort &port, std::uint8_t minimum,
                     std::uint8_t fallback, std::uint8_t step)
      : port_(port), minimum_(minimum), fallback_(fallback), step_(step),
        saved_(fallback), draft_(fallback) {}

  void load();
  bool preview(int percent);
  bool adjust(int direction);
  bool save();
  // Back, Cancel and wake all restore the last successfully saved request.
  bool cancel();
  void restart();

  std::uint8_t value() const { return draft_; }
  std::uint8_t saved() const { return saved_; }
  std::uint8_t minimum() const { return minimum_; }
  BrightnessError error() const { return error_; }

private:
  bool valid(int value) const { return value >= minimum_ && value <= 100; }
  BrightnessPort &port_;
  std::uint8_t minimum_, fallback_, step_, saved_, draft_;
  BrightnessError error_ = BrightnessError::None;
};

} // namespace attadipa::apps
