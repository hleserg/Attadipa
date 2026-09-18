// What the simulator does with a --frames or --zoom value that is not one.
//
// A host test rather than a simulator one, and that is the point of it. The
// three tests that already compile `sim/` need LVGL and therefore a simulator
// build, so a defect in the public command line was invisible to every check in
// an ordinary CI run -- which is where #578's `--frames 4294967296` was found
// by reading rather than by anything failing. `sim/options.cpp` needs no LVGL
// and no SDL: it is argv in, `Options` out, and it links here beside the code
// it parses for.
//
// The values under test are the two with an amplifier behind them. `frames`
// uses 0 as "run until the window closes", so a count that wraps to zero does
// not become a wrong finite number -- it becomes an unbounded run, and headless
// there is no window to close it. `zoom` is handed to SDL as the window size,
// so a NaN or an infinity is not a wrong window but no window.

#include "../sim/options.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

void check(bool ok, const char *what) {
  std::printf("  %-4s %s\n", ok ? "ok" : "FAIL", what);
  if (!ok) {
    ++failures;
  }
}

// One parse, from a real argv. `parse_options` takes `char **`, so the strings
// are writable copies the way a shell would hand them over.
attadipa::sim::ParseResult run(const char *flag, const char *value,
                               attadipa::sim::Options &out) {
  char argv0[] = "attadipa_sim";
  char flag_buffer[32];
  char buffer[64];
  std::snprintf(flag_buffer, sizeof(flag_buffer), "%s", flag);
  std::snprintf(buffer, sizeof(buffer), "%s", value);
  char *argv[] = {argv0, flag_buffer, buffer};
  return attadipa::sim::parse_options(3, argv, out);
}

void refused(const char *value, const char *what) {
  attadipa::sim::Options out;
  const attadipa::sim::ParseResult result = run("--frames", value, out);
  // Both halves matter. An accepted value that happens to land on a harmless
  // number is still a run the operator did not ask for, and `frames` must not
  // have moved off its default either.
  check(result == attadipa::sim::ParseResult::Error && out.frames == 0, what);
}

void accepted(const char *value, std::uint32_t want, const char *what) {
  attadipa::sim::Options out;
  const attadipa::sim::ParseResult result = run("--frames", value, out);
  check(result == attadipa::sim::ParseResult::Ok && out.frames == want, what);
}

// The same two, for the other flag. `zoom` defaults to 1.0F rather than to a
// sentinel, so a refusal has to leave exactly that: a window scaled by
// something the operator did not ask for is the defect, and 1.0F is the only
// value that is not one.
void refused_zoom(const char *value, const char *what) {
  attadipa::sim::Options out;
  const attadipa::sim::ParseResult result = run("--zoom", value, out);
  check(result == attadipa::sim::ParseResult::Error && out.zoom == 1.0F, what);
}

void accepted_zoom(const char *value, float want, const char *what) {
  attadipa::sim::Options out;
  const attadipa::sim::ParseResult result = run("--zoom", value, out);
  check(result == attadipa::sim::ParseResult::Ok && out.zoom == want, what);
}

} // namespace

int main() {
  std::printf("simulator --frames:\n");

  // The two from #578, measured on the reviewed tree: the first narrowed to 0
  // and ran forever, the second to UINT32_MAX -- an ESTIMATED 248 days on the
  // 5 ms delay alone.
  refused("4294967296", "one past UINT32_MAX is refused, not wrapped to the "
                        "run-forever sentinel");
  refused("-1", "a negative count is refused, not negated into 4294967295");

  // `strtoul` reports ERANGE for this one and would otherwise hand back
  // ULONG_MAX, which fits nothing and means nothing.
  refused("18446744073709551616", "a value past unsigned long is refused");

  // `strtoul` skips leading whitespace before it looks for the sign, so a sign
  // check on argv[0] alone would have missed this.
  refused(" -1", "a sign behind whitespace is still a sign");
  refused("-0", "minus zero is a negative count, not the sentinel");

  refused("", "an empty value is refused");
  refused("12x", "trailing text is refused");
  refused("0x10", "a hex literal is refused rather than read as 0");

  // The other direction: the fix must not have narrowed what works.
  accepted("17", 17, "an ordinary count still parses");
  accepted("0", 0, "zero is still the run-until-the-window-closes sentinel");
  accepted("4294967295", 4294967295U, "the largest count that fits is kept");
  accepted("+7", 7, "a leading plus is unambiguous and does not wrap");

  std::printf("simulator --zoom:\n");

  // The reason this flag is in the same test as `--frames`: it had the same
  // defect in the same file and the fix for one did not touch the other. The
  // old guard was `value <= 0.0`, and every comparison with a NaN is false, so
  // this reached `lv_sdl_window_set_zoom` and SDL sized a window from it.
  refused_zoom("nan", "NaN is not a scale factor");
  refused_zoom("-nan", "a signed NaN is refused by the same bound");
  refused_zoom("inf", "infinity is not a scale factor");

  // A finite double on both sides of what a float can carry. `1e300` is a
  // perfectly good double and becomes infinity the moment it is narrowed;
  // `1e-300` becomes zero, a window with no pixels in it.
  refused_zoom("1e300", "a double that becomes infinity as a float is refused");
  refused_zoom("1e-300", "a double that becomes zero as a float is refused");
  refused_zoom("1e400", "a value past the range of double is refused");

  refused_zoom("0", "a zero scale factor is refused");
  refused_zoom("-2", "a negative scale factor is refused");
  refused_zoom("65", "a factor past the upper bound is refused");
  refused_zoom("0.03", "a factor below the lower bound is refused");
  refused_zoom("2x", "trailing text is refused");
  refused_zoom("", "an empty value is refused");

  // And the other direction, at both ends of the range it does accept.
  accepted_zoom("2", 2.0F, "an ordinary scale factor still parses");
  accepted_zoom("0.0625", 0.0625F, "the smallest factor in range is kept");
  accepted_zoom("64", 64.0F, "the largest factor in range is kept");

  std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
  return failures == 0 ? 0 : 1;
}
