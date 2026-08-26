#include "attadipa/core/mesh_service.h"

namespace attadipa::core {

const char* to_string(MeshDelivery delivery)
{
    switch (delivery) {
    case MeshDelivery::None:      return "none";
    case MeshDelivery::Queued:    return "queued";
    case MeshDelivery::Accepted:  return "accepted";
    case MeshDelivery::Confirmed: return "confirmed";
    case MeshDelivery::Failed:    return "failed";
    }
    return "unknown";
}

}  // namespace attadipa::core
