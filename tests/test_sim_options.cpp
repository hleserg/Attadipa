// What the simulator does with a --frames value that is not a frame count.
//
// A host test rather than a simulator one, and that is the point of it. The
// three tests that already compile `sim/` need LVGL and therefore a simulator
// build, so a defect in the public command line was invisible to every check in
// an ordinary CI run -- which is where #578's `--frames 4294967296` was found
// by reading rather than by anything failing. `sim/options.cpp` needs no LVGL
// and no SDL: it is argv in, `Options` out, and it links here beside the code
// it parses for.
//
// The value under test is the one with an amplifier behind it. `frames` uses 0
// as "run until the window closes", so a count that wraps to zero does not
// become a wrong finite number -- it becomes an unbounded run, and headless
// there is no window to close it.

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
attadipa::sim::ParseResult run(const char *value,
                               attadipa::sim::Options &out) {
  char argv0[] = "attadipa_sim";
  char flag[] = "--frames";
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%s", value);
  char *argv[] = {argv0, flag, buffer};
  return attadipa::sim::parse_options(3, argv, out);
}

void refused(const char *value, const char *what) {
  attadipa::sim::Options out;
  const attadipa::sim::ParseResult result = run(value, out);
  // Both halves matter. An accepted value that happens to land on a harmless
  // number is still a run the operator did not ask for, and `frames` must not
  // have moved off its default either.
  check(result == attadipa::sim::ParseResult::Error && out.frames == 0, what);
}

void accepted(const char *value, std::uint32_t want, const char *what) {
  attadipa::sim::Options out;
  const attadipa::sim::ParseResult result = run(value, out);
  check(result == attadipa::sim::ParseResult::Ok && out.frames == want, what);
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

  std::printf("%s\n", failures == 0 ? "all passed" : "FAILED");
  return failures == 0 ? 0 : 1;
}
