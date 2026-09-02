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
    ClockMonthApr = 20,
    ClockMonthAug = 21,
    ClockMonthDec = 22,
    ClockMonthFeb = 23,
    ClockMonthJan = 24,
    ClockMonthJul = 25,
    ClockMonthJun = 26,
    ClockMonthMar = 27,
    ClockMonthMay = 28,
    ClockMonthNov = 29,
    ClockMonthOct = 30,
    ClockMonthSep = 31,
    ClockTimeInvalid = 32,
    ClockTimeStale = 33,
    ClockWeekdayFri = 34,
    ClockWeekdayMon = 35,
    ClockWeekdaySat = 36,
    ClockWeekdaySun = 37,
    ClockWeekdayThu = 38,
    ClockWeekdayTue = 39,
    ClockWeekdayWed = 40,
    DiagnosticCapabilities = 41,
    DiagnosticGeometry = 42,
    DiagnosticHardware = 43,
    DiagnosticRadio = 44,
    DiagnosticRadioChip = 45,
    DiagnosticRadioLora = 46,
    DiagnosticRadioMeshcore = 47,
    HardwareAccelerometer = 48,
    HardwareAudioIn = 49,
    HardwareAudioOut = 50,
    HardwareBatterySense = 51,
    HardwareBle = 52,
    HardwareButtons = 53,
    HardwareDisplay = 54,
    HardwareGnss = 55,
    HardwareGyroscope = 56,
    HardwareHaptic = 57,
    HardwareInfrared = 58,
    HardwareMagnetometer = 59,
    HardwarePmu = 60,
    HardwareRadio = 61,
    HardwareRtc = 62,
    HardwareSdCard = 63,
    HardwareStateAbsent = 64,
    HardwareStateFailed = 65,
    HardwareStateInitialising = 66,
    HardwareStateRailOff = 67,
    HardwareStateReady = 68,
    HardwareStateUntouched = 69,
    HardwareTouch = 70,
    HardwareUsb = 71,
    HardwareWifi = 72,
    Language = 73,
    LanguageEnglish = 74,
    LanguageRussian = 75,
    MeshcoreImpossible = 76,
    MeshcoreNeedsWork = 77,
    MeshcoreSupported = 78,
    MeshcoreUntested = 79,
    No = 80,
    ProductName = 81,
    ProvisionCancelled = 82,
    ProvisionDone = 83,
    ProvisionFailed = 84,
    ProvisionHintDate = 85,
    ProvisionHintOffset = 86,
    ProvisionHintPasskey = 87,
    ProvisionHintTime = 88,
    ProvisionKeyCancel = 89,
    ProvisionKeyErase = 90,
    ProvisionKeyOk = 91,
    ProvisionRejected = 92,
    ProvisionSkipped = 93,
    ProvisionTitleDate = 94,
    ProvisionTitleDone = 95,
    ProvisionTitleOffset = 96,
    ProvisionTitlePasskey = 97,
    ProvisionTitleTime = 98,
    RadioChipUnknown = 99,
    Yes = 100,
};
inline constexpr std::uint16_t kStringIdCount = 101;

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
