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
    ClockBattery = 20,
    ClockCharging = 21,
    ClockChildGreeting = 22,
    ClockDate = 23,
    ClockNoTime = 24,
    DiagnosticCapabilities = 25,
    DiagnosticGeometry = 26,
    DiagnosticHardware = 27,
    DiagnosticRadio = 28,
    DiagnosticRadioChip = 29,
    DiagnosticRadioLora = 30,
    DiagnosticRadioMeshcore = 31,
    HardwareAccelerometer = 32,
    HardwareAudioIn = 33,
    HardwareAudioOut = 34,
    HardwareBatterySense = 35,
    HardwareBle = 36,
    HardwareButtons = 37,
    HardwareDisplay = 38,
    HardwareGnss = 39,
    HardwareGyroscope = 40,
    HardwareHaptic = 41,
    HardwareInfrared = 42,
    HardwareMagnetometer = 43,
    HardwarePmu = 44,
    HardwareRadio = 45,
    HardwareRtc = 46,
    HardwareSdCard = 47,
    HardwareStateAbsent = 48,
    HardwareStateFailed = 49,
    HardwareStateInitialising = 50,
    HardwareStateRailOff = 51,
    HardwareStateReady = 52,
    HardwareStateUntouched = 53,
    HardwareTouch = 54,
    HardwareUsb = 55,
    HardwareWifi = 56,
    Language = 57,
    LanguageEnglish = 58,
    LanguageRussian = 59,
    MeshcoreImpossible = 60,
    MeshcoreNeedsWork = 61,
    MeshcoreSupported = 62,
    MeshcoreUntested = 63,
    MonthApr = 64,
    MonthAug = 65,
    MonthDec = 66,
    MonthFeb = 67,
    MonthJan = 68,
    MonthJul = 69,
    MonthJun = 70,
    MonthMar = 71,
    MonthMay = 72,
    MonthNov = 73,
    MonthOct = 74,
    MonthSep = 75,
    No = 76,
    ProductName = 77,
    RadioChipUnknown = 78,
    WeekdayFri = 79,
    WeekdayMon = 80,
    WeekdaySat = 81,
    WeekdaySun = 82,
    WeekdayThu = 83,
    WeekdayTue = 84,
    WeekdayWed = 85,
    Yes = 86,
};
inline constexpr std::uint16_t kStringIdCount = 87;

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
