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
    ClockNoTouch = 32,
    ClockTimeInvalid = 33,
    ClockTimeStale = 34,
    ClockWeekdayFri = 35,
    ClockWeekdayMon = 36,
    ClockWeekdaySat = 37,
    ClockWeekdaySun = 38,
    ClockWeekdayThu = 39,
    ClockWeekdayTue = 40,
    ClockWeekdayWed = 41,
    DiagnosticCapabilities = 42,
    DiagnosticGeometry = 43,
    DiagnosticHardware = 44,
    DiagnosticRadio = 45,
    DiagnosticRadioChip = 46,
    DiagnosticRadioLora = 47,
    DiagnosticRadioMeshcore = 48,
    HardwareAccelerometer = 49,
    HardwareAudioIn = 50,
    HardwareAudioOut = 51,
    HardwareBatterySense = 52,
    HardwareBle = 53,
    HardwareButtons = 54,
    HardwareDisplay = 55,
    HardwareGnss = 56,
    HardwareGyroscope = 57,
    HardwareHaptic = 58,
    HardwareInfrared = 59,
    HardwareMagnetometer = 60,
    HardwarePmu = 61,
    HardwareRadio = 62,
    HardwareRtc = 63,
    HardwareSdCard = 64,
    HardwareStateAbsent = 65,
    HardwareStateFailed = 66,
    HardwareStateInitialising = 67,
    HardwareStateRailOff = 68,
    HardwareStateReady = 69,
    HardwareStateUntouched = 70,
    HardwareTouch = 71,
    HardwareUsb = 72,
    HardwareWifi = 73,
    Language = 74,
    LanguageEnglish = 75,
    LanguageRussian = 76,
    MeshcoreImpossible = 77,
    MeshcoreNeedsWork = 78,
    MeshcoreSupported = 79,
    MeshcoreUntested = 80,
    NavAgeHours = 81,
    NavAgeMinutes = 82,
    NavAgeSeconds = 83,
    NavCardinalE = 84,
    NavCardinalN = 85,
    NavCardinalNe = 86,
    NavCardinalNw = 87,
    NavCardinalS = 88,
    NavCardinalSe = 89,
    NavCardinalSw = 90,
    NavCardinalW = 91,
    NavCaveatNoProvider = 92,
    NavCaveatNoReceiver = 93,
    NavCaveatNodeUnverified = 94,
    NavDistanceKm = 95,
    NavDistanceKmTenths = 96,
    NavDistanceM = 97,
    NavDistanceSaturated = 98,
    NavNoFix = 99,
    NavNodePositionStale = 100,
    NavNodePositionUnknown = 101,
    NavNodeUnavailable = 102,
    NavOwnPositionDegraded = 103,
    NavOwnPositionStale = 104,
    NavOwnReceiverSilent = 105,
    NavReady = 106,
    NavTitle = 107,
    NavWaitingForGps = 108,
    No = 109,
    ProductName = 110,
    ProvisionCancelled = 111,
    ProvisionDone = 112,
    ProvisionFailed = 113,
    ProvisionForgottenSkipped = 114,
    ProvisionHintDate = 115,
    ProvisionHintNode = 116,
    ProvisionHintOffset = 117,
    ProvisionHintPasskey = 118,
    ProvisionHintTime = 119,
    ProvisionKeyCancel = 120,
    ProvisionKeyErase = 121,
    ProvisionKeyForget = 122,
    ProvisionKeyOk = 123,
    ProvisionNodeForgotten = 124,
    ProvisionNodeForgottenRam = 125,
    ProvisionNodeKept = 126,
    ProvisionNodeNothing = 127,
    ProvisionNodePending = 128,
    ProvisionNodeReplayInhibited = 129,
    ProvisionPending = 130,
    ProvisionRejected = 131,
    ProvisionSkipped = 132,
    ProvisionTitleDate = 133,
    ProvisionTitleDone = 134,
    ProvisionTitleNode = 135,
    ProvisionTitleOffset = 136,
    ProvisionTitlePasskey = 137,
    ProvisionTitleTime = 138,
    RadioChipUnknown = 139,
    Yes = 140,
};
inline constexpr std::uint16_t kStringIdCount = 141;

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
