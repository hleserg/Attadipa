#include "attadipa/core/motion.h"

namespace attadipa::core {

const char* to_string(SensorBody body)
{
    switch (body) {
        case SensorBody::Unknown:   return "Unknown";
        case SensorBody::Watch:     return "Watch";
        case SensorBody::Node:      return "Node";
        case SensorBody::Companion: return "Companion";
    }
    return "?";
}

}  // namespace attadipa::core
