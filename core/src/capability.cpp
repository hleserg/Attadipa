#include "firefly/core/capability.h"

namespace firefly::core {

// Log and diagnostics strings. User-facing capability names go through tr()
// and exist in both catalogues — docs/adr/0010-localization.md. The core does
// not speak English at users.
const char* to_string(Capability capability)
{
    switch (capability) {
        case Capability::Time:              return "Time";
        case Capability::Position:          return "Position";
        case Capability::Heading:           return "Heading";
        case Capability::MotionSensing:     return "MotionSensing";
        case Capability::MeshMessaging:     return "MeshMessaging";
        case Capability::Haptics:           return "Haptics";
        case Capability::AudioPlayback:     return "AudioPlayback";
        case Capability::AudioCapture:      return "AudioCapture";
        case Capability::NotificationRelay: return "NotificationRelay";
        case Capability::InfraredBlast:     return "InfraredBlast";
        case Capability::PersistentStorage: return "PersistentStorage";
        case Capability::RemovableStorage:  return "RemovableStorage";
        case Capability::CompanionLink:     return "CompanionLink";
    }
    return "?";
}

}  // namespace firefly::core
