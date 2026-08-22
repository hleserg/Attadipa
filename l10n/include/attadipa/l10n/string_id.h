// GENERATED FILE -- do not edit.
//
// Written by tools/l10n/gen_strings.py from l10n/strings.toml. Edit the TOML
// and regenerate; a stale copy of this file is a failing test
// (`ctest -R l10n_generated_is_current`), not a silent divergence.

#ifndef ATTADIPA_L10N_STRING_ID_H
#define ATTADIPA_L10N_STRING_ID_H

#include <cstdint>

namespace attadipa::l10n {

// Every user-facing string, as an identifier. UI code holds one of these and
// never the text -- ADR-0010 §1. That is what makes the coverage check static:
// an enumerator can be counted at build time and a string literal cannot.
enum class StringId : std::uint16_t {
    AvailabilityFailed = 0,
    AvailabilityIncompatible = 1,
    AvailabilityOff = 2,
    AvailabilityReady = 3,
    AvailabilityUnprovisioned = 4,
    AvailabilityUnreachable = 5,
    AvailabilityUnsupported = 6,
    CapabilityAudioIn = 7,
    CapabilityAudioOut = 8,
    CapabilityCompanion = 9,
    CapabilityHaptics = 10,
    CapabilityHeading = 11,
    CapabilityInfrared = 12,
    CapabilityMesh = 13,
    CapabilityMotion = 14,
    CapabilityNotifications = 15,
    CapabilityPosition = 16,
    CapabilityRemovable = 17,
    CapabilityStorage = 18,
    CapabilityTime = 19,
    DiagnosticCapabilities = 20,
    DiagnosticGeometry = 21,
    DiagnosticHardware = 22,
    DiagnosticRadio = 23,
    DiagnosticRadioChip = 24,
    DiagnosticRadioLora = 25,
    DiagnosticRadioMeshcore = 26,
    HardwareAccelerometer = 27,
    HardwareAudioIn = 28,
    HardwareAudioOut = 29,
    HardwareBatterySense = 30,
    HardwareBle = 31,
    HardwareButtons = 32,
    HardwareDisplay = 33,
    HardwareGnss = 34,
    HardwareGyroscope = 35,
    HardwareHaptic = 36,
    HardwareInfrared = 37,
    HardwareMagnetometer = 38,
    HardwarePmu = 39,
    HardwareRadio = 40,
    HardwareRtc = 41,
    HardwareSdCard = 42,
    HardwareStateAbsent = 43,
    HardwareStateFailed = 44,
    HardwareStateInitialising = 45,
    HardwareStateRailOff = 46,
    HardwareStateReady = 47,
    HardwareStateUntouched = 48,
    HardwareTouch = 49,
    HardwareUsb = 50,
    HardwareWifi = 51,
    Language = 52,
    LanguageEnglish = 53,
    LanguageRussian = 54,
    MeshcoreImpossible = 55,
    MeshcoreNeedsWork = 56,
    MeshcoreSupported = 57,
    MeshcoreUntested = 58,
    No = 59,
    ProductName = 60,
    RadioChipUnknown = 61,
    Yes = 62,
};
inline constexpr std::uint16_t kStringIdCount = 63;

// Counted strings are a separate type on purpose. `tr(StringId)` on an entry
// that needs a number, or `tr_plural` on one that does not, is then a compile
// error instead of a string with a stray %u in it.
enum class PluralId : std::uint16_t {
    DiagnosticCapabilityCount = 0,
};
inline constexpr std::uint16_t kPluralIdCount = 1;

// The identifier's own spelling, for logs and for the loud missing-string
// path. Never for display: it is not a translation of anything.
const char* string_id_name(StringId id);
const char* plural_id_name(PluralId id);

}  // namespace attadipa::l10n

#endif  // ATTADIPA_L10N_STRING_ID_H
