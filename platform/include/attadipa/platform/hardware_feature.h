#pragma once

#include <cstdint>

// The hardware inventory layer: what is physically on this board.
//
// This header answers "is the part here" and "is its driver up". It never
// answers "can the user do X" — that is attadipa/core/capability.h, a different
// vocabulary for a different audience. See docs/adr/0007-two-capability-layers.md.
//
// Nothing under apps/ links the library that owns this header. That is not a
// convention; it is the link line, and an application that includes this file
// fails to build.

namespace attadipa::platform {

// One entry per part, and per sensing axis rather than per package: a BMA423 is
// an Accelerometer and nothing else, while a QMI8658 is an Accelerometer and a
// Gyroscope. Enumerating axes is what lets "the T-Watch cannot report rotation"
// be a fact in the type system rather than a comment.
//
// The names carry a deliberate suffix where a product capability of similar
// name exists: MagnetometerSensor, AudioOutDevice. Heading and AudioPlayback
// are the product-side words, and the compiler should reject anyone who reaches
// for the wrong one.
enum class HardwareFeature : std::uint8_t {
    Display,
    Touch,
    Buttons,

    Pmu,
    BatterySense,
    Rtc,

    Accelerometer,
    Gyroscope,
    MagnetometerSensor,  // neither shipping board has one; the seat exists anyway

    Radio,               // a radio. Whether it can do LoRa is a fact about it — ADR-0003
    GnssReceiver,

    HapticActuator,
    AudioOutDevice,
    AudioInDevice,

    IrTransmitter,
    SdCard,

    Wifi,
    Ble,
    Usb,
};

inline constexpr std::uint8_t kHardwareFeatureCount =
    static_cast<std::uint8_t>(HardwareFeature::Usb) + 1;

// What the driver for a present part is doing right now.
//
// The interesting value is Untouched. Final §32 and CLAUDE.md's ownership rule
// say the same thing: owning a part does not mean initialising it. A rail left
// off, a pin left high-Z and a bus never probed are legitimate owned states,
// and they must not read as failures. Absent and Failed already had names;
// "we own it and chose to leave it alone" did not, so it gets one here.
enum class HardwareState : std::uint8_t {
    Absent,        // present() == false. There is no driver to have a state.
    Untouched,     // owned, deliberately not brought up. Not an error.
    RailOff,       // the supply that feeds it is down. Can be brought up.
    Initialising,  // bring-up in progress
    Failed,        // bring-up was attempted and did not succeed
    Ready,         // usable now
};

const char* to_string(HardwareFeature feature);
const char* to_string(HardwareState state);

}  // namespace attadipa::platform
