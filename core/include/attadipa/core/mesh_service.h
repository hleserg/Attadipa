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
    // The node's own public key, from RESP_CODE_SELF_INFO. `node_name` is not
    // an identity: the bench ran two nodes whose names differed by an emoji and
    // whose advertisements are interchangeable to `advertises_meshcore()`
    // (docs/research/MESHCORE_T114_FIRST_CONTACT.md:63 "public key"). This is
    // what tells them apart, and it is not permanent either -- a factory reset
    // on the node regenerates it
    // (`docs/research/MESHCORE_T114_FIRST_CONTACT.md:50` "a factory reset
    // regenerates it"), which is a new identity by design and is meant to be
    // visible as one.
    MeshPeerId node_id{};
    bool has_node_id = false;
    // WHICH NODE THIS WATCH IS PINNED TO, AND THE LAST ONE IT TURNED AWAY.
    //
    // Unlike `node_id` above, neither is session state, and a disconnect does
    // not clear them. That is the point of them: a watch that refuses the only
    // node in range ends up with no session at all, so everything session-scoped
    // on the mesh screen is empty exactly when an operator most needs to know
    // why -- and until #356 there is no in-image way to re-pin, so the recovery
    // is `idf.py erase-flash` and nothing on a blank screen says so.
    //
    // `has_refused` is set when a handshake reads a key that is not `pinned_id`,
    // and cleared when one reads a key that is. Nothing else clears it; in
    // particular a reconnect does not.
    MeshPeerId pinned_id{};
    bool has_pinned = false;
    MeshPeerId refused_id{};
    bool has_refused = false;
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
