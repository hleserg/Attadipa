#include <cstdio>
#include <string>

#include "replay/replay.h"

// The rig's own test. A rig that passes everything is indistinguishable from a
// rig that checks nothing, and the second is worse than having no rig at all —
// it produces a directory of green scenarios that assert nothing.
//
// So this file feeds the runner things it must reject: a fixture whose
// expectations are deliberately wrong, a fixture that does not exist, and
// several malformed ones built in memory.

namespace {

int failures = 0;

void check(bool condition, const char* what, int line)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

std::string fixtures;

// The negative fixture: a perfectly good fix with expectations that say
// otherwise. The runner must report three mismatches, not zero.
void test_a_wrong_expectation_is_reported()
{
    firefly::replay::Scenario scenario;
    std::string               error;
    const bool loaded = firefly::replay::load(fixtures + "/13-a-fixture-can-fail.trace", scenario,
                                              error);
    if (!loaded) {
        std::fprintf(stderr, "FAIL: could not load the negative fixture: %s\n", error.c_str());
        ++failures;
        return;
    }

    const firefly::replay::Result result = firefly::replay::run(
        scenario, firefly::core::default_trust_policy(), firefly::core::ValidityPolicy{});

    CHECK(result.failures.size() == 3);   // validity, trust, and the missing reason
    for (const firefly::replay::Failure& failure : result.failures) {
        CHECK(failure.line > 0);          // every failure points at a line to look at
        CHECK(!failure.what.empty());
    }
}

// A fixture that cannot be read is a failure, not a skip. An unreadable trace
// quietly not running while the suite reports success is the same dishonesty as
// writing PASS for a test that never executed.
void test_a_missing_fixture_is_a_failure()
{
    firefly::replay::Scenario scenario;
    std::string               error;
    CHECK(!firefly::replay::load(fixtures + "/does-not-exist.trace", scenario, error));
    CHECK(!error.empty());
}

// The reader is strict, and each of these is a way a fixture can be wrong that
// would otherwise run as a shorter test than it claims to be.
void test_malformed_fixtures_are_refused()
{
    struct Case { const char* body; const char* why; };
    const Case cases[] = {
        {"scenario x\nat 0\n  fix banana\n",            "an unknown fix type"},
        {"scenario x\nat 0\n  wobble 3\n",              "an unknown keyword"},
        {"scenario x\nat 0\n  pos 5000000\n",           "a coordinate with one number"},
        {"scenario x\nat 0\n  expect trust sideways\n", "an unknown trust state"},
        {"scenario x\nat 0\n  expect reason vibes\n",   "an unknown trust reason"},
        {"scenario x\nat 0\n  expect weather sunny\n",  "expecting something the rig cannot check"},
        {"scenario x\n  fix 3d\n",                      "a field before the first step"},
        {"at 0\n  fix 3d\n",                            "no scenario line"},
        {"scenario x\n",                                "no steps"},
        {"scenario x\nat -5\n",                         "a negative timestamp"},
    };

    for (const Case& one : cases) {
        const std::string path = "/tmp/firefly-replay-malformed.trace";
        std::FILE*        file = std::fopen(path.c_str(), "w");
        if (file == nullptr) {
            std::fprintf(stderr, "FAIL: cannot write a temporary fixture\n");
            ++failures;
            return;
        }
        std::fputs(one.body, file);
        std::fclose(file);

        firefly::replay::Scenario scenario;
        std::string               error;
        if (firefly::replay::load(path, scenario, error)) {
            std::fprintf(stderr, "FAIL: accepted a fixture with %s\n", one.why);
            ++failures;
        } else if (error.empty()) {
            std::fprintf(stderr, "FAIL: refused a fixture with %s and said nothing\n", one.why);
            ++failures;
        }
        std::remove(path.c_str());
    }
}

// The property that makes a replay rig worth having: the same trace gives the
// same answer, every time, on every machine. Nothing in the path reads a clock
// or allocates in a way that depends on address layout.
void test_replay_is_deterministic()
{
    firefly::replay::Scenario scenario;
    std::string               error;
    if (!firefly::replay::load(fixtures + "/04-spoofing-stops-navigation.trace", scenario, error)) {
        std::fprintf(stderr, "FAIL: %s\n", error.c_str());
        ++failures;
        return;
    }

    const firefly::replay::Result first = firefly::replay::run(
        scenario, firefly::core::default_trust_policy(), firefly::core::ValidityPolicy{});
    for (int i = 0; i < 20; ++i) {
        const firefly::replay::Result again = firefly::replay::run(
            scenario, firefly::core::default_trust_policy(), firefly::core::ValidityPolicy{});
        CHECK(again.failures.size() == first.failures.size());
        CHECK(again.final_state == first.final_state);
        CHECK(again.steps_run == first.steps_run);
    }
}

// A fixture carries its own explanation, and the runner keeps it. A trace that
// fails at three in the morning has to be able to say what it was about.
void test_a_fixture_explains_itself()
{
    firefly::replay::Scenario scenario;
    std::string               error;
    CHECK(firefly::replay::load(fixtures + "/07-a-walk-is-not-a-jump.trace", scenario, error));
    CHECK(!scenario.name.empty());
    CHECK(scenario.description.size() > 40);
    CHECK(scenario.steps.size() >= 5);

    // Every step remembers which line of the file it came from, or a failure
    // report points at nothing.
    for (const firefly::replay::Step& step : scenario.steps) {
        CHECK(step.line > 0);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_replay_rig <scenario directory>\n");
        return 2;
    }
    fixtures = argv[1];

    test_a_wrong_expectation_is_reported();
    test_a_missing_fixture_is_a_failure();
    test_malformed_fixtures_are_refused();
    test_replay_is_deterministic();
    test_a_fixture_explains_itself();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("replay rig: all checks passed (the rig can fail, which is the point)\n");
    return 0;
}
