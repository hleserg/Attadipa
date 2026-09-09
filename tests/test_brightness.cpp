#include "attadipa/apps/brightness.h"

#include <cstdio>
#include <initializer_list>

using namespace attadipa::apps;

struct Port final : BrightnessPort {
  BrightnessRead read = BrightnessRead::Missing;
  std::uint8_t stored = 0, applied = 0, wake = 5;
  unsigned writes = 0;
  bool apply_ok = true, store_ok = true;
  BrightnessRead load(std::uint8_t &value) override { value = stored; return read; }
  bool apply(std::uint8_t value) override {
    if (!apply_ok) return false;
    applied = value;
    return true;
  }
  bool store(std::uint8_t value) override {
    ++writes;
    if (!store_ok) return false;
    read = BrightnessRead::Present;
    stored = wake = value;
    return true;
  }
};

int main() {
  unsigned failures = 0;
  const auto check = [&](bool ok, const char *why) {
    if (!ok) { std::fprintf(stderr, "%s\n", why); ++failures; }
  };
  Port port;
  BrightnessSettings settings(port, 5, 5, 5);
  settings.load();
  check(settings.saved() == 5 && port.writes == 0, "missing state uses default without writing");
  check(settings.preview(65) && port.applied == 65 && port.wake == 5 && port.writes == 0,
        "preview applies immediately but never commits or changes wake level");
  check(settings.cancel() && settings.value() == 5 && port.applied == 5, "cancel restores saved");
  check(settings.preview(65) && settings.save() && port.stored == 65 && port.wake == 65,
        "Save commits and updates wake");
  BrightnessSettings rebooted(port, 5, 5, 5);
  rebooted.load();
  check(rebooted.saved() == 65 && rebooted.cancel() && port.applied == 65, "reboot restores committed value");
  port.store_ok = false;
  check(settings.preview(80) && !settings.save() && settings.error() == BrightnessError::Save &&
        settings.saved() == 65 && port.stored == 65 && port.wake == 65, "failed save keeps draft and committed state distinct");
  check(settings.cancel() && port.applied == 65, "failed Save remains cancellable");
  port.store_ok = true;
  check(settings.preview(80) && settings.save(), "retry can save");
  port.apply_ok = false;
  check(!settings.preview(90) && settings.value() == 80 && settings.error() == BrightnessError::Apply,
        "failed preview never claims new value");
  port.apply_ok = true;
  for (int value : {-1, 0, 4, 101, 261}) {
    check(!settings.preview(value) && settings.value() == 80, "reject invalid input before narrowing");
  }
  check(settings.preview(100) && settings.adjust(1) && settings.value() == 100, "upper bound");
  check(settings.preview(5) && settings.adjust(-1) && settings.value() == 5, "no screen-off trap");
  for (auto value : {0, 4, 101, 255}) {
    port.stored = static_cast<std::uint8_t>(value);
    settings.load();
    check(settings.saved() == 5 && settings.error() == BrightnessError::Load, "corrupt stored value uses fallback honestly");
  }
  port.read = BrightnessRead::Failed;
  settings.load();
  check(settings.saved() == 5 && settings.error() == BrightnessError::Load, "read failure uses fallback honestly");
  check(settings.save() && port.stored == 5, "Save repairs missing or invalid stored state");
  return failures != 0;
}
