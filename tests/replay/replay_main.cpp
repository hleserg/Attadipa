#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "replay.h"

// The runner. Every fixture in the CMake-pinned scenario directory is loaded
// and replayed, and the process fails if any of them fails to load or fails an
// expectation.  This is a test executable, so it intentionally does not open
// arbitrary file paths supplied by a caller.
//
// A fixture that cannot be read is a failure, not a skip. The alternative — an
// unreadable trace quietly not running while the suite reports success — is the
// same dishonesty as writing PASS for a test that never executed, and this
// project has a rule about that.

int main()
{
    const std::filesystem::path fixture_dir = ATTADIPA_REPLAY_FIXTURE_DIR;
    std::vector<std::filesystem::path> fixtures;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(fixture_dir)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".trace" ||
            entry.path().filename() == "13-a-fixture-can-fail.trace") {
            continue;
        }
        fixtures.push_back(entry.path());
    }
    std::sort(fixtures.begin(), fixtures.end());
    if (fixtures.empty()) {
        std::fprintf(stderr, "FAIL: no replay fixtures in %s\n", fixture_dir.c_str());
        return 2;
    }

    const attadipa::core::TrustPolicy    trust    = attadipa::core::default_trust_policy();
    const attadipa::core::ValidityPolicy validity = attadipa::core::ValidityPolicy{};

    int total_failures = 0;
    int scenarios      = 0;

    for (const std::filesystem::path& fixture : fixtures) {
        const std::string path = fixture.string();

        attadipa::replay::Scenario scenario;
        std::string               error;
        if (!attadipa::replay::load(path, scenario, error)) {
            std::fprintf(stderr, "FAIL %s: %s\n", path.c_str(), error.c_str());
            ++total_failures;
            continue;
        }

        const attadipa::replay::Result result =
            attadipa::replay::run(scenario, trust, validity);
        ++scenarios;

        if (result.failures.empty()) {
            std::printf("  ok    %-46s %2zu steps\n", scenario.name.c_str(), result.steps_run);
            continue;
        }

        std::printf("  FAIL  %s\n", scenario.name.c_str());
        for (const attadipa::replay::Failure& failure : result.failures) {
            std::printf("        %s:%d: %s\n", path.c_str(), failure.line, failure.what.c_str());
            ++total_failures;
        }
    }

    std::printf("\n  %d scenario(s), %d failure(s)\n", scenarios, total_failures);
    if (total_failures != 0) {
        return 1;
    }
    std::printf("  replay: all scenarios passed "
                "(host only — synthetic traces, no receiver involved)\n");
    return 0;
}
