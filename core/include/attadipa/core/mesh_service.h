#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "attadipa/core/availability.h"
#include "attadipa/core/clock.h"
#include "attadipa/core/transport_state.h"

namespace attadipa::core {

inline constexpr std::size_t kMeshPublicKeyBytes = 32;
inline constexpr std::size_t kMeshPeerNameBytes = 32;
inline constexpr std::size_t kMeshTextBytes = 128;

struct MeshPeerId {
    std::array<std::uint8_t, kMeshPublicKeyBytes> public_key{};

    friend bool operator==(const MeshPeerId& left, const MeshPeerId& right)
    {
        return left.public_key == right.public_key;
    }
};

struct MeshPeer {
    MeshPeerId id{};
    std::array<char, kMeshPeerNameBytes + 1> name{};
};

enum class MeshDelivery : std::uint8_t {
    None,
    Queued,
    Accepted,
    Confirmed,
    Failed,
};

struct MeshStatus {
    Availability availability = Availability::Unreachable;
    TransportPhase transport = TransportPhase::Absent;
    std::array<char, kMeshPeerNameBytes + 1> node_name{};
    std::array<char, kMeshPeerNameBytes + 1> last_sender{};
    std::array<char, kMeshTextBytes + 1> last_message{};
    MeshDelivery delivery = MeshDelivery::None;
    std::uint16_t mtu = 0;
    std::uint16_t peers_reported = 0;
    std::uint16_t peers_retained = 0;
    std::int8_t snr_quarter_db = 0;
    bool has_snr = false;
    bool peers_truncated = false;
    bool message_truncated = false;
};

class MeshProvider {
public:
    virtual ~MeshProvider() = default;

    virtual MeshStatus status() const = 0;
    virtual std::size_t peer_count() const = 0;
    virtual bool peer(std::size_t index, MeshPeer& out) const = 0;
    virtual bool send_private(const MeshPeerId& peer, std::string_view text,
                              WallTime timestamp) = 0;
};

class MeshService {
public:
    explicit MeshService(MeshProvider& provider) : provider_(provider) {}

    MeshStatus status() const { return provider_.status(); }
    std::size_t peer_count() const { return provider_.peer_count(); }
    bool peer(std::size_t index, MeshPeer& out) const
    {
        return provider_.peer(index, out);
    }
    bool send_private(const MeshPeerId& peer, std::string_view text,
                      WallTime timestamp)
    {
        return provider_.send_private(peer, text, timestamp);
    }

private:
    MeshProvider& provider_;
};

const char* to_string(MeshDelivery delivery);

}  // namespace attadipa::core
