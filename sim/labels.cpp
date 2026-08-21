#include "labels.h"

#include "attadipa/l10n/tr.h"

namespace attadipa::sim {

using core::Availability;
using core::Capability;
using l10n::StringId;
using platform::HardwareFeature;
using platform::HardwareState;
using platform::MeshCoreSupport;
using platform::RadioChip;

// Every switch below is exhaustive and has no `default:`, deliberately. Adding
// an enumerator to `Capability` should stop the build here rather than draw a
// blank row on a watch, which is what a `default: return StringId::Unknown`
// would have done quietly.

StringId label_of(Capability capability)
{
    switch (capability) {
        case Capability::Time:              return StringId::CapabilityTime;
        case Capability::Position:          return StringId::CapabilityPosition;
        case Capability::Heading:           return StringId::CapabilityHeading;
        case Capability::MotionSensing:     return StringId::CapabilityMotion;
        case Capability::MeshMessaging:     return StringId::CapabilityMesh;
        case Capability::Haptics:           return StringId::CapabilityHaptics;
        case Capability::AudioPlayback:     return StringId::CapabilityAudioOut;
        case Capability::AudioCapture:      return StringId::CapabilityAudioIn;
        case Capability::NotificationRelay: return StringId::CapabilityNotifications;
        case Capability::InfraredBlast:     return StringId::CapabilityInfrared;
        case Capability::PersistentStorage: return StringId::CapabilityStorage;
        case Capability::RemovableStorage:  return StringId::CapabilityRemovable;
        case Capability::CompanionLink:     return StringId::CapabilityCompanion;
    }
    return StringId::CapabilityTime;
}

StringId label_of(Availability availability)
{
    switch (availability) {
        case Availability::Unsupported:   return StringId::AvailabilityUnsupported;
        case Availability::Unprovisioned: return StringId::AvailabilityUnprovisioned;
        case Availability::Unreachable:   return StringId::AvailabilityUnreachable;
        case Availability::Incompatible:  return StringId::AvailabilityIncompatible;
        case Availability::Failed:        return StringId::AvailabilityFailed;
        case Availability::Off:           return StringId::AvailabilityOff;
        case Availability::Ready:         return StringId::AvailabilityReady;
    }
    return StringId::AvailabilityUnsupported;
}

StringId label_of(HardwareFeature feature)
{
    switch (feature) {
        case HardwareFeature::Display:            return StringId::HardwareDisplay;
        case HardwareFeature::Touch:              return StringId::HardwareTouch;
        case HardwareFeature::Buttons:            return StringId::HardwareButtons;
        case HardwareFeature::Pmu:                return StringId::HardwarePmu;
        case HardwareFeature::BatterySense:       return StringId::HardwareBatterySense;
        case HardwareFeature::Rtc:                return StringId::HardwareRtc;
        case HardwareFeature::Accelerometer:      return StringId::HardwareAccelerometer;
        case HardwareFeature::Gyroscope:          return StringId::HardwareGyroscope;
        case HardwareFeature::MagnetometerSensor: return StringId::HardwareMagnetometer;
        case HardwareFeature::Radio:              return StringId::HardwareRadio;
        case HardwareFeature::GnssReceiver:       return StringId::HardwareGnss;
        case HardwareFeature::HapticActuator:     return StringId::HardwareHaptic;
        case HardwareFeature::AudioOutDevice:     return StringId::HardwareAudioOut;
        case HardwareFeature::AudioInDevice:      return StringId::HardwareAudioIn;
        case HardwareFeature::IrTransmitter:      return StringId::HardwareInfrared;
        case HardwareFeature::SdCard:             return StringId::HardwareSdCard;
        case HardwareFeature::Wifi:               return StringId::HardwareWifi;
        case HardwareFeature::Ble:                return StringId::HardwareBle;
        case HardwareFeature::Usb:                return StringId::HardwareUsb;
    }
    return StringId::HardwareDisplay;
}

StringId label_of(HardwareState state)
{
    switch (state) {
        case HardwareState::Absent:       return StringId::HardwareStateAbsent;
        case HardwareState::Untouched:    return StringId::HardwareStateUntouched;
        case HardwareState::RailOff:      return StringId::HardwareStateRailOff;
        case HardwareState::Initialising: return StringId::HardwareStateInitialising;
        case HardwareState::Failed:       return StringId::HardwareStateFailed;
        case HardwareState::Ready:        return StringId::HardwareStateReady;
    }
    return StringId::HardwareStateAbsent;
}

StringId label_of(MeshCoreSupport support)
{
    switch (support) {
        case MeshCoreSupport::Untested:   return StringId::MeshcoreUntested;
        case MeshCoreSupport::Supported:  return StringId::MeshcoreSupported;
        case MeshCoreSupport::NeedsWork:  return StringId::MeshcoreNeedsWork;
        case MeshCoreSupport::Impossible: return StringId::MeshcoreImpossible;
    }
    return StringId::MeshcoreUntested;
}

const char* chip_name(RadioChip chip)
{
    return chip == RadioChip::Unknown ? nullptr : platform::to_string(chip);
}

}  // namespace attadipa::sim
