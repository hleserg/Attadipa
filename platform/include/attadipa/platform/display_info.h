#pragma once

#include <cstdint>

namespace attadipa::platform {

// Panel technology, because it changes what a designer may do. On AMOLED a
// black pixel costs no power and on IPS it costs the same as a white one, so
// "dark theme saves battery" is true on one board and false on the other. The
// day/night work needs to be able to ask.
enum class PanelTechnology : std::uint8_t { Unknown, Ips, Amoled };

struct DisplayInfo {
    std::uint16_t   width_px            = 0;
    std::uint16_t   height_px           = 0;
    // Thousandths of an inch, so that 2.06" is 2060 and no float appears in a
    // header that embedded code includes. Diagonal, as panels are specified.
    std::uint16_t   diagonal_milli_inch = 0;
    PanelTechnology technology          = PanelTechnology::Unknown;
    // Board-level transfer fact, consumed once by the LVGL flush port.
    bool            rgb565_swap_bytes    = false;

    // Pixels per inch, rounded to the nearest integer.
    //
    // DERIVED, not measured: computed from the pixel count and the vendor's
    // quoted diagonal. It is good enough to make 8 px mean roughly the same
    // physical distance on a 1.54" and a 2.06" panel, which is what the spacing
    // tokens need (T-009). It is not good enough to be quoted as a spec.
    std::uint16_t dpi() const;
};

}  // namespace attadipa::platform
