#include <cstdio>
#include <string>
#include <vector>

#include "replay.h"

// The runner. Every fixture named on the command line is loaded and replayed,
// and the process fails if any of them fails to load or fails an expectation.
//
// A fixture that cannot be read is a failure, not a skip. The alternative — an
// unreadable trace quietly not running while the suite reports success — is the
// same dishonesty as writing PASS for a test that never executed, and this
// project has a rule about that.

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: replay <fixture.trace> [...]\n");
        return 2;
    }

    const attadipa::core::TrustPolicy    trust    = attadipa::core::default_trust_policy();
    const attadipa::core::ValidityPolicy validity = attadipa::core::ValidityPolicy{};

    int total_failures = 0;
    int scenarios      = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string path = argv[i];

        attadipa::replay::Scenario scenario;
        std::string               error;
        // codeql[cpp/path-injection]: This is a developer-only replay CLI. It
        // deliberately opens the explicitly supplied local trace and performs
        // no privileged operation with its contents.
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
