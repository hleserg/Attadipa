#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "firefly/core/position.h"
#include "firefly/core/trust.h"

// A replayable, deterministic navigation test rig.
//
// The owner calls this one of the most important results of the upstream
// research, and the reason is that the interesting GNSS failures cannot be
// staged. Nobody can arrange to be spoofed on a Tuesday afternoon, and a
// detector for an event that cannot be produced on demand is a detector that
// gets written once and never verified again.
//
// So the rig replays *timestamped observations* into the same pure code the
// device runs — `classify()` and `TrustEvaluator` — and asserts the verdict at
// every step, not only at the end. Nothing in the path reads a clock, allocates
// during the run or depends on ordering, so the same trace produces the same
// result on every machine and in every build.
//
// The fixture format is the normalized observation, not NMEA. That is the level
// at which the logic under test operates, and it keeps the rig honest about
// what it covers: it exercises the trust and validity model, and it does *not*
// exercise a parser, because Firefly has no parser yet — minmea is a `WRAP`
// decision in the reuse ledger and is not vendored. When it is, an NMEA
// front-end feeds this same rig with the same assertions, and a GPX or a raw
// vendor-binary capture does the same. The format below is deliberately shaped
// so that adding those front-ends adds a reader, not a second rig.
//
// A note on the coordinates in every fixture: they are **synthetic**, chosen to
// be obviously nowhere. No real location belonging to anybody appears in this
// repository, and none may be added.

namespace firefly::replay {

// One assertion attached to one step. Absent fields are not checked, so a
// fixture can pin exactly what it is about and stay readable.
struct Expectation {
    bool                       check_validity = false;
    core::PositionValidity     validity       = core::PositionValidity::NoFix;
    bool                       check_trust    = false;
    core::TrustState           trust          = core::TrustState::Trusted;
    std::vector<core::TrustReason> reasons_present;
    std::vector<core::TrustReason> reasons_absent;
};

struct Step {
    core::MonotonicTime         at{};
    core::GnssObservation       observation{};
    core::MotionEvidence        motion{};
    std::optional<core::WallTime> device_time;
    bool                        is_other_provider = false;
    Expectation                 expect{};
    int                         line = 0;
};

struct Scenario {
    std::string       name;
    std::string       description;
    std::vector<Step> steps;
};

struct Failure {
    int         line = 0;
    std::string what;
};

struct Result {
    bool                 parsed = false;
    std::string          parse_error;
    std::vector<Failure> failures;
    core::TrustState     final_state = core::TrustState::Trusted;
    std::size_t          steps_run   = 0;
};

// Read a fixture. Returns false and fills `error` on a malformed file — a rig
// that silently skips a trace it could not read would report success for tests
// that never ran, which is the failure mode this project has already been
// bitten by once.
bool load(const std::string& path, Scenario& out, std::string& error);

// Replay it. Deterministic: no clock, no randomness, no filesystem.
Result run(const Scenario& scenario, const core::TrustPolicy& policy,
           const core::ValidityPolicy& validity_policy);

}  // namespace firefly::replay
