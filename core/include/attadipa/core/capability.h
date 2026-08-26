// SPDX-FileCopyrightText: 2026 Sergey Khlebnikov and Attadipa contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "attadipa/core/availability.h"

// What an application can ask for.
//
// The other vocabulary — Display, Rtc, Radio, GnssReceiver — is
// attadipa/platform/hardware_feature.h, and applications do not link the library
// that owns it. An application asking about a GNSS chip is asking about silicon;
// this enum is the list of things a *product* can do.
// docs/adr/0007-two-capability-layers.md.

namespace attadipa::core {

enum class Capability : std::uint8_t {
    Time,               // a wall clock worth displaying
    Position,           // a geographic fix
    Heading,            // an orientation or a course, with a frame
    MotionSensing,      // steps, wrist gestures, activity
    MeshMessaging,      // messages to and from other devices
    Haptics,            // semantic tactile feedback
    AudioPlayback,
    AudioCapture,
    NotificationRelay,  // a phone's notifications, on the wrist
    InfraredBlast,      // controlling other devices in the room
    PersistentStorage,  // settings and app state that survive a reboot
    RemovableStorage,   // media the user can take out
    CompanionLink,      // the phone link itself, for its own settings screen
};

inline constexpr std::uint8_t kCapabilityCount =
    static_cast<std::uint8_t>(Capability::CompanionLink) + 1;

constexpr std::uint16_t capability_bit(Capability capability)
{
    return static_cast<std::uint16_t>(1u << static_cast<std::uint16_t>(capability));
}

// Notably absent, recorded here so the question is not reopened every quarter:
//
//   Navigation      — an application built on Position and Heading. Adding it
//                     would make the Navigator gate itself on its own existence.
//   Display, Touch  — an application that cannot draw is not running.
//   Wifi, Ble       — transports. What the user cares about is NotificationRelay
//                     or MeshMessaging, and which radio carried it is the
//                     service's business.
//   weather, quests — data a node feeds us. Feeds are not capabilities
//                     (docs/adr/0004-capability-sources.md §4).

struct CapabilityChanged {
    Capability   capability = Capability::Time;
    Availability from       = Availability::Unsupported;
    Availability to         = Availability::Unsupported;
    ProviderRef  provider   = {};
};

const char* to_string(Capability capability);

}  // namespace attadipa::core
