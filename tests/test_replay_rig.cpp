#include <cstdio>
#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

// Where this file puts the fixtures it writes by hand.
//
// Not a fixed name under a shared directory, which was wrong twice over: two
// runs of this test on one machine — a CI matrix, two people on a build host —
// race for the same path, and in a world-writable directory whoever gets there
// first decides what that path points at. mkdtemp settles both in one syscall:
// the name is one nobody else has and the directory is 0700 before it exists
// to anybody, so there is no window between picking the path and owning it.
class Scratch
{
public:
    Scratch()
    {
        const char* base = std::getenv("TMPDIR");
        path_ = std::string(base != nullptr && base[0] != '\0' ? base : "/tmp")
                + "/firefly-replay-XXXXXX";
        ok_ = ::mkdtemp(&path_[0]) != nullptr;
    }

    ~Scratch()
    {
        if (ok_) {
            ::rmdir(path_.c_str());   // best effort; each test removes its own file
        }
    }

    Scratch(const Scratch&)            = delete;
    Scratch& operator=(const Scratch&) = delete;

    bool        ok() const { return ok_; }
    std::string file(const char* name) const { return path_ + "/" + name; }

private:
    std::string path_;
    bool        ok_ = false;
};

// Create the file 0600 outright rather than letting fopen create it 0666 and
// trusting the umask to narrow it afterwards. The umask is inherited from
// whoever started the process and is not ours to assume; a mode argument is.
std::FILE* create_private(const std::string& path)
{
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_EXCL,
                          S_IRUSR | S_IWUSR);
    if (fd < 0) {
        return nullptr;
    }
    std::FILE* file = ::fdopen(fd, "w");
    if (file == nullptr) {
        ::close(fd);
    }
    return file;
}

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

        // The two keywords that let a fixture reach Stale, and the ways of
        // writing them that mean nothing.
        {"scenario x\nat 1000\n  age 5000\n",         "an age older than the trace itself"},
        {"scenario x\nat 1000\n  age\n",              "an age with no number"},
        {"scenario x\nat 1000\n  age -1\n",           "a negative age"},
        {"scenario x\nat 0\n  fix 3d\n  pos 5000000 10000000\n"
         "at 1000\n  fix 3d\n  hold\n",               "a hold that is not the first line of its step"},
        {"scenario x\nat 0\n  fix 3d\n  pos 5000000 10000000\n"
         "at 1000\n  hold\n  fix 3d\n",               "a hold that then describes what arrived"},
        {"scenario x\nat 0\n  hold\n",                "a hold with nothing yet to hold"},
        {"scenario x\nat 0\n  provider other\n  pos 5000000 10000000\n"
         "at 1000\n  hold\n",                          "a hold after nothing but a provider's answer"},
    };

    Scratch scratch;
    if (!scratch.ok()) {
        std::fprintf(stderr, "FAIL: cannot make a private directory for the fixtures\n");
        ++failures;
        return;
    }

    for (const Case& one : cases) {
        const std::string path = scratch.file("malformed.trace");
        std::FILE*        file = create_private(path);
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

// The positive half of the same story: the two keywords have to mean what the
// fixtures assume they mean, and neither is observable from the outside except
// through the verdict. A silent no-op here would leave every stale scenario
// passing for the wrong reason — which is precisely what happened before these
// keywords existed, and why 02-fix-goes-stale asserted `valid` three times.
void test_age_and_hold_parse_into_what_they_claim()
{
    Scratch scratch;
    if (!scratch.ok()) {
        std::fprintf(stderr, "FAIL: cannot make a private directory for the fixture\n");
        ++failures;
        return;
    }

    const std::string path = scratch.file("freshness.trace");
    std::FILE*        file = create_private(path);
    if (file == nullptr) {
        std::fprintf(stderr, "FAIL: cannot write a temporary fixture\n");
        ++failures;
        return;
    }
    std::fputs("scenario freshness\n"
               "at 1000\n  fix 3d\n  pos 5000000 10000000\n"
               "at 5000\n  hold\n"
               "at 9000\n  fix 3d\n  age 4000\n  pos 5000000 10000000\n",
               file);
    std::fclose(file);

    firefly::replay::Scenario scenario;
    std::string               error;
    if (!firefly::replay::load(path, scenario, error)) {
        std::fprintf(stderr, "FAIL: %s\n", error.c_str());
        ++failures;
        std::remove(path.c_str());
        return;
    }
    std::remove(path.c_str());

    CHECK(scenario.steps.size() == 3);
    if (scenario.steps.size() != 3) {
        return;
    }

    // Without `age`, an observation is exactly as fresh as the moment it is
    // judged at — the default the rig had, and the reason it could not produce
    // a stale one.
    CHECK(scenario.steps[0].observation.observed_at.ms == 1000);
    CHECK(!scenario.steps[0].is_hold);

    // A hold carries no observation of its own. run() supplies the held one.
    CHECK(scenario.steps[1].is_hold);
    CHECK(scenario.steps[1].at.ms == 5000);
    CHECK(!scenario.steps[1].observation.position.has_value());

    // And `age` moves the observation's own clock backwards, not the step's.
    CHECK(scenario.steps[2].at.ms == 9000);
    CHECK(scenario.steps[2].observation.observed_at.ms == 5000);
}

// run() also takes scenarios nobody parsed, and a hold with nothing behind it
// would otherwise classify a default-constructed observation and report a
// confident NoFix for a step that never had an input. It says so instead.
void test_a_hold_with_nothing_held_is_reported()
{
    firefly::replay::Scenario scenario;
    scenario.name = "hand-built";

    firefly::replay::Step step;
    step.at      = firefly::core::MonotonicTime{1000};
    step.line    = 7;
    step.is_hold = true;
    scenario.steps.push_back(step);

    const firefly::replay::Result result = firefly::replay::run(
        scenario, firefly::core::default_trust_policy(), firefly::core::ValidityPolicy{});

    CHECK(result.failures.size() == 1);
    CHECK(result.steps_run == 1);
    if (result.failures.size() == 1) {
        CHECK(result.failures[0].line == 7);
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
    test_age_and_hold_parse_into_what_they_claim();
    test_a_hold_with_nothing_held_is_reported();

    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("replay rig: all checks passed (the rig can fail, which is the point)\n");
    return 0;
}
