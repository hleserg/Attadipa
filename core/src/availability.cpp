#include "attadipa/core/availability.h"

namespace attadipa::core {

int remedy_rank(Availability availability)
{
    switch (availability) {
        case Availability::Ready:         return 6;
        case Availability::Off:           return 5;  // turn it on
        case Availability::Failed:        return 4;  // it broke; a restart might help
        case Availability::Unreachable:   return 3;  // move closer
        case Availability::Incompatible:  return 2;  // update something
        case Availability::Unprovisioned: return 1;  // acquire and pair a node
        case Availability::Unsupported:   return 0;  // nothing to be done
    }
    return 0;
}

const char* to_string(Availability availability)
{
    switch (availability) {
        case Availability::Unsupported:   return "unsupported";
        case Availability::Unprovisioned: return "unprovisioned";
        case Availability::Unreachable:   return "unreachable";
        case Availability::Incompatible:  return "incompatible";
        case Availability::Failed:        return "failed";
        case Availability::Off:           return "off";
        case Availability::Ready:         return "ready";
    }
    return "?";
}

const char* to_string(Origin origin)
{
    switch (origin) {
        case Origin::Local: return "local";
        case Origin::Node:  return "node";
    }
    return "?";
}

const char* to_string(Validity validity)
{
    switch (validity) {
        case Validity::Unknown: return "unknown";
        case Validity::Valid:   return "valid";
        case Validity::Stale:   return "stale";
        case Validity::Invalid: return "invalid";
    }
    return "?";
}

}  // namespace attadipa::core
