#pragma once

#include <cstdint>

// The vocabulary for "is the link up", owned by core so that everything above
// and below it uses the same words.
//
// The machine that produces these values lives in link/, because it is
// transport plumbing. The words themselves live here, because a diagnostics
// snapshot, a settings screen and a service all need them and none of them may
// depend on a transport implementation to get them. Direction matters: link/
// includes this header, and core never includes link/.
//
// The distinction that is the whole point: `Attached` and `Ready` are different
// states. A USB peripheral that has enumerated is attached; whether a host has
// opened the port and is reading is a separate question, and on the ESP32-S3's
// USB-Serial-JTAG there is no line-state register that answers it. MeshCore's
// `ArduinoSerialInterface::isConnected()` returns `true` unconditionally with
// the comment "no way of knowing, so assume yes", and the consequences reach
// all the way to a device that cannot show its BLE pairing PIN
// (docs/upstream/meshcore-1.17-review.md §2). Firefly cannot spell "assume yes".

namespace firefly::core {

enum class TransportPhase : std::uint8_t {
    Absent,      // the peripheral or radio is not there at all
    Attached,    // it exists and is powered; nobody is talking on it
    Connecting,  // a peer is arriving — advertising, enumerating, handshaking
    Ready,       // a peer has been heard from within the liveness window
    Suspended,   // deliberately quiesced, e.g. for a sleep state
    Faulted,     // it failed, and needs a reset rather than a retry
};

inline constexpr std::uint8_t kTransportPhaseCount =
    static_cast<std::uint8_t>(TransportPhase::Faulted) + 1;

// Why the last session ended. Kept because "it disconnects sometimes" is not a
// bug report and this is what turns it into one — MeshCore's #2333 spent its
// length reconstructing exactly this from symptoms.
enum class DisconnectReason : std::uint8_t {
    None,              // there has not been a disconnect
    Unknown,           // there has, and the transport could not say why
    PeerClosed,        // the other end went away politely
    LivenessTimeout,   // it stopped answering
    ProtocolError,     // it said something that could not be parsed
    LocalRequest,      // we closed it
    SubsystemRestart,  // the stack below was restarted underneath us
    Fault,             // the transport itself failed
};

// Which kind of transport this is. Firefly's node link must not be nailed to
// BLE — the owner's requirement, and MeshCore's own #3049 is the same
// realisation arriving upstream.
enum class TransportKind : std::uint8_t {
    Unknown,
    Bluetooth,
    Usb,
    Uart,
    WifiTcp,
    EspNow,
};

const char* to_string(TransportPhase phase);
const char* to_string(DisconnectReason reason);
const char* to_string(TransportKind kind);

}  // namespace firefly::core
