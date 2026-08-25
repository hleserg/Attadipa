#pragma once

#include <cstdint>

namespace attadipa::core {

// Motion evidence is meaningful only for the physical body it was measured on.
enum class SensorBody : std::uint8_t {
    Unknown,
    Watch,
    Node,
    Companion,
};

struct MotionEvidence {
    SensorBody body = SensorBody::Unknown;
    bool known = false;
    bool moving = false;

    constexpr bool speaks_for(SensorBody about) const
    {
        return known && about != SensorBody::Unknown && body == about;
    }

    constexpr bool says_at_rest(SensorBody about) const
    {
        return speaks_for(about) && !moving;
    }

    constexpr bool says_in_motion(SensorBody about) const
    {
        return speaks_for(about) && moving;
    }

    constexpr bool is_coherent() const
    {
        return !known || body != SensorBody::Unknown;
    }
};

const char* to_string(SensorBody body);

}  // namespace attadipa::core
