#include "replay.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

// The fixture reader and the replay loop.
//
// The reader is strict on purpose. Every unknown keyword, every missing
// argument and every unparseable number is an error that stops the load, and
// `load()` returns false rather than a partial scenario. A rig that quietly
// skipped a line it did not understand would run a shorter test than the
// fixture describes and report success for the steps that never happened —
// which is the same class of dishonesty as writing PASS for a test that did not
// run on hardware.

namespace attadipa::replay {
namespace {

using namespace attadipa::core;

struct Parser {
    std::string error;
    int         line = 0;

    bool fail(const std::string& what)
    {
        std::ostringstream out;
        out << "line " << line << ": " << what;
        error = out.str();
        return false;
    }
};

bool to_i64(const std::string& text, std::int64_t& out)
{
    if (text.empty()) {
        return false;
    }
    char*             end   = nullptr;
    const long long   value = std::strtoll(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != '\0') {
        return false;
    }
    out = static_cast<std::int64_t>(value);
    return true;
}

bool sensor_body(const std::string& text, SensorBody& out)
{
    if (text == "watch") { out = SensorBody::Watch; return true; }
    if (text == "node") { out = SensorBody::Node; return true; }
    if (text == "companion") { out = SensorBody::Companion; return true; }
    return false;
}

bool indication(const std::string& text, ReceiverIndication& out)
{
    if (text == "unknown")     { out = ReceiverIndication::Unknown;     return true; }
    if (text == "unsupported") { out = ReceiverIndication::Unsupported; return true; }
    if (text == "none")        { out = ReceiverIndication::None;        return true; }
    if (text == "warning")     { out = ReceiverIndication::Warning;     return true; }
    if (text == "critical")    { out = ReceiverIndication::Critical;    return true; }
    return false;
}

bool fix_type(const std::string& text, FixType& out)
{
    if (text == "unknown") { out = FixType::Unknown;       return true; }
    if (text == "none")    { out = FixType::NoFix;         return true; }
    if (text == "time")    { out = FixType::TimeOnly;      return true; }
    if (text == "2d")      { out = FixType::TwoD;          return true; }
    if (text == "3d")      { out = FixType::ThreeD;        return true; }
    if (text == "dr")      { out = FixType::DeadReckoning; return true; }
    return false;
}

bool validity_of(const std::string& text, PositionValidity& out)
{
    if (text == "nofix")    { out = PositionValidity::NoFix;    return true; }
    if (text == "stale")    { out = PositionValidity::Stale;    return true; }
    if (text == "degraded") { out = PositionValidity::Degraded; return true; }
    if (text == "valid")    { out = PositionValidity::Valid;    return true; }
    return false;
}

bool trust_of(const std::string& text, TrustState& out)
{
    if (text == "untrusted") { out = TrustState::Untrusted; return true; }
    if (text == "degraded")  { out = TrustState::Degraded;  return true; }
    if (text == "trusted")   { out = TrustState::Trusted;   return true; }
    return false;
}

// Reason names are the enum names in kebab case, so a fixture reads like a
// sentence and a new reason cannot be referred to before it exists.
bool reason_of(const std::string& text, TrustReason& out)
{
    struct Entry { const char* name; TrustReason reason; };
    static const Entry table[] = {
        {"receiver-jamming",          TrustReason::ReceiverJamming},
        {"receiver-spoofing",         TrustReason::ReceiverSpoofing},
        {"protection-level-invalid",  TrustReason::ProtectionLevelInvalid},
        {"protection-level-exceeded", TrustReason::ProtectionLevelExceeded},
        {"motion-disagreement",       TrustReason::MotionDisagreement},
        {"implausible-speed",         TrustReason::ImplausibleSpeed},
        {"implausible-altitude-rate", TrustReason::ImplausibleAltitudeRate},
        {"position-jump",             TrustReason::PositionJump},
        {"clock-disagreement",        TrustReason::ClockDisagreement},
        {"provider-disagreement",     TrustReason::ProviderDisagreement},
        {"constellation-anomaly",     TrustReason::ConstellationAnomaly},
        {"accuracy-poor",             TrustReason::AccuracyPoor},
        {"insufficient-satellites",   TrustReason::InsufficientSatellites},
        {"stale-position",            TrustReason::StalePosition},
        {"fix-lost",                  TrustReason::FixLost},
    };
    for (const Entry& entry : table) {
        if (text == entry.name) {
            out = entry.reason;
            return true;
        }
    }
    return false;
}

}  // namespace

bool load(const std::string& path, Scenario& out, std::string& error)
{
    std::ifstream file(path);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }

    Parser parser;
    out = Scenario{};

    // How many observation-describing keywords the current step has seen. Used
    // only to keep `hold` honest: a step where nothing arrived cannot also
    // describe what arrived.
    int  fields_in_step = 0;

    std::string text;
    while (std::getline(file, text)) {
        ++parser.line;

        // Comments and blank lines. A trace is meant to be read by a person
        // trying to understand a failure, so it has to be able to explain
        // itself.
        const std::size_t hash = text.find('#');
        if (hash != std::string::npos) {
            text.erase(hash);
        }
        std::istringstream fields(text);
        std::string        keyword;
        if (!(fields >> keyword)) {
            continue;
        }

        auto word = [&fields](std::string& into) { return static_cast<bool>(fields >> into); };
        auto number = [&fields](std::int64_t& into) {
            std::string token;
            return (fields >> token) && to_i64(token, into);
        };

        if (keyword == "scenario") {
            std::getline(fields, out.name);
            if (!out.name.empty() && out.name.front() == ' ') {
                out.name.erase(0, 1);
            }
            continue;
        }
        if (keyword == "describe") {
            std::string rest;
            std::getline(fields, rest);
            if (!rest.empty() && rest.front() == ' ') {
                rest.erase(0, 1);
            }
            out.description += out.description.empty() ? rest : " " + rest;
            continue;
        }

        if (keyword == "at") {
            std::int64_t ms = 0;
            if (!number(ms) || ms < 0) {
                parser.fail("`at` needs a non-negative millisecond timestamp");
                error = parser.error;
                return false;
            }
            Step step;
            step.at   = MonotonicTime{static_cast<std::uint64_t>(ms)};
            step.line = parser.line;
            // By default an observation is as fresh as the moment it is judged
            // at. `age` is how a fixture says otherwise.
            step.observation.observed_at = step.at;
            out.steps.push_back(step);
            fields_in_step = 0;
            continue;
        }

        if (out.steps.empty()) {
            parser.fail("`" + keyword + "` before the first `at`");
            error = parser.error;
            return false;
        }
        Step&            step = out.steps.back();
        GnssObservation& o    = step.observation;

        // Everything except `expect` describes what arrived, and in a `hold`
        // step nothing did. Rejecting the combination rather than quietly
        // preferring one of them is the same rule as the rest of this reader:
        // a contradiction in a fixture is a bug in the fixture, and a rig that
        // resolves it silently tests something nobody wrote down.
        if (keyword != "expect") {
            if (step.is_hold) {
                parser.fail("`" + keyword + "` in a `hold` step — a step where nothing "
                            "arrived cannot describe what arrived");
                error = parser.error;
                return false;
            }
            ++fields_in_step;
        }

        if (keyword == "hold") {
            // Must come first, so that the check above catches every field in
            // the step regardless of the order they were written in.
            if (fields_in_step != 1) {
                parser.fail("`hold` must be the first line of its step");
                error = parser.error;
                return false;
            }
            bool anything_to_hold = false;
            // All but the last, which is this step — a hold cannot be its own
            // antecedent.
            for (std::size_t i = 0; i + 1 < out.steps.size(); ++i) {
                // A provider's answer is compared and never adopted, so there
                // is nothing of it to hold on to.
                const Step& earlier = out.steps[i];
                if (!earlier.is_hold && !earlier.is_other_provider) {
                    anything_to_hold = true;
                }
            }
            if (!anything_to_hold) {
                parser.fail("`hold` before any observation — there is nothing to hold");
                error = parser.error;
                return false;
            }
            step.is_hold = true;
        } else if (keyword == "age") {
            std::int64_t ms = 0;
            if (!number(ms) || ms < 0) {
                parser.fail("`age` needs a non-negative millisecond age");
                error = parser.error;
                return false;
            }
            // Monotonic time has no before-zero. Wrapping a uint64 here would
            // turn "forty seconds old" into "584 million years in the future",
            // and every freshness test downstream would pass for the wrong
            // reason.
            if (static_cast<std::uint64_t>(ms) > step.at.ms) {
                parser.fail("`age` is larger than the step's own time — the observation "
                            "would predate the start of the trace");
                error = parser.error;
                return false;
            }
            o.observed_at = MonotonicTime{step.at.ms - static_cast<std::uint64_t>(ms)};
        } else if (keyword == "fix") {
            std::string kind;
            if (!word(kind) || !fix_type(kind, o.fix_type)) {
                parser.fail("`fix` needs one of unknown none time 2d 3d dr");
                error = parser.error;
                return false;
            }
        } else if (keyword == "pos") {
            std::int64_t lat = 0, lon = 0;
            if (!number(lat) || !number(lon)) {
                parser.fail("`pos` needs latitude and longitude in degrees x 1e7");
                error = parser.error;
                return false;
            }
            o.position = Position{static_cast<std::int32_t>(lat), static_cast<std::int32_t>(lon)};
        } else if (keyword == "alt") {
            std::int64_t mm = 0;
            if (!number(mm)) { parser.fail("`alt` needs millimetres"); error = parser.error; return false; }
            o.altitude_msl_mm = static_cast<std::int32_t>(mm);
        } else if (keyword == "acc") {
            std::int64_t mm = 0;
            if (!number(mm) || mm < 0) { parser.fail("`acc` needs millimetres"); error = parser.error; return false; }
            o.horizontal_accuracy_mm = static_cast<std::uint32_t>(mm);
        } else if (keyword == "speed") {
            std::int64_t mm_s = 0;
            if (!number(mm_s) || mm_s < 0) { parser.fail("`speed` needs mm/s"); error = parser.error; return false; }
            o.speed_mm_s = static_cast<std::uint32_t>(mm_s);
        } else if (keyword == "sats") {
            std::int64_t used = 0, in_view = 0;
            if (!number(used) || !number(in_view)) {
                parser.fail("`sats` needs used and in-view counts");
                error = parser.error;
                return false;
            }
            o.satellites_used    = static_cast<std::uint8_t>(used);
            o.satellites_in_view = static_cast<std::uint8_t>(in_view);
        } else if (keyword == "hdop") {
            std::int64_t centi = 0;
            if (!number(centi)) { parser.fail("`hdop` needs dilution x 100"); error = parser.error; return false; }
            o.hdop_centi = static_cast<std::uint16_t>(centi);
        } else if (keyword == "jam" || keyword == "spoof") {
            std::string what;
            ReceiverIndication value = ReceiverIndication::Unknown;
            if (!word(what) || !indication(what, value)) {
                parser.fail("`" + keyword + "` needs unknown unsupported none warning or critical");
                error = parser.error;
                return false;
            }
            if (keyword == "jam") { o.jamming = value; } else { o.spoofing = value; }
        } else if (keyword == "plevel") {
            std::string  valid;
            std::int64_t horizontal = 0, vertical = 0;
            if (!word(valid) || !number(horizontal) || !number(vertical) ||
                (valid != "valid" && valid != "invalid")) {
                parser.fail("`plevel` needs valid|invalid and two millimetre bounds");
                error = parser.error;
                return false;
            }
            ProtectionLevel level;
            level.valid         = valid == "valid";
            level.horizontal_mm = static_cast<std::uint32_t>(horizontal);
            level.vertical_mm   = static_cast<std::uint32_t>(vertical);
            o.protection_level  = level;
        } else if (keyword == "rtime") {
            std::int64_t seconds = 0;
            std::string  valid;
            if (!number(seconds) || !word(valid) || (valid != "valid" && valid != "invalid")) {
                parser.fail("`rtime` needs unix seconds and valid|invalid");
                error = parser.error;
                return false;
            }
            o.receiver_time       = WallTime{seconds};
            o.receiver_time_valid = valid == "valid";
        } else if (keyword == "dtime") {
            std::int64_t seconds = 0;
            if (!number(seconds)) { parser.fail("`dtime` needs unix seconds"); error = parser.error; return false; }
            step.device_time = WallTime{seconds};
        } else if (keyword == "motion") {
            std::string what;
            if (!word(what)) { parser.fail("`motion` needs unknown, or still|moving and a body"); error = parser.error; return false; }
            if (what == "unknown") {
                step.motion = MotionEvidence{};
            } else if (what == "still" || what == "moving") {
                std::string whose;
                SensorBody body = SensorBody::Unknown;
                if (!word(whose) || !sensor_body(whose, body)) {
                    parser.fail("`motion still|moving` needs watch, node or companion"); error = parser.error; return false;
                }
                step.motion = MotionEvidence{body, true, what == "moving"};
            } else { parser.fail("`motion` needs unknown, or still|moving and a body"); error = parser.error; return false; }
        } else if (keyword == "provider") {
            std::string what;
            if (!word(what) || what != "other") {
                parser.fail("`provider` only takes `other`");
                error = parser.error;
                return false;
            }
            step.is_other_provider = true;
            o.source               = PositionSource::NodeGnss;
        } else if (keyword == "expect") {
            std::string what;
            if (!word(what)) { parser.fail("`expect` needs something to expect"); error = parser.error; return false; }
            if (what == "validity") {
                std::string value;
                if (!word(value) || !validity_of(value, step.expect.validity)) {
                    parser.fail("`expect validity` needs nofix stale degraded or valid");
                    error = parser.error;
                    return false;
                }
                step.expect.check_validity = true;
            } else if (what == "trust") {
                std::string value;
                if (!word(value) || !trust_of(value, step.expect.trust)) {
                    parser.fail("`expect trust` needs untrusted degraded or trusted");
                    error = parser.error;
                    return false;
                }
                step.expect.check_trust = true;
            } else if (what == "reason" || what == "no-reason") {
                std::string value;
                TrustReason reason = TrustReason::FixLost;
                if (!word(value) || !reason_of(value, reason)) {
                    parser.fail("unknown trust reason `" + value + "`");
                    error = parser.error;
                    return false;
                }
                if (what == "reason") {
                    step.expect.reasons_present.push_back(reason);
                } else {
                    step.expect.reasons_absent.push_back(reason);
                }
            } else {
                parser.fail("`expect " + what + "` is not something this rig checks");
                error = parser.error;
                return false;
            }
        } else {
            parser.fail("unknown keyword `" + keyword + "`");
            error = parser.error;
            return false;
        }
    }

    if (out.name.empty()) {
        error = "the fixture has no `scenario` line";
        return false;
    }
    if (out.steps.empty()) {
        error = "the fixture has no steps";
        return false;
    }
    return true;
}

Result run(const Scenario& scenario, const TrustPolicy& policy,
           const ValidityPolicy& validity_policy)
{
    Result result;
    result.parsed = true;

    TrustEvaluator evaluator(policy);

    // What a location service would be holding: the most recent observation
    // that actually arrived. A provider's answer never becomes this — it is
    // compared and discarded — and a `hold` step re-reads it without replacing
    // it, which is the entire difference between the two.
    GnssObservation held{};
    bool            have_held = false;

    for (const Step& step : scenario.steps) {
        PositionValidity validity = PositionValidity::NoFix;

        if (step.is_other_provider) {
            // A second provider's answer for the same moment. It is compared,
            // never adopted: disagreement is evidence about both of them and
            // belongs to neither.
            evaluator.compare_provider(step.observation, step.at);
            evaluator.engine().update(step.at);
        } else if (step.is_hold) {
            // The reader rejects a hold with nothing to hold, but run() also
            // takes hand-built scenarios, and a rig that would dereference a
            // default-constructed observation there would report a confident
            // NoFix for a step that never had an input.
            if (!have_held) {
                result.failures.push_back(
                    Failure{step.line, "`hold` with no observation held"});
                ++result.steps_run;
                continue;
            }
            // Nothing arrived. The retained fix is re-judged against a later
            // clock and nothing else changes — in particular the detectors are
            // not re-run against an observation they have already seen.
            validity = classify(held, step.at, validity_policy);
            evaluator.refresh(validity, step.at);
        } else {
            validity = classify(step.observation, step.at, validity_policy);
            evaluator.observe(step.observation, validity, step.motion, step.device_time, step.at);
            held      = step.observation;
            have_held = true;
        }

        ++result.steps_run;

        auto say = [&result, &step](const std::string& what) {
            result.failures.push_back(Failure{step.line, what});
        };

        if (step.expect.check_validity && !step.is_other_provider &&
            validity != step.expect.validity) {
            say(std::string("validity is ") + to_string(validity) + ", expected " +
                to_string(step.expect.validity));
        }
        if (step.expect.check_trust && evaluator.state() != step.expect.trust) {
            say(std::string("trust is ") + to_string(evaluator.state()) + ", expected " +
                to_string(step.expect.trust));
        }
        for (TrustReason reason : step.expect.reasons_present) {
            if (!evaluator.engine().holds(reason)) {
                say(std::string("expected reason ") + to_string(reason) + ", which is not held");
            }
        }
        for (TrustReason reason : step.expect.reasons_absent) {
            if (evaluator.engine().holds(reason)) {
                say(std::string("reason ") + to_string(reason) + " is held, and should not be");
            }
        }
    }

    result.final_state = evaluator.state();
    return result;
}

}  // namespace attadipa::replay
