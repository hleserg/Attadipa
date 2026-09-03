#include "attadipa/core/location_service.h"

#include <cstdio>

namespace attadipa::core {

namespace {

bool same_position(Position left, Position right)
{
    return left.latitude_e7 == right.latitude_e7 &&
           left.longitude_e7 == right.longitude_e7;
}

// Degrees × 10^7 as a signed decimal, without floating point. The widening
// before the negation is not decoration: `-2147483648` has no positive
// counterpart in `std::int32_t`, and negating it in place is undefined
// behaviour that the sanitiser build would find at the worst possible moment --
// a hostile or corrupt coordinate is exactly the input that reaches here.
void format_e7(std::int32_t value, char* out, std::size_t size)
{
    const bool negative = value < 0;
    const std::int64_t magnitude =
        negative ? -static_cast<std::int64_t>(value) : static_cast<std::int64_t>(value);
    std::snprintf(out, size, "%s%lld.%07lld", negative ? "-" : "",
                  static_cast<long long>(magnitude / 10000000),
                  static_cast<long long>(magnitude % 10000000));
}

// Enough of the origin key to tell two nodes apart on a diagnostics line, and
// not so much that the line stops fitting anywhere. Four bytes is the same
// prefix the Companion protocol itself routes contacts by.
constexpr std::size_t kOriginPrefixBytes = 4;

void format_origin(const MeshPeerId& origin, char* out, std::size_t size)
{
    static const char kHex[] = "0123456789abcdef";
    std::size_t written = 0;
    for (std::size_t i = 0; i < kOriginPrefixBytes && written + 2 < size; ++i) {
        out[written++] = kHex[(origin.public_key[i] >> 4) & 0x0F];
        out[written++] = kHex[origin.public_key[i] & 0x0F];
    }
    if (written < size) out[written] = '\0';
}

}  // namespace

void LocationService::poll()
{
    availability_ = provider_.availability();

    PositionSample sample;
    if (!provider_.sample(sample)) {
        // Nothing new. The retained observation stays and its age keeps
        // growing: a link that went away is not a source saying "no position",
        // and clearing here would put words in its mouth. `NoFix` is already
        // the verdict, so there is nothing to downgrade either.
        //
        // THE RECEIVER STATE IS NOT RETAINED WITH IT, AND THE DIFFERENCE IS THE
        // WHOLE POINT OF KEEPING THEM APART. An observation is a thing the node
        // said at a stamped moment, and it stays true about that moment however
        // old it gets. "The receiver is running" is not about a moment; it is a
        // claim about the node *now*, and a provider with no sample is a
        // provider that cannot make one. Left alone it read `recv running`
        // beside `avail unreachable` for a node that had been gone for an hour,
        // and flipped back to `unknown` on the next reconnect without the
        // coordinate ever changing -- the same fact asserted, retracted and
        // re-asserted by nothing but the link.
        receiver_ = ReceiverPresence::Unknown;
        return;
    }

    receiver_ = sample.receiver;

    const bool origin_changed =
        has_origin_ != sample.has_origin ||
        (sample.has_origin && !(origin_ == sample.origin));
    if (origin_changed) {
        // A different key is a different node, not the same node somewhere
        // else. Re-attributing the retained coordinate would invent a fact
        // neither node stated.
        observation_.reset();
    }

    if (observation_.has_value() && observation_->position.has_value() &&
        sample.observation.position.has_value() &&
        same_position(*observation_->position, *sample.observation.position)) {
        // The same coordinate again. That is evidence *against* a live fix --
        // the node's own write gate leaves the last value in place when its
        // receiver stops solving -- so neither age is refreshed and the
        // observation keeps the stamp it arrived with, across a reconnect
        // included. The receiver state above is still updated: it is a separate
        // fact and it can genuinely change while the coordinate does not.
        return;
    }

    observation_ = sample.observation;
    origin_      = sample.origin;
    has_origin_  = sample.has_origin;
}

void LocationService::forget()
{
    observation_.reset();
    origin_     = MeshPeerId{};
    has_origin_ = false;
    // Not "the receiver is off" -- that would be a claim about a node this
    // watch no longer talks to. `Unknown` is the absence of a claim, which is
    // the only honest thing to hold about a source that has been repudiated.
    receiver_ = ReceiverPresence::Unknown;
}

LocationState LocationService::state(MonotonicTime now) const
{
    LocationState out;
    out.availability = availability_;
    out.receiver     = receiver_;
    out.origin       = origin_;
    out.has_origin   = has_origin_;
    // `trust` is left empty on purpose: no evaluation has run, and an optional
    // with no value is how this tree says so (diagnostics.h, `GnssStatus`).

    if (!observation_.has_value()) return out;

    out.source   = observation_->source;
    out.fix_type = observation_->fix_type;
    // The one classifier this repository has, judging a node coordinate the
    // same way it judges a local one. Nothing here second-guesses it.
    out.validity = classify(*observation_, now, policy_);

    if (observation_->position.has_value()) {
        out.has_position          = true;
        out.position.value        = *observation_->position;
        out.position.age_at_us_ms = elapsed(observation_->observed_at, now).value;
        // `age_at_source_ms` is deliberately left at its default and is not a
        // measurement. `Validity::Unknown` is the field that says so, and a
        // consumer that reads the age without reading this first has the bug
        // the tests were written to catch.
        out.position.validity     = Validity::Unknown;
    }
    return out;
}

std::optional<Millis> LocationService::age_at_us(MonotonicTime now) const
{
    if (!observation_.has_value() || !observation_->position.has_value()) {
        return std::nullopt;
    }
    return elapsed(observation_->observed_at, now);
}

std::optional<Millis> LocationService::age_at_source(MonotonicTime) const
{
    // Always empty, and it is not a stub. No producer this slice can build
    // states when its coordinate was observed, so there is no duration to
    // report; returning `Millis{0}` would be the false measurement the whole
    // design refuses.
    return std::nullopt;
}

const char* to_string(ReceiverPresence presence)
{
    switch (presence) {
    case ReceiverPresence::Unknown:     return "unknown";
    case ReceiverPresence::NotDetected: return "not detected";
    case ReceiverPresence::PoweredOff:  return "off";
    case ReceiverPresence::Running:     return "running";
    }
    return "unknown";
}

std::size_t format_location_line(const LocationState& state, char* out,
                                 std::size_t size)
{
    if (out == nullptr || size == 0) return 0;

    char latitude[16]  = "UNKNOWN";
    char longitude[16] = "UNKNOWN";
    char age[16]       = "UNKNOWN";
    char origin[2 * kOriginPrefixBytes + 1] = "UNKNOWN";

    if (state.has_position) {
        format_e7(state.position.value.latitude_e7, latitude, sizeof(latitude));
        format_e7(state.position.value.longitude_e7, longitude, sizeof(longitude));
        std::snprintf(age, sizeof(age), "%lums",
                      static_cast<unsigned long>(state.position.age_at_us_ms));
    }
    if (state.has_origin) {
        format_origin(state.origin, origin, sizeof(origin));
    }

    // `age_src` is the literal word and never a number: see rule 3 in the
    // header. It is printed beside the measured age rather than omitted,
    // because a line that shows one age and not the other reads as though the
    // one shown were both.
    const int written = std::snprintf(
        out, size,
        "pos %s,%s src %s fix %s validity %s age_us %s age_src UNKNOWN "
        "recv %s avail %s trust %s node %s",
        latitude, longitude, to_string(state.source), to_string(state.fix_type),
        to_string(state.validity), age, to_string(state.receiver),
        to_string(state.availability), to_string(state.trust), origin);
    return written < 0 ? 0 : static_cast<std::size_t>(written);
}

}  // namespace attadipa::core
