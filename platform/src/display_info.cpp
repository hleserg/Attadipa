#include "firefly/platform/display_info.h"

#include <cmath>

namespace firefly::platform {

std::uint16_t DisplayInfo::dpi() const
{
    if (diagonal_milli_inch == 0 || width_px == 0 || height_px == 0) {
        return 0;
    }

    const double w             = static_cast<double>(width_px);
    const double h             = static_cast<double>(height_px);
    const double diagonal_px   = std::sqrt(w * w + h * h);
    const double diagonal_inch = static_cast<double>(diagonal_milli_inch) / 1000.0;

    return static_cast<std::uint16_t>(diagonal_px / diagonal_inch + 0.5);
}

}  // namespace firefly::platform
