#include "firefly/core/diagnostics.h"

namespace firefly::core {

const char* to_string(ResetReason reason)
{
    switch (reason) {
        case ResetReason::Unknown:       return "Unknown";
        case ResetReason::PowerOn:       return "PowerOn";
        case ResetReason::Software:      return "Software";
        case ResetReason::Panic:         return "Panic";
        case ResetReason::Watchdog:      return "Watchdog";
        case ResetReason::Brownout:      return "Brownout";
        case ResetReason::DeepSleepWake: return "DeepSleepWake";
        case ResetReason::ExternalPin:   return "ExternalPin";
    }
    return "?";
}

const char* to_string(TransportPhase phase)
{
    switch (phase) {
        case TransportPhase::Absent:     return "Absent";
        case TransportPhase::Attached:   return "Attached";
        case TransportPhase::Connecting: return "Connecting";
        case TransportPhase::Ready:      return "Ready";
        case TransportPhase::Suspended:  return "Suspended";
        case TransportPhase::Faulted:    return "Faulted";
    }
    return "?";
}

const char* to_string(DisconnectReason reason)
{
    switch (reason) {
        case DisconnectReason::None:             return "None";
        case DisconnectReason::Unknown:          return "Unknown";
        case DisconnectReason::PeerClosed:       return "PeerClosed";
        case DisconnectReason::LivenessTimeout:  return "LivenessTimeout";
        case DisconnectReason::ProtocolError:    return "ProtocolError";
        case DisconnectReason::LocalRequest:     return "LocalRequest";
        case DisconnectReason::SubsystemRestart: return "SubsystemRestart";
        case DisconnectReason::Fault:            return "Fault";
    }
    return "?";
}

const char* to_string(TransportKind kind)
{
    switch (kind) {
        case TransportKind::Unknown:   return "Unknown";
        case TransportKind::Bluetooth: return "Bluetooth";
        case TransportKind::Usb:       return "Usb";
        case TransportKind::Uart:      return "Uart";
        case TransportKind::WifiTcp:   return "WifiTcp";
        case TransportKind::EspNow:    return "EspNow";
    }
    return "?";
}

}  // namespace firefly::core
