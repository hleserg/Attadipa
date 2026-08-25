#include "attadipa/platform/board_profile.h"

#include <cstring>

// The board profiles, transcribed from docs/research/HARDWARE_MATRIX.md.
//
// Every entry below is a fact that was traced to a schematic or a vendor BSP
// before it was written here. Where the matrix says PARTIAL or UNKNOWN, this
// file says so in a comment rather than picking the convenient value —
// CLAUDE.md's first rule, and the reason the T-Watch's radio chip defaults to
// Unknown even though the schematic footprint hints at an SX1262.

namespace attadipa::platform {
namespace {

using HF = HardwareFeature;

// LilyGO T-Watch S3 Plus.
//
// Not present, and each absence is load-bearing: no gyroscope (BMA423 is an
// accelerometer), no magnetometer, no SD card.
constexpr std::uint32_t kTWatchFeatures =
    feature_bit(HF::Display) |          // ST7789V3, 240x240 IPS 1.3"
    feature_bit(HF::Touch) |            // FT6336U, on its own I2C bus
    feature_bit(HF::Buttons) |          // BOOT on the GNSS daughterboard; PWR via the PMU
    feature_bit(HF::Pmu) |              // AXP2101
    feature_bit(HF::BatterySense) |     // through the AXP2101
    feature_bit(HF::Rtc) |              // PCF8563, with a rechargeable backup cell
    feature_bit(HF::Accelerometer) |    // BMA423
    feature_bit(HF::Radio) |            // one of five chips — see below
    feature_bit(HF::GnssReceiver) |     // MIA-M10Q or LS550G, on an FPC daughterboard
    feature_bit(HF::HapticActuator) |   // DRV2605L, a real waveform driver
    feature_bit(HF::AudioOutDevice) |   // MAX98357A
    feature_bit(HF::AudioInDevice) |    // SPM1423HM4H-B, PDM
    feature_bit(HF::IrTransmitter) |    // IR12-21C on GPIO 2
    feature_bit(HF::Wifi) |
    feature_bit(HF::Ble) |
    feature_bit(HF::Usb);

// Waveshare ESP32-S3-Touch-AMOLED-2.06.
//
// No radio, no GNSS, no IR. It also has a bare motor rather than a haptic
// driver IC, which is a HapticActuator all the same — the difference belongs to
// the driver, not to the inventory. Buttons are PARTIAL in the matrix (D5): the
// keys are on the board, the GPIO assignment is not resolved, and the vendor
// BSP declares none. Present, therefore, and not yet drivable.
constexpr std::uint32_t kWaveshareFeatures =
    feature_bit(HF::Display) |          // CO5300, 410x502 AMOLED, driven through SH8601
    feature_bit(HF::Touch) |            // FT3168
    feature_bit(HF::Buttons) |          // PARTIAL — see D5
    feature_bit(HF::Pmu) |              // AXP2101 — the one part the two boards share
    feature_bit(HF::BatterySense) |
    feature_bit(HF::Rtc) |              // PCF85063ATL
    feature_bit(HF::Accelerometer) |    // QMI8658, 6-axis
    feature_bit(HF::Gyroscope) |        // the same part, the other axis set
    feature_bit(HF::HapticActuator) |   // bare motor on GPIO 18 — no driver IC
    feature_bit(HF::AudioOutDevice) |   // ES8311 codec
    feature_bit(HF::AudioInDevice) |    // ES7210, two microphones
    feature_bit(HF::SdCard) |           // the slot is on the board; which bus
                                        // mode it is wired for is not settled —
                                        // D14, and no card has enumerated yet
    feature_bit(HF::Wifi) |
    feature_bit(HF::Ble) |
    feature_bit(HF::Usb);

BoardProfile make_twatch()
{
    BoardProfile p;
    p.id                          = "t-watch-s3-plus";
    p.name                        = "LilyGO T-Watch S3 Plus";
    p.display.width_px            = 240;
    p.display.height_px           = 240;
    // CONFLICTING, and this is the conservative half of the conflict rather
    // than the confident one — OPEN_QUESTIONS D15. LilyGoLib's spec tables say
    // 1.3" for the S3 and the S3 Plus by name; the schematic's LCD sheet says
    // QT154C2408 / LCD_1.54-TOUCH, and that vendor's sibling part QT154H2201 is
    // published as 1.54", 240x240, ST7789V, so the "154" field decodes. No
    // document both names the Plus and shows the panel.
    //
    // 1300 is kept because it yields the *higher* dpi (261 against 220), and a
    // physical minimum converted at the higher dpi produces more pixels. If the
    // panel turns out to be 1.54", every touch target is physically larger than
    // designed. The other way round it would be smaller, and a too-small touch
    // target is the failure that reaches a wrist.
    p.display.diagonal_milli_inch = 1300;
    p.display.technology          = PanelTechnology::Ips;
    p.present_mask                = kTWatchFeatures;
    // Unknown, and deliberately so -- but no longer because nobody has said.
    // A2 was ANSWERED on 2026-08-22 (OWNER_DECISIONS.md, A1-A3, issue #54 --
    // by question, not by OD number; see radio_info.cpp): the order
    // listing names SX1262 at 868 MHz. A listing is a seller's claim, not a
    // marking read off the part, and this value is what the firmware bets a
    // radio on, so it stays Unknown until the watch is in hand and the marking
    // is read -- ADR-0003's rule, not a doubt about the owner. Every capability
    // derived from the radio therefore comes out "we cannot say", which is
    // still the truth. Override it with --radio in the simulator.
    p.radio = radio_info_for(RadioChip::Unknown);

    // Traced, not recalled: HARDWARE_MATRIX ':112'. PWR is SW7 and wires to the
    // AXP2101's `PWRON` pin -- it **never reaches a GPIO**, so every press
    // arrives as a PMU interrupt. BOOT and RST both sit on the GNSS
    // daughterboard and reach the main board over the FPC, which is why a unit
    // without that daughterboard has no reset button and no way into download
    // mode at all.
    //
    // RST is not listed. It resets the SoC; there is no software event to
    // deliver and nothing for a debug channel to simulate. BOOT is listed and
    // marked not injectable for the same reason in weaker form: it is a
    // boot-mode strap read at reset, not an interface control.
    p.buttons[0] = ButtonSpec{"power", /*role_known=*/true, /*injectable=*/true};
    p.buttons[1] = ButtonSpec{"boot", /*role_known=*/true, /*injectable=*/false};
    p.button_count = 2;
    return p;
}

BoardProfile make_waveshare()
{
    BoardProfile p;
    p.id                          = "waveshare-amoled-206";
    p.name                        = "Waveshare ESP32-S3-Touch-AMOLED-2.06";
    p.display.width_px            = 410;
    p.display.height_px           = 502;
    p.display.diagonal_milli_inch = 2060;
    p.display.technology          = PanelTechnology::Amoled;
    // The known-working board path swaps RGB565 before the upstream driver,
    // which transmits the framebuffer verbatim. T-166 verifies this on-panel.
    p.display.rgb565_swap_bytes    = true;
    p.present_mask                = kWaveshareFeatures;
    p.radio                       = {};  // no radio is fitted; the struct is meaningless

    // The current official schematic and product page name the two case keys
    // PWR and BOOT. PWR reaches the AXP2101 PWRON input; its edge status comes
    // from the PMU, while SYS_OUT/GPIO10 is the resulting power state rather
    // than the key level. BOOT pulls GPIO0 low. The owner physically counted
    // the same two case buttons, so D5's earlier ambiguity came from the old
    // text extraction, not the drawing. BOOT stays restrictive because it is
    // also a boot-mode strap.
    p.buttons[0] = ButtonSpec{"power", /*role_known=*/true, /*injectable=*/true};
    p.buttons[1] = ButtonSpec{"boot", /*role_known=*/true, /*injectable=*/false};
    p.button_count = 2;
    return p;
}

// Function-local statics rather than a namespace-scope array: these are built
// by calling radio_info_for(), so they are dynamically initialised, and a
// namespace-scope array would be readable before its initialiser ran if
// anything else's static initialiser reached it first.
const BoardProfile* profiles(std::uint8_t& count_out)
{
    static const BoardProfile kProfiles[] = {make_twatch(), make_waveshare()};
    count_out = static_cast<std::uint8_t>(sizeof(kProfiles) / sizeof(kProfiles[0]));
    return kProfiles;
}

}  // namespace

const BoardProfile* board_profiles(std::uint8_t& count_out)
{
    return profiles(count_out);
}

const BoardProfile* find_board_profile(const char* id)
{
    if (id == nullptr) {
        return nullptr;
    }

    std::uint8_t count = 0;
    const BoardProfile* all = profiles(count);
    for (std::uint8_t i = 0; i < count; ++i) {
        if (std::strcmp(all[i].id, id) == 0) {
            return &all[i];
        }
    }
    return nullptr;
}

}  // namespace attadipa::platform
