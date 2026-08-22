#include "attadipa/platform/hardware_feature.h"

namespace attadipa::platform {

const char* to_string(HardwareFeature feature)
{
    switch (feature) {
        case HardwareFeature::Display:            return "Display";
        case HardwareFeature::Touch:              return "Touch";
        case HardwareFeature::Buttons:            return "Buttons";
        case HardwareFeature::Pmu:                return "Pmu";
        case HardwareFeature::BatterySense:       return "BatterySense";
        case HardwareFeature::Rtc:                return "Rtc";
        case HardwareFeature::Accelerometer:      return "Accelerometer";
        case HardwareFeature::Gyroscope:          return "Gyroscope";
        case HardwareFeature::MagnetometerSensor: return "MagnetometerSensor";
        case HardwareFeature::Radio:              return "Radio";
        case HardwareFeature::GnssReceiver:       return "GnssReceiver";
        case HardwareFeature::HapticActuator:     return "HapticActuator";
        case HardwareFeature::AudioOutDevice:     return "AudioOutDevice";
        case HardwareFeature::AudioInDevice:      return "AudioInDevice";
        case HardwareFeature::IrTransmitter:      return "IrTransmitter";
        case HardwareFeature::SdCard:             return "SdCard";
        case HardwareFeature::Wifi:               return "Wifi";
        case HardwareFeature::Ble:                return "Ble";
        case HardwareFeature::Usb:                return "Usb";
    }
    return "?";
}

// These strings are for logs and the diagnostics screen, not for users. User-
// facing text goes through tr() and exists in both catalogues
// (docs/adr/0010-localization.md); a part name is neither translated nor
// translatable.
const char* to_string(HardwareState state)
{
    switch (state) {
        case HardwareState::Absent:       return "absent";
        case HardwareState::Untouched:    return "untouched";
        case HardwareState::RailOff:      return "rail off";
        case HardwareState::Initialising: return "initialising";
        case HardwareState::Failed:       return "failed";
        case HardwareState::Ready:        return "ready";
    }
    return "?";
}

}  // namespace attadipa::platform
